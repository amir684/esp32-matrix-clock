#include "buttons.h"

#include "config.h"

namespace btn {

static const uint8_t PINS[3] = {PIN_BTN1, PIN_BTN2, PIN_BTN3};
static const uint32_t DEBOUNCE_MS = 30, LONG_MS = 700;

struct State {
  bool stable = false;   // debounced level: touched
  bool lastRaw = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
  bool longSent = false;
};
static State st[3];

void begin() {
  // Pull-down so an unplugged module reads as "not touched"
  for (uint8_t p : PINS) pinMode(p, INPUT_PULLDOWN);
}

Event poll(int &which, bool activeLow) {
  uint32_t now = millis();
  for (int i = 0; i < 3; i++) {
    State &s = st[i];
    bool raw = digitalRead(PINS[i]) == (activeLow ? LOW : HIGH);
    if (raw != s.lastRaw) {
      s.lastRaw = raw;
      s.changedAt = now;
    }
    if (raw != s.stable && now - s.changedAt >= DEBOUNCE_MS) {
      s.stable = raw;
      if (raw) {
        s.pressedAt = now;
        s.longSent = false;
      } else if (!s.longSent) {
        which = i;
        return SHORT;
      }
    }
    if (s.stable && !s.longSent && now - s.pressedAt >= LONG_MS) {
      s.longSent = true;
      which = i;
      return LONG;
    }
  }
  return NONE;
}

} // namespace btn
