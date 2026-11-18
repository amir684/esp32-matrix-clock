#include "rtc.h"
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "hebcal.h"

namespace rtc {

static const uint8_t ADDR = 0x68;
static const long UNIX_EPOCH_FIXED = 719163; // R.D. day number of 1 Jan 1970
static bool found = false;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool readRegs(uint8_t reg, uint8_t *buf, int n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)ADDR, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  Wire.beginTransmission(ADDR);
  found = Wire.endTransmission() == 0;
  if (found) {
    uint8_t ctrl;
    // Control register: oscillator on battery (EOSC=0), square wave off, no alarms
    if (readRegs(0x0E, &ctrl, 1)) writeReg(0x0E, 0x1C);
  }
  return found;
}

bool present() { return found; }

bool read(time_t &utc) {
  if (!found) return false;
  uint8_t r[7], status;
  if (!readRegs(0x00, r, 7) || !readRegs(0x0F, &status, 1)) return false;
  if (status & 0x80) return false; // oscillator stopped: the time is not to be trusted
  int sec = bcd2bin(r[0] & 0x7F), min = bcd2bin(r[1] & 0x7F);
  int hour;
  if (r[2] & 0x40) { // 12-hour mode (we never set it, but another program might have)
    hour = bcd2bin(r[2] & 0x1F) % 12;
    if (r[2] & 0x20) hour += 12;
  } else {
    hour = bcd2bin(r[2] & 0x3F);
  }
  int day = bcd2bin(r[4] & 0x3F), month = bcd2bin(r[5] & 0x1F);
  int year = 2000 + bcd2bin(r[6]) + ((r[5] & 0x80) ? 100 : 0);
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || min > 59 || sec > 59 || year < 2024) return false;
  long days = heb::fixedFromGregorian(year, month, day) - UNIX_EPOCH_FIXED;
  utc = (time_t)days * 86400 + hour * 3600 + min * 60 + sec;
  return true;
}

bool write(time_t utc) {
  if (!found) return false;
  long days = utc / 86400;
  long secs = utc % 86400;
  int y, m, d;
  heb::gregorianFromFixed(days + UNIX_EPOCH_FIXED, y, m, d);
  int dow = heb::dayOfWeek(days + UNIX_EPOCH_FIXED); // 0 = Sunday
  Wire.beginTransmission(ADDR);
  Wire.write(0x00);
  Wire.write(bin2bcd(secs % 60));
  Wire.write(bin2bcd((secs / 60) % 60));
  Wire.write(bin2bcd(secs / 3600)); // 24-hour mode
  Wire.write(dow + 1);
  Wire.write(bin2bcd(d));
  Wire.write(bin2bcd(m) | (y >= 2100 ? 0x80 : 0));
  Wire.write(bin2bcd(y % 100));
  if (Wire.endTransmission() != 0) return false;
  uint8_t status;
  if (readRegs(0x0F, &status, 1)) writeReg(0x0F, status & ~0x80);
  return true;
}

float temperature() {
  if (!found) return NAN;
  uint8_t r[2];
  if (!readRegs(0x11, r, 2)) return NAN;
  return (int8_t)r[0] + (r[1] >> 6) * 0.25f;
}

} // namespace rtc
