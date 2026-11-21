#include "info.h"

#include "bme.h"
#include "hebcal.h"
#include "rtc.h"
#include "settings.h"
#include "zmanim.h"

namespace info {

static const char *GREG_MONTHS[] = {"",      "ינואר",  "פברואר", "מרץ",     "אפריל",   "מאי",   "יוני",
                                    "יולי",  "אוגוסט", "ספטמבר", "אוקטובר", "נובמבר", "דצמבר"};

// Per-day cache
static long dayFixed = 0;     // civil date the cache is for
static time_t sunrise = 0, sunset = 0;
static time_t sunsetTomorrow = 0;
static time_t havdalahToday = 0, havdalahTomorrow = 0;
static int gy, gm, gd;

// Refreshed every second
static bool evening = false;  // after sunset, the Hebrew date has turned
static long hebFixed = 0;     // the day the Hebrew date refers to
static time_t nowT = 0;
static float tempC = NAN, humid = NAN, pressure = NAN;

static char sWeekday[16], sHebShort[32], sGregShort[8], sTemp[12], sHum[8];

void invalidate() { dayFixed = 0; }

static ZmanOptions zopts() {
  ZmanOptions o;
  o.lat = settings.lat;
  o.lon = settings.lon;
  o.elevation = 0;
  return o;
}

static void computeDay(long fixed) {
  dayFixed = fixed;
  Zmanim z;
  computeZmanim(fixed, zopts(), z);
  sunrise = z.t[Z_NETZ];
  sunset = z.t[Z_SHKIA];
  sunsetTomorrow = sunTime(fixed + 1, settings.lat, settings.lon, 0, 0, false);
  havdalahToday = sunTime(fixed, settings.lat, settings.lon, settings.havdalahDeg, 0, false);
  havdalahTomorrow = sunTime(fixed + 1, settings.lat, settings.lon, settings.havdalahDeg, 0, false);
}

static void fmtTime(time_t t, char *out, size_t len) {
  if (!t) {
    snprintf(out, len, "--:--");
    return;
  }
  t += 30; // nearest minute
  tm lt;
  localtime_r(&t, &lt);
  snprintf(out, len, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

void update(time_t now) {
  nowT = now;
  tm lt;
  localtime_r(&now, &lt);
  gy = lt.tm_year + 1900;
  gm = lt.tm_mon + 1;
  gd = lt.tm_mday;
  long fixed = heb::fixedFromGregorian(gy, gm, gd);
  if (fixed != dayFixed) computeDay(fixed);
  evening = settings.dayAtSunset && sunset && now >= sunset;
  hebFixed = evening ? fixed + 1 : fixed;

  static uint32_t lastTemp = 0;
  if (!lastTemp || millis() - lastTemp > 10000) {
    lastTemp = millis() | 1;
    // A BME280 measures the room; the DS3231's sensor sits on its chip and is the fallback
    float t, h, p;
    if (bme::read(t, h, p)) {
      tempC = t + settings.bmeOffset;
      humid = h;
      pressure = p;
    } else {
      t = rtc::temperature();
      tempC = isnan(t) ? NAN : t + settings.tempOffset;
      humid = pressure = NAN;
    }
  }

  int dow = heb::dayOfWeek(fixed);
  strlcpy(sWeekday, heb::dayName(dow), sizeof sWeekday);
  heb::HDate hd = heb::fromFixed(hebFixed);
  heb::formatDate(hd, false, sHebShort, sizeof sHebShort);
  snprintf(sGregShort, sizeof sGregShort, "%d/%d", gd, gm);
  if (isnan(tempC)) sTemp[0] = 0;
  else snprintf(sTemp, sizeof sTemp, "%d°", (int)lroundf(tempC));
  if (isnan(humid)) sHum[0] = 0;
  else snprintf(sHum, sizeof sHum, "%d%%", (int)lroundf(humid));
}

const char *weekdayShort() { return sWeekday; }
const char *hebDateShort() { return sHebShort; }
const char *gregShort() { return sGregShort; }
const char *tempShort() { return sTemp; }
const char *humidityShort() { return sHum; }
float temperature() { return tempC; }

bool sunNight(time_t now) {
  if (!sunrise || !sunset) return false;
  return now >= sunset || now < sunrise;
}

// ---------------- Ticker ----------------

static CRGB itemColor(uint8_t item) {
  static const uint32_t PALETTE[IT_COUNT] = {
      0xFFD8A0, // weekday
      0xFFB000, // Hebrew date
      0x00C8FF, // civil date
      0xFF40E0, // events
      0x40FF70, // parasha
      0xFF7A00, // Shabbat times
      0xB070FF, // omer
      0xFF4830, // temperature
      0xFFE040, // sun
      0xFF40A0, // message (replaced by its own color)
      0x40B0FF, // humidity
      0x80E0C0, // air pressure
  };
  if (settings.tickerColors == TCOL_SINGLE) return CRGB(settings.tickerColor);
  if (item == IT_MESSAGE) return CRGB(settings.messageColor);
  return CRGB(PALETTE[item]);
}

static void stripBars(char *s) {
  for (; *s; s++)
    if (*s == '|') *s = ' ';
}

static Segment *seg(Segment *out, int &n, int max, uint8_t item) {
  if (n >= max) return nullptr;
  Segment *s = &out[n++];
  s->item = item;
  s->color = itemColor(item);
  s->text[0] = 0;
  return s;
}

int buildTicker(Segment *out, int max) {
  int n = 0;
  Segment *s;
  long fixed = dayFixed;
  int dow = heb::dayOfWeek(fixed);
  heb::HDate hd = heb::fromFixed(hebFixed);

  if (settings.has(IT_WEEKDAY) && (s = seg(out, n, max, IT_WEEKDAY))) {
    if (evening) snprintf(s->text, sizeof s->text, "ליל %s", heb::dayName(dow + 1));
    else if (dow == 6) strlcpy(s->text, "שבת שלום", sizeof s->text);
    else snprintf(s->text, sizeof s->text, "יום %s", heb::dayName(dow));
  }
  if (settings.has(IT_HEBDATE) && (s = seg(out, n, max, IT_HEBDATE))) heb::formatDate(hd, true, s->text, sizeof s->text);
  if (settings.has(IT_GREGDATE) && (s = seg(out, n, max, IT_GREGDATE))) {
    if (settings.gregLong) snprintf(s->text, sizeof s->text, "%d ב%s %d", gd, GREG_MONTHS[gm], gy);
    else snprintf(s->text, sizeof s->text, "%02d.%02d.%04d", gd, gm, gy);
  }
  if (settings.has(IT_EVENTS)) {
    heb::Event ev[8];
    int k = heb::events(hebFixed, settings.israel, ev, 8);
    for (int i = 0; i < k; i++) {
      // notes meant for the printed calendar are not worth scrolling
      if (ev[i].id == heb::EV_TAL_UMATAR || ev[i].id == heb::EV_BEDIKAT_CHAMETZ) continue;
      if (!(s = seg(out, n, max, IT_EVENTS))) break;
      heb::eventName(ev[i], hd.year, s->text, sizeof s->text);
      stripBars(s->text);
    }
  }
  if (settings.has(IT_PARASHA) && dow == 5) { // Friday only (until midnight): this Shabbat's portion
    long shabbat = hebFixed + (6 - heb::dayOfWeek(hebFixed));
    heb::Parasha p = heb::parasha(shabbat, settings.israel, settings.parashaScheme);
    if (p.first) {
      char name[64];
      heb::formatParasha(p, name, sizeof name);
      if ((s = seg(out, n, max, IT_PARASHA))) snprintf(s->text, sizeof s->text, "פרשת %s", name);
    }
  }
  if (settings.has(IT_SHABBAT)) {
    char t[8];
    bool restToday = heb::isRestDay(fixed, settings.israel);
    bool restTomorrow = heb::isRestDay(fixed + 1, settings.israel);
    bool restAfter = heb::isRestDay(fixed + 2, settings.israel);
    if (restToday && !restTomorrow && nowT < havdalahToday + 1800) {
      fmtTime(havdalahToday, t, sizeof t);
      if ((s = seg(out, n, max, IT_SHABBAT)))
        snprintf(s->text, sizeof s->text, "%s %s", dow == 6 ? "צאת שבת" : "צאת החג", t);
    } else if (!restToday && restTomorrow && nowT < sunset) {
      // the eve: when it comes in and, if it is a single day, when it goes out
      const char *what = dow + 1 == 6 ? "שבת" : "החג";
      fmtTime(sunset - settings.candleMinutes * 60, t, sizeof t);
      if ((s = seg(out, n, max, IT_SHABBAT))) snprintf(s->text, sizeof s->text, "כניסת %s %s", what, t);
      if (!restAfter) {
        fmtTime(havdalahTomorrow, t, sizeof t);
        if ((s = seg(out, n, max, IT_SHABBAT))) snprintf(s->text, sizeof s->text, "צאת %s %s", what, t);
      }
    } else if (evening && restTomorrow && !restAfter) {
      fmtTime(havdalahTomorrow, t, sizeof t);
      if ((s = seg(out, n, max, IT_SHABBAT)))
        snprintf(s->text, sizeof s->text, "%s %s", dow + 1 == 6 ? "צאת שבת" : "צאת החג", t);
    }
  }
  if (settings.has(IT_OMER)) {
    int o = heb::omerDay(hebFixed);
    if (o && (s = seg(out, n, max, IT_OMER))) snprintf(s->text, sizeof s->text, "היום %d לעומר", o);
  }
  if (settings.has(IT_TEMP) && sTemp[0] && (s = seg(out, n, max, IT_TEMP)))
    snprintf(s->text, sizeof s->text, "טמפרטורה %s", sTemp);
  if (settings.has(IT_HUMIDITY) && sHum[0] && (s = seg(out, n, max, IT_HUMIDITY)))
    snprintf(s->text, sizeof s->text, "לחות %s", sHum);
  if (settings.has(IT_PRESSURE) && !isnan(pressure) && (s = seg(out, n, max, IT_PRESSURE)))
    snprintf(s->text, sizeof s->text, "לחץ אוויר %d מיליבר", (int)lroundf(pressure));
  if (settings.has(IT_SUN) && sunrise) {
    char a[8], b[8];
    fmtTime(sunrise, a, sizeof a);
    fmtTime(sunset, b, sizeof b);
    if ((s = seg(out, n, max, IT_SUN))) snprintf(s->text, sizeof s->text, "זריחה %s  שקיעה %s", a, b);
  }
  if (settings.has(IT_MESSAGE) && settings.message[0] && (s = seg(out, n, max, IT_MESSAGE)))
    strlcpy(s->text, settings.message, sizeof s->text);
  return n;
}

void statusJson(JsonDocument &doc) {
  heb::HDate hd = heb::fromFixed(hebFixed);
  char buf[64];
  heb::formatDate(hd, true, buf, sizeof buf);
  doc["hebDate"] = buf;
  doc["weekday"] = sWeekday;
  snprintf(buf, sizeof buf, "%02d.%02d.%04d", gd, gm, gy);
  doc["gregDate"] = buf;
  if (!isnan(tempC)) doc["temp"] = roundf(tempC * 10) / 10;
  if (!isnan(humid)) doc["humidity"] = roundf(humid);
  if (!isnan(pressure)) doc["pressure"] = roundf(pressure * 10) / 10;
  fmtTime(sunrise, buf, sizeof buf);
  doc["sunrise"] = buf;
  fmtTime(sunset, buf, sizeof buf);
  doc["sunset"] = buf;
  doc["evening"] = evening;
  static Segment segs[16];
  int n = buildTicker(segs, 16);
  JsonArray a = doc["ticker"].to<JsonArray>();
  for (int i = 0; i < n; i++) a.add(segs[i].text);
}

} // namespace info
