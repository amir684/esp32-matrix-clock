#pragma once
// Three TTP223 touch modules. Short touch = released before 700 ms, long touch = held 700 ms
// (fires once, while still held).
#include <Arduino.h>

namespace btn {

enum Event : uint8_t { NONE, SHORT, LONG };

void begin();
// Call often. Returns the event of one button per call (0..2 in `which`), or NONE.
Event poll(int &which, bool activeLow);

} // namespace btn
