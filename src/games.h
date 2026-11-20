#pragma once
// Small games on the matrix: snake, a running dino, flappy and a sideways tetris.
// Played from the app (arrow keys + action key) or with the three touch buttons.
// The game runs on the clock; the app only sends key presses. Best scores are kept in flash.
#include <Arduino.h>

namespace game {

enum Key : uint8_t { LEFT, RIGHT, UP, DOWN, A, KEY_N };

int count();
const char *name(int i); // Hebrew

void start(int i);
void stop();
int current(); // -1 = no game

// From the app: down = pressed / released. keepalive = "still held" (no new press);
// a key not refreshed for 1.5 s counts as released, so a lost release cannot stick.
void key(Key k, bool down, bool keepalive = false);
// Touch buttons 0..2, short or long touch
void button(int which, bool isLong);

// Draws one frame into the matrix buffer. false = the game was left alone too long (back to the clock).
bool frame();

int score();
int best(int i);
bool over();

// Speed level 1..5 (where each game starts) and whether it speeds up while playing. Kept in flash.
// Changing the game on screen starts it again at the new speed.
int level(int i);
bool ramp(int i);
void setSpeed(int i, int level, bool ramp);
// Factory reset: forget every best score and speed
void resetAll();

} // namespace game
