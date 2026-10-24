// lorawan-bme280-node: LoRaWAN reference device for the TRIX IoT standard.
// Heltec WiFi LoRa 32 V2 (ESP32 + SX1276) + BME280. Wakes every interval_s,
// sends one 10-byte uplink (payload v1, fPort 1) and deep-sleeps again.
//
// The node does not speak MQTT. The network server decodes the payload into
// standard key names and TRIX stores it like any other device:
//
//   node -> SenseCAP gateway -> ChirpStack (codec) -> HTTP -> TRIX
//
// ChirpStack v4 setup (once per site):
//   1. Device profile: region AS923 (AS923-1), MAC version LoRaWAN 1.0.3,
//      regional parameters A, OTAA, class A, ADR on.
//      Codec: "JavaScript functions", paste codec/trix-lorawan-bme280.js.
//   2. Application: one per TRIX site. Integrations > HTTP:
//        Endpoint  https://trix.technology/api/ingest/lorawan?site=<slug>
//        Headers   the token header TRIX expects (value from the site's
//                  settings in TRIX; never paste it into this file)
//        Encoding  JSON
//   3. Device: DevEUI/JoinEUI/AppKey into include/lorawan_keys.h, and a tag
//      trix_device = <device-id> (lowercase, as in IoT-Standard-v1). Without
//      the tag TRIX falls back to the ChirpStack device name.
//   4. Interval: queue a downlink on fPort 10, e.g. {"interval_s": 600} with
//      the codec, or the raw bytes 02 58. It applies after the next uplink.
//
// Session handling: the LoRaWAN session lives in RTC memory, so a deep-sleep
// wake does not re-join. DevNonces are kept in NVS, so after a power cut the
// node re-joins with a fresh nonce and ChirpStack accepts it (1.0.3+ rejects
// a reused DevNonce). Failed joins back off from 1 to 60 minutes.
#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <Wire.h>
#include <esp_sleep.h>

#include "trix_lorawan_payload.h"

#if __has_include("lorawan_keys.h")
#include "lorawan_keys.h"
#else
#error "include/lorawan_keys.h missing: cp include/lorawan_keys.example.h include/lorawan_keys.h"
#endif

#ifndef DEFAULT_INTERVAL_S
#define DEFAULT_INTERVAL_S 300
#endif

// Heltec WiFi LoRa 32 V2: SX1276 on the default SPI bus (SCK 5, MISO 19, MOSI 27).
static constexpr int LORA_NSS = 18;
static constexpr int LORA_DIO0 = 26;
static constexpr int LORA_RST = 14;
static constexpr int LORA_DIO1 = 35;

SX1276 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RST, LORA_DIO1);
LoRaWANNode node(&radio, &LORAWAN_BAND, LORAWAN_SUBBAND);
Adafruit_BME280 bme;
Preferences prefs;

// Survives deep sleep, lost on power cut or reset.
RTC_DATA_ATTR uint8_t rtcSession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
RTC_DATA_ATTR bool rtcSessionValid = false;
RTC_DATA_ATTR uint8_t rtcJoinFailures = 0;

static uint16_t intervalS = DEFAULT_INTERVAL_S;

static void sleepFor(uint32_t seconds) {
  Serial.printf("sleep %lu s\n", (unsigned long)seconds);
  Serial.flush();
  radio.sleep();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

static uint16_t readBatteryMv() {
#if BAT_ADC_PIN >= 0
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  float mv = (sum / 8.0f) * BAT_DIVIDER;
  // Below 2.5 V there is no cell (or no divider): report "not measured".
  return mv < 2500.0f ? 0 : (uint16_t)lroundf(mv);
#else
  return 0;
#endif
}

static TrixLwReading readSensors() {
  TrixLwReading r = {};
  Wire.begin(BME_SDA, BME_SCL);
  r.sensorOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
  if (r.sensorOk) {
    // Forced mode: one conversion, then the sensor sleeps (about 0.1 uA).
    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
    r.sensorOk = bme.takeForcedMeasurement();
    r.temperatureC = bme.readTemperature();
    r.humidityPct = bme.readHumidity();
    r.pressureHpa = bme.readPressure() / 100.0f;
  }
  r.batteryMv = readBatteryMv();
  return r;
}

static void loadSettings() {
  prefs.begin("trix-lw", false);
  intervalS = prefs.getUShort("interval_s", DEFAULT_INTERVAL_S);
  if (intervalS < TRIX_LW_INTERVAL_MIN || intervalS > TRIX_LW_INTERVAL_MAX) intervalS = DEFAULT_INTERVAL_S;
}

static bool joinOrRestore() {
  int16_t state = node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI, nullptr, LORAWAN_APP_KEY);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("beginOTAA failed %d\n", state);
    return false;
  }
  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  if (prefs.getBytes("nonces", nonces, sizeof(nonces)) == sizeof(nonces)) node.setBufferNonces(nonces);
  if (rtcSessionValid) node.setBufferSession(rtcSession);

  state = node.activateOTAA();
  // The nonces buffer changes on every join attempt; keep it even on failure
  // so the next attempt never reuses a DevNonce.
  prefs.putBytes("nonces", node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) return true;
  if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
    Serial.println("joined");
    return true;
  }
  Serial.printf("join failed %d\n", state);
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n%s %s\n", FW_TYPE, FW_VERSION);
  loadSettings();

#ifdef LORAWAN_KEYS_PLACEHOLDER
  Serial.println("include/lorawan_keys.h still has placeholder keys: not joining");
  sleepFor(TRIX_LW_INTERVAL_MAX);
#endif

  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("radio init failed %d\n", state);
    sleepFor(intervalS);
  }

  if (!joinOrRestore()) {
    rtcSessionValid = false;
    uint32_t backoff = 60UL << (rtcJoinFailures < 6 ? rtcJoinFailures : 6);
    if (rtcJoinFailures < 255) rtcJoinFailures++;
    sleepFor(backoff > 3600 ? 3600 : backoff);
  }
  rtcJoinFailures = 0;

  TrixLwReading r = readSensors();
  uint8_t up[TRIX_LW_PAYLOAD_LEN];
  size_t upLen = trixLwEncode(r, up, sizeof(up));

  uint8_t down[16];
  size_t downLen = 0;
  LoRaWANEvent_t evDown = {};
  state = node.sendReceive(up, upLen, TRIX_LW_FPORT_UPLINK, down, &downLen, false, nullptr, &evDown);
  if (state < RADIOLIB_ERR_NONE) Serial.printf("uplink failed %d\n", state);

  if (state > 0 && downLen > 0) {
    uint16_t next;
    if (trixLwParseConfig(evDown.fPort, down, downLen, &next) && next != intervalS) {
      intervalS = next;
      prefs.putUShort("interval_s", intervalS);
      Serial.printf("interval_s = %u\n", intervalS);
    }
  }

  memcpy(rtcSession, node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  rtcSessionValid = true;
  prefs.end();

  // Never sleep less than the regional duty cycle allows.
  uint32_t waitS = intervalS;
  uint32_t dutyS = (node.timeUntilUplink() + 999) / 1000;
  sleepFor(dutyS > waitS ? dutyS : waitS);
}

void loop() {}
