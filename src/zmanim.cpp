#include "zmanim.h"
#include <math.h>

static const double RAD = M_PI / 180.0;

// ---------------- NOAA solar position (Meeus), t in Julian centuries since J2000 ----------------

static double meanLongitude(double t) { return fmod(280.46646 + t * (36000.76983 + 0.0003032 * t), 360.0); }
static double meanAnomaly(double t) { return 357.52911 + t * (35999.05029 - 0.0001537 * t); }
static double eccentricity(double t) { return 0.016708634 - t * (0.000042037 + 0.0000001267 * t); }

static double obliquity(double t) {
  double seconds = 21.448 - t * (46.8150 + t * (0.00059 - t * 0.001813));
  double e0 = 23.0 + (26.0 + seconds / 60.0) / 60.0;
  return e0 + 0.00256 * cos((125.04 - 1934.136 * t) * RAD);
}

static double declination(double t) {
  double m = meanAnomaly(t) * RAD;
  double c = sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t)) + sin(2 * m) * (0.019993 - 0.000101 * t) +
             sin(3 * m) * 0.000289;
  double lambda = meanLongitude(t) + c - 0.00569 - 0.00478 * sin((125.04 - 1934.136 * t) * RAD);
  return asin(sin(obliquity(t) * RAD) * sin(lambda * RAD)) / RAD;
}

// Equation of time in minutes
static double equationOfTime(double t) {
  double eps = obliquity(t) * RAD, l0 = meanLongitude(t) * RAD, e = eccentricity(t), m = meanAnomaly(t) * RAD;
  double y = tan(eps / 2) * tan(eps / 2);
  double eq = y * sin(2 * l0) - 2 * e * sin(m) + 4 * e * y * sin(m) * cos(2 * l0) - 0.5 * y * y * sin(4 * l0) -
              1.25 * e * e * sin(2 * m);
  return eq / RAD * 4.0;
}

// Minutes after 0:00 UTC of the event, or NAN if the sun never gets there.
static double utcMinutes(double julianDay, double lat, double lon, double zenith, bool rising) {
  double minutes = 720; // first guess: noon
  for (int pass = 0; pass < 2; pass++) {
    double t = (julianDay + minutes / 1440.0 - 2451545.0) / 36525.0;
    if (pass == 0) { // start from local solar noon
      minutes = 720 - 4 * lon - equationOfTime(t);
      t = (julianDay + minutes / 1440.0 - 2451545.0) / 36525.0;
    }
    double decl = declination(t) * RAD;
    double cosH = cos(zenith * RAD) / (cos(lat * RAD) * cos(decl)) - tan(lat * RAD) * tan(decl);
    if (cosH < -1 || cosH > 1) return NAN;
    double h = acos(cosH) / RAD;
    minutes = 720 - 4 * (lon + (rising ? h : -h)) - equationOfTime(t);
  }
  return minutes;
}

time_t sunTime(long fixed, double lat, double lon, double degreesBelow, double elevation, bool rising) {
  double zenith = 90.0 + degreesBelow;
  if (degreesBelow == 0) {
    zenith += (34.0 + 16.0) / 60.0; // refraction 34 arcminutes + solar radius 16
    if (elevation > 0) zenith += acos(6356.9 / (6356.9 + elevation / 1000.0)) / RAD; // dip of the horizon
  }
  double jd = (double)fixed + 1721424.5; // Julian day at 0:00 UTC of this civil date
  double minutes = utcMinutes(jd, lat, lon, zenith, rising);
  if (isnan(minutes)) return 0;
  double unixMidnight = ((double)fixed - 719163.0) * 86400.0; // R.D. 719163 = 1 Jan 1970
  return (time_t)llround(unixMidnight + minutes * 60.0);
}

void computeZmanim(long fixed, const ZmanOptions &o, Zmanim &z) {
  for (int i = 0; i < Z_COUNT; i++) z.t[i] = 0;
  time_t netz = sunTime(fixed, o.lat, o.lon, 0, o.elevationMode == ELEVATION_BOTH ? o.elevation : 0, true);
  time_t shkia = sunTime(fixed, o.lat, o.lon, 0, o.elevationMode != ELEVATION_NONE ? o.elevation : 0, false);
  if (!netz || !shkia) return;
  double shaa = (shkia - netz) / 12.0; // proportional hour, Gra / Rambam

  z.t[Z_NETZ] = netz;
  z.t[Z_SHKIA] = shkia;
  z.t[Z_SOF_SHEMA_GRA] = netz + (time_t)lround(shaa * 3);
  z.t[Z_SOF_TEFILA_GRA] = netz + (time_t)lround(shaa * 4);
  z.t[Z_CHATZOT] = netz + (time_t)lround(shaa * 6);
  z.t[Z_MINCHA_GEDOLA] = netz + (time_t)lround(shaa * 6.5);
  z.t[Z_MINCHA_KETANA] = netz + (time_t)lround(shaa * 9.5);
  z.t[Z_PLAG] = netz + (time_t)lround(shaa * 10.75);
  z.t[Z_CHATZOT_LAYLA] = z.t[Z_CHATZOT] + 12 * 3600;

  switch (o.alotMode) {
  case ALOT_16_1_DEG: z.t[Z_ALOT] = sunTime(fixed, o.lat, o.lon, 16.1, 0, true); break;
  case ALOT_19_8_DEG: z.t[Z_ALOT] = sunTime(fixed, o.lat, o.lon, 19.8, 0, true); break;
  case ALOT_72_ZMANIYOT: z.t[Z_ALOT] = netz - (time_t)lround(shaa * 1.2); break;
  case ALOT_90_MIN: z.t[Z_ALOT] = netz - 90 * 60; break;
  default: z.t[Z_ALOT] = netz - 72 * 60;
  }

  // Magen Avraham: the day runs from dawn to a nightfall that mirrors it (same measure after sunset)
  time_t mgaEnd = 0;
  switch (o.alotMode) {
  case ALOT_16_1_DEG: mgaEnd = sunTime(fixed, o.lat, o.lon, 16.1, 0, false); break;
  case ALOT_19_8_DEG: mgaEnd = sunTime(fixed, o.lat, o.lon, 19.8, 0, false); break;
  default: mgaEnd = shkia + (netz - z.t[Z_ALOT]);
  }
  if (z.t[Z_ALOT] && mgaEnd) {
    double shaaMga = (mgaEnd - z.t[Z_ALOT]) / 12.0;
    z.t[Z_SOF_SHEMA_MGA] = z.t[Z_ALOT] + (time_t)lround(shaaMga * 3);
    z.t[Z_SOF_TEFILA_MGA] = z.t[Z_ALOT] + (time_t)lround(shaaMga * 4);
  }
  z.t[Z_MISHEYAKIR] = sunTime(fixed, o.lat, o.lon, o.misheyakirDeg, 0, true);

  auto after = [&](uint8_t mode, double value) -> time_t {
    if (mode == TZEIT_DEGREES) return sunTime(fixed, o.lat, o.lon, value, 0, false);
    if (mode == TZEIT_ZMANIYOT) return shkia + (time_t)lround(shaa * value / 60.0);
    return shkia + (time_t)lround(value * 60);
  };
  z.t[Z_TZEIT] = after(o.tzeitMode, o.tzeitValue);
  z.t[Z_TZEIT_RT] = after(o.rtMode, o.rtValue);
}
