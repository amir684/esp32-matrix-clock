#include "games.h"

#include "matrix.h"

#ifndef GAMES_NO_NVS
#include <Preferences.h>
#endif

namespace game {

using mx::Paint;

static const int H = MAT_H;
static int W = 32;
static int cur = -1;
static int sc = 0;
static bool isOver = false, isRecord = false, waiting = false;
static uint32_t overAt = 0, lastInput = 0, lastFrame = 0;
static bool heldK[KEY_N];
static uint32_t heldAt[KEY_N];
static float dt = 0; // seconds since the previous frame

static const int N_GAMES = 4;
static const char *const NAMES[N_GAMES] = {"סנייק", "דינוזאור", "פלאפי", "טטריס"};
static uint16_t bests[N_GAMES];
static uint8_t levels[N_GAMES] = {3, 3, 3, 3};
static bool ramps[N_GAMES] = {true, true, true, true};
static bool bestsLoaded = false;
static int lv = 2;          // level of the game on screen, 0..4
static bool speedUp = true; // it speeds up while playing
static const uint32_t IDLE_MS = 90000;

// ---------------- helpers ----------------

static uint32_t rng = 1;
static uint32_t rnd() { // xorshift32
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
static int irand(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }

static bool held(Key k) { return heldK[k] && millis() - heldAt[k] < 1500; }

static void loadBests() {
  if (bestsLoaded) return;
  bestsLoaded = true;
#ifndef GAMES_NO_NVS
  Preferences p;
  if (p.begin("games", true)) {
    for (int i = 0; i < N_GAMES; i++) {
      char k[4] = {'b', char('0' + i), 0};
      bests[i] = p.getUShort(k, 0);
      k[0] = 'l';
      levels[i] = constrain(p.getUChar(k, 3), 1, 5);
      k[0] = 'r';
      ramps[i] = p.getBool(k, true);
    }
    p.end();
  }
#endif
}

static void saveBest(int i) {
#ifndef GAMES_NO_NVS
  Preferences p;
  if (p.begin("games", false)) {
    char k[4] = {'b', char('0' + i), 0};
    p.putUShort(k, bests[i]);
    p.end();
  }
#else
  (void)i;
#endif
}

static void saveSpeed(int i) {
#ifndef GAMES_NO_NVS
  Preferences p;
  if (p.begin("games", false)) {
    char k[4] = {'l', char('0' + i), 0};
    p.putUChar(k, levels[i]);
    k[0] = 'r';
    p.putBool(k, ramps[i]);
    p.end();
  }
#else
  (void)i;
#endif
}

static void gameOver() {
  if (isOver) return;
  isOver = true;
  overAt = millis();
  isRecord = sc > bests[cur];
  if (isRecord) {
    bests[cur] = sc;
    saveBest(cur);
  }
}

// ---------------- snake ----------------
// The field wraps around at the edges (with only 8 rows, walls would end it too soon).

namespace snake {
static const int N = MAX_W * MAT_H;
static uint16_t cells[N]; // ring buffer of y * MAX_W + x, newest = head
static int headI, len;
static int dx, dy, ndx, ndy;
static int fx, fy;
static uint32_t stepAt;

static uint16_t at(int i) { return cells[(headI - i + N) % N]; } // 0 = head
static int cx(uint16_t c) { return c % MAX_W; }
static int cy(uint16_t c) { return c / MAX_W; }
static bool onSnake(int x, int y, int skipTail) {
  uint16_t c = y * MAX_W + x;
  for (int i = 0; i < len - skipTail; i++)
    if (at(i) == c) return true;
  return false;
}
static void placeFood() {
  if (len >= W * H) {
    gameOver();
    return;
  }
  do {
    fx = irand(W);
    fy = irand(H);
  } while (onSnake(fx, fy, 0));
}
static void init() {
  len = 3;
  headI = 2;
  for (int i = 0; i < 3; i++) cells[i] = 4 * MAX_W + 3 + i;
  dx = ndx = 1;
  dy = ndy = 0;
  stepAt = millis();
  placeFood();
}
static void turn(int x, int y) {
  if (x == -dx && y == -dy) return; // no turning back into itself
  ndx = x;
  ndy = y;
}
static void key(Key k) {
  if (k == LEFT) turn(-1, 0);
  else if (k == RIGHT) turn(1, 0);
  else if (k == UP) turn(0, -1);
  else if (k == DOWN) turn(0, 1);
}
// Touch buttons turn relative to the way it goes: 1 = left, 3 = right
static void button(int b, bool) {
  if (b == 0) turn(ndy, -ndx);
  else if (b == 2) turn(-ndy, ndx);
}
static void tick() {
  static const int BASE[5] = {320, 240, 180, 135, 100};
  int iv = BASE[lv];
  if (speedUp) iv = max(BASE[lv] * 45 / 100, iv - (len - 3) * 5);
  if (millis() - stepAt < (uint32_t)iv) return;
  stepAt = millis();
  dx = ndx;
  dy = ndy;
  int x = (cx(at(0)) + dx + W) % W, y = (cy(at(0)) + dy + H) % H;
  bool eat = x == fx && y == fy;
  if (onSnake(x, y, eat ? 0 : 1)) {
    gameOver();
    return;
  }
  headI = (headI + 1) % N;
  cells[headI] = y * MAX_W + x;
  if (eat) {
    len++;
    sc++;
    placeFood();
  }
}
static void draw() {
  uint8_t pulse = 120 + (uint8_t)(sin8(millis() / 3) / 2);
  mx::px(fx, fy, CRGB(pulse, 0, pulse / 8));
  for (int i = len - 1; i >= 0; i--) {
    uint16_t c = at(i);
    CRGB col = i == 0 ? CRGB(220, 255, 90) : CRGB(CHSV(96 + (i * 3) % 40, 255, 255 - min(i * 5, 160)));
    mx::px(cx(c), cy(c), col);
  }
}
} // namespace snake

// ---------------- dino ----------------
// Runs on row 7. Jump over cacti, duck under birds (they come once the score reaches 8).

namespace dino {
struct Ob {
  float x;
  uint8_t w, h;
  bool bird;
};
static const int MAX_OB = 8, DX = 2;
static Ob obs[MAX_OB];
static int nObs;
static float y, vy, speed, nextAt, groundOff;
static uint32_t duckUntil;
// A jump is always about 3.9 rows high and about 10 columns long, whatever the speed: slow levels get a
// slower, floatier jump instead of one that lands before the cactus has passed
static float g = 46;
static const float JUMP_H = 3.9f, JUMP_LEN = 10.5f;

static void init() {
  nObs = 0;
  y = vy = 0;
  static const float START[5] = {9, 12, 15, 19, 24};
  speed = START[lv];
  nextAt = 12;
  groundOff = 0;
  duckUntil = 0;
  waiting = true;
}
static bool ducking() { return y == 0 && vy == 0 && (held(DOWN) || millis() < duckUntil); }
static void jump() {
  waiting = false;
  if (y == 0 && vy == 0) {
    float T = constrain(JUMP_LEN / speed, 0.5f, 1.1f); // time in the air
    vy = 4 * JUMP_H / T;
    g = 8 * JUMP_H / (T * T);
  }
}
static void key(Key k) {
  if (k == A || k == UP) jump();
  else if (k == DOWN) {
    waiting = false;
    duckUntil = millis() + 350;
  }
}
static void button(int b, bool) {
  if (b == 2) {
    waiting = false;
    duckUntil = millis() + 600;
  } else jump();
}
// The dino's box: x0..x1, y0..y1 inclusive
static void box(int &x0, int &x1, int &y0, int &y1) {
  if (ducking()) {
    x0 = DX, x1 = DX + 2, y0 = 5, y1 = 6;
  } else {
    int b = 6 - (int)y;
    x0 = DX, x1 = DX + 1, y0 = b - 2, y1 = b;
  }
}
static void tick() {
  if (waiting) return;
  if (y > 0 || vy > 0) {
    vy -= g * dt;
    y += vy * dt;
    if (y <= 0) y = vy = 0;
  }
  float d = speed * dt;
  groundOff += d;
  if (speedUp) speed = min(34.f, speed + 0.3f * dt);
  int k = 0;
  for (int i = 0; i < nObs; i++) {
    obs[i].x -= d;
    if (obs[i].x + obs[i].w < 0) sc++;
    else obs[k++] = obs[i];
  }
  nObs = k;
  nextAt -= d;
  if (nextAt <= 0 && nObs < MAX_OB) {
    Ob o;
    o.x = W;
    o.bird = sc >= 8 && irand(4) == 0;
    if (o.bird) o.w = 2, o.h = 0;
    else {
      o.w = 1 + irand(2);
      o.h = 1 + irand(o.w == 2 ? 2 : 3);
    }
    obs[nObs++] = o;
    nextAt = speed * 0.9f + 5 + irand(14); // always room to land between two
  }
  int x0, x1, y0, y1;
  box(x0, x1, y0, y1);
  for (int i = 0; i < nObs; i++) {
    const Ob &o = obs[i];
    int ox0 = (int)floorf(o.x), ox1 = ox0 + o.w - 1;
    int oy0 = o.bird ? 3 : 7 - o.h, oy1 = o.bird ? 4 : 6;
    if (ox1 >= x0 && ox0 <= x1 && oy1 >= y0 && oy0 <= y1) {
      gameOver();
      return;
    }
  }
}
static void draw() {
  int off = (int)groundOff;
  for (int x = 0; x < W; x++) mx::px(x, 7, (x + off) % 6 == 0 ? CRGB(90, 50, 10) : CRGB(30, 16, 4));
  for (int i = 0; i < nObs; i++) {
    const Ob &o = obs[i];
    int ox = (int)floorf(o.x);
    if (o.bird) {
      bool up = (millis() / 180) & 1;
      mx::px(ox, 4, CRGB(60, 150, 255));
      mx::px(ox + 1, 4, CRGB(60, 150, 255));
      mx::px(ox + (up ? 0 : 1), 3, CRGB(30, 90, 200));
    } else {
      for (int c = 0; c < o.w; c++)
        for (int r = 0; r < o.h; r++) mx::px(ox + c, 6 - r, CRGB(20, 190, 40));
    }
  }
  int x0, x1, y0, y1;
  box(x0, x1, y0, y1);
  CRGB body(255, 190, 40);
  mx::fillRect(x0, y0, x1 + 1, y1 + 1, body);
  mx::px(x1, y0, CRGB(255, 255, 255)); // the eye side
  if (waiting && ((millis() / 400) & 1)) mx::px(W - 2, 2, CRGB(255, 255, 255)); // "press to start"
}
} // namespace dino

// ---------------- flappy ----------------

namespace flappy {
struct Pipe {
  float x;
  int8_t gy; // first row of the gap
  bool passed;
};
static const int MAX_P = 10, BX = 6, GAP = 3, PW = 2;
static Pipe pipes[MAX_P];
static int n;
static float y, vy, speed, nextAt;
static int lastGy;

static void init() {
  n = 0;
  y = 3.5f;
  vy = 0;
  static const float START[5] = {6, 8, 10, 12, 14};
  speed = START[lv];
  nextAt = 6;
  lastGy = 2;
  waiting = true;
}
static void flap() {
  waiting = false;
  vy = -9.5f;
}
static void key(Key k) {
  if (k == A || k == UP) flap();
}
static void button(int, bool) { flap(); }
static void tick() {
  if (waiting) {
    y = 3.5f + sinf(millis() / 220.0f) * 0.7f;
    return;
  }
  vy = min(14.f, vy + 28 * dt);
  y += vy * dt;
  if (y < 0) y = 0, vy = 0;
  if (y >= H) {
    gameOver();
    return;
  }
  float d = speed * dt;
  if (speedUp) speed = min(18.f, speed + 0.12f * dt);
  int k = 0;
  for (int i = 0; i < n; i++) {
    pipes[i].x -= d;
    if (!pipes[i].passed && pipes[i].x + PW <= BX) {
      pipes[i].passed = true;
      sc++;
    }
    if (pipes[i].x + PW >= 0) pipes[k++] = pipes[i];
  }
  n = k;
  nextAt -= d;
  if (nextAt <= 0 && n < MAX_P) {
    int gy = constrain(lastGy + irand(7) - 3, 0, H - GAP);
    lastGy = gy;
    pipes[n++] = {(float)W, (int8_t)gy, false};
    nextAt = 10 + irand(4);
  }
  int by = (int)y;
  for (int i = 0; i < n; i++) {
    int px = (int)floorf(pipes[i].x);
    if (BX >= px && BX < px + PW && (by < pipes[i].gy || by >= pipes[i].gy + GAP)) {
      gameOver();
      return;
    }
  }
}
static void draw() {
  for (int i = 0; i < n; i++) {
    int px = (int)floorf(pipes[i].x);
    for (int r = 0; r < H; r++) {
      if (r >= pipes[i].gy && r < pipes[i].gy + GAP) continue;
      mx::px(px, r, CRGB(20, 200, 40));
      mx::px(px + 1, r, CRGB(10, 110, 20));
    }
  }
  int by = constrain((int)y, 0, H - 1);
  mx::px(BX, by, CRGB(255, 220, 0));
  mx::px(BX - 1, by, CRGB(200, 80, 0));
}
} // namespace flappy

// ---------------- tetris ----------------
// Played sideways: the 8 rows of the display are the 8 columns of the well, and pieces fall from the
// right edge to the left one. Up / down move a piece, A rotates, left drops it.

namespace tetris {
static const int C = MAT_H;
static uint8_t board[MAX_W][C]; // [depth][column], depth 0 = the left edge (the bottom of the well)
static int D;                   // depth of the well = display width
static const int8_t SHAPES[7][4][2] = {
    {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, // I
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, // O
    {{0, 1}, {1, 1}, {2, 1}, {1, 2}}, // T
    {{0, 1}, {1, 1}, {1, 2}, {2, 2}}, // S
    {{1, 1}, {2, 1}, {0, 2}, {1, 2}}, // Z
    {{0, 1}, {1, 1}, {2, 1}, {0, 2}}, // J
    {{0, 1}, {1, 1}, {2, 1}, {2, 2}}, // L
};
static const uint8_t BOX[7] = {4, 2, 3, 3, 3, 3, 3};
static const CRGB COLORS[8] = {CRGB::Black,        CRGB(0, 220, 255), CRGB(255, 220, 0), CRGB(170, 0, 255),
                               CRGB(0, 230, 40),   CRGB(255, 20, 0),  CRGB(0, 60, 255),  CRGB(255, 110, 0)};
static int8_t cell[4][2]; // (column, depth) inside the piece's box
static int type, pc, pd, lines;
static uint32_t fallAt, moveAt;

static bool fits(const int8_t (*cs)[2], int c0, int d0) {
  for (int i = 0; i < 4; i++) {
    int c = c0 + cs[i][0], d = d0 + cs[i][1];
    if (c < 0 || c >= C || d < 0 || d >= D || board[d][c]) return false;
  }
  return true;
}
static void spawn() {
  type = irand(7);
  memcpy(cell, SHAPES[type], sizeof cell);
  pc = (C - BOX[type]) / 2;
  pd = D - BOX[type];
  fallAt = millis();
  if (!fits(cell, pc, pd)) gameOver();
}
static void init() {
  D = W;
  memset(board, 0, sizeof board);
  lines = 0;
  spawn();
}
static void move(int dc) {
  if (fits(cell, pc + dc, pd)) pc += dc;
}
static void rotate() {
  if (type == 1) return; // O
  int n = BOX[type];
  int8_t r[4][2];
  for (int i = 0; i < 4; i++) {
    r[i][0] = cell[i][1];
    r[i][1] = n - 1 - cell[i][0];
  }
  for (int kick : {0, -1, 1, -2, 2})
    if (fits(r, pc + kick, pd)) {
      memcpy(cell, r, sizeof cell);
      pc += kick;
      return;
    }
}
static void lock() {
  for (int i = 0; i < 4; i++) board[pd + cell[i][1]][pc + cell[i][0]] = type + 1;
  int cleared = 0, w = 0;
  for (int d = 0; d < D; d++) {
    bool full = true;
    for (int c = 0; c < C; c++) full = full && board[d][c];
    if (full) {
      cleared++;
      continue;
    }
    if (w != d) memcpy(board[w], board[d], C);
    w++;
  }
  for (; w < D; w++) memset(board[w], 0, C);
  static const uint8_t POINTS[5] = {0, 1, 3, 5, 8};
  sc += POINTS[cleared];
  lines += cleared;
  spawn();
}
static void step() {
  if (fits(cell, pc, pd - 1)) pd--;
  else lock();
  fallAt = millis();
}
static void drop() {
  while (fits(cell, pc, pd - 1)) pd--;
  lock();
}
static void key(Key k) {
  if (k == UP) move(-1), moveAt = millis() + 160;
  else if (k == DOWN) move(1), moveAt = millis() + 160;
  else if (k == A || k == RIGHT) rotate();
  else if (k == LEFT) drop();
}
static void button(int b, bool isLong) {
  if (b == 0) move(-1);
  else if (b == 2) move(1);
  else if (isLong) drop();
  else rotate();
}
static void tick() {
  // held up / down keeps moving after a short pause
  if (millis() >= moveAt) {
    if (held(UP)) move(-1), moveAt = millis() + 70;
    else if (held(DOWN)) move(1), moveAt = millis() + 70;
  }
  static const int BASE[5] = {900, 650, 450, 300, 180};
  int iv = BASE[lv];
  if (speedUp) iv = max(80, iv - (lines / 6) * 55);
  if (millis() - fallAt >= (uint32_t)iv) step();
}
static void draw() {
  for (int d = 0; d < D; d++)
    for (int c = 0; c < C; c++)
      if (board[d][c]) {
        CRGB col = COLORS[board[d][c]];
        col.nscale8(170);
        mx::px(d, c, col);
      }
  if (!isOver)
    for (int i = 0; i < 4; i++) mx::px(pd + cell[i][1], pc + cell[i][0], COLORS[type + 1]);
}
} // namespace tetris

// ---------------- common ----------------

int count() { return N_GAMES; }
const char *name(int i) { return i >= 0 && i < N_GAMES ? NAMES[i] : ""; }
int current() { return cur; }
int score() { return sc; }
bool over() { return isOver; }
int best(int i) {
  loadBests();
  return i >= 0 && i < N_GAMES ? bests[i] : 0;
}
int level(int i) {
  loadBests();
  return i >= 0 && i < N_GAMES ? levels[i] : 3;
}
bool ramp(int i) {
  loadBests();
  return i >= 0 && i < N_GAMES ? ramps[i] : true;
}
void setSpeed(int i, int l, bool r) {
  if (i < 0 || i >= N_GAMES) return;
  loadBests();
  l = constrain(l, 1, 5);
  if (levels[i] == l && ramps[i] == r) return;
  levels[i] = l;
  ramps[i] = r;
  saveSpeed(i);
  if (cur == i) start(i);
}

void start(int i) {
  if (i < 0 || i >= N_GAMES) return;
  loadBests();
  rng ^= millis() * 2654435761u + 1;
  if (!rng) rng = 1;
  cur = i;
  lv = levels[i] - 1;
  speedUp = ramps[i];
  W = mx::W();
  sc = 0;
  isOver = isRecord = waiting = false;
  memset(heldK, 0, sizeof heldK);
  lastInput = lastFrame = millis();
  switch (cur) {
  case 0: snake::init(); break;
  case 1: dino::init(); break;
  case 2: flappy::init(); break;
  default: tetris::init(); break;
  }
}

void stop() { cur = -1; }

void resetAll() {
  stop();
#ifndef GAMES_NO_NVS
  Preferences p;
  if (p.begin("games", false)) {
    p.clear();
    p.end();
  }
#endif
  for (int i = 0; i < N_GAMES; i++) {
    bests[i] = 0;
    levels[i] = 3;
    ramps[i] = true;
  }
  bestsLoaded = true;
}

static bool canRestart() { return isOver && millis() - overAt > 900; }

void key(Key k, bool down, bool keepalive) {
  if (cur < 0 || k >= KEY_N) return;
  lastInput = millis();
  heldK[k] = down;
  heldAt[k] = millis();
  if (!down || keepalive) return;
  if (isOver) {
    if (canRestart()) start(cur);
    return;
  }
  switch (cur) {
  case 0: snake::key(k); break;
  case 1: dino::key(k); break;
  case 2: flappy::key(k); break;
  default: tetris::key(k); break;
  }
}

void button(int which, bool isLong) {
  if (cur < 0) return;
  lastInput = millis();
  if (isOver) {
    if (canRestart()) start(cur);
    return;
  }
  switch (cur) {
  case 0: snake::button(which, isLong); break;
  case 1: dino::button(which, isLong); break;
  case 2: flappy::button(which, isLong); break;
  default: tetris::button(which, isLong); break;
  }
}

static void drawGame() {
  switch (cur) {
  case 0: snake::draw(); break;
  case 1: dino::draw(); break;
  case 2: flappy::draw(); break;
  default: tetris::draw(); break;
  }
}

// Game over: a red flash over the last picture, then the score (rainbow and "שיא!" for a new best)
static void drawOver() {
  uint32_t el = millis() - overAt;
  if (el < 900) {
    drawGame();
    if ((el / 150) % 2 == 0) {
      CRGB *b = mx::buffer();
      for (int i = 0; i < MAT_H * MAX_W; i++)
        if (b[i]) b[i] = CRGB(max(b[i].r, max(b[i].g, b[i].b)), 0, 0);
    }
    return;
  }
  Paint p = Paint::solid(CRGB(255, 255, 255));
  if (isRecord) {
    p.mode = Paint::RAINBOW;
    p.hue = millis() / 8;
  }
  if (isRecord && (el / 1500) % 2 == 1) {
    const char *t = "שיא!";
    mx::drawText(t, (W + mx::textWidth(t)) / 2, 0, p);
    return;
  }
  char s[8];
  snprintf(s, sizeof s, "%d", sc);
  int w = mx::digitsWidth(s, mx::DF_MEDIUM);
  mx::drawDigits(s, (W - w) / 2, 0, mx::DF_MEDIUM, p, false);
}

bool frame() {
  if (cur < 0) return false;
  uint32_t t = millis();
  if (t - lastInput > IDLE_MS) {
    stop();
    return false;
  }
  dt = min(0.05f, (t - lastFrame) / 1000.0f);
  lastFrame = t;
  mx::clear();
  if (!isOver) {
    switch (cur) {
    case 0: snake::tick(); break;
    case 1: dino::tick(); break;
    case 2: flappy::tick(); break;
    default: tetris::tick(); break;
    }
  }
  if (isOver) drawOver();
  else drawGame();
  return true;
}

} // namespace game
