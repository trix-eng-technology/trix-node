#ifdef ARDUINO
#include "TrixNode.h"

#include <DNSServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <time.h>

#define TLOG(fmt, ...) Serial.printf("[trix] " fmt "\n", ##__VA_ARGS__)

static const char* NVS_NS = "trix";
static const uint32_t OTA_PERIOD_MS = 6UL * 3600UL * 1000UL;
static const uint32_t PORTAL_TIMEOUT_MS = 10UL * 60UL * 1000UL;
// Never got WiFi since boot for this long: the saved network is probably
// gone (new router, new password), so open the setup page instead of
// leaving the node unreachable until someone reflashes it.
static const uint32_t NO_WIFI_PORTAL_MS = 5UL * 60UL * 1000UL;
static const size_t JSON_OUT_MAX = 1600;

TrixNode::TrixNode(const char* type, const char* version, const uint8_t* pubkey)
    : _type(type), _version(version), _pubkey(pubkey) {}

// ---------------------------------------------------------------- settings

static void getStr(Preferences& p, const char* key, char* dst, size_t size) {
  String v = p.getString(key, "");
  strlcpy(dst, v.c_str(), size);
}

void TrixNode::loadSettings() {
  _prefs.begin(NVS_NS, true);
  getStr(_prefs, "ssid", _ssid, sizeof(_ssid));
  getStr(_prefs, "pass", _pass, sizeof(_pass));
  getStr(_prefs, "dev", _deviceId, sizeof(_deviceId));
  getStr(_prefs, "site", _site, sizeof(_site));
  getStr(_prefs, "prov", _provUrl, sizeof(_provUrl));
  getStr(_prefs, "host", _host, sizeof(_host));
  _port = _prefs.getUShort("port", 1883);
  _tls = _prefs.getBool("tls", false);
  getStr(_prefs, "user", _user, sizeof(_user));
  getStr(_prefs, "mpass", _mqttPass, sizeof(_mqttPass));
  getStr(_prefs, "ota", _otaUrl, sizeof(_otaUrl));
  _interval = _prefs.getULong("interval", _defaultInterval);
  _prefs.end();

  if (!_deviceId[0]) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(_deviceId, sizeof(_deviceId), "%s-%02x%02x%02x", _type, mac[3], mac[4], mac[5]);
  }
  if (_interval < 5 || _interval > 86400) _interval = _defaultInterval;
}

bool TrixNode::haveNetworkSettings() const {
  return _ssid[0] && (_provUrl[0] || (_host[0] && _site[0]));
}

void TrixNode::factoryReset() {
  _prefs.begin(NVS_NS, false);
  _prefs.clear();
  _prefs.end();
  TLOG("settings cleared, rebooting into setup");
  delay(300);
  ESP.restart();
}

// ---------------------------------------------------------------- lifecycle

void TrixNode::begin() {
  if (_ledPin >= 0) pinMode(_ledPin, OUTPUT);

  if (_resetPin >= 0) {
    pinMode(_resetPin, INPUT_PULLUP);
    uint32_t start = millis();
    while (digitalRead(_resetPin) == LOW && millis() - start < 5000) delay(20);
    if (digitalRead(_resetPin) == LOW) factoryReset();
  }

  WiFi.mode(WIFI_STA);  // MAC is valid only after the radio is up
  loadSettings();
  TLOG("%s %s on %s (lib %s), device %s", _type, _version, ESP.getChipModel(), TRIX_NODE_VERSION, _deviceId);

  if (!haveNetworkSettings()) {
    startPortal();  // never returns: saves and reboots
  }

  WiFi.setHostname(_deviceId);
  WiFi.setAutoReconnect(true);
  if (!connectWifi(20000)) TLOG("wifi not up yet, will keep retrying");

  configTime(0, 0, "pool.ntp.org", "time.google.com");

  if (_provUrl[0] && !_host[0] && WiFi.status() == WL_CONNECTED) provisionFromUrl();

  // The broker must prove it is the TRIX broker (Let's Encrypt chain), or a
  // man in the middle would get the site's MQTT credentials.
  if (_rootCAs) _secure.setCACert(_rootCAs);
  else _secure.setInsecure();
  _mqtt.setBufferSize(JSON_OUT_MAX + 256);
  _mqtt.setKeepAlive(30);
  _mqtt.setCallback([this](char* t, uint8_t* p, unsigned int l) { onMessage(t, p, l); });

  _otaJitter = esp_random() % (30UL * 60UL * 1000UL);
}

bool TrixNode::connectWifi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.begin(_ssid, _pass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    if (_ledPin >= 0) digitalWrite(_ledPin, (millis() / 250) % 2);
    delay(50);
  }
  if (WiFi.status() == WL_CONNECTED) {
    _everOnline = true;
    TLOG("wifi %s ip %s rssi %d", _ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  return false;
}

void TrixNode::loop() {
  // Factory reset by holding the reset button 5 s while running. (Holding
  // GPIO0 during power-up selects the ROM bootloader instead, so the boot-time
  // check in begin() only catches a press that starts right after reset.)
  if (_resetPin >= 0) {
    if (digitalRead(_resetPin) == LOW) {
      if (_pressStart == 0) _pressStart = millis() | 1;
      else if (millis() - _pressStart > 5000) factoryReset();
    } else {
      _pressStart = 0;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (_ledPin >= 0) digitalWrite(_ledPin, (millis() / 250) % 2);
    if (!_everOnline && millis() > NO_WIFI_PORTAL_MS) {
      TLOG("no wifi since boot, opening setup portal");
      startPortal();  // reboots when saved or after 10 min
    }
    return;  // auto-reconnect is on; nothing else works without WiFi
  }
  _everOnline = true;

  if (!_host[0]) {
    // Zero-touch provisioning failed at boot (server down?): retry slowly.
    static uint32_t lastProv = 0;
    if (_provUrl[0] && millis() - lastProv > 60000) {
      lastProv = millis();
      provisionFromUrl();
    }
    return;
  }

  if (!_mqtt.connected()) {
    if (millis() - _lastMqttTry >= _mqttBackoff) {
      _lastMqttTry = millis();
      if (connectMqtt()) {
        _mqttBackoff = 2000;
      } else {
        _mqttBackoff = min<uint32_t>(_mqttBackoff * 2, 60000);
      }
    }
  } else {
    _mqtt.loop();
    if (_ledPin >= 0) digitalWrite(_ledPin, HIGH);
  }

  reprovisionTick();
  otaTick();
}

bool TrixNode::connected() { return WiFi.status() == WL_CONNECTED && _mqtt.connected(); }

bool TrixNode::due() {
  uint32_t now = millis();
  if (_firstDue || now - _lastDue >= _interval * 1000UL) {
    _firstDue = false;
    _lastDue = now;
    return true;
  }
  return false;
}

bool TrixNode::timeSynced() const { return time(nullptr) > 1700000000; }

// ---------------------------------------------------------------- provisioning

bool TrixNode::provisionFromUrl() {
  TLOG("provisioning from %s", _provUrl);
  HTTPClient http;
  WiFiClientSecure tls;
  WiFiClient plain;
  bool https = strncmp(_provUrl, "https://", 8) == 0;
  // The answer carries the MQTT password, so the server must be the real one.
  if (https && _rootCAs) tls.setCACert(_rootCAs);
  else if (https) tls.setInsecure();
  if (!(https ? http.begin(tls, _provUrl) : http.begin(plain, _provUrl))) return false;
  http.setTimeout(10000);
  http.useHTTP10(true);  // getStream() is the raw socket: no chunked framing for the JSON parser
  int code = http.GET();
  if (code != 200) {
    char why[100] = "";
    if (https && code < 0) tls.lastError(why, sizeof(why));
    TLOG("provisioning failed: http %d %s", code, why);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err || !(doc["ok"] | false)) {
    TLOG("provisioning failed: bad response");
    return false;
  }
  const char* host = doc["broker"]["host"] | "";
  const char* site = doc["site"]["slug"] | "";
  if (!host[0] || !site[0]) return false;

  strlcpy(_host, host, sizeof(_host));
  _port = doc["broker"]["port"] | 1883;
  _tls = doc["broker"]["tls"] | false;
  strlcpy(_site, site, sizeof(_site));
  strlcpy(_user, doc["mqtt"]["username"] | "", sizeof(_user));
  strlcpy(_mqttPass, doc["mqtt"]["password"] | "", sizeof(_mqttPass));
  strlcpy(_otaUrl, doc["ota"]["checkUrl"] | "", sizeof(_otaUrl));

  saveBroker();
  TLOG("provisioned: site %s broker %s:%u%s", _site, _host, _port, _tls ? " tls" : "");
  return true;
}

void TrixNode::saveBroker() {
  _prefs.begin(NVS_NS, false);
  _prefs.putString("host", _host);
  _prefs.putUShort("port", _port);
  _prefs.putBool("tls", _tls);
  _prefs.putString("site", _site);
  _prefs.putString("user", _user);
  _prefs.putString("mpass", _mqttPass);
  _prefs.putString("ota", _otaUrl);
  _prefs.end();
}

// Remote re-provisioning with a safety net: a node that cannot reach the new
// broker within 5 minutes goes back to the one that worked, so a wrong server
// setting never strands a node in the field.
void TrixNode::reprovisionTick() {
  static const uint32_t TRIAL_MS = 5UL * 60UL * 1000UL;
  if (_reprovRequested) {
    _reprovRequested = false;
    Broker b{};
    strlcpy(b.host, _host, sizeof(b.host));
    b.port = _port;
    b.tls = _tls;
    strlcpy(b.site, _site, sizeof(b.site));
    strlcpy(b.user, _user, sizeof(b.user));
    strlcpy(b.pass, _mqttPass, sizeof(b.pass));
    if (!provisionFromUrl()) return;  // nothing changed
    if (strcmp(b.host, _host) == 0 && b.port == _port && b.tls == _tls && strcmp(b.site, _site) == 0) {
      TLOG("reprovision: broker unchanged");
      return;
    }
    _prevBroker = b;
    _reprovDeadline = millis() + TRIAL_MS;
    TLOG("reprovision: trying %s:%u, falls back to %s:%u if it fails", _host, _port, b.host, b.port);
    _mqtt.disconnect();
    _lastMqttTry = 0;
    _mqttBackoff = 2000;
    return;
  }
  if (!_reprovDeadline) return;
  if (_mqtt.connected()) {
    TLOG("reprovision: new broker accepted");
    _reprovDeadline = 0;
    return;
  }
  if ((int32_t)(millis() - _reprovDeadline) < 0) return;
  TLOG("reprovision: new broker unreachable, back to %s:%u", _prevBroker.host, _prevBroker.port);
  strlcpy(_host, _prevBroker.host, sizeof(_host));
  _port = _prevBroker.port;
  _tls = _prevBroker.tls;
  strlcpy(_site, _prevBroker.site, sizeof(_site));
  strlcpy(_user, _prevBroker.user, sizeof(_user));
  strlcpy(_mqttPass, _prevBroker.pass, sizeof(_mqttPass));
  saveBroker();
  _reprovDeadline = 0;
  _lastMqttTry = 0;
  _mqttBackoff = 2000;
}

// ---------------------------------------------------------------- mqtt

void TrixNode::topic(char* out, size_t outSize, const char* channel) const {
  snprintf(out, outSize, "trix/%s/%s/%s", _site, _deviceId, channel);
}

bool TrixNode::connectMqtt() {
  char lwt[160];
  topic(lwt, sizeof(lwt), "status");
  // Chosen per attempt: re-provisioning may switch the broker to TLS.
  if (_tls) _mqtt.setClient(_secure);
  else _mqtt.setClient(_plain);
  _mqtt.setServer(_host, _port);
  // Device ids are unique only within a site; the broker needs a global id.
  char clientId[100];
  snprintf(clientId, sizeof(clientId), "%s/%s", _site, _deviceId);
  bool ok = _mqtt.connect(clientId, _user[0] ? _user : nullptr, _mqttPass[0] ? _mqttPass : nullptr,
                          lwt, 1, true, "{\"online\":false}");
  if (!ok) {
    int st = _mqtt.state();
    char why[100] = "";
    if (_tls && st == MQTT_CONNECT_FAILED) _secure.lastError(why, sizeof(why));
    // A TLS failure (bad certificate, wrong time) is retried with backoff and
    // never falls back to plain MQTT.
    TLOG("mqtt connect failed, state %d %s", st, why);
    // Credentials rotated on the server: fetch fresh ones (at most every 10 min).
    static uint32_t lastReprov = 0;
    if ((st == MQTT_CONNECT_BAD_CREDENTIALS || st == MQTT_CONNECT_UNAUTHORIZED) && _provUrl[0] &&
        (lastReprov == 0 || millis() - lastReprov > 600000UL)) {
      lastReprov = millis();
      provisionFromUrl();
    }
    return false;
  }
  TLOG("mqtt connected %s:%u as %s", _host, _port, _deviceId);
  char cmd[160];
  topic(cmd, sizeof(cmd), "cmd");
  _mqtt.subscribe(cmd, 1);
  publishBirth();
  publishMeta();
  reportPendingOta();
  if (!_bootChecked) {
    _bootChecked = true;
    // Check once per boot, spread over 2 min so a whole site coming back
    // after a power cut does not hit the server in the same second.
    _bootCheckAt = millis() + (esp_random() % 120000UL) + 1;
  }
  return true;
}

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_EXT: return "external";
    default: return "other";
  }
}

bool TrixNode::publishJson(const char* channel, JsonDocument& doc, bool retain) {
  char t[160];
  topic(t, sizeof(t), channel);
  return publishTo(t, channel, doc, retain);
}

bool TrixNode::publishFor(const char* nodeId, const char* channel, JsonDocument& doc, bool retain) {
  if (!trix::validId(nodeId) || !trix::validChannel(channel)) {
    TLOG("publishFor: bad id or channel");
    return false;
  }
  char t[160];
  snprintf(t, sizeof(t), "trix/%s/%s/%s", _site, nodeId, channel);
  return publishTo(t, channel, doc, retain);
}

bool TrixNode::publishTelemetryFor(const char* nodeId, JsonDocument& doc) {
  if (timeSynced()) doc["ts"] = (uint32_t)time(nullptr);
  return publishFor(nodeId, "telemetry", doc, false);
}

bool TrixNode::publishTo(const char* t, const char* what, JsonDocument& doc, bool retain) {
  if (!_mqtt.connected()) return false;
  static char buf[JSON_OUT_MAX];  // static: keeps 1.6 KB off the loop task stack
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf) - 1) {
    TLOG("payload for %s too large", what);
    return false;
  }
  return _mqtt.publish(t, (const uint8_t*)buf, n, retain);
}

void TrixNode::publishBirth() {
  JsonDocument d;
  d["online"] = true;
  d["fw"] = _version;
  d["ip"] = WiFi.localIP().toString();
  d["rssi_dbm"] = WiFi.RSSI();
  d["reset_reason"] = resetReason();
  d["uptime_s"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  publishJson("status", d, true);
}

void TrixNode::publishMeta() {
  JsonDocument d;
  d["type"] = _type;
  d["fw"] = _version;
  d["lib"] = TRIX_NODE_VERSION;
  d["protocol"] = TRIX_PROTOCOL;
  d["chip"] = ESP.getChipModel();
#ifdef ARDUINO_BOARD
  d["hw"] = ARDUINO_BOARD;
#endif
  d["mac"] = WiFi.macAddress();
  d["interval_s"] = _interval;
  d["ota"] = _pubkey != nullptr;
  if (_metaExtra) _metaExtra(d.as<JsonObject>());
  publishJson("meta", d, true);
}

bool TrixNode::publishTelemetry(JsonDocument& doc) {
  if (timeSynced()) doc["ts"] = (uint32_t)time(nullptr);
  doc["seq"] = ++_seq;
  return publishJson("telemetry", doc, false);
}

void TrixNode::onMessage(char* t, uint8_t* payload, unsigned int len) {
  char expect[160];
  topic(expect, sizeof(expect), "cmd");
  if (strcmp(t, expect) != 0) return;
  JsonDocument cmd;
  if (deserializeJson(cmd, payload, len)) {
    TLOG("cmd: not json");
    return;
  }
  handleCommand(cmd);
}

void TrixNode::handleCommand(JsonDocument& cmd) {
  const char* action = cmd["action"] | "";
  JsonDocument ack;
  JsonObject reply = ack.to<JsonObject>();  // an object even when the app adds nothing
  if (cmd["id"].is<const char*>()) reply["id"] = cmd["id"].as<const char*>();
  bool ok = true;
  bool reboot = false;

  if (strcmp(action, "ping") == 0) {
    reply["uptime_s"] = millis() / 1000;
    reply["fw"] = _version;
  } else if (strcmp(action, "reboot") == 0) {
    reboot = true;
  } else if (strcmp(action, "factory_reset") == 0) {
    // Clears WiFi and broker settings; the node comes back as a setup AP.
    _prefs.begin(NVS_NS, false);
    _prefs.clear();
    _prefs.end();
    reboot = true;
  } else if (strcmp(action, "reprovision") == 0) {
    // Moves a node to whatever broker TRIX hands out now (e.g. LAN 1885 to
    // mqtts 8883) without touching it. Runs from loop(), after this ack.
    if (!_provUrl[0]) {
      ok = false;
      reply["error"] = "no provisioning URL saved";
    } else {
      _reprovRequested = true;
    }
  } else if (strcmp(action, "ota_check") == 0) {
    _otaRequested = true;
  } else if (strcmp(action, "set") == 0) {
    if (cmd["interval_s"].is<uint32_t>()) {
      uint32_t v = cmd["interval_s"].as<uint32_t>();
      if (v >= 5 && v <= 86400) {
        _interval = v;
        _prefs.begin(NVS_NS, false);
        _prefs.putULong("interval", v);
        _prefs.end();
        publishMeta();
      } else {
        ok = false;
        reply["error"] = "interval_s out of range 5..86400";
      }
    } else if (!(_onCommand && _onCommand(action, cmd.as<JsonObjectConst>(), reply))) {
      ok = false;
      reply["error"] = "nothing to set";
    }
  } else if (_onCommand && _onCommand(action, cmd.as<JsonObjectConst>(), reply)) {
    // handled by the application
  } else {
    ok = false;
    reply["error"] = "unknown action";
  }

  if (!reply["ok"].is<bool>()) reply["ok"] = ok;
  publishJson("ack", ack, false);
  if (reboot) {
    _mqtt.loop();
    delay(500);
    ESP.restart();
  }
}

// ---------------------------------------------------------------- ota

void TrixNode::otaReport(const char* state, const char* version, const char* reason) {
  TLOG("ota %s %s %s", state, version ? version : "", reason ? reason : "");
  JsonDocument d;
  d["state"] = state;
  if (version) d["version"] = version;
  if (reason) d["reason"] = reason;
  d["from"] = _version;
  publishJson("ota", d, false);
}

void TrixNode::reportPendingOta() {
  _prefs.begin(NVS_NS, false);
  String pending = _prefs.getString("ota_pend", "");
  if (pending.length()) _prefs.remove("ota_pend");
  _prefs.end();
  if (!pending.length()) return;
  if (pending == _version) {
    otaReport("success", _version);
  } else {
    otaReport("failed", pending.c_str(), "booted_other_version");
  }
}

void TrixNode::otaTick() {
  if (!_pubkey || !_otaUrl[0]) return;
  if (_bootCheckAt && (int32_t)(millis() - _bootCheckAt) >= 0) {
    _bootCheckAt = 0;
    _otaRequested = true;
  }
  bool periodic = _lastOtaCheck != 0 && millis() - _lastOtaCheck > OTA_PERIOD_MS + _otaJitter;
  if (!_otaRequested && !periodic) return;
  // Manifests expire; without NTP time the device cannot tell, so wait.
  if (!timeSynced()) return;
  _otaRequested = false;
  _lastOtaCheck = millis();
  otaCheck();
}

// Stream sink for HTTPClient::writeToStream: every byte of the body goes to
// the inactive OTA slot and into the running sha256 at the same time.
class OtaSink : public Stream {
 public:
  uint32_t total = 0;
  bool failed = false;
  explicit OtaSink(PubSubClient& mqtt) : _mqtt(mqtt) {
    mbedtls_sha256_init(&_ctx);
    mbedtls_sha256_starts(&_ctx, 0);
  }
  size_t write(const uint8_t* buf, size_t n) override {
    if (failed) return 0;
    if (Update.write(const_cast<uint8_t*>(buf), n) != n) {
      failed = true;
      return 0;
    }
    mbedtls_sha256_update(&_ctx, buf, n);
    total += n;
    // Keep the MQTT session alive so the broker does not fire our LWT and
    // the verifying/success reports still have a connection to go out on.
    if (millis() - _lastPing > 1000) {
      _lastPing = millis();
      _mqtt.loop();
    }
    return n;
  }
  size_t write(uint8_t c) override { return write(&c, 1); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  void finish(uint8_t digest[32]) {
    mbedtls_sha256_finish(&_ctx, digest);
    mbedtls_sha256_free(&_ctx);
  }

 private:
  PubSubClient& _mqtt;
  uint32_t _lastPing = 0;
  mbedtls_sha256_context _ctx;
};

static bool beginHttp(HTTPClient& http, WiFiClientSecure& tls, WiFiClient& plain, const String& url) {
  if (url.startsWith("https://")) {
    // Authenticity comes from the Ed25519 token and the sha256, not from TLS,
    // and on purpose no CA check here: a CA change in front of the server must
    // never cut a node in the field off from the update that would fix it.
    tls.setInsecure();
    return http.begin(tls, url);
  }
  return http.begin(plain, url);
}

bool TrixNode::otaCheck() {
  String fw = String(_version);
  fw.replace("+", "%2B");  // build metadata would otherwise arrive as a space
  String url = String(_otaUrl) + "?type=" + _type + "&fw=" + fw + "&id=" + _deviceId;
  HTTPClient http;
  WiFiClientSecure tls;
  WiFiClient plain;
  if (!beginHttp(http, tls, plain, url)) return false;
  http.setTimeout(10000);
  http.useHTTP10(true);  // the JSON below is parsed straight off the socket
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code != 404) otaReport("failed", nullptr, "check_http");
    return false;
  }
  JsonDocument m;
  DeserializationError err = deserializeJson(m, http.getStream());
  http.end();
  if (err || !(m["ok"] | false)) return false;
  if (m["upToDate"] | false) {
    TLOG("ota: up to date (%s)", _version);
    return true;
  }

  const char* type = m["type"] | "";
  const char* version = m["version"] | "";
  const char* sha = m["sha256"] | "";
  const char* binUrl = m["url"] | "";
  uint32_t size = m["size"] | 0;
  trix::SignedManifest s;
  if (!trix::verifyToken(m["token"] | "", _pubkey, s)) {
    otaReport("rejected", version, "signature");
    return false;
  }
  if (strcmp(s.type, _type) != 0 || strcmp(s.type, type) != 0 || strcmp(s.version, version) != 0 ||
      s.size != size || strcasecmp(s.sha256, sha) != 0) {
    otaReport("rejected", version, "manifest_mismatch");
    return false;
  }
  if (!trix::semverGreater(s.version, _version)) {
    otaReport("rejected", s.version, "not_newer");
    return false;
  }
  if ((uint32_t)time(nullptr) > s.exp) {
    otaReport("rejected", s.version, "expired");
    return false;
  }

  otaReport("downloading", s.version);
  if (!beginHttp(http, tls, plain, String(binUrl))) return false;
  http.setTimeout(20000);
  if (http.GET() != 200) {
    http.end();
    otaReport("failed", s.version, "download_http");
    return false;
  }
  if (!Update.begin(s.size)) {
    http.end();
    otaReport("failed", s.version, "no_space");
    return false;
  }

  // writeToStream decodes chunked transfer encoding (Cloudflare may use it),
  // so the bytes hashed and flashed are the file, not the HTTP framing.
  OtaSink sink(_mqtt);
  int written = http.writeToStream(&sink);
  http.end();
  uint32_t got = (written > 0 && !sink.failed) ? sink.total : 0;

  uint8_t digest[32];
  sink.finish(digest);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);

  otaReport("verifying", s.version);
  if (got != s.size || strcasecmp(hex, s.sha256) != 0) {
    Update.abort();
    otaReport("failed", s.version, got != s.size ? "short_download" : "sha256");
    return false;
  }
  if (!Update.end(true)) {
    otaReport("failed", s.version, "update_end");
    return false;
  }
  _prefs.begin(NVS_NS, false);
  _prefs.putString("ota_pend", s.version);
  _prefs.end();
  TLOG("ota: %s written and verified, rebooting", s.version);
  _mqtt.loop();
  delay(300);
  ESP.restart();
  return true;
}

// ---------------------------------------------------------------- setup portal

static String htmlEscape(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += *s;
    }
  }
  return o;
}

void TrixNode::startPortal() {
  runPortal();
  ESP.restart();
}

bool TrixNode::runPortal() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apName[32];
  snprintf(apName, sizeof(apName), "trix-setup-%02x%02x%02x", mac[3], mac[4], mac[5]);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName);
  DNSServer dns;
  dns.start(53, "*", WiFi.softAPIP());
  WebServer server(80);
  TLOG("setup portal: join wifi '%s' and open http://%s", apName, WiFi.softAPIP().toString().c_str());

  int found = WiFi.scanNetworks();
  String options;
  for (int i = 0; i < found && i < 20; i++) {
    options += "<option value=\"" + htmlEscape(WiFi.SSID(i).c_str()) + "\">";
  }

  bool saved = false;
  auto page = [&]() {
    String h =
        "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
        "<title>TRIX setup</title><style>body{font-family:sans-serif;max-width:480px;margin:16px auto;padding:0 12px}"
        "label{display:block;margin-top:10px}input{width:100%;padding:6px;box-sizing:border-box}"
        "fieldset{margin-top:14px}button{margin-top:16px;padding:10px;width:100%}</style>"
        "<h2>TRIX node setup</h2><p>" + htmlEscape(_type) + " " + htmlEscape(_version) + "</p>"
        "<form method=post action=/save>"
        "<label>WiFi SSID<input name=ssid list=ssids required value=\"" + htmlEscape(_ssid) + "\"></label>"
        "<datalist id=ssids>" + options + "</datalist>"
        "<label>WiFi password<input name=pass type=password></label>"
        "<label>Device id (a-z 0-9 -)<input name=dev pattern='[a-z0-9][a-z0-9-]{0,47}' required value=\"" + htmlEscape(_deviceId) + "\"></label>"
        "<fieldset><legend>Zero-touch (recommended)</legend>"
        "<label>TRIX provisioning URL<input name=prov placeholder='https://trix.technology/api/portal/.../config.json' value=\"" + htmlEscape(_provUrl) + "\"></label></fieldset>"
        "<fieldset><legend>Or manual broker</legend>"
        "<label>Site slug<input name=site value=\"" + htmlEscape(_site) + "\"></label>"
        "<label>Broker host<input name=host value=\"" + htmlEscape(_host) + "\"></label>"
        "<label>Port<input name=port type=number value=\"" + String(_port) + "\"></label>"
        "<label><input name=tls type=checkbox style='width:auto'" + String(_tls ? " checked" : "") + "> TLS</label>"
        "<label>MQTT user<input name=user value=\"" + htmlEscape(_user) + "\"></label>"
        "<label>MQTT password<input name=mpass type=password></label>"
        "<label>OTA check URL<input name=ota placeholder='https://trix.technology/api/ota/check' value=\"" + htmlEscape(_otaUrl) + "\"></label>"
        "</fieldset><button>Save and reboot</button></form>";
    server.send(200, "text/html", h);
  };
  server.on("/", HTTP_GET, page);
  server.onNotFound(page);  // captive portal: every URL shows the form
  server.on("/save", HTTP_POST, [&]() {
    String dev = server.arg("dev");
    dev.toLowerCase();
    _prefs.begin(NVS_NS, false);
    _prefs.putString("ssid", server.arg("ssid"));
    if (server.arg("pass").length() || !_pass[0]) _prefs.putString("pass", server.arg("pass"));
    _prefs.putString("dev", dev);
    String prov = server.arg("prov");
    prov.trim();
    _prefs.putString("prov", prov);
    if (prov.length()) {
      // Zero-touch: broker settings come from TRIX on first connect.
      _prefs.remove("host");
    } else {
      _prefs.putString("site", server.arg("site"));
      _prefs.putString("host", server.arg("host"));
      _prefs.putUShort("port", (uint16_t)server.arg("port").toInt());
      _prefs.putBool("tls", server.hasArg("tls"));
      _prefs.putString("user", server.arg("user"));
      if (server.arg("mpass").length()) _prefs.putString("mpass", server.arg("mpass"));
      _prefs.putString("ota", server.arg("ota"));
    }
    _prefs.end();
    server.send(200, "text/html", "<meta charset=utf-8><p>Saved. Rebooting...</p>");
    saved = true;
  });
  server.begin();

  uint32_t start = millis();
  while (!saved && millis() - start < PORTAL_TIMEOUT_MS) {
    dns.processNextRequest();
    server.handleClient();
    if (_ledPin >= 0) digitalWrite(_ledPin, (millis() / 1000) % 2);
    delay(5);
  }
  delay(500);
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  return saved;
}

#endif  // ARDUINO
