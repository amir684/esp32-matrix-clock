#include "api.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <sys/time.h>

#include "ble.h"
#include "effects.h"
#include "games.h"
#include "info.h"
#include "matrix.h"
#include "net.h"
#include "rtc.h"
#include "settings.h"
#include "ui.h"

namespace api {

static bool restart = false, netChange = false, clockChange = false;

bool restartRequested() { return restart; }
bool netChanged() {
  bool v = netChange;
  netChange = false;
  return v;
}
bool clockChanged() {
  bool v = clockChange;
  clockChange = false;
  return v;
}

static Response json(JsonDocument &doc, int code = 200) {
  Response r{code, ""};
  serializeJson(doc, r.body);
  return r;
}
static Response ok() { return {200, "{\"ok\":true}"}; }
static Response error(int code, const char *msg) { return {code, String("{\"error\":\"") + msg + "\"}"}; }

// ---------------- Handlers ----------------

// The game on screen (-1 = none), its score, and every game's best
static void gameJson(JsonDocument &doc) {
  doc["game"] = ui::mode() == ui::GAME ? game::current() : -1;
  doc["gameScore"] = game::score();
  doc["gameOver"] = game::over();
  JsonArray g = doc["games"].to<JsonArray>();
  for (int i = 0; i < game::count(); i++) {
    JsonObject o = g.add<JsonObject>();
    o["name"] = game::name(i);
    o["best"] = game::best(i);
    o["level"] = game::level(i);
    o["ramp"] = game::ramp(i);
  }
}
static Response gameState() {
  JsonDocument doc;
  gameJson(doc);
  return json(doc);
}

static Response status() {
  JsonDocument doc;
  time_t now = time(nullptr);
  doc["epoch"] = (long)now;
  doc["timeValid"] = net::timeValid();
  if (net::timeValid()) {
    tm lt;
    localtime_r(&now, &lt);
    char b[24];
    strftime(b, sizeof b, "%H:%M:%S", &lt);
    doc["time"] = b;
    info::statusJson(doc);
  }
  doc["rtc"] = rtc::present();
  doc["ntp"] = net::ntpSynced();
  doc["mode"] = ui::mode() == ui::PARTY ? "party" : ui::mode() == ui::GAME ? "game" : "clock";
  doc["effectNow"] = ui::effectIndex();
  doc["power"] = ui::power();
  doc["night"] = ui::night();
  doc["bright"] = ui::brightness();
  bool connected = WiFi.status() == WL_CONNECTED;
  doc["wifi"] = connected;
  doc["ip"] = net::address();
  doc["ssid"] = connected ? WiFi.SSID() : String("");
  doc["rssi"] = connected ? WiFi.RSSI() : 0;
  doc["wifiSleep"] = WiFi.getSleep();
  doc["ap"] = net::apActive() ? settings.name : "";
  doc["ble"] = ble::connected();
  doc["version"] = FIRMWARE_VERSION;
  doc["heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  JsonArray fxs = doc["effects"].to<JsonArray>();
  for (int i = 0; i < fx::count(); i++) fxs.add(fx::name(i));
  gameJson(doc);
  return json(doc);
}

static Response getSettings() {
  JsonDocument doc;
  settings.toJson(doc, false);
  return json(doc);
}

static Response postSettings(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return error(400, "bad json");
  bool net = !doc["wifiMode"].isNull() || !doc["wifiSsid"].isNull() || !doc["wifiPass"].isNull() ||
             !doc["apPass"].isNull() || !doc["hostname"].isNull();
  bool clock = !doc["tz"].isNull() || !doc["ntpServer"].isNull();
  settings.fromJson(doc);
  settings.save();
  applyHardware();
  info::invalidate();
  if (net || clock) clockChange = true;
  if (net) netChange = true;
  return getSettings();
}

static Response setTime(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return error(400, "bad json");
  long long epoch = doc["epoch"] | 0LL;
  if (epoch < 1700000000LL) return error(400, "bad time");
  timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  rtc::write((time_t)epoch);
  info::invalidate();
  return ok();
}

static Response action(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return error(400, "bad json");
  String a = doc["do"] | "";
  if (a == "party") ui::setMode(ui::PARTY);
  else if (a == "clock") ui::setMode(ui::CLOCK);
  else if (a == "toggleMode") ui::setMode(ui::mode() == ui::PARTY ? ui::CLOCK : ui::PARTY);
  else if (a == "nextEffect") {
    if (ui::mode() != ui::PARTY) ui::setMode(ui::PARTY);
    ui::nextEffect();
  } else if (a == "ticker") {
    ui::wake();
    ui::startTicker();
  } else if (a == "power") ui::setPower(doc["on"] | !ui::power());
  else if (a == "theme") ui::nextTheme();
  else if (a == "testChain") ui::startTest(ui::TEST_CHAIN);
  else if (a == "testMap") ui::startTest(ui::TEST_MAP);
  else if (a == "testColors") ui::startTest(ui::TEST_COLORS);
  else if (a == "testStop") ui::startTest(ui::TEST_NONE);
  else if (a == "game") ui::startGame(doc["game"] | 0);
  else if (a == "gameSpeed") {
    int g = doc["game"] | -1;
    game::setSpeed(g, doc["level"] | game::level(g), doc["ramp"] | game::ramp(g));
    return gameState();
  } else if (a == "gameStop") {
    if (ui::mode() == ui::GAME) ui::setMode(ui::CLOCK);
  } else if (a == "key") {
    // a game key from the app; the answer carries the score so the app needs no extra request
    static const char *const KEYS[game::KEY_N] = {"left", "right", "up", "down", "a"};
    String k = doc["k"] | "";
    for (int i = 0; i < game::KEY_N; i++)
      if (k == KEYS[i]) game::key((game::Key)i, (doc["s"] | 1) != 0, (doc["r"] | 0) != 0);
    return gameState();
  } else if (a == "notice") ui::notice(doc["text"] | "", CRGB::Black, 6000);
  else if (a == "reboot") restart = true;
  else if (a == "factory") {
    settings.defaults();
    settings.save();
    game::resetAll();
    restart = true;
  } else return error(400, "unknown action");
  if (ui::takeDirty()) settings.save();
  return ok();
}

// The logical frame, 6 hex digits per pixel, row by row (for the live preview)
static Response frame() {
  int w = mx::W();
  static const char HEX_[] = "0123456789abcdef";
  char *buf = (char *)malloc(w * MAT_H * 6 + 1);
  if (!buf) return error(500, "memory");
  char *p = buf;
  const CRGB *b = mx::buffer();
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++) {
      const CRGB &c = b[y * MAX_W + x];
      for (uint8_t k : {c.r, c.g, c.b}) {
        *p++ = HEX_[k >> 4];
        *p++ = HEX_[k & 15];
      }
    }
  *p = 0;
  Response r{200, ""};
  r.body.reserve(w * MAT_H * 6 + 24);
  r.body = "{\"w\":";
  r.body += w;
  r.body += ",\"px\":\"";
  r.body += buf;
  r.body += "\"}";
  free(buf);
  return r;
}

static Response scan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    if (!WiFi.SSID(i).length()) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  return json(doc);
}

Response handle(const String &method, const String &path, const String &body) {
  String p = path;
  int q = p.indexOf('?');
  if (q >= 0) p = p.substring(0, q);
  bool post = method == "POST";
  if (!post) {
    if (p == "/api/status") return status();
    if (p == "/api/settings" || p == "/api/backup") return getSettings();
    if (p == "/api/frame") return frame();
    if (p == "/api/game") return gameState();
    if (p == "/api/scan") return scan();
  } else {
    if (p == "/api/settings") return postSettings(body);
    if (p == "/api/time") return setTime(body);
    if (p == "/api/action") return action(body);
  }
  return error(404, "not found");
}

} // namespace api
