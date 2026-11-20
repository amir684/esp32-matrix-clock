#pragma once
// What is on the display: the clock face, the info ticker, notices, party effects, tests.
#include <Arduino.h>
#include <FastLED.h>

namespace ui {

enum Mode : uint8_t { CLOCK, PARTY, GAME };
enum Test : uint8_t { TEST_NONE, TEST_CHAIN, TEST_MAP, TEST_COLORS };

void begin();
void frame(time_t now, bool timeValid); // call every ~20 ms

// A short message; scrolls once if it is wider than the display. color Black = rainbow.
void notice(const char *text, CRGB color, uint32_t ms = 3000);
void startTicker();

void setMode(Mode m);
void startGame(int i); // see games.h; back to the clock when it is left alone
Mode mode();
void nextEffect();
int effectIndex(); // the effect on screen now

void setPower(bool on);
bool power();
void wake(); // show the display for a moment while it is switched off for the night

void brightnessStep();
void nextTheme();
void startTest(Test t);

bool night();
uint8_t brightness(); // what is being sent to the LEDs now
bool takeDirty();     // settings changed by a button; true once

} // namespace ui
