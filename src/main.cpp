// RGB matrix clock: 8x32 or 8x64 WS2812 on an ESP32, DS3231 for the time, three touch buttons,
// Hebrew and civil date ticker, night dimming and a party mode. Configured from its own web page.
#include <Arduino.h>

#include "ble.h"
#include "buttons.h"
#include "info.h"
#include "matrix.h"
#include "net.h"
#include "settings.h"
#include "ui.h"

static const uint32_t FRAME_MS = 20;

void applyHardware() {
  mx::configure(settings.width, settings.tileW, settings.corner, settings.columns, settings.serpentine,
                settings.chainRtl, settings.altRotated, settings.mirrorX, settings.flipY, settings.colorOrder,
                settings.maxMilliamps);
}

static void handleButtons() {
  int which;
  btn::Event e = btn::poll(which, settings.btnActiveLow);
  if (e == btn::NONE) return;
  ui::wake();
  if (!ui::power() && !(which == 0 && e == btn::LONG)) { // any touch switches the display back on
    ui::setPower(true);
    return;
  }
  bool party = ui::mode() == ui::PARTY;
  switch (which) {
  case 0:
    if (e == btn::SHORT) ui::setMode(party ? ui::CLOCK : ui::PARTY);
    else ui::setPower(!ui::power());
    break;
  case 1:
    if (e == btn::SHORT) ui::brightnessStep();
    else if (party) ui::nextEffect();
    else ui::nextTheme();
    break;
  case 2:
    if (e == btn::SHORT) {
      if (party) ui::notice(info::hebDateShort(), CRGB::Black, 2500);
      else ui::startTicker();
    } else {
      String a = net::address();
      if (net::apActive()) a = String(AP_NAME " ") + a;
      ui::notice(a.c_str(), CRGB(0, 200, 120), 6000);
    }
    break;
  }
}

void setup() {
  Serial.begin(115200);
  settings.defaults();
  settings.load();

  mx::begin();
  applyHardware();
  btn::begin();
  ui::begin();
  net::begin();
  if (settings.bleOn) ble::begin();
  Serial.printf("matrix clock %s, %dx8\n", FIRMWARE_VERSION, settings.width);
}

void loop() {
  net::loop();
  ble::loop();
  handleButtons();

  // Settings changed by a button are saved once the touching stops, to spare the flash
  static uint32_t dirtyAt = 0;
  if (ui::takeDirty()) dirtyAt = millis() | 1;
  if (dirtyAt && millis() - dirtyAt > 5000) {
    dirtyAt = 0;
    settings.save();
  }

  static uint32_t lastFrame = 0;
  if (millis() - lastFrame < FRAME_MS) return;
  lastFrame = millis();
  ui::frame(time(nullptr), net::timeValid());
}
