#pragma once
// Hebrew calendar (algorithms from Dershowitz & Reingold, "Calendrical Calculations"),
// holidays, special Shabbatot and the weekly Torah portion for several customs.
// Dates are "fixed" day numbers (R.D.: 1 = Monday 1 Jan 1 CE). No Arduino dependencies.
#include <stddef.h>
#include <stdint.h>

namespace heb {

enum Month { NISAN = 1, IYAR, SIVAN, TAMUZ, AV, ELUL, TISHREI, HESHVAN, KISLEV, TEVET, SHVAT, ADAR, ADAR2 };
// In a leap year ADAR is Adar I and ADAR2 is Adar II.

struct HDate {
  int year, month, day;
};

long fixedFromGregorian(int y, int m, int d);
void gregorianFromFixed(long fixed, int &y, int &m, int &d);
int dayOfWeek(long fixed); // 0 = Sunday .. 6 = Shabbat

bool isLeap(int year);
int monthsInYear(int year);
int daysInMonth(int month, int year);
int daysInYear(int year);
long toFixed(int year, int month, int day);
HDate fromFixed(long fixed);

const char *monthName(int month, int year);
const char *dayName(int dow);                      // ראשון .. שבת
void numberToHebrew(int n, char *out, size_t len); // 21 -> כ"א, 787 -> תשפ"ז
void yearToHebrew(int year, bool thousands, char *out, size_t len); // 5787 -> תשפ"ז or ה'תשפ"ז
void formatDate(const HDate &d, bool withYear, char *out, size_t len);

// ---------------- Events ----------------

enum EventId : uint8_t {
  EV_NONE = 0,
  // Yom tov (work forbidden)
  EV_ROSH_HASHANA, // arg = 1 or 2
  EV_YOM_KIPPUR,
  EV_SUKKOT,         // arg = day 1 (or 2 abroad)
  EV_SHMINI_ATZERET, // in Israel also Simchat Torah
  EV_SIMCHAT_TORAH,  // abroad only (23 Tishrei)
  EV_PESACH,         // arg = day 1, 7 (or 2, 8 abroad)
  EV_SHAVUOT,        // arg = day 1 (or 2 abroad)
  // Chol hamoed
  EV_CHOL_HAMOED_SUKKOT, // arg = which day of chol hamoed (1..5)
  EV_HOSHANA_RABA,
  EV_CHOL_HAMOED_PESACH, // arg = which day of chol hamoed (1..5)
  // Eves
  EV_EREV_ROSH_HASHANA,
  EV_EREV_YOM_KIPPUR,
  EV_EREV_SUKKOT,
  EV_EREV_PESACH,
  EV_EREV_SHAVUOT,
  EV_EREV_TISHA_BAV,
  // Minor holidays
  EV_CHANUKAH, // arg = day 1..8
  EV_TU_BISHVAT,
  EV_PURIM,
  EV_SHUSHAN_PURIM,
  EV_PURIM_MESHULASH,
  EV_PURIM_KATAN,
  EV_SHUSHAN_PURIM_KATAN,
  EV_PESACH_SHENI,
  EV_LAG_BAOMER,
  EV_TU_BAV,
  EV_ISRU_CHAG,
  // Fasts
  EV_TZOM_GEDALIAH,
  EV_ASARA_BTEVET,
  EV_TAANIT_ESTHER,
  EV_TAANIT_BECHOROT,
  EV_TZOM_TAMUZ,
  EV_TISHA_BAV,
  // Modern Israeli days
  EV_YOM_HASHOAH,
  EV_YOM_HAZIKARON,
  EV_YOM_HAATZMAUT,
  EV_YOM_YERUSHALAYIM,
  EV_SIGD,
  // Rosh Chodesh: arg low 4 bits = month it begins, high bits = 0 single day, 1 first of two, 2 second of two
  EV_ROSH_CHODESH,
  // Special Shabbatot
  EV_SHABBAT_SHUVA,
  EV_SHABBAT_SHIRA,
  EV_SHABBAT_SHEKALIM,
  EV_SHABBAT_ZACHOR,
  EV_SHABBAT_PARAH,
  EV_SHABBAT_HACHODESH,
  EV_SHABBAT_HAGADOL,
  EV_SHABBAT_CHAZON,
  EV_SHABBAT_NACHAMU,
  EV_SHABBAT_MEVARCHIM, // arg = month being blessed
  // Notes the printed calendars carry
  EV_TAL_UMATAR,        // the request for rain begins (Israel: 7 Heshvan)
  EV_BEDIKAT_CHAMETZ,   // the search for chametz is this evening
  EV_COUNT
};

enum EventFlag : uint8_t {
  F_YOMTOV = 1,    // candle lighting before, work forbidden
  F_FAST = 2,
  F_MODERN = 4,
  F_MINOR = 8,
  F_SHABBAT = 16,  // special Shabbat
  F_EREV = 32,
  F_CHOLHAMOED = 64,
  F_ROSHCHODESH = 128,
};

struct Event {
  uint8_t id;
  uint8_t arg;
};

// Fills `out` with everything that falls on the given civil day (daytime part of the Hebrew date).
// israel = false gives the two-day diaspora festivals. Returns the count.
int events(long fixed, bool israel, Event *out, int max);
uint8_t eventFlags(uint8_t id);
const char *eventKey(uint8_t id); // stable ASCII key (used by tests and the web API)
// Hebrew name, worded as in the printed Yemenite calendar. A '|' marks where a two-line
// display should break it; show it as a space anywhere else.
void eventName(const Event &e, int hebYear, char *out, size_t len);

bool isYomTov(long fixed, bool israel);
bool isRestDay(long fixed, bool israel); // Shabbat or yom tov
int omerDay(long fixed);                 // 1..49, or 0 outside the Omer

// ---------------- Weekly Torah portion ----------------

enum ParashaScheme : uint8_t {
  SCHEME_STANDARD = 0, // Ashkenaz, Sefarad, Yemenite Shami
  SCHEME_BALADI = 1,   // Yemenite Baladi: Chukat-Balak joined instead of Matot-Masei
  SCHEME_DARDAI = 2,   // old Yemenite custom: Korach + Chukat to "ויסעו מקדש", then the rest of Chukat + Balak
};

struct Parasha {
  uint8_t first;  // 1 = Bereshit .. 54 = Vezot Haberakha, 0 = no regular reading (festival)
  uint8_t second; // joined portion, or 0
  uint8_t split;  // SCHEME_DARDAI only: 1 = "Korach + Chukat part 1", 2 = "Chukat part 2 + Balak"
};

Parasha parasha(long shabbatFixed, bool israel, uint8_t scheme);
const char *parashaName(int index);                          // Hebrew
void formatParasha(const Parasha &p, char *out, size_t len); // "מטות-מסעי", "" if none

// ---------------- Molad ----------------

struct Molad {
  long fixed;   // the day it falls on
  int dow;      // 0 = Sunday
  int hour;     // 0..23, civil clock (Jerusalem mean time)
  int minute;   // 0..59
  int chalakim; // 0..17
};
Molad molad(int year, int month);
// The latest moment for blessing the new moon: half a lunar month after the molad.
Molad kiddushLevanaEnd(int year, int month);

} // namespace heb
