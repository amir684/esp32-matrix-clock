#pragma once
// Halachic times of day. Sun positions use the NOAA solar algorithm (the same one
// KosherJava and Hebcal use), accurate to a few seconds. No Arduino dependencies.
#include <stdint.h>
#include <time.h>

enum ZmanId : uint8_t {
  Z_ALOT,           // dawn
  Z_MISHEYAKIR,     // earliest tallit and tefillin
  Z_NETZ,           // sunrise
  Z_SOF_SHEMA_MGA,  // latest Shema, Magen Avraham (day measured from dawn, by the chosen dawn opinion)
  Z_SOF_SHEMA_GRA,  // latest Shema, Gra / Rambam
  Z_SOF_TEFILA_MGA, // latest morning prayer, Magen Avraham
  Z_SOF_TEFILA_GRA, // latest morning prayer, Gra / Rambam
  Z_CHATZOT,        // midday
  Z_MINCHA_GEDOLA,
  Z_MINCHA_KETANA,
  Z_PLAG,           // plag hamincha
  Z_SHKIA,          // sunset
  Z_TZEIT,          // nightfall, by the chosen opinion
  Z_TZEIT_RT,       // nightfall of Rabbenu Tam
  Z_CHATZOT_LAYLA,  // midnight
  Z_COUNT
};

enum AlotMode : uint8_t { ALOT_16_1_DEG, ALOT_72_MIN, ALOT_72_ZMANIYOT, ALOT_90_MIN, ALOT_19_8_DEG };
enum TzeitMode : uint8_t { TZEIT_DEGREES, TZEIT_MINUTES, TZEIT_ZMANIYOT };
// Where the height of the place counts. The printed Yemenite calendar gives sunrise for a
// level horizon and the sunset actually seen from the heights, so that is the default.
enum ElevationMode : uint8_t { ELEVATION_NONE, ELEVATION_SUNSET, ELEVATION_BOTH };

struct ZmanOptions {
  double lat = 31.778, lon = 35.22;  // degrees, east and north positive
  double elevation = 0;              // meters; affects sunrise and sunset only
  uint8_t elevationMode = ELEVATION_SUNSET;
  uint8_t alotMode = ALOT_72_MIN;
  double misheyakirDeg = 11.5;
  uint8_t tzeitMode = TZEIT_DEGREES;
  double tzeitValue = 5.9;          // degrees below the horizon, or minutes after sunset
  uint8_t rtMode = TZEIT_ZMANIYOT;   // Rabbenu Tam: fixed or proportional minutes
  double rtValue = 72;
};

struct Zmanim {
  time_t t[Z_COUNT]; // UTC; 0 = does not occur (far north)
};

// `fixed` is the R.D. day number of the civil date.
void computeZmanim(long fixed, const ZmanOptions &opt, Zmanim &out);

// UTC time at which the sun's center is `degreesBelow` under the geometric horizon
// (0 means visible sunrise/sunset, corrected for refraction, the solar radius and elevation).
time_t sunTime(long fixed, double lat, double lon, double degreesBelow, double elevation, bool rising);
