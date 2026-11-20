#include "settings.h"
#include <Preferences.h>

Settings settings;

static const char *ITEM_KEYS[IT_COUNT] = {"weekday", "hebdate", "gregdate", "events", "parasha",
                                          "shabbat", "omer",    "temp",     "sun",    "message"};

void Settings::defaults() {
  *this = Settings();
  items = (1u << IT_WEEKDAY) | (1u << IT_HEBDATE) | (1u << IT_GREGDATE) | (1u << IT_EVENTS) |
          (1u << IT_PARASHA) | (1u << IT_SHABBAT) | (1u << IT_OMER) | (1u << IT_TEMP) | (1u << IT_MESSAGE);
}

template <typename T> static T clampTo(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- small helpers so every field is listed once per direction ----

static void getStr(const JsonDocument &d, const char *k, char *dst, size_t size) {
  JsonVariantConst v = d[k];
  if (v.is<const char *>()) strlcpy(dst, v.as<const char *>(), size);
}
template <typename T> static void getNum(const JsonDocument &d, const char *k, T &dst, T lo, T hi) {
  JsonVariantConst v = d[k];
  if (v.is<double>() || v.is<long>()) dst = (T)clampTo<double>(v.as<double>(), lo, hi);
}
static void getBool(const JsonDocument &d, const char *k, bool &dst) {
  JsonVariantConst v = d[k];
  if (v.is<bool>()) dst = v.as<bool>();
  else if (v.is<int>()) dst = v.as<int>() != 0;
}
// Colors travel as "#RRGGBB"
static void getColor(const JsonDocument &d, const char *k, uint32_t &dst) {
  JsonVariantConst v = d[k];
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (*s == '#') s++;
    dst = strtoul(s, nullptr, 16) & 0xFFFFFF;
  } else if (v.is<long>()) {
    dst = v.as<long>() & 0xFFFFFF;
  }
}
static String colorStr(uint32_t c) {
  char b[8];
  snprintf(b, sizeof b, "#%06lX", (unsigned long)(c & 0xFFFFFF));
  return b;
}

void Settings::toJson(JsonDocument &doc, bool withSecrets) const {
  doc["width"] = width;
  doc["tileW"] = tileW;
  doc["corner"] = corner;
  doc["columns"] = columns;
  doc["serpentine"] = serpentine;
  doc["chainRtl"] = chainRtl;
  doc["altRotated"] = altRotated;
  doc["mirrorX"] = mirrorX;
  doc["flipY"] = flipY;
  doc["colorOrder"] = colorOrder;
  doc["maxMilliamps"] = maxMilliamps;

  doc["face"] = face;
  doc["hour24"] = hour24;
  doc["leadingZero"] = leadingZero;
  doc["showSeconds"] = showSeconds;
  doc["blinkColon"] = blinkColon;
  doc["colorMode"] = colorMode;
  doc["color1"] = colorStr(color1);
  doc["color2"] = colorStr(color2);
  doc["colonColor"] = colonColor ? colorStr(colonColor) : String("");
  doc["brightness"] = brightness;

  doc["tickerEvery"] = tickerEvery;
  doc["scrollMs"] = scrollMs;
  JsonObject it = doc["items"].to<JsonObject>();
  for (int i = 0; i < IT_COUNT; i++) it[ITEM_KEYS[i]] = has((Item)i);
  doc["gregLong"] = gregLong;
  doc["dayAtSunset"] = dayAtSunset;
  doc["tickerColors"] = tickerColors;
  doc["tickerColor"] = colorStr(tickerColor);
  doc["message"] = message;
  doc["messageColor"] = colorStr(messageColor);
  doc["tempOffset"] = tempOffset;

  doc["city"] = city;
  doc["lat"] = lat;
  doc["lon"] = lon;
  doc["israel"] = israel;
  doc["candleMinutes"] = candleMinutes;
  doc["havdalahDeg"] = havdalahDeg;
  doc["parashaScheme"] = parashaScheme;

  doc["nightOn"] = nightOn;
  doc["nightBySun"] = nightBySun;
  doc["nightFrom"] = nightFrom;
  doc["nightTo"] = nightTo;
  doc["nightBrightness"] = nightBrightness;
  doc["nightColorOn"] = nightColorOn;
  doc["nightColor"] = colorStr(nightColor);
  doc["nightTicker"] = nightTicker;
  doc["nightOff"] = nightOff;

  doc["effect"] = effect;
  doc["effectSeconds"] = effectSeconds;
  doc["partyBrightness"] = partyBrightness;
  doc["partySpeed"] = partySpeed;
  doc["partyClock"] = partyClock;
  doc["partyMinutes"] = partyMinutes;
  doc["partyText"] = partyText;

  doc["btnActiveLow"] = btnActiveLow;

  doc["wifiMode"] = wifiMode;
  doc["wifiSsid"] = wifiSsid;
  doc["hostname"] = hostname;
  doc["name"] = name;
  doc["tz"] = tz;
  doc["ntpServer"] = ntpServer;
  doc["bleOn"] = bleOn;
  if (withSecrets) {
    doc["wifiPass"] = wifiPass;
    doc["apPass"] = apPass;
  } else {
    doc["hasWifiPass"] = wifiPass[0] != 0;
    doc["hasApPass"] = apPass[0] != 0;
  }
}

void Settings::fromJson(const JsonDocument &d) {
  getNum<uint8_t>(d, "width", width, 32, 64);
  width = width > 32 ? 64 : 32;
  getNum<uint8_t>(d, "tileW", tileW, 8, 32);
  if (tileW != 8 && tileW != 16) tileW = 32;
  getNum<uint8_t>(d, "corner", corner, 0, 3);
  getBool(d, "columns", columns);
  getBool(d, "serpentine", serpentine);
  getBool(d, "chainRtl", chainRtl);
  getBool(d, "altRotated", altRotated);
  getBool(d, "mirrorX", mirrorX);
  getBool(d, "flipY", flipY);
  getNum<uint8_t>(d, "colorOrder", colorOrder, 0, 5);
  getNum<uint16_t>(d, "maxMilliamps", maxMilliamps, 0, 40000);
  if (maxMilliamps && maxMilliamps < 300) maxMilliamps = 300;

  getNum<uint8_t>(d, "face", face, 0, FACE_INFO);
  getBool(d, "hour24", hour24);
  getBool(d, "leadingZero", leadingZero);
  getBool(d, "showSeconds", showSeconds);
  getBool(d, "blinkColon", blinkColon);
  getNum<uint8_t>(d, "colorMode", colorMode, 0, COL_HOUR);
  getColor(d, "color1", color1);
  getColor(d, "color2", color2);
  if (d["colonColor"].is<const char *>() && !d["colonColor"].as<const char *>()[0]) colonColor = 0;
  else getColor(d, "colonColor", colonColor);
  getNum<uint8_t>(d, "brightness", brightness, 1, 255);

  getNum<uint16_t>(d, "tickerEvery", tickerEvery, 0, 3600);
  getNum<uint8_t>(d, "scrollMs", scrollMs, 10, 120);
  JsonObjectConst it = d["items"];
  if (it)
    for (int i = 0; i < IT_COUNT; i++)
      if (it[ITEM_KEYS[i]].is<bool>()) {
        if (it[ITEM_KEYS[i]].as<bool>()) items |= 1u << i;
        else items &= ~(1u << i);
      }
  getBool(d, "gregLong", gregLong);
  getBool(d, "dayAtSunset", dayAtSunset);
  getNum<uint8_t>(d, "tickerColors", tickerColors, 0, TCOL_RAINBOW);
  getColor(d, "tickerColor", tickerColor);
  getStr(d, "message", message, sizeof message);
  getColor(d, "messageColor", messageColor);
  getNum<float>(d, "tempOffset", tempOffset, -15, 15);

  getStr(d, "city", city, sizeof city);
  getNum<float>(d, "lat", lat, -66, 66);
  getNum<float>(d, "lon", lon, -180, 180);
  getBool(d, "israel", israel);
  getNum<uint8_t>(d, "candleMinutes", candleMinutes, 0, 90);
  getNum<float>(d, "havdalahDeg", havdalahDeg, 3, 20);
  getNum<uint8_t>(d, "parashaScheme", parashaScheme, 0, 2);

  getBool(d, "nightOn", nightOn);
  getBool(d, "nightBySun", nightBySun);
  getNum<uint16_t>(d, "nightFrom", nightFrom, 0, 1439);
  getNum<uint16_t>(d, "nightTo", nightTo, 0, 1439);
  getNum<uint8_t>(d, "nightBrightness", nightBrightness, 1, 255);
  getBool(d, "nightColorOn", nightColorOn);
  getColor(d, "nightColor", nightColor);
  getBool(d, "nightTicker", nightTicker);
  getBool(d, "nightOff", nightOff);

  getNum<uint8_t>(d, "effect", effect, 0, 40);
  getNum<uint16_t>(d, "effectSeconds", effectSeconds, 3, 3600);
  getNum<uint8_t>(d, "partyBrightness", partyBrightness, 1, 255);
  getNum<uint8_t>(d, "partySpeed", partySpeed, 1, 10);
  getNum<uint8_t>(d, "partyClock", partyClock, 0, 2);
  getNum<uint16_t>(d, "partyMinutes", partyMinutes, 0, 1440);
  getStr(d, "partyText", partyText, sizeof partyText);

  getBool(d, "btnActiveLow", btnActiveLow);

  getNum<uint8_t>(d, "wifiMode", wifiMode, 0, 1);
  getStr(d, "wifiSsid", wifiSsid, sizeof wifiSsid);
  getStr(d, "wifiPass", wifiPass, sizeof wifiPass);
  getStr(d, "apPass", apPass, sizeof apPass);
  if (apPass[0] && strlen(apPass) < 8) apPass[0] = 0; // WPA2 needs at least 8 characters
  getStr(d, "hostname", hostname, sizeof hostname);
  if (!hostname[0]) strlcpy(hostname, DEFAULT_HOSTNAME, sizeof hostname);
  getStr(d, "name", name, sizeof name);
  // strlcpy may have cut a Hebrew letter in half: drop the broken tail
  size_t n = strlen(name), k = n;
  while (k > 0 && ((uint8_t)name[k - 1] & 0xC0) == 0x80) k--;            // continuation bytes at the end
  if (k > 0 && ((uint8_t)name[k - 1] & 0xC0) == 0xC0) {                   // their lead byte
    uint8_t lead = name[k - 1];
    size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    if (n - (k - 1) < want) name[k - 1] = 0;
  }
  while (name[0] == ' ') memmove(name, name + 1, strlen(name));
  if (!name[0]) strlcpy(name, AP_NAME, sizeof name);
  getStr(d, "tz", tz, sizeof tz);
  if (!tz[0]) strlcpy(tz, IL_TZ, sizeof tz);
  getStr(d, "ntpServer", ntpServer, sizeof ntpServer);
  getBool(d, "bleOn", bleOn);
}

bool Settings::load() {
  Preferences p;
  if (!p.begin("clock", true)) return false;
  String s = p.getString("json", "");
  p.end();
  if (!s.length()) return false;
  JsonDocument doc;
  if (deserializeJson(doc, s)) return false;
  fromJson(doc);
  return true;
}

void Settings::save() const {
  JsonDocument doc;
  toJson(doc, true);
  String s;
  serializeJson(doc, s);
  Preferences p;
  if (!p.begin("clock", false)) return;
  p.putString("json", s);
  p.end();
}
