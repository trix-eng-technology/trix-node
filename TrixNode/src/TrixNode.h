// TrixNode — common runtime for TRIX IoT nodes (protocol trix-node/1).
//
// One object per firmware. It owns WiFi, provisioning, the MQTT session
// (LWT, birth, meta, commands) and signed OTA, so a sensor sketch only reads
// its sensors and hands TrixNode a JSON object:
//
//   #include <TrixNode.h>
//   #include "trix_ota_pubkey.h"            // OTA public key issued by TRIX
//   TrixNode node("bme280-node", FW_VERSION, TRIX_OTA_PUBKEY);
//
//   void setup() { node.begin(); }
//   void loop() {
//     node.loop();
//     if (node.due()) {
//       JsonDocument d;
//       d["temperature_c"] = bme.readTemperature();
//       node.publishTelemetry(d);
//     }
//   }
//
// First boot (nothing in NVS) opens a SoftAP "trix-setup-xxxxxx" with a page
// at 192.168.4.1 for WiFi, the device id and either the TRIX provisioning URL
// (zero-touch) or manual broker settings. Holding the reset pin LOW for 5 s
// at boot clears the settings and opens the page again.
//
// Full contract: docs/device-protocol-v1.md
#pragma once
#ifdef ARDUINO

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <functional>

#include "trix_id.h"
#include "trix_roots.h"
#include "trix_token.h"

#define TRIX_NODE_VERSION "1.2.0"
#define TRIX_PROTOCOL "trix-node/1"

class TrixNode {
 public:
  // Return true if handled; put extra reply fields into `reply`.
  using CommandHandler = std::function<bool(const char* action, JsonObjectConst cmd, JsonObject reply)>;

  // type: device type, same as the firmware's deviceType in TRIX (a-z0-9-).
  // version: this build's semver. pubkey: 32-byte OTA public key, or nullptr
  // to disable OTA entirely.
  TrixNode(const char* type, const char* version, const uint8_t* pubkey);

  void setResetPin(int pin) { _resetPin = pin; }   // default 0 (BOOT); -1 = none
  void setDefaultInterval(uint32_t seconds) { _defaultInterval = seconds; }
  void setStatusLed(int pin) { _ledPin = pin; }
  void onCommand(CommandHandler handler) { _onCommand = handler; }
  // Root CAs (PEM) that MQTT over TLS and HTTPS provisioning must chain to.
  // Default: TRIX_ROOT_CAS (Let's Encrypt + Google Trust Services). A lab
  // broker with its own CA passes that CA here; nullptr turns verification
  // off, which lets anyone on the path pose as the broker. Call before begin().
  void setRootCAs(const char* pem) { _rootCAs = pem; }
  // Extra fields merged into the retained meta message (sensors, location...).
  void setMetaExtra(std::function<void(JsonObject meta)> fn) { _metaExtra = fn; }

  void begin();
  void loop();

  // True once per telemetry interval (interval_s, settable by cmd "set").
  bool due();
  bool connected();
  // Adds ts (when the clock is synced) and seq, then publishes to .../telemetry.
  bool publishTelemetry(JsonDocument& doc);
  // Escape hatch for app-specific JSON under this device's tree.
  bool publishJson(const char* channel, JsonDocument& doc, bool retain = false);

  // Gateways (LoRa P2P receiver, Modbus master, ...) publish for the nodes
  // behind them, under the same site: trix/<site>/<nodeId>/<channel>. The
  // broker ACL already lets the site user write anywhere in trix/<site>/#.
  // nodeId must follow the device-id rule (a-z0-9-, 1-48); false otherwise.
  bool publishFor(const char* nodeId, const char* channel, JsonDocument& doc, bool retain = false);
  // Adds ts (when the clock is synced); keeps the node's own seq if the
  // caller set one, since the gateway's counter means nothing for that node.
  bool publishTelemetryFor(const char* nodeId, JsonDocument& doc);

  void requestOtaCheck() { _otaRequested = true; }
  void startPortal();          // opens the setup page (blocks until saved or 10 min)
  void factoryReset();         // clears NVS and reboots into the portal

  const char* deviceId() const { return _deviceId; }
  const char* siteSlug() const { return _site; }
  uint32_t intervalSeconds() const { return _interval; }

 private:
  void loadSettings();
  bool haveNetworkSettings() const;
  bool connectWifi(uint32_t timeoutMs);
  bool provisionFromUrl();
  void reprovisionTick();
  void saveBroker();
  bool connectMqtt();
  void onMessage(char* topic, uint8_t* payload, unsigned int len);
  void handleCommand(JsonDocument& cmd);
  void publishBirth();
  void publishMeta();
  void topic(char* out, size_t outSize, const char* channel) const;
  bool publishTo(const char* topic, const char* what, JsonDocument& doc, bool retain);
  bool timeSynced() const;
  void otaTick();
  bool otaCheck();
  void otaReport(const char* state, const char* version, const char* reason = nullptr);
  void reportPendingOta();
  bool runPortal();

  const char* _type;
  const char* _version;
  const uint8_t* _pubkey;

  Preferences _prefs;
  WiFiClient _plain;
  WiFiClientSecure _secure;
  PubSubClient _mqtt;

  char _ssid[33] = "";
  char _pass[65] = "";
  char _deviceId[49] = "";
  char _site[49] = "";
  char _provUrl[192] = "";
  char _host[96] = "";
  uint16_t _port = 1883;
  bool _tls = false;
  const char* _rootCAs = TRIX_ROOT_CAS;
  char _user[64] = "";
  char _mqttPass[96] = "";
  char _otaUrl[160] = "";

  int _resetPin = 0;
  int _ledPin = -1;
  uint32_t _defaultInterval = 60;
  uint32_t _interval = 60;
  uint32_t _seq = 0;
  uint32_t _lastDue = 0;
  bool _firstDue = true;
  uint32_t _lastMqttTry = 0;
  uint32_t _mqttBackoff = 2000;
  uint32_t _lastOtaCheck = 0;
  uint32_t _otaJitter = 0;
  bool _otaRequested = false;
  // cmd "reprovision": fetch the broker again from the saved URL. The old
  // settings are kept until the new broker accepts us, else restored.
  bool _reprovRequested = false;
  uint32_t _reprovDeadline = 0;  // 0 = no switch on trial
  struct Broker {
    char host[96];
    uint16_t port;
    bool tls;
    char site[49];
    char user[64];
    char pass[96];
  } _prevBroker{};
  bool _bootChecked = false;
  uint32_t _bootCheckAt = 0;      // millis() of the jittered first OTA check
  uint32_t _pressStart = 0;       // reset button held since (0 = not held)
  bool _everOnline = false;       // WiFi connected at least once since boot

  CommandHandler _onCommand;
  std::function<void(JsonObject)> _metaExtra;
};

#endif  // ARDUINO
