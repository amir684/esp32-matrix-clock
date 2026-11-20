// LED test for an ESP32-C3 SuperMini (env: c3-ledtest). Not part of the clock firmware.
//
// 1. Fills the whole chain red, green, blue, white for one second each: if the colours come out
//    in another order, pick the matching "colour order" in the clock's matrix settings.
// 2. Lights the LEDs one at a time in chain order (data-in first) and prints the index.
//    Every group of 8 LEDs (one column on most 8xN matrices) gets its own colour, so the path
//    of the chain - serpentine or not, which corner - is easy to see.
//
// BOOT button (GPIO9): pause / resume.
// Serial (115200, USB):  p pause/resume   n next LED   b previous LED   + faster   - slower
//                        <number> jump to that LED      c<number> number of LEDs (default 512)
//                        r restart with the colour test
#include <Arduino.h>
#include <FastLED.h>

#ifndef LED_PIN
#define LED_PIN 4        // data to DIN of the first matrix (through a 330 ohm resistor)
#endif
#ifndef MAX_LEDS
#define MAX_LEDS 512     // two 8x32 matrices
#endif
#define BOOT_BTN 9       // BOOT button on the SuperMini, LOW when pressed
#define BOARD_LED 8      // blue LED on the SuperMini, lit when LOW

static CRGB leds[MAX_LEDS];
static uint16_t count = MAX_LEDS;
static uint16_t pos = 0;
static uint16_t stepMs = 40;
static bool paused = false;
static int8_t colourStep = 0;   // 0..3 = colour test, -1 = chase
static uint32_t lastStep = 0;

static void showLed(uint16_t i) {
  fill_solid(leds, MAX_LEDS, CRGB::Black);
  leds[i] = CHSV((i / 8) * 37, 255, 255);
  FastLED.show();
  Serial.printf("LED %u  (matrix %u, #%u in matrix, group of 8: %u)\n", i, i / 256 + 1, i % 256, (i % 256) / 8);
  digitalWrite(BOARD_LED, (i & 1) ? HIGH : LOW);
}

static void colourTest(int8_t s) {
  static const CRGB cols[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
  static const char *names[] = {"red", "green", "blue", "white"};
  fill_solid(leds, MAX_LEDS, CRGB::Black);
  fill_solid(leds, count, cols[s]);
  FastLED.show();
  Serial.printf("colour test: all %s\n", names[s]);
}

static void restart() {
  colourStep = 0;
  pos = 0;
  paused = false;
  colourTest(0);
  lastStep = millis();
}

static void command(String s) {
  s.trim();
  if (s.isEmpty()) return;
  char c = s[0];
  if (c == 'p') {
    paused = !paused;
    Serial.println(paused ? "paused" : "running");
  } else if (c == 'n' || c == 'b') {
    colourStep = -1;
    paused = true;
    pos = (c == 'n') ? (pos + 1) % count : (pos + count - 1) % count;
    showLed(pos);
  } else if (c == '+' || c == '-') {
    stepMs = (c == '+') ? std::max<int>(5, stepMs / 2) : std::min<int>(2000, stepMs * 2);
    Serial.printf("%u ms per LED\n", stepMs);
  } else if (c == 'c') {
    count = constrain(s.substring(1).toInt(), 1, MAX_LEDS);
    Serial.printf("%u LEDs\n", count);
    restart();
  } else if (c == 'r') {
    restart();
  } else if (isDigit(c)) {
    colourStep = -1;
    pos = constrain(s.toInt(), 0, count - 1);
    showLed(pos);
    lastStep = millis();
  } else {
    Serial.println("p pause | n next | b back | + faster | - slower | <num> go to LED | c<num> LED count | r restart");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  pinMode(BOARD_LED, OUTPUT);
  FastLED.addLeds<WS2812B, LED_PIN, RGB>(leds, MAX_LEDS);  // RGB = no reordering: shows the strip's own order
  FastLED.setBrightness(40);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1000);  // about 1A, safe on USB power
  FastLED.clear(true);
  delay(1500);  // time for the USB serial port to come up
  Serial.printf("\nLED test: pin GPIO%d, %u LEDs\n", LED_PIN, count);
  restart();
}

void loop() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { command(line); line = ""; }
    else line += ch;
  }

  static bool lastBtn = HIGH;
  static uint32_t btnAt = 0;
  bool b = digitalRead(BOOT_BTN);
  if (b != lastBtn && millis() - btnAt > 50) {
    btnAt = millis();
    lastBtn = b;
    if (b == LOW) command("p");
  }

  if (paused || millis() - lastStep < (colourStep >= 0 ? 1000u : stepMs)) return;
  lastStep = millis();
  if (colourStep >= 0) {
    if (++colourStep < 4) { colourTest(colourStep); return; }
    colourStep = -1;
    pos = 0;
  } else if (++pos >= count) {
    pos = 0;
    Serial.println("--- end of chain, starting again ---");
  }
  showLed(pos);
}
