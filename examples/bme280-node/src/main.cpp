// bme280-node — reference device for the TRIX IoT standard (trix-node/1).
// Reads a BME280 every interval_s and publishes one telemetry JSON.
#include <Adafruit_BME280.h>
#include <TrixNode.h>
#include <Wire.h>

// OTA public key issued by TRIX (the matching private key signs every release). Without the file
// the node still runs; it just never updates over the air.
#if __has_include("trix_ota_pubkey.h")
#include "trix_ota_pubkey.h"
#define OTA_KEY TRIX_OTA_PUBKEY
#else
#warning "include/trix_ota_pubkey.h missing: OTA disabled in this build"
#define OTA_KEY nullptr
#endif

TrixNode node(FW_TYPE, FW_VERSION, OTA_KEY);
Adafruit_BME280 bme;
bool sensorOk = false;

void setup() {
  Serial.begin(115200);
  delay(200);
  Wire.begin();
  sensorOk = bme.begin(0x76) || bme.begin(0x77);

  node.setDefaultInterval(60);
  node.setMetaExtra([](JsonObject meta) {
    JsonArray s = meta["sensors"].to<JsonArray>();
    s.add("bme280");
    meta["sensor_ok"] = sensorOk;
  });
  // App-specific commands go here; reboot/ping/ota_check/set interval_s are built in.
  node.onCommand([](const char* action, JsonObjectConst, JsonObject reply) {
    if (strcmp(action, "read_now") == 0) {
      reply["temperature_c"] = sensorOk ? bme.readTemperature() : NAN;
      return true;
    }
    return false;
  });
  node.begin();
}

void loop() {
  node.loop();
  if (!node.connected() || !node.due()) return;

  JsonDocument d;
  if (sensorOk) {
    d["temperature_c"] = roundf(bme.readTemperature() * 100) / 100;
    d["humidity_pct"] = roundf(bme.readHumidity() * 10) / 10;
    d["pressure_hpa"] = roundf(bme.readPressure() / 10.0f) / 10;
  } else {
    d["sensor_error"] = true;
  }
  d["rssi_dbm"] = WiFi.RSSI();
  node.publishTelemetry(d);
}
