#pragma once
// What the clock knows about today: Hebrew and civil date, holidays, Torah portion,
// Shabbat times, temperature. Builds the segments the info ticker scrolls.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>

struct Segment {
  char text[112];
  CRGB color;
  uint8_t item; // settings Item
};

namespace info {

void update(time_t now); // once a second; the heavy work is cached per day
void invalidate();       // settings changed

int buildTicker(Segment *out, int max);

// Short strings for the side box of the 8x64 "clock + info" face
const char *weekdayShort();  // "רביעי" / "שבת"
const char *hebDateShort();  // "כ"ז חשון"
const char *gregShort();     // "18/11"
const char *tempShort();     // "24°", "" without a DS3231

float temperature();         // with the offset, NAN without a DS3231
bool sunNight(time_t now);   // between sunset and sunrise

void statusJson(JsonDocument &doc);

} // namespace info
