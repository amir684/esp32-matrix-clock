#include "effects.h"

#include "matrix.h"

namespace fx {

static const int H = MAT_H;
static uint32_t t = 0;   // animation time, advances faster with the speed setting
static uint32_t step = 0; // frames since reset
static uint8_t spd = 5;

// ---------------- Effects ----------------

static void rainbow() {
  int w = mx::W();
  uint8_t base = t >> 4;
  for (int x = 0; x < w; x++)
    for (int y = 0; y < H; y++) mx::px(x, y, CHSV(base + x * 5 + y * 10, 255, 255));
}

static void plasma() {
  int w = mx::W();
  uint16_t tt = t >> 2;
  for (int x = 0; x < w; x++)
    for (int y = 0; y < H; y++) {
      uint8_t v = sin8(x * 12 + tt) / 3 + sin8(y * 40 + tt * 3 / 2) / 3 + sin8((x + y) * 10 + tt / 2) / 3;
      mx::px(x, y, ColorFromPalette(PartyColors_p, v + (tt >> 2)));
    }
}

static void noise(const TProgmemRGBPalette16 &pal, int scale) {
  int w = mx::W();
  for (int x = 0; x < w; x++)
    for (int y = 0; y < H; y++) {
      uint8_t v = inoise8(x * scale, y * scale * 2, t >> 1);
      mx::px(x, y, ColorFromPalette(pal, qadd8(v, v / 3)));
    }
}

static uint8_t heat[MAX_W][MAT_H];

static void fire() {
  int w = mx::W();
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < H; y++) heat[x][y] = qsub8(heat[x][y], random8(0, 60));
    for (int y = 0; y < H - 1; y++) // heat rises: row 7 is the bottom
      heat[x][y] = (heat[x][y + 1] * 2 + heat[x][min(y + 2, H - 1)]) / 3;
    if (random8() < 140) heat[x][H - 1] = qadd8(heat[x][H - 1], random8(120, 255));
    for (int y = 0; y < H; y++) mx::px(x, y, HeatColor(scale8(heat[x][y], 230)));
  }
}

struct Drop {
  int8_t y;
  uint8_t speed, wait;
};
static Drop drops[MAX_W];

static void matrixRain() {
  int w = mx::W();
  mx::fade(50);
  for (int x = 0; x < w; x++) {
    Drop &d = drops[x];
    if (d.speed == 0) {
      d.y = -random8(0, 16);
      d.speed = random8(2, 6);
    }
    if (++d.wait < d.speed) continue;
    d.wait = 0;
    d.y++;
    if (d.y >= 0 && d.y < H) {
      mx::px(x, d.y, CRGB(180, 255, 180));
      if (d.y > 0) mx::px(x, d.y - 1, CRGB(0, 200, 30));
    }
    if (d.y > H + random8(0, 10)) d.speed = 0;
  }
}

static void confetti() {
  int w = mx::W();
  mx::fade(25);
  for (int i = 0; i < w / 16; i++)
    if (random8() < 120) mx::px(random8(w), random8(H), CHSV((t >> 5) + random8(64), 200, 255));
}

static void stars() {
  int w = mx::W();
  mx::fade(8);
  if (random8() < 90) {
    uint8_t kind = random8(3);
    CRGB c = kind == 0 ? CRGB(255, 255, 255) : kind == 1 ? CRGB(120, 160, 255) : CRGB(255, 200, 120);
    mx::px(random8(w), random8(H), c);
  }
}

static void scanner() {
  int w = mx::W();
  mx::fade(40);
  for (int k = 0; k < 3; k++) {
    int pos = beatsin16(10 + k * 3, 0, w - 1, 0, k * 20000);
    int y = beatsin8(7 + k * 4, 0, H - 1, 0, k * 80);
    CRGB c = CHSV((t >> 4) + k * 85, 255, 255);
    for (int dy = -1; dy <= 1; dy++) mx::add(pos, y + dy, c);
    mx::add(pos - 1, y, c);
    mx::add(pos + 1, y, c);
  }
}

static uint8_t level[MAX_W / 2], target[MAX_W / 2], peak[MAX_W / 2];

static void equalizer() {
  int w = mx::W(), bars = w / 2;
  mx::clear();
  // A made-up beat: every bar jumps on the beat, with bass on the left
  bool beat = (step % (12 - spd / 2)) == 0;
  for (int b = 0; b < bars; b++) {
    if (beat || random8() < 30) {
      int bass = 255 - b * 160 / bars;
      target[b] = random8(bass / 4, bass);
    }
    level[b] = level[b] < target[b] ? qadd8(level[b], 40) : qsub8(level[b], 12);
    if (target[b] > 20) target[b] -= 20;
    int h = level[b] * H / 256;
    if (h >= peak[b] / 32) peak[b] = h * 32 + 31;
    else if (peak[b] > 4) peak[b] -= 4;
    for (int y = 0; y < h; y++) {
      CRGB c = CHSV(96 - y * 96 / H, 255, 255); // green at the bottom to red at the top
      mx::px(b * 2, H - 1 - y, c);
      mx::px(b * 2 + 1, H - 1 - y, c);
    }
    int p = peak[b] / 32;
    if (p > 0 && p <= H) {
      CRGB c = CHSV((t >> 4) + b * 8, 180, 255);
      mx::px(b * 2, H - p, c);
      mx::px(b * 2 + 1, H - p, c);
    }
  }
}

struct Meteor {
  int16_t x; // x8 fixed point
  int8_t y;
  uint8_t hue, speed;
  bool live;
};
static Meteor meteors[8];

static void meteor() {
  int w = mx::W();
  mx::fade(60);
  for (Meteor &m : meteors) {
    if (!m.live) {
      if (random8() < 12) m = {(int16_t)(-random8(0, 40) * 8), (int8_t)random8(H), random8(), (uint8_t)random8(6, 16), true};
      continue;
    }
    m.x += m.speed;
    int x = m.x / 8;
    mx::px(x, m.y, CRGB::White);
    mx::add(x - 1, m.y, CHSV(m.hue, 200, 160));
    if (x > w + 4) m.live = false;
  }
}

static void pulse() {
  int w = mx::W();
  uint8_t beat = beatsin8(60 + spd * 6, 0, 255);
  uint8_t hue = t >> 5;
  for (int x = 0; x < w; x++)
    for (int y = 0; y < H; y++) {
      int dx = x - w / 2, dy = (y - H / 2) * 3;
      uint8_t dist = sqrt16(dx * dx + dy * dy) * 6;
      uint8_t v = sin8(dist - (t >> 1));
      mx::px(x, y, CHSV(hue + dist / 2, 255, scale8(v, 100 + beat / 2)));
    }
}

struct Ripple {
  int8_t x, y;
  uint8_t r, hue;
};
static Ripple ripples[4];

static void ripple() {
  int w = mx::W();
  mx::fade(70);
  for (Ripple &rp : ripples) {
    if (rp.r == 0 || rp.r > 40) {
      if (random8() < 20) rp = {(int8_t)random8(w), (int8_t)random8(H), 1, random8()};
      continue;
    }
    for (int x = 0; x < w; x++)
      for (int y = 0; y < H; y++) {
        int dx = x - rp.x, dy = (y - rp.y) * 2;
        int d = sqrt16(dx * dx + dy * dy);
        if (d == rp.r) mx::add(x, y, CHSV(rp.hue, 220, 255 - rp.r * 6));
      }
    if (step % 2 == 0) rp.r++;
  }
}

struct Particle {
  int16_t x, y, vx, vy; // x16 fixed point
  uint8_t hue, life;
};
static Particle sparks[40];
static int16_t rocketX, rocketY;
static bool rocket = false;
static uint8_t rocketHue;

static void fireworks() {
  int w = mx::W();
  mx::fade(45);
  if (!rocket && random8() < 10) {
    rocket = true;
    rocketX = random8(4, w - 4) * 16;
    rocketY = (H - 1) * 16;
    rocketHue = random8();
  }
  if (rocket) {
    rocketY -= 10;
    mx::px(rocketX / 16, rocketY / 16, CRGB(255, 200, 120));
    if (rocketY < random8(16, 48)) { // burst
      rocket = false;
      for (Particle &p : sparks) {
        if (p.life) continue;
        uint8_t a = random8();
        int sp = random8(4, 18);
        p = {rocketX, rocketY, (int16_t)((cos8(a) - 128) * sp / 64), (int16_t)((sin8(a) - 128) * sp / 128), (uint8_t)(rocketHue + random8(30)), (uint8_t)random8(20, 40)};
        if (random8() < 10) break; // about 25 sparks per burst
      }
    }
  }
  for (Particle &p : sparks) {
    if (!p.life) continue;
    p.x += p.vx;
    p.y += p.vy;
    p.vy += 1; // gravity
    p.life--;
    mx::add(p.x / 16, p.y / 16, CHSV(p.hue, 200, min(255, p.life * 10)));
    if (p.y / 16 > H) p.life = 0;
  }
}

static int textX = 0;

static void scrollText(const char *text) {
  int w = mx::W();
  mx::clear();
  // background glow
  for (int x = 0; x < w; x++) {
    CRGB c = CHSV((t >> 3) - x * 4, 255, 40);
    for (int y = 0; y < H; y++) mx::px(x, y, c);
  }
  int tw = mx::textWidth(text);
  if (step == 0 || textX - tw > w) textX = 0; // enters from the left: Hebrew reads from the right
  if (step % 2 == 0) textX++;
  mx::Paint p;
  p.mode = mx::Paint::RAINBOW;
  p.hue = t >> 3;
  p.hueStep = 9;
  mx::drawText(text, textX, 0, p);
}

// ---------------- Table ----------------

struct Fx {
  const char *name;
  void (*fn)();
};

static void lava() { noise(LavaColors_p, 28); }
static void ocean() { noise(OceanColors_p, 24); }
static void forest() { noise(ForestColors_p, 30); }
static void party() { noise(PartyColors_p, 20); }

static const Fx TABLE[] = {
    {"קשת", rainbow},   {"פלזמה", plasma},   {"אש", fire},         {"גשם דיגיטלי", matrixRain},
    {"קונפטי", confetti}, {"לבה", lava},      {"אוקיינוס", ocean},  {"יער", forest},
    {"מסיבה", party},    {"כוכבים", stars},  {"סורק", scanner},    {"אקולייזר", equalizer},
    {"מטאורים", meteor}, {"פעימה", pulse},   {"אדוות", ripple},    {"זיקוקים", fireworks},
    {"טקסט", nullptr},
};
static const int N = sizeof TABLE / sizeof TABLE[0];

int count() { return N; }
const char *name(int i) { return (i >= 0 && i < N) ? TABLE[i].name : ""; }

void reset() {
  step = 0;
  memset(heat, 0, sizeof heat);
  memset(drops, 0, sizeof drops);
  memset(level, 0, sizeof level);
  memset(target, 0, sizeof target);
  memset(peak, 0, sizeof peak);
  memset(meteors, 0, sizeof meteors);
  memset(ripples, 0, sizeof ripples);
  memset(sparks, 0, sizeof sparks);
  rocket = false;
  mx::clear();
}

void frame(int i, uint8_t speed, const char *text) {
  spd = constrain(speed, 1, 10);
  t += spd * 4;
  if (i < 0 || i >= N) i = 0;
  if (TABLE[i].fn) TABLE[i].fn();
  else scrollText(text && *text ? text : "PARTY!");
  step++;
}

} // namespace fx
