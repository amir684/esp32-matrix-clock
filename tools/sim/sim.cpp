// Runs the real display code (matrix, ui, info, hebcal, zmanim) on a PC, with small stand-ins for
// Arduino and FastLED in stubs/. It
//   1. checks the LED mapping for every wiring option, through the actual SPI bit stream, and
//   2. prints frames of every clock face and of the info ticker for render.py to draw.
// Output: "MAPPING bad=N", then for each frame "FRAME <name> <width>" and 8 lines of hex RGB pixels.
// Build and run: tools/sim/run.ps1
#include <Arduino.h>
#include <esp32-hal-spi.h>
#include <set>
#include <vector>

#include "effects.h"
#include "matrix.h"
#include "rtc.h"
#include "settings.h"
#include "ui.h"

uint32_t g_millis = 1000;
std::vector<uint8_t> g_spiOut;
SerialStub Serial;
CFastLED FastLED;
Settings settings;

void Settings::defaults() {
  *this = Settings();
  items = 0x2FF; // everything but sunrise/sunset, as in settings.cpp
}

// ---- stand-ins for the hardware modules ----
namespace rtc {
bool begin() { return true; }
bool present() { return true; }
bool read(time_t &) { return false; }
bool write(time_t) { return true; }
float temperature() { return 25.0f; }
} // namespace rtc
namespace fx {
int count() { return 17; }
const char *name(int) { return "x"; }
void frame(int, uint8_t, const char *) {}
void reset() {}
} // namespace fx

static void dump(const char *name) {
  int w = mx::W();
  printf("FRAME %s %d\n", name, w);
  CRGB *b = mx::buffer();
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < w; x++) printf("%02x%02x%02x", b[y * MAX_W + x].r, b[y * MAX_W + x].g, b[y * MAX_W + x].b);
    printf("\n");
  }
}

static void run(time_t t, uint32_t ms) {
  uint32_t end = g_millis + ms;
  while (g_millis < end) {
    g_millis += 20;
    ui::frame(t + (g_millis - 1000) / 1000, true);
  }
}

static void apply() {
  mx::configure(settings.width, settings.tileW, settings.corner, settings.columns, settings.serpentine, settings.chainRtl,
                settings.altRotated, settings.mirrorX, settings.flipY, settings.colorOrder, settings.maxMilliamps);
}

// ---------------- Mapping ----------------

static int mapOf[MAT_H][MAX_W]; // chain position of every logical pixel

// Decodes the SPI stream back to the first color byte of each LED (4 SPI bits per data bit, 1100 = 1)
static std::vector<int> firstBytes(int n) {
  std::vector<int> r(n);
  for (int i = 0; i < n; i++) {
    const uint8_t *q = &g_spiOut[i * 12];
    uint32_t bits = ((uint32_t)q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
    int v = 0;
    for (int b = 7; b >= 0; b--) {
      uint32_t nib = (bits >> (b * 4)) & 15;
      if (nib != 0b1000 && nib != 0b1100) v = -100000;  // malformed bit: the pixel ends up unmapped
      v = (v << 1) | ((nib >> 2) & 1);
    }
    r[i] = v;
  }
  return r;
}

// Two frames: each pixel shows the low, then the high part of its own number in red
static void readMap(int width, int tile, int corner, bool cols, bool serp, bool rtl, bool alt, bool mir, bool flip) {
  mx::configure(width, tile, corner, cols, serp, rtl, alt, mir, flip, 1 /* RGB: red goes first */, 0);
  memset(mapOf, -1, sizeof mapOf);
  int n = width * MAT_H;
  std::vector<int> lo, hi;
  for (int pass = 0; pass < 2; pass++) {
    mx::clear();
    for (int y = 0; y < MAT_H; y++)
      for (int x = 0; x < width; x++) {
        int k = y * width + x;
        mx::px(x, y, CRGB(pass ? k / 255 + 1 : k % 255 + 1, 0, 0));
      }
    mx::show(255);
    (pass ? hi : lo) = firstBytes(n);
  }
  for (int i = 0; i < n; i++) {
    int k = (hi[i] - 1) * 255 + (lo[i] - 1);
    if (k >= 0 && k < n) mapOf[k / width][k % width] = i;
  }
}

static int mappingTest() {
  int bad = 0;
  // every option must give every pixel its own LED
  for (int width : {32, 64})
    for (int tile : {8, 16, 32})
      for (int corner = 0; corner < 4; corner++)
        for (int cols = 0; cols < 2; cols++)
          for (int serp = 0; serp < 2; serp++)
            for (int rtl = 0; rtl < 2; rtl++)
              for (int alt = 0; alt < 2; alt++)
                for (int mir = 0; mir < 2; mir++)
                  for (int flip = 0; flip < 2; flip++) {
                    readMap(width, tile, corner, cols, serp, rtl, alt, mir, flip);
                    std::set<int> seen;
                    for (int y = 0; y < MAT_H; y++)
                      for (int x = 0; x < width; x++) seen.insert(mapOf[y][x]);
                    if ((int)seen.size() != width * MAT_H || *seen.begin() != 0 || *seen.rbegin() != width * MAT_H - 1) bad++;
                  }
  // known panels
  // 8x32, columns, zigzag, from the top left: (0,0)=0, (0,7)=7, (1,7)=8, (1,0)=15
  readMap(32, 32, 0, true, true, false, false, false, false);
  if (mapOf[0][0] != 0 || mapOf[7][0] != 7 || mapOf[7][1] != 8 || mapOf[0][1] != 15) bad += 100;
  // from the bottom left: (0,7)=0, (0,0)=7, (1,0)=8
  readMap(32, 32, 2, true, true, false, false, false, false);
  if (mapOf[7][0] != 0 || mapOf[0][0] != 7 || mapOf[0][1] != 8) bad += 1000;
  // two panels, the second one upside down: it starts at its bottom right
  readMap(64, 32, 0, true, true, false, true, false, false);
  if (mapOf[7][63] != 256 || mapOf[0][63] != 256 + 7 || mapOf[0][0] != 0) bad += 10000;
  return bad;
}

// ---------------- Frames ----------------

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  mx::begin();
  int bad = mappingTest();
  printf("MAPPING bad=%d\n", bad);

  settings.defaults();
  const time_t T = 1795005296; // Wednesday 18 Nov 2026, 14:34:56 in Israel

  struct {
    int w, face;
    const char *name;
  } faces[] = {{32, FACE_BIG, "32_big"}, {32, FACE_SECONDS, "32_seconds"}, {32, FACE_THIN, "32_thin"},
               {64, FACE_BIG, "64_big"}, {64, FACE_SECONDS, "64_seconds"}, {64, FACE_THIN, "64_thin"},
               {64, FACE_INFO, "64_info"}};
  settings.tickerEvery = 0;
  ui::begin();
  run(T, 2000); // past the greeting
  for (auto &f : faces) {
    settings.width = f.w;
    settings.face = f.face;
    apply();
    run(T, 100);
    dump(f.name);
  }
  settings.colorMode = COL_GRADIENT;
  settings.color1 = 0xFF1800;
  settings.color2 = 0xFFC000;
  settings.width = 32;
  settings.face = FACE_BIG;
  apply();
  run(T, 100);
  dump("32_gradient");
  settings.colorMode = COL_RAINBOW;
  settings.width = 64;
  apply();
  run(T, 100);
  dump("64_rainbow");
  settings.colorMode = COL_SOLID;
  settings.color1 = 0xFFB050;

  // the info ticker, every 1.3 s
  settings.face = FACE_INFO;
  apply();
  ui::startTicker();
  for (int i = 0; i < 12; i++) {
    run(T, 1300);
    char n[32];
    snprintf(n, sizeof n, "ticker_%02d", i);
    dump(n);
  }
  // Friday afternoon: candle lighting
  run(T + 2 * 86400, 60000);
  ui::startTicker();
  for (int i = 0; i < 14; i++) {
    run(T + 2 * 86400, 1300);
    char n[32];
    snprintf(n, sizeof n, "friday_%02d", i);
    dump(n);
  }
  return bad ? 1 : 0;
}
