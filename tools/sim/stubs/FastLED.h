#pragma once
// A tiny stand-in for FastLED: colors and the calls matrix.cpp makes.
#include <cstdint>

struct CHSV {
  uint8_t h, s, v;
  CHSV(uint8_t h_, uint8_t s_, uint8_t v_) : h(h_), s(s_), v(v_) {}
};

struct CRGB {
  uint8_t r = 0, g = 0, b = 0;
  CRGB() {}
  CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  CRGB(uint32_t c) : r(c >> 16), g(c >> 8), b(c) {}
  CRGB(int c) : CRGB((uint32_t)c) {}
  CRGB(const CHSV &hsv) {
    // plain HSV, good enough to look at
    float h = hsv.h * 6.0f / 256, s = hsv.s / 255.0f, v = hsv.v / 255.0f;
    int i = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float rr, gg, bb;
    switch (i % 6) {
    case 0: rr = v, gg = t, bb = p; break;
    case 1: rr = q, gg = v, bb = p; break;
    case 2: rr = p, gg = v, bb = t; break;
    case 3: rr = p, gg = q, bb = v; break;
    case 4: rr = t, gg = p, bb = v; break;
    default: rr = v, gg = p, bb = q;
    }
    r = rr * 255, g = gg * 255, b = bb * 255;
  }
  CRGB &operator+=(const CRGB &o) {
    r = r + o.r > 255 ? 255 : r + o.r;
    g = g + o.g > 255 ? 255 : g + o.g;
    b = b + o.b > 255 ? 255 : b + o.b;
    return *this;
  }
  CRGB &nscale8_video(uint8_t s) {
    r = r ? (r * s >> 8) + (s ? 1 : 0) : 0;
    g = g ? (g * s >> 8) + (s ? 1 : 0) : 0;
    b = b ? (b * s >> 8) + (s ? 1 : 0) : 0;
    return *this;
  }
  CRGB &nscale8(uint8_t s) {
    r = r * s >> 8;
    g = g * s >> 8;
    b = b * s >> 8;
    return *this;
  }
  explicit operator bool() const { return r || g || b; }
  bool operator==(const CRGB &o) const { return r == o.r && g == o.g && b == o.b; }
  enum : uint32_t { Black = 0, White = 0xFFFFFF, Red = 0xFF0000, Green = 0x008000, Blue = 0x0000FF, Yellow = 0xFFFF00 };
};

inline uint8_t sin8(uint8_t t) { return (uint8_t)(128 + 127 * __builtin_sin(t * 6.2831853 / 256)); }
inline CRGB blend(const CRGB &a, const CRGB &b, uint8_t t) {
  return CRGB(a.r + ((b.r - a.r) * t) / 255, a.g + ((b.g - a.g) * t) / 255, a.b + ((b.b - a.b) * t) / 255);
}
inline void fadeToBlackBy(CRGB *p, int n, uint8_t amt) {
  for (int i = 0; i < n; i++) {
    p[i].r = p[i].r * (255 - amt) / 255;
    p[i].g = p[i].g * (255 - amt) / 255;
    p[i].b = p[i].b * (255 - amt) / 255;
  }
}

struct CLEDController {
  CRGB *leds = nullptr;
  int n = 0;
  CLEDController &setLeds(CRGB *d, int k) {
    leds = d;
    n = k;
    return *this;
  }
};
struct WS2812B {};
enum { RGB = 0 };
enum { TypicalLEDStrip = 0 };

struct CFastLED {
  CLEDController c;
  uint8_t bright = 255;
  template <typename T, int PIN, int ORDER> CLEDController &addLeds(CRGB *d, int n) { return c.setLeds(d, n); }
  void setCorrection(int) {}
  void setDither(int) {}
  void clear(bool) {}
  void setBrightness(uint8_t b) { bright = b; }
  void show() {}
  void setMaxPowerInVoltsAndMilliamps(int, int) {}
  void setMaxPowerInMilliWatts(uint32_t) {}
};
extern CFastLED FastLED;

inline uint8_t scale8(uint8_t v, uint8_t s) { return (v * (1 + s)) >> 8; }
inline uint8_t calculate_max_brightness_for_power_mW(const CRGB *, uint16_t, uint8_t b, uint32_t) { return b; }
