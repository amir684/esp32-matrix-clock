#pragma once
// Party animations. Each draws a full frame into the matrix buffer.
#include <Arduino.h>

namespace fx {

int count();
const char *name(int i); // Hebrew, for the web page and the on-screen notice
// i = 0 .. count()-1; speed 1..10; text is used by the scrolling-text effect
void frame(int i, uint8_t speed, const char *text);
void reset(); // call when switching to another effect

} // namespace fx
