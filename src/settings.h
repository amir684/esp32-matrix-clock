#pragma once
// Everything the user can change from the web page. Stored as one JSON document in NVS.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"

enum Face : uint8_t {
  FACE_BIG,     // HH:MM in bold digits (HH:MM:SS on 8x64)
  FACE_SECONDS, // HH:MM + small seconds + a seconds bar
  FACE_THIN,    // HH:MM:SS in thin digits
  FACE_INFO,    // 8x64: big HH:MM + a box that rotates weekday / date / temperature
};

enum ColorMode : uint8_t {
  COL_SOLID,
  COL_GRADIENT, // color1 on the left to color2 on the right
  COL_RAINBOW,  // moving rainbow
  COL_HOUR,     // hue follows the time of day
};

enum TickerColors : uint8_t { TCOL_PER_ITEM, TCOL_SINGLE, TCOL_RAINBOW };

// Things the info ticker can scroll, one bit each
enum Item : uint8_t {
  IT_WEEKDAY,
  IT_HEBDATE,
  IT_GREGDATE,
  IT_EVENTS,   // holidays, fasts, Rosh Chodesh
  IT_PARASHA,
  IT_SHABBAT,  // candle lighting / end of Shabbat
  IT_OMER,
  IT_TEMP,
  IT_SUN,      // sunrise and sunset
  IT_MESSAGE,
  IT_COUNT
};

enum WifiMode : uint8_t { WIFI_STANDALONE, WIFI_HOME };

struct Settings {
  // ---- Matrix ----
  uint8_t width = 32;      // 32 or 64
  uint8_t tileW = 32;      // width of each physically separate matrix: 8, 16 or 32
  uint8_t corner = 0;      // where the first LED of a tile sits: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
  bool columns = true;     // the strip runs up/down the columns (most 8x32 panels) instead of along the rows
  bool serpentine = true;  // zigzag: every other line runs back
  bool chainRtl = false;   // tiles are chained right to left
  bool altRotated = false; // every second tile in the chain is mounted upside down
  bool mirrorX = false, flipY = false;
  uint8_t colorOrder = 0;  // 0 GRB (WS2812B), 1 RGB, 2 BRG, 3 RBG, 4 GBR, 5 BGR
  uint16_t maxMilliamps = 2000; // power supply limit, 0 = no limit

  // ---- Clock ----
  uint8_t face = FACE_BIG;
  bool hour24 = true;
  bool leadingZero = true;
  bool showSeconds = true;
  bool blinkColon = true;
  uint8_t colorMode = COL_SOLID;
  uint32_t color1 = 0xFFB050; // warm white
  uint32_t color2 = 0x3080FF;
  uint32_t colonColor = 0;    // 0 = same as the digits
  uint8_t brightness = 60;    // 1..255

  // ---- Info ticker ----
  uint16_t tickerEvery = 60;  // seconds between scrolls, 0 = only on request
  uint8_t scrollMs = 30;      // per pixel
  uint16_t items = 0;         // bit per Item, set in defaults()
  bool gregLong = true;       // "18 בנובמבר 2026" instead of "18.11.2026"
  bool dayAtSunset = true;    // the Hebrew date turns at sunset ("ליל ...")
  uint8_t tickerColors = TCOL_PER_ITEM;
  uint32_t tickerColor = 0x40C0FF;
  char message[100] = "";
  uint32_t messageColor = 0xFF40A0;
  float tempOffset = -1.5;    // the DS3231 runs a little warmer than the room

  // ---- Place (Jewish calendar) ----
  char city[32] = "ירושלים";
  float lat = 31.778, lon = 35.22;
  bool israel = true;
  uint8_t candleMinutes = 40;
  float havdalahDeg = 8.5;    // end of Shabbat: degrees below the horizon
  uint8_t parashaScheme = 0;  // heb::ParashaScheme

  // ---- Night ----
  bool nightOn = true;
  bool nightBySun = false;    // sunset to sunrise instead of the two hours below
  uint16_t nightFrom = 23 * 60, nightTo = 6 * 60; // minutes after midnight
  uint8_t nightBrightness = 4;
  bool nightColorOn = true;   // paint the clock in one calm color at night
  uint32_t nightColor = 0xFF2000;
  bool nightTicker = false;   // scroll the info at night too
  bool nightOff = false;      // switch the display off completely at night

  // ---- Party ----
  uint8_t effect = 0;         // 0 = cycle through all
  uint16_t effectSeconds = 20;
  uint8_t partyBrightness = 160;
  uint8_t partySpeed = 5;     // 1..10
  uint8_t partyClock = 1;     // 0 never, 1 every half minute for a few seconds, 2 always
  uint16_t partyMinutes = 0;  // back to the clock after this long, 0 = never
  char partyText[48] = "PARTY!";

  // ---- Buttons ----
  bool btnActiveLow = false;  // TTP223 with the "B" jumper soldered

  // ---- Time and network ----
  uint8_t wifiMode = WIFI_STANDALONE;
  char wifiSsid[33] = "";
  char wifiPass[65] = "";
  char apPass[65] = "";       // empty = open network
  char hostname[25] = DEFAULT_HOSTNAME;
  char name[30] = AP_NAME;    // the clock's name: Bluetooth, its own WiFi network, the page title (29 bytes fit a BLE scan response)
  char tz[48] = IL_TZ;
  char ntpServer[48] = "pool.ntp.org";
  bool bleOn = true;          // Bluetooth access for the installed app (takes effect after a restart)

  void defaults();
  bool has(Item i) const { return items & (1u << i); }
  void toJson(JsonDocument &doc, bool withSecrets) const;
  void fromJson(const JsonDocument &doc); // fields missing from doc keep their values
  bool load();
  void save() const;
};

extern Settings settings;
