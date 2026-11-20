#include "matrix.h"

#include "font.h"

// The LEDs are driven by the SPI hardware: every WS2812 bit becomes four SPI bits at 3.2 MHz
// (0 -> 1000 = 0.31 us high, 1 -> 1100 = 0.62 us high), so the timing comes from hardware.
// Four, not three: the core sends the frame in 64-byte pieces with a short pause between them. With four
// bits per data bit every piece ends at the end of a data bit, while the line is low, and the pause is
// harmless; with three bits a piece could end in the middle of a high pulse, stretch it, and turn 0 bits
// into 1 bits (pale, washed-out colors). Three bits also gave 0.42 us for a 0, above the WS2812B limit. Not FastLED's controller nor the RMT driver:
// both keep interrupt code in IRAM, and next to WiFi and Bluetooth the ESP32 has no IRAM left.
// The core's SPI functions work on the registers directly, with no interrupt at all.
// FastLED is still used for colors, effects math and the power calculation.
#include <esp32-hal-spi.h>

namespace mx {

static CRGB fb[MAT_H][MAX_W];
static CRGB out[MAX_LEDS];        // in chain order
static uint16_t map_[MAT_H][MAX_W];
static int w = 32;
static int order = 0;
static int maxmA = 0;
static int clipX0 = 0, clipX1 = MAX_W;
static spi_t *spi = nullptr;
static uint32_t *wire = nullptr;   // the encoded frame: 12 bytes per LED, word aligned for the SPI FIFO
static uint8_t enc[256][4];        // one color byte -> 32 SPI bits
static portMUX_TYPE wireLock = portMUX_INITIALIZER_UNLOCKED;

// ---------------- Paint ----------------

CRGB Paint::at(int x, int y) const {
  switch (mode) {
  case GRADIENT: {
    int span = x1 - x0;
    int t = span <= 0 ? 0 : constrain((x - x0) * 255 / span, 0, 255);
    return blend(c1, c2, t);
  }
  case RAINBOW: return CHSV(hue + x * hueStep + y * 2, 255, 255);
  default: return c1;
  }
}

// ---------------- Wiring ----------------

void begin() {
  for (int v = 0; v < 256; v++) {
    uint32_t bits = 0;
    for (int b = 7; b >= 0; b--) bits = (bits << 4) | ((v & (1 << b)) ? 0b1100 : 0b1000);
    enc[v][0] = bits >> 24;
    enc[v][1] = bits >> 16;
    enc[v][2] = bits >> 8;
    enc[v][3] = bits;
  }
  wire = (uint32_t *)malloc(MAX_LEDS * 12);
  spi = spiStartBus(VSPI, 0, SPI_MODE0, SPI_MSBFIRST);
  if (spi) {
    spiSetClockDiv(spi, spiFrequencyToClockDiv(spi, 3200000));
    spiAttachMOSI(spi, PIN_LED);
  }
  if (!spi || !wire) Serial.println("LED output could not be started");
}

void configure(int width, int tileW, int corner, bool columns, bool serpentine, bool chainRtl, bool altRotated,
               bool mirrorX, bool flipY, int colorOrder, int maxMilliamps) {
  w = width > 32 ? 64 : 32;
  if (tileW != 8 && tileW != 16 && tileW != 32) tileW = 32;
  if (tileW > w) tileW = w;
  int tiles = w / tileW;
  order = colorOrder;
  maxmA = maxMilliamps;
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++) {
      int lx = mirrorX ? w - 1 - x : x;
      int ly = flipY ? MAT_H - 1 - y : y;
      int tile = lx / tileW;
      lx %= tileW;
      int chain = chainRtl ? tiles - 1 - tile : tile; // position of this tile in the data chain
      if (altRotated && (chain & 1)) {
        lx = tileW - 1 - lx;
        ly = MAT_H - 1 - ly;
      }
      if (corner == 1 || corner == 3) lx = tileW - 1 - lx; // first LED on the right
      if (corner == 2 || corner == 3) ly = MAT_H - 1 - ly; // first LED at the bottom
      int idx;
      if (columns) idx = lx * MAT_H + ((serpentine && (lx & 1)) ? MAT_H - 1 - ly : ly);
      else idx = ly * tileW + ((serpentine && (ly & 1)) ? tileW - 1 - lx : lx);
      map_[y][x] = chain * tileW * MAT_H + idx;
    }
  memset(out, 0, sizeof out);
}

int W() { return w; }

// ---------------- Frame buffer ----------------

void clear() { memset(fb, 0, sizeof fb); }

void px(int x, int y, CRGB c) {
  if (x < clipX0 || x >= clipX1 || x >= w || y < 0 || y >= MAT_H) return;
  fb[y][x] = c;
}
void add(int x, int y, CRGB c) {
  if (x < clipX0 || x >= clipX1 || x >= w || y < 0 || y >= MAT_H) return;
  fb[y][x] += c;
}
CRGB get(int x, int y) {
  if (x < 0 || x >= w || y < 0 || y >= MAT_H) return CRGB::Black;
  return fb[y][x];
}
void fill(CRGB c) {
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++) fb[y][x] = c;
}
void fade(uint8_t amount) {
  for (int y = 0; y < MAT_H; y++) fadeToBlackBy(fb[y], w, amount);
}
void fillRect(int x0, int y0, int x1, int y1, CRGB c) {
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) px(x, y, c);
}
void setClip(int x0, int x1) {
  clipX0 = max(0, x0);
  clipX1 = min(MAX_W, x1);
}
void resetClip() { setClip(0, MAX_W); }
CRGB *buffer() { return &fb[0][0]; }

// ---------------- Output ----------------

static inline uint8_t *putByte(uint8_t *p, uint8_t v) {
  p[0] = enc[v][0];
  p[1] = enc[v][1];
  p[2] = enc[v][2];
  p[3] = enc[v][3];
  return p + 4;
}

// Sends out[0 .. n) to the chain
static void send(int n, uint8_t brightness) {
  if (!spi || !wire) return;
  if (maxmA > 0) brightness = calculate_max_brightness_for_power_mW(out, n, brightness, 5 * maxmA);
  uint8_t *s = (uint8_t *)wire;
  for (int i = 0; i < n; i++) {
    // color correction of a typical WS2812 strip (blue and green are brighter than red), then brightness;
    // video scaling keeps a dim pixel at least at 1 instead of letting it go dark
    CRGB c = out[i];
    c.g = scale8(c.g, 176);
    c.b = scale8(c.b, 240);
    c.nscale8_video(brightness);
    uint8_t a, b, d; // the three bytes in the order this strip wants them
    switch (order) {
    case 1: a = c.r, b = c.g, d = c.b; break; // RGB
    case 2: a = c.b, b = c.r, d = c.g; break; // BRG
    case 3: a = c.r, b = c.b, d = c.g; break; // RBG
    case 4: a = c.g, b = c.b, d = c.r; break; // GBR
    case 5: a = c.b, b = c.g, d = c.r; break; // BGR
    default: a = c.g, b = c.r, d = c.b;       // GRB (WS2812B)
    }
    s = putByte(putByte(putByte(s, a), b), d);
  }
  // The SPI FIFO holds 64 bytes (5.3 LEDs) and is refilled by the CPU. An interrupt between two refills
  // would hold the line low long enough for the LEDs to take it as the end of the frame, so this core
  // takes no interrupts meanwhile (512 LEDs: 15.4 ms). WiFi and Bluetooth live on the other core.
  portENTER_CRITICAL(&wireLock);
  spiWriteNL(spi, wire, n * 12);
  portEXIT_CRITICAL(&wireLock);
}

void show(uint8_t brightness) {
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++) out[map_[y][x]] = fb[y][x];
  send(w * MAT_H, brightness);
}

void showRaw(int index, int trail, uint8_t brightness) {
  int n = w * MAT_H;
  memset(out, 0, sizeof out);
  for (int i = 0; i <= trail; i++) {
    int k = index - i;
    if (k < 0 || k >= n) continue;
    out[k] = i == 0 ? CRGB(CRGB::White) : CRGB(CHSV(k * 255 / n, 255, 255 - i * 255 / (trail + 1)));
  }
  // Mark the very first LED in red so the start of the chain is obvious
  if (index > 0) out[0] = CRGB::Red;
  send(n, brightness);
}

// ---------------- Text ----------------

static const Glyph FONT_EXTRA[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'!', {"#", "#", "#", "#", "#", ".", "#"}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'(', {".#", "#.", "#.", "#.", "#.", "#.", ".#"}},
    {')', {"#.", ".#", ".#", ".#", ".#", ".#", "#."}},
    {'/', {"..#", "..#", ".#.", ".#.", ".#.", "#..", "#.."}},
    {'+', {"...", "...", ".#.", "###", ".#.", "...", "..."}},
    {'=', {"...", "...", "###", "...", "###", "...", "..."}},
    {'_', {"....", "....", "....", "....", "....", "....", "####"}},
    {'@', {".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".###."}},
    {'%', {"##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"}},
    {'*', {".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."}},
    {0x00B0, {"###", "#.#", "###"}},                                        // °
    {0x2665, {".#.#.", "#####", "#####", "#####", ".###.", "..#..", "....."}}, // ♥
};

template <size_t N> static const Glyph *findIn(const Glyph (&table)[N], uint16_t cp) {
  for (const Glyph &g : table)
    if (g.cp == cp) return &g;
  return nullptr;
}
static const Glyph *glyphFor(uint16_t cp) {
  const Glyph *g = findIn(FONT, cp);
  return g ? g : findIn(FONT_EXTRA, cp);
}

static bool isLtr(uint16_t c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == ':' || c == '.' || c == '/' || c == '@' ||
         c == '_' || c == '%' || c == 0x00B0;
}
static bool isHebrew(uint16_t c) { return c >= 0x05D0 && c <= 0x05EA; }

struct Decoded {
  const Glyph *g[160];
  uint16_t cp[160];
  int n;
};

static void decode(const char *s, Decoded &d) {
  const uint8_t *p = (const uint8_t *)s;
  d.n = 0;
  while (*p && d.n < 160) {
    uint16_t c;
    if (p[0] < 0x80) {
      c = *p++;
    } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
      c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
      c = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else {
      p++;
      continue;
    }
    if (c >= 'a' && c <= 'z') c -= 32;        // only capitals are drawn
    if (c == 0x05BE || c == 0x2013) c = '-';  // maqaf, dash
    if (c == 0x05F3 || c == 0x2019) c = '\''; // geresh
    if (c == 0x05F4 || c == 0x201D) c = '"';  // gershayim
    if (c == 0x2764) c = 0x2665;              // heart
    const Glyph *g = glyphFor(c);
    if (!g) continue; // not in the font (vowel points, emoji...)
    d.cp[d.n] = c;
    d.g[d.n] = g;
    d.n++;
  }
  // Brackets face the other way inside right-to-left text
  for (int i = 0; i < d.n; i++) {
    if (d.cp[i] != '(' && d.cp[i] != ')') continue;
    bool nearHebrew = (i > 0 && isHebrew(d.cp[i - 1])) || (i + 1 < d.n && isHebrew(d.cp[i + 1]));
    if (nearHebrew) d.g[i] = glyphFor(d.cp[i] == '(' ? ')' : '(');
  }
}

static int gw(const Glyph *g) { return (int)strlen(g->r[0]); }

// End (exclusive) of the left-to-right run that starts at i, or i + 1 for a single RTL glyph.
static int runEnd(const Decoded &d, int i) {
  if (!isLtr(d.cp[i])) return i + 1;
  int j = i + 1;
  for (;;) {
    while (j < d.n && isLtr(d.cp[j])) j++;
    // a single space or hyphen between left-to-right glyphs belongs to the run ("12 PM", "MATRIX-CLOCK")
    if (j + 1 < d.n && (d.cp[j] == ' ' || d.cp[j] == '-' || d.cp[j] == '!') && isLtr(d.cp[j + 1])) j++;
    else break;
  }
  // trailing '!' stays with a Latin word
  while (j < d.n && d.cp[j] == '!' && d.cp[j - 1] >= 'A' && d.cp[j - 1] <= 'Z') j++;
  return j;
}

static void drawGlyph(const Glyph *g, int x, int y, const Paint &p) {
  for (int r = 0; r < 8; r++) {
    const char *s = g->r[r];
    if (!s) continue;
    for (int c = 0; s[c]; c++)
      if (s[c] == '#') px(x + c, y + r, p.at(x + c, y + r));
  }
}

static Decoded dec; // too big for the stack of the web server; calls are never nested

static int layout(const char *utf8, bool draw, int xRight, int y, const Paint &p) {
  decode(utf8, dec);
  int x = xRight;
  for (int i = 0; i < dec.n;) {
    int j = runEnd(dec, i);
    int rw = 0;
    for (int k = i; k < j; k++) rw += (k > i ? 1 : 0) + gw(dec.g[k]);
    if (draw && x - rw < clipX1 && x > clipX0) {
      int gx = x - rw;
      for (int k = i; k < j; k++) {
        drawGlyph(dec.g[k], gx, y, p);
        gx += gw(dec.g[k]) + 1;
      }
    }
    x -= rw + 1;
    i = j;
  }
  return dec.n ? xRight - x - 1 : 0;
}

int textWidth(const char *utf8) { return layout(utf8, false, 0, 0, Paint()); }
void drawText(const char *utf8, int xRight, int y, const Paint &p) { layout(utf8, true, xRight, y, p); }
void drawTextLeft(const char *utf8, int xLeft, int y, const Paint &p) {
  layout(utf8, true, xLeft + textWidth(utf8), y, p);
}

// ---------------- Clock digits ----------------

// Bold 5x8: 2-pixel strokes, the whole height of the matrix
static const char *const DIG_BIG[10][8] = {
    {".###.", "##.##", "##.##", "##.##", "##.##", "##.##", "##.##", ".###."},
    {"..##.", ".###.", "####.", "..##.", "..##.", "..##.", "..##.", ".####"},
    {".###.", "##.##", "...##", "...##", "..##.", ".##..", "##...", "#####"},
    {"####.", "...##", "...##", ".###.", "...##", "...##", "...##", "####."},
    {"##.##", "##.##", "##.##", "##.##", "#####", "...##", "...##", "...##"},
    {"#####", "##...", "##...", "####.", "...##", "...##", "##.##", ".###."},
    {".###.", "##...", "##...", "####.", "##.##", "##.##", "##.##", ".###."},
    {"#####", "...##", "...##", "..##.", "..##.", ".##..", ".##..", ".##.."},
    {".###.", "##.##", "##.##", ".###.", "##.##", "##.##", "##.##", ".###."},
    {".###.", "##.##", "##.##", "##.##", ".####", "...##", "...##", ".###."},
};
// Rounded 4x7
static const char *const DIG_MED[10][7] = {
    {".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."}, {"..#.", ".##.", "..#.", "..#.", "..#.", "..#.", ".###"},
    {".##.", "#..#", "...#", "..#.", ".#..", "#...", "####"}, {"###.", "...#", "...#", ".##.", "...#", "...#", "###."},
    {"#..#", "#..#", "#..#", "####", "...#", "...#", "...#"}, {"####", "#...", "###.", "...#", "...#", "#..#", ".##."},
    {".##.", "#...", "#...", "###.", "#..#", "#..#", ".##."}, {"####", "...#", "..#.", "..#.", ".#..", ".#..", ".#.."},
    {".##.", "#..#", "#..#", ".##.", "#..#", "#..#", ".##."}, {".##.", "#..#", "#..#", ".###", "...#", "...#", ".##."},
};
// Square 3x7
static const char *const DIG_THIN[10][7] = {
    {"###", "#.#", "#.#", "#.#", "#.#", "#.#", "###"}, {".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"},
    {"###", "..#", "..#", "###", "#..", "#..", "###"}, {"###", "..#", "..#", "###", "..#", "..#", "###"},
    {"#.#", "#.#", "#.#", "###", "..#", "..#", "..#"}, {"###", "#..", "#..", "###", "..#", "..#", "###"},
    {"###", "#..", "#..", "###", "#.#", "#.#", "###"}, {"###", "..#", "..#", ".#.", ".#.", ".#.", ".#."},
    {"###", "#.#", "#.#", "###", "#.#", "#.#", "###"}, {"###", "#.#", "#.#", "###", "..#", "..#", "###"},
};
// Small 3x5
static const char *const DIG_SMALL[10][5] = {
    {"###", "#.#", "#.#", "#.#", "###"}, {".#.", "##.", ".#.", ".#.", "###"}, {"###", "..#", "###", "#..", "###"},
    {"###", "..#", ".##", "..#", "###"}, {"#.#", "#.#", "###", "..#", "..#"}, {"###", "#..", "###", "..#", "###"},
    {"###", "#..", "###", "#.#", "###"}, {"###", "..#", ".#.", ".#.", ".#."}, {"###", "#.#", "###", "#.#", "###"},
    {"###", "#.#", "###", "..#", "###"},
};

int digitW(DigitFont f) { return f == DF_BIG ? 5 : f == DF_MEDIUM ? 4 : 3; }
int digitH(DigitFont f) { return f == DF_BIG ? 8 : f == DF_SMALL ? 5 : 7; }
static int colonW(DigitFont f) { return f == DF_BIG ? 2 : 1; }

static const char *rowOf(DigitFont f, int d, int r) {
  switch (f) {
  case DF_BIG: return DIG_BIG[d][r];
  case DF_MEDIUM: return DIG_MED[d][r];
  case DF_THIN: return DIG_THIN[d][r];
  default: return DIG_SMALL[d][r];
  }
}

int digitsWidth(const char *s, DigitFont f) {
  int x = 0;
  for (int i = 0; s[i]; i++) x += (i ? 1 : 0) + (s[i] == ':' ? colonW(f) : digitW(f));
  return x;
}

int drawDigits(const char *s, int x, int y, DigitFont f, const Paint &p, bool colonOn, const Paint *colon) {
  int x0 = x, h = digitH(f), dw = digitW(f);
  for (int i = 0; s[i]; i++) {
    if (i) x++;
    char c = s[i];
    if (c == ':') {
      const Paint &cp = colon ? *colon : p;
      if (colonOn) {
        int cw = colonW(f);
        // two dots, placed a third and two thirds down the digit
        int a = f == DF_BIG ? 1 : f == DF_SMALL ? 1 : 2;
        int b = f == DF_BIG ? 5 : f == DF_SMALL ? 3 : 4;
        int dh = f == DF_BIG ? 2 : 1;
        for (int k = 0; k < cw; k++)
          for (int j = 0; j < dh; j++) {
            px(x + k, y + a + j, cp.at(x + k, y + a + j));
            px(x + k, y + b + j, cp.at(x + k, y + b + j));
          }
      }
      x += colonW(f);
    } else if (c >= '0' && c <= '9') {
      for (int r = 0; r < h; r++) {
        const char *row = rowOf(f, c - '0', r);
        for (int k = 0; k < dw; k++)
          if (row[k] == '#') px(x + k, y + r, p.at(x + k, y + r));
      }
      x += dw;
    } else {
      x += dw; // blank digit
    }
  }
  return x - x0;
}

} // namespace mx
