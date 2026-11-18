#pragma once
// The LED matrix: a logical 8 x W frame buffer (x = 0 is the left column, y = 0 the top row)
// mapped onto whatever physical wiring the panels have.
#include <Arduino.h>
#include <FastLED.h>

#include "config.h"

namespace mx {

// How to color the pixels of a text or digits
struct Paint {
  enum Mode : uint8_t { SOLID, GRADIENT, RAINBOW };
  uint8_t mode = SOLID;
  CRGB c1 = CRGB::White, c2 = CRGB::White;
  int x0 = 0, x1 = 31;  // GRADIENT: c1 at x0, c2 at x1
  uint8_t hue = 0;      // RAINBOW: hue at x = 0
  uint8_t hueStep = 7;  // RAINBOW: hue change per column
  CRGB at(int x, int y) const;
  static Paint solid(CRGB c) {
    Paint p;
    p.c1 = p.c2 = c;
    return p;
  }
};

void begin();
// Wiring, see Settings. Rebuilds the mapping table; cheap.
void configure(int width, int tileW, int corner, bool columns, bool serpentine, bool chainRtl, bool altRotated,
               bool mirrorX, bool flipY, int colorOrder, int maxMilliamps);
int W(); // 32 or 64

// ---- frame buffer ----
void clear();
void px(int x, int y, CRGB c);
void add(int x, int y, CRGB c); // additive
CRGB get(int x, int y);
void fill(CRGB c);
void fade(uint8_t amount); // dims everything by amount/256
void fillRect(int x0, int y0, int x1, int y1, CRGB c); // x1, y1 exclusive
void setClip(int x0, int x1); // columns that may be drawn (x1 exclusive)
void resetClip();
CRGB *buffer(); // MAT_H rows of MAX_W, row-major

// Sends the buffer to the LEDs. brightness 0..255 (the power limit may lower it further).
void show(uint8_t brightness);
// Lights one LED by its position in the chain, bypassing the mapping (wiring test).
void showRaw(int index, int trail, uint8_t brightness);

// ---- text (8 pixels high, Hebrew right to left with Latin/digit runs left to right) ----
int textWidth(const char *utf8);
// xRight = the column just right of the text's last pixel
void drawText(const char *utf8, int xRight, int y, const Paint &p);
void drawTextLeft(const char *utf8, int xLeft, int y, const Paint &p);

// ---- clock digits ----
enum DigitFont : uint8_t { DF_BIG, DF_MEDIUM, DF_THIN, DF_SMALL };
int digitW(DigitFont f);
int digitH(DigitFont f);
// Draws '0'..'9', ' ' (blank, digit wide) and ':' ; returns the width used
int drawDigits(const char *s, int x, int y, DigitFont f, const Paint &p, bool colonOn, const Paint *colon = nullptr);
int digitsWidth(const char *s, DigitFont f);

} // namespace mx
