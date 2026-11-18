#pragma once
// DS3231 real-time clock. It keeps UTC; the time zone is applied by the ESP32.
#include <time.h>

namespace rtc {

bool begin();             // false = no DS3231 answered on the I2C bus
bool present();
bool read(time_t &utc);   // false if missing or if it lost power (oscillator-stop flag) since last set
bool write(time_t utc);   // also clears the oscillator-stop flag
float temperature();      // degrees C, NAN if missing; resolution 0.25

} // namespace rtc
