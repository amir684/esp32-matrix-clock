#pragma once
// Just enough of Arduino to compile the display code on a PC.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

using std::isnan;
using std::max;
using std::min;

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
template <typename T, typename A, typename B> inline T constrain(T v, A lo, B hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline size_t strlcpy(char *d, const char *s, size_t n) {
  size_t l = strlen(s);
  if (n) {
    size_t c = l < n - 1 ? l : n - 1;
    memcpy(d, s, c);
    d[c] = 0;
  }
  return l;
}

inline tm *sim_localtime_r(const time_t *t, tm *out) {
  time_t il = *t + 2 * 3600; // the simulation runs in Israel winter time
  tm *g = gmtime(&il);
  *out = *g;
  return out;
}
#define localtime_r sim_localtime_r

inline uint8_t random8() { return rand() & 255; }
inline uint8_t random8(uint8_t n) { return rand() % n; }

struct SerialStub {
  void println(const char *s) { fputs(s, stderr); fputc(10, stderr); }
};
extern SerialStub Serial;
