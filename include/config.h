#pragma once
// ---------------- Wiring (ESP32 DevKit V1) ----------------
// WS2812 DIN  -> GPIO27 through a 330 ohm resistor. Driven by the VSPI hardware (see matrix.cpp). A 74AHCT125 / 74HCT245 level shifter
//                (3.3V -> 5V) is recommended: without it some matrices flicker or show wrong colors.
// DS3231      -> SDA GPIO21, SCL GPIO22, VCC 3.3V
// TTP223 x3   -> OUT to GPIO32 / GPIO33 / GPIO25, VCC 3.3V (default jumpers: momentary, active high)
// Two 8x32 matrices are chained: DOUT of the first to DIN of the second.
// Feed the 5V of the matrices straight from the power supply, not through the ESP32 board.
#define PIN_LED 27
#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_BTN1 32 // mode: clock / party        long: display on / off
#define PIN_BTN2 33 // brightness                 long: next color theme (clock) / next effect (party)
#define PIN_BTN3 25 // show the date now          long: show the network address

#define MAX_W 64 // widest display supported (two 8x32 matrices)
#define MAT_H 8
#define MAX_LEDS (MAX_W * MAT_H)

#define FIRMWARE_VERSION "1.1.0"
#define AP_NAME "MATRIX-CLOCK" // the clock's own WiFi network
#define DEFAULT_HOSTNAME "matrixclock"
#define IL_TZ "IST-2IDT,M3.4.4/26,M10.5.0"
