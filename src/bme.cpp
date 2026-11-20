#include "bme.h"

#include <Arduino.h>
#include <Wire.h>

// Register map and compensation formulas: Bosch BME280 datasheet, sections 5 and 8.1 (floating point).
namespace bme {

static uint8_t addr = 0, chip = 0;
static uint16_t T1, P1;
static int16_t T2, T3, P2, P3, P4, P5, P6, P7, P8, P9, H2, H4, H5;
static uint8_t H1, H3;
static int8_t H6;

static bool readRegs(uint8_t reg, uint8_t *buf, int n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}
static uint16_t u16(const uint8_t *b) { return b[0] | (b[1] << 8); } // calibration words are little-endian
static int16_t s16(const uint8_t *b) { return (int16_t)u16(b); }

static bool setup() {
  writeReg(0xE0, 0xB6); // soft reset
  delay(5);
  for (int i = 0; i < 20; i++) { // wait while it copies its calibration (status bit 0)
    uint8_t st;
    if (readRegs(0xF3, &st, 1) && !(st & 0x01)) break;
    delay(2);
  }
  uint8_t c[26];
  if (!readRegs(0x88, c, 26)) return false;
  T1 = u16(c), T2 = s16(c + 2), T3 = s16(c + 4);
  P1 = u16(c + 6), P2 = s16(c + 8), P3 = s16(c + 10), P4 = s16(c + 12), P5 = s16(c + 14);
  P6 = s16(c + 16), P7 = s16(c + 18), P8 = s16(c + 20), P9 = s16(c + 22);
  H1 = c[25];
  if (chip == 0x60) {
    uint8_t h[7];
    if (!readRegs(0xE1, h, 7)) return false;
    H2 = s16(h);
    H3 = h[2];
    H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
    H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
    H6 = (int8_t)h[6];
    writeReg(0xF2, 0x01); // humidity x1; takes effect with the write to 0xF4 below
  }
  // Still in sleep mode after the reset, so the config register can be written:
  // a measurement every 1000 ms, IIR filter 4 (steadier pressure)
  writeReg(0xF5, 0xA8);
  // temperature x1, pressure x4, normal mode. Measuring once a second warms it by far less than 0.1 degree.
  return writeReg(0xF4, 0x2F);
}

bool begin() {
  for (uint8_t a : {0x76, 0x77}) {
    addr = a;
    uint8_t id;
    if (!readRegs(0xD0, &id, 1)) continue;
    if (id == 0x60 || id == 0x58 || id == 0x56 || id == 0x57) {
      chip = id;
      if (setup()) {
        Serial.printf("%s at 0x%02X\n", model(), addr);
        return true;
      }
    }
  }
  addr = chip = 0;
  return false;
}

bool present() { return chip != 0; }
bool hasHumidity() { return chip == 0x60; }
const char *model() { return chip == 0x60 ? "BME280" : chip ? "BMP280" : ""; }

bool read(float &tempC, float &humidity, float &hPa) {
  if (!chip) return false;
  uint8_t d[8];
  int n = hasHumidity() ? 8 : 6;
  if (!readRegs(0xF7, d, n)) return false;
  int32_t adcP = (int32_t)d[0] << 12 | d[1] << 4 | d[2] >> 4;
  int32_t adcT = (int32_t)d[3] << 12 | d[4] << 4 | d[5] >> 4;
  if (adcT == 0x80000) return false; // no measurement yet

  double v1 = (adcT / 16384.0 - T1 / 1024.0) * T2;
  double v2 = (adcT / 131072.0 - T1 / 8192.0) * (adcT / 131072.0 - T1 / 8192.0) * T3;
  double tFine = v1 + v2;
  tempC = tFine / 5120.0;

  hPa = NAN;
  if (adcP != 0x80000) {
    v1 = tFine / 2.0 - 64000.0;
    v2 = v1 * v1 * P6 / 32768.0;
    v2 = v2 + v1 * P5 * 2.0;
    v2 = v2 / 4.0 + P4 * 65536.0;
    v1 = (P3 * v1 * v1 / 524288.0 + P2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * P1;
    if (v1 != 0) {
      double p = 1048576.0 - adcP;
      p = (p - v2 / 4096.0) * 6250.0 / v1;
      v1 = P9 * p * p / 2147483648.0;
      v2 = p * P8 / 32768.0;
      p = p + (v1 + v2 + P7) / 16.0;
      hPa = p / 100.0;
    }
  }

  humidity = NAN;
  if (hasHumidity()) {
    int32_t adcH = d[6] << 8 | d[7];
    if (adcH != 0x8000) {
      double h = tFine - 76800.0;
      h = (adcH - (H4 * 64.0 + H5 / 16384.0 * h)) *
          (H2 / 65536.0 * (1.0 + H6 / 67108864.0 * h * (1.0 + H3 / 67108864.0 * h)));
      h = h * (1.0 - H1 * h / 524288.0);
      humidity = constrain(h, 0.0, 100.0);
    }
  }
  return tempC > -40 && tempC < 85;
}

} // namespace bme
