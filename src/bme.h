#pragma once
// Optional BME280 (temperature, humidity, air pressure) or BMP280 (no humidity) on the same I2C bus as
// the DS3231: SDA GPIO21, SCL GPIO22, VCC 3.3V. Address 0x76 or 0x77, found by itself.
// When one is fitted it measures the room temperature instead of the DS3231.

namespace bme {

bool begin();          // after Wire.begin (rtc::begin). false = no sensor answered
bool present();
bool hasHumidity();    // BME280 yes, BMP280 no
const char *model();   // "BME280" / "BMP280" / ""

// Reads the sensor (it measures by itself once a second). false = no sensor or a bad reading
bool read(float &tempC, float &humidity, float &hPa);

} // namespace bme
