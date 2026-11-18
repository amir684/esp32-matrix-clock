#include "ui.h"

#include "effects.h"
#include "info.h"
#include "matrix.h"
#include "settings.h"

namespace ui {

using mx::Paint;

static Mode curMode = CLOCK;
static bool powerOn = true;
static uint32_t wakeUntil = 0;
static bool isNight = false;
static uint8_t curBright = 0;
static bool dirty = false;

// The clock
static tm lt;
static time_t lastSec = 0;
static uint32_t secMillis = 0; // millis() when the second last changed
static bool timeOk = false;
static bool whiteClock = false; // party overlay: draw the clock in plain white

// Info ticker. The clock slides up and out, the ticker scrolls through, the clock slides back in.
enum Phase : uint8_t { P_CLOCK, P_OUT, P_TICKER, P_IN };
static Phase phase = P_CLOCK;
static uint32_t phaseAt = 0;
static const int MAX_SEG = 16, GAP = 9, SLIDE_MS = 220;
static Segment segs[MAX_SEG];
static int segW[MAX_SEG], segN = 0, tickTotal = 0, tickPos = 0;
static uint32_t tickStep = 0, lastTickerEnd = 0;

// Notice
static char nText[128];
static CRGB nColor;
static bool nActive = false, nScroll = false;
static uint32_t nUntil = 0, nStep = 0;
static int nW = 0, nPos = 0;

// Party
static int fxIdx = 0;
static uint32_t fxSince = 0, partySince = 0;
static CRGB fxBuf[MAT_H * MAX_W]; // the effect's own frame, kept apart from overlays

// Tests
static Test test = TEST_NONE;
static uint32_t testAt = 0;

struct Theme {
  const char *name;
  uint8_t mode;
  uint32_t c1, c2;
};
static const Theme THEMES[] = {
    {"לבן חם", COL_SOLID, 0xFFB050, 0},        {"לבן", COL_SOLID, 0xFFFFFF, 0},
    {"תכלת", COL_SOLID, 0x00B0FF, 0},          {"ירוק", COL_SOLID, 0x20FF40, 0},
    {"סגול", COL_SOLID, 0xA040FF, 0},          {"אדום", COL_SOLID, 0xFF1808, 0},
    {"שקיעה", COL_GRADIENT, 0xFF1800, 0xFFC000}, {"ים", COL_GRADIENT, 0x0040FF, 0x00FFC0},
    {"ורוד סגול", COL_GRADIENT, 0xFF0080, 0x6020FF}, {"קשת", COL_RAINBOW, 0, 0},
    {"צבע לפי השעה", COL_HOUR, 0, 0},
};
static const int THEME_N = sizeof THEMES / sizeof THEMES[0];

// ---------------- Painting the clock ----------------

static Paint clockPaint(int x0, int x1) {
  if (whiteClock) return Paint::solid(CRGB::White);
  if (isNight && settings.nightColorOn && curMode == CLOCK) return Paint::solid(CRGB(settings.nightColor));
  Paint p;
  switch (settings.colorMode) {
  case COL_GRADIENT:
    p.mode = Paint::GRADIENT;
    p.c1 = CRGB(settings.color1);
    p.c2 = CRGB(settings.color2);
    p.x0 = x0;
    p.x1 = x1;
    return p;
  case COL_RAINBOW:
    p.mode = Paint::RAINBOW;
    p.hue = millis() / 30;
    p.hueStep = 6;
    return p;
  case COL_HOUR: return Paint::solid(CHSV((lt.tm_hour * 60 + lt.tm_min) * 255 / 1440, 230, 255));
  default: return Paint::solid(CRGB(settings.color1));
  }
}

static Paint colonPaint(const Paint &digits) {
  if (whiteClock || (isNight && settings.nightColorOn && curMode == CLOCK) || !settings.colonColor) return digits;
  return Paint::solid(CRGB(settings.colonColor));
}

static CRGB dim(CRGB c, uint8_t amount) {
  c.nscale8_video(amount);
  return c;
}

// Side box of the 8x64 "clock + info" face: one short item at a time, sliding up
static void drawInfoBox(int x0, int x1, int y) {
  const char *items[4];
  uint32_t colors[4];
  int n = 0;
  items[n] = info::weekdayShort(), colors[n++] = 0xFFD8A0;
  items[n] = info::hebDateShort(), colors[n++] = 0xFFB000;
  if (info::tempShort()[0]) items[n] = info::tempShort(), colors[n++] = 0xFF4830;
  items[n] = info::gregShort(), colors[n++] = 0x00C8FF;

  const uint32_t PERIOD = 4000, SLIDE = 300;
  uint32_t ms = millis();
  int i = (ms / PERIOD) % n, next = (i + 1) % n;
  uint32_t ph = ms % PERIOD;
  int off = ph > PERIOD - SLIDE ? (ph - (PERIOD - SLIDE)) * 8 / SLIDE : 0;
  bool nightColor = isNight && settings.nightColorOn;
  mx::setClip(x0, x1);
  for (int k = 0; k < 2; k++) {
    int idx = k ? next : i;
    int yy = y - off + k * 8;
    if (k && !off) break;
    int tw = mx::textWidth(items[idx]);
    Paint p = Paint::solid(nightColor ? CRGB(settings.nightColor) : CRGB(colors[idx]));
    if (!nightColor && settings.colorMode == COL_RAINBOW) {
      p.mode = Paint::RAINBOW;
      p.hue = ms / 30 + 128;
    }
    mx::drawText(items[idx], x0 + (x1 - x0 + tw) / 2, yy, p);
  }
  mx::resetClip();
}

static void drawClock(int y) {
  int w = mx::W();
  if (!timeOk) { // no time yet: dashes, blinking
    if ((millis() / 500) & 1) {
      int tw = mx::textWidth("--:--");
      mx::drawText("--:--", (w + tw) / 2, y, Paint::solid(CRGB(settings.color1)));
    }
    return;
  }
  int h = lt.tm_hour;
  if (!settings.hour24) {
    h %= 12;
    if (!h) h = 12;
  }
  bool lead = settings.leadingZero && settings.hour24;
  char hh0 = (h >= 10 || lead) ? '0' + h / 10 : ' ';
  bool colonOn = !settings.blinkColon || millis() - secMillis < 500;
  char s[12];
  uint8_t face = settings.face;
  if (face == FACE_INFO && w < 64) face = FACE_BIG;

  switch (face) {
  case FACE_SECONDS: {
    snprintf(s, sizeof s, "%c%d:%02d", hh0, h % 10, lt.tm_min);
    int dw = mx::digitsWidth(s, mx::DF_MEDIUM);
    int total = settings.showSeconds ? dw + 2 + mx::digitsWidth("00", mx::DF_SMALL) : dw;
    int x = (w - total) / 2;
    Paint p = clockPaint(x, x + total - 1);
    Paint cp = colonPaint(p);
    mx::drawDigits(s, x, y, mx::DF_MEDIUM, p, colonOn, &cp);
    if (settings.showSeconds) {
      char ss[3] = {char('0' + lt.tm_sec / 10), char('0' + lt.tm_sec % 10), 0};
      mx::drawDigits(ss, x + dw + 2, y + 2, mx::DF_SMALL, p, true);
      int len = (lt.tm_sec + 1) * dw / 60;
      for (int i = 0; i < len; i++) mx::px(x + i, y + 7, dim(p.at(x + i, y + 7), 70));
    }
    break;
  }
  case FACE_THIN: {
    if (settings.showSeconds) snprintf(s, sizeof s, "%c%d:%02d:%02d", hh0, h % 10, lt.tm_min, lt.tm_sec);
    else snprintf(s, sizeof s, "%c%d:%02d", hh0, h % 10, lt.tm_min);
    int dw = mx::digitsWidth(s, mx::DF_THIN);
    int x = (w - dw) / 2;
    Paint p = clockPaint(x, x + dw - 1);
    Paint cp = colonPaint(p);
    mx::drawDigits(s, x, y, mx::DF_THIN, p, colonOn, &cp);
    break;
  }
  case FACE_INFO: {
    snprintf(s, sizeof s, "%c%d:%02d", hh0, h % 10, lt.tm_min);
    int dw = mx::digitsWidth(s, mx::DF_BIG); // 26
    Paint p = clockPaint(1, dw);
    Paint cp = colonPaint(p);
    mx::drawDigits(s, 1, y, mx::DF_BIG, p, colonOn, &cp);
    // a thin divider that fills up with the seconds
    int sx = dw + 2;
    for (int r = 0; r < 8; r++) {
      bool lit = settings.showSeconds && (7 - r) * 60 / 8 < lt.tm_sec + 1;
      mx::px(sx, y + r, dim(p.at(sx, y + r), lit ? 90 : 18));
    }
    drawInfoBox(sx + 2, w, y);
    break;
  }
  default: { // FACE_BIG
    if (w >= 64 && settings.showSeconds) snprintf(s, sizeof s, "%c%d:%02d:%02d", hh0, h % 10, lt.tm_min, lt.tm_sec);
    else snprintf(s, sizeof s, "%c%d:%02d", hh0, h % 10, lt.tm_min);
    int dw = mx::digitsWidth(s, mx::DF_BIG);
    int x = (w - dw) / 2;
    Paint p = clockPaint(x, x + dw - 1);
    Paint cp = colonPaint(p);
    mx::drawDigits(s, x, y, mx::DF_BIG, p, colonOn, &cp);
    break;
  }
  }
}

// ---------------- Ticker ----------------

void startTicker() {
  segN = info::buildTicker(segs, MAX_SEG);
  if (!segN) return;
  tickTotal = 0;
  for (int i = 0; i < segN; i++) {
    segW[i] = mx::textWidth(segs[i].text);
    tickTotal += segW[i] + (i ? GAP : 0);
  }
  tickPos = 0;
  tickStep = millis();
  phase = P_OUT;
  phaseAt = millis();
}

static void drawTicker() {
  int w = mx::W();
  bool nightColor = isNight && settings.nightColorOn;
  int right = tickPos; // Hebrew reads from the right: the first segment leads, entering from the left
  for (int i = 0; i < segN; i++) {
    int left = right - segW[i];
    if (left < w && right > 0) {
      Paint p = Paint::solid(nightColor ? CRGB(settings.nightColor) : segs[i].color);
      if (!nightColor && settings.tickerColors == TCOL_RAINBOW) {
        p.mode = Paint::RAINBOW;
        p.hue = millis() / 20;
        p.hueStep = 5;
      }
      mx::drawText(segs[i].text, right, 0, p);
    }
    if (i + 1 < segN) { // a small dot between items
      int dx = left - GAP / 2 - 1;
      CRGB c = nightColor ? dim(CRGB(settings.nightColor), 120) : CRGB(90, 90, 90);
      mx::px(dx, 3, c);
      mx::px(dx + 1, 3, c);
      mx::px(dx, 4, c);
      mx::px(dx + 1, 4, c);
    }
    right = left - GAP;
  }
}

static void advanceTicker() {
  uint32_t ms = millis();
  int stepMs = settings.scrollMs;
  int moves = 0;
  while (ms - tickStep >= (uint32_t)stepMs && moves < 4) {
    tickPos++;
    tickStep += stepMs;
    moves++;
  }
  if (ms - tickStep > 200) tickStep = ms; // we were held up; do not race to catch up
}

// ---------------- Notice ----------------

void notice(const char *text, CRGB color, uint32_t ms) {
  strlcpy(nText, text, sizeof nText);
  nColor = color;
  nW = mx::textWidth(nText);
  nScroll = nW > mx::W();
  nPos = 0;
  nStep = millis();
  nUntil = millis() + ms;
  nActive = true;
}

static void drawNotice() {
  int w = mx::W();
  mx::clear();
  Paint p = Paint::solid(nColor);
  if (!nColor) {
    p.mode = Paint::RAINBOW;
    p.hue = millis() / 15;
    p.hueStep = 8;
  }
  if (!nScroll) {
    mx::drawText(nText, (w + nW) / 2, 0, p);
    if ((int32_t)(millis() - nUntil) >= 0) nActive = false;
    return;
  }
  while (millis() - nStep >= 25) {
    nPos++;
    nStep += 25;
  }
  mx::drawText(nText, nPos, 0, p);
  if (nPos - nW > w) nActive = false;
}

// ---------------- Party ----------------

static void drawParty() {
  if (settings.partyMinutes && millis() - partySince > settings.partyMinutes * 60000UL) {
    setMode(CLOCK);
    return;
  }
  int n = fx::count();
  if (settings.effect) {
    int want = (settings.effect - 1) % n;
    if (want != fxIdx) {
      fxIdx = want;
      fx::reset();
      memset(fxBuf, 0, sizeof fxBuf);
    }
  } else if (millis() - fxSince > settings.effectSeconds * 1000UL) {
    fxIdx = (fxIdx + 1) % n;
    fxSince = millis();
    fx::reset();
    memset(fxBuf, 0, sizeof fxBuf);
  }
  memcpy(mx::buffer(), fxBuf, sizeof fxBuf);
  fx::frame(fxIdx, settings.partySpeed, settings.partyText);
  memcpy(fxBuf, mx::buffer(), sizeof fxBuf);

  bool showClock = timeOk && (settings.partyClock == 2 || (settings.partyClock == 1 && lt.tm_sec % 30 < 6));
  if (!showClock || nActive) return;
  // Clock on top in white, with a black outline so it stands out of any effect
  static bool mask[MAT_H][MAX_W];
  int w = mx::W();
  mx::clear();
  whiteClock = true;
  drawClock(0);
  whiteClock = false;
  CRGB *b = mx::buffer();
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++) mask[y][x] = (bool)b[y * MAX_W + x];
  memcpy(b, fxBuf, sizeof fxBuf);
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++) {
      if (!mask[y][x]) continue;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          int xx = x + dx, yy = y + dy;
          if (xx >= 0 && xx < w && yy >= 0 && yy < MAT_H && !mask[yy][xx]) b[yy * MAX_W + xx] = CRGB::Black;
        }
    }
  for (int y = 0; y < MAT_H; y++)
    for (int x = 0; x < w; x++)
      if (mask[y][x]) b[y * MAX_W + x] = CRGB::White;
}

// ---------------- Tests ----------------

// Returns true while a test owns the display (and has already sent its frame)
static bool runTest() {
  if (test == TEST_NONE) return false;
  uint32_t el = millis() - testAt;
  int w = mx::W();
  if (test == TEST_CHAIN) {
    int n = w * MAT_H;
    int idx = el / 25;
    if (idx > n + 8) {
      test = TEST_NONE;
      return false;
    }
    mx::showRaw(idx, 8, 50);
    return true;
  }
  if (test == TEST_MAP) {
    if (el > 25000) {
      test = TEST_NONE;
      return false;
    }
    mx::clear();
    mx::fillRect(0, 0, 2, 2, CRGB::Red);           // top left
    mx::fillRect(w - 2, 0, w, 2, CRGB::Green);     // top right
    mx::fillRect(0, MAT_H - 2, 2, MAT_H, CRGB::Blue); // bottom left
    mx::fillRect(w - 2, MAT_H - 2, w, MAT_H, CRGB::Yellow); // bottom right
    int run = (el / 40) % (w - 4);
    mx::px(2 + run, 0, CRGB::White); // runs along the top row from left to right
    mx::drawText("123", w / 2 + 6, 0, Paint::solid(CRGB(255, 255, 255)));
    mx::show(50);
    return true;
  }
  // TEST_COLORS: red, green, blue, white
  int ph = el / 1500;
  if (ph > 3) {
    test = TEST_NONE;
    return false;
  }
  static const CRGB C[4] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
  static const char *T[4] = {"אדום", "ירוק", "כחול", "לבן"};
  mx::fill(C[ph]);
  int tw = mx::textWidth(T[ph]);
  mx::drawText(T[ph], (w + tw) / 2, 0, Paint::solid(CRGB::Black));
  mx::show(30);
  return true;
}

void startTest(Test t) {
  test = t;
  testAt = millis();
}

// ---------------- Controls ----------------

void setMode(Mode m) {
  if (m == PARTY) {
    partySince = fxSince = millis();
    if (curMode != PARTY) {
      fxIdx = settings.effect ? (settings.effect - 1) % fx::count() : random8(fx::count());
      fx::reset();
      memset(fxBuf, 0, sizeof fxBuf);
    }
  }
  curMode = m;
  phase = P_CLOCK;
}
Mode mode() { return curMode; }
int effectIndex() { return fxIdx; }

void nextEffect() {
  int n = fx::count();
  if (settings.effect) {
    settings.effect = settings.effect % n + 1;
    dirty = true;
  } else {
    fxIdx = (fxIdx + 1) % n;
    fxSince = millis();
    fx::reset();
    memset(fxBuf, 0, sizeof fxBuf);
  }
  notice(fx::name(settings.effect ? settings.effect - 1 : fxIdx), CRGB::Black, 900);
}

void setPower(bool on) { powerOn = on; }
bool power() { return powerOn; }
void wake() { wakeUntil = millis() + 15000; }

void brightnessStep() {
  static const uint8_t DAY[] = {10, 30, 70, 140, 255}, NIGHT[] = {1, 3, 8, 20, 50};
  bool useNight = curMode == CLOCK && isNight;
  uint8_t *field = curMode == PARTY ? &settings.partyBrightness : useNight ? &settings.nightBrightness : &settings.brightness;
  const uint8_t *lv = useNight ? NIGHT : DAY;
  uint8_t next = lv[0];
  for (int i = 0; i < 5; i++)
    if (lv[i] > *field) {
      next = lv[i];
      break;
    }
  *field = next;
  dirty = true;
  char b[32];
  snprintf(b, sizeof b, "%d%%", max(1, next * 100 / 255));
  notice(b, CRGB(255, 255, 255), 1200);
}

void nextTheme() {
  int cur = -1;
  for (int i = 0; i < THEME_N; i++)
    if (THEMES[i].mode == settings.colorMode &&
        (THEMES[i].mode >= COL_RAINBOW || (THEMES[i].c1 == settings.color1 &&
                                           (THEMES[i].mode == COL_SOLID || THEMES[i].c2 == settings.color2)))) {
      cur = i;
      break;
    }
  const Theme &t = THEMES[(cur + 1) % THEME_N];
  settings.colorMode = t.mode;
  if (t.mode <= COL_GRADIENT) {
    settings.color1 = t.c1;
    if (t.mode == COL_GRADIENT) settings.color2 = t.c2;
  }
  dirty = true;
  notice(t.name, t.mode == COL_SOLID ? CRGB(t.c1) : t.mode == COL_GRADIENT ? CRGB(t.c2) : CRGB::Black, 1200);
}

bool night() { return isNight; }
uint8_t brightness() { return curBright; }
bool takeDirty() {
  bool d = dirty;
  dirty = false;
  return d;
}

// ---------------- Frame ----------------

void begin() {
  curBright = 0;
  notice("שלום", CRGB::Black, 1500);
}

static void updateNight(time_t now) {
  if (!timeOk || !settings.nightOn) {
    isNight = false;
    return;
  }
  if (settings.nightBySun) {
    isNight = info::sunNight(now);
    return;
  }
  int m = lt.tm_hour * 60 + lt.tm_min;
  int a = settings.nightFrom, b = settings.nightTo;
  if (a == b) isNight = false;
  else if (a < b) isNight = m >= a && m < b;
  else isNight = m >= a || m < b;
}

void frame(time_t now, bool valid) {
  timeOk = valid;
  if (now != lastSec) {
    lastSec = now;
    secMillis = millis();
    localtime_r(&now, &lt);
    if (valid) info::update(now);
    updateNight(now);
  }

  // Brightness, with a soft ramp
  uint8_t target;
  bool nightOff = curMode == CLOCK && isNight && settings.nightOff && millis() > wakeUntil;
  if (!powerOn || nightOff) target = 0;
  else if (curMode == PARTY) target = settings.partyBrightness;
  else if (isNight) target = settings.nightBrightness;
  else target = settings.brightness;
  if (curBright < target) curBright += max(1, (target - curBright) / 6);
  else if (curBright > target) curBright -= max(1, (curBright - target) / 6);

  if (runTest()) return;

  if (curMode == PARTY) {
    drawParty();
    if (nActive) drawNotice();
    mx::show(curBright);
    return;
  }

  // Clock mode
  if (nActive) {
    drawNotice();
    mx::show(curBright);
    return;
  }
  bool tickerAllowed = valid && settings.tickerEvery && (!isNight || settings.nightTicker);
  if (phase == P_CLOCK && tickerAllowed && millis() - lastTickerEnd >= settings.tickerEvery * 1000UL) startTicker();

  mx::clear();
  uint32_t el = millis() - phaseAt;
  switch (phase) {
  case P_OUT:
    if (el >= (uint32_t)SLIDE_MS) {
      phase = P_TICKER;
      tickStep = millis();
    } else {
      drawClock(-(int)(el * 8 / SLIDE_MS));
    }
    break;
  case P_TICKER:
    advanceTicker();
    drawTicker();
    if (tickPos - tickTotal > mx::W()) {
      phase = P_IN;
      phaseAt = millis();
    }
    break;
  case P_IN:
    if (el >= (uint32_t)SLIDE_MS) {
      phase = P_CLOCK;
      lastTickerEnd = millis();
      drawClock(0);
    } else {
      drawClock(8 - (int)(el * 8 / SLIDE_MS));
    }
    break;
  default: drawClock(0);
  }
  mx::show(curBright);
}

} // namespace ui
