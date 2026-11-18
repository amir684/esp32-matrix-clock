#include "hebcal.h"
#include <stdio.h>
#include <string.h>

namespace heb {

static const long EPOCH = -1373427; // fixed date of 1 Tishrei, year 1

static long long floorDiv(long long a, long long b) {
  long long q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}
static long long mod(long long a, long long b) {
  long long r = a % b;
  return r < 0 ? r + b : r;
}

static void append(char *out, size_t len, const char *s) {
  size_t n = strlen(out);
  if (n < len) snprintf(out + n, len - n, "%s", s);
}

// ---------------- Gregorian ----------------

static bool gregLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

long fixedFromGregorian(int y, int m, int d) {
  long long y1 = y - 1;
  long long f = 365 * y1 + floorDiv(y1, 4) - floorDiv(y1, 100) + floorDiv(y1, 400) + floorDiv(367LL * m - 362, 12) + d;
  if (m > 2) f -= gregLeap(y) ? 1 : 2;
  return (long)f;
}

void gregorianFromFixed(long fixed, int &y, int &m, int &d) {
  long long d0 = fixed - 1;
  long long n400 = floorDiv(d0, 146097), d1 = mod(d0, 146097);
  long long n100 = d1 / 36524, d2 = d1 % 36524;
  long long n4 = d2 / 1461, d3 = d2 % 1461;
  long long n1 = d3 / 365;
  y = (int)(400 * n400 + 100 * n100 + 4 * n4 + n1);
  if (!(n100 == 4 || n1 == 4)) y++;
  long prior = fixed - fixedFromGregorian(y, 1, 1);
  long corr = fixed < fixedFromGregorian(y, 3, 1) ? 0 : (gregLeap(y) ? 1 : 2);
  m = (int)((12 * (prior + corr) + 373) / 367);
  d = (int)(fixed - fixedFromGregorian(y, m, 1) + 1);
}

int dayOfWeek(long fixed) { return (int)mod(fixed, 7); }

// ---------------- Hebrew ----------------

bool isLeap(int y) { return mod(7LL * y + 1, 19) < 7; }
int monthsInYear(int y) { return isLeap(y) ? 13 : 12; }

static long elapsedDays(int y) {
  long long months = floorDiv(235LL * y - 234, 19);
  long long parts = 12084 + 13753LL * months;
  long long days = 29 * months + floorDiv(parts, 25920);
  return (long)(mod(3 * (days + 1), 7) < 3 ? days + 1 : days);
}

static long newYearUncached(int y) {
  long ny0 = elapsedDays(y - 1), ny1 = elapsedDays(y), ny2 = elapsedDays(y + 1);
  int delay = (ny2 - ny1 == 356) ? 2 : (ny1 - ny0 == 382) ? 1 : 0;
  return EPOCH + ny1 + delay;
}

// Rosh Hashana is asked for constantly; remember the last few years.
static long newYear(int y) {
  static int cacheYear[4] = {0, 0, 0, 0};
  static long cacheVal[4];
  int slot = y & 3;
  if (cacheYear[slot] != y) {
    cacheVal[slot] = newYearUncached(y);
    cacheYear[slot] = y;
  }
  return cacheVal[slot];
}

int daysInYear(int y) { return (int)(newYear(y + 1) - newYear(y)); }

int daysInMonth(int m, int y) {
  if (m == IYAR || m == TAMUZ || m == ELUL || m == TEVET || m == ADAR2) return 29;
  if (m == ADAR && !isLeap(y)) return 29;
  if (m == HESHVAN || m == KISLEV) {
    int len = daysInYear(y);
    if (m == HESHVAN && !(len == 355 || len == 385)) return 29;
    if (m == KISLEV && (len == 353 || len == 383)) return 29;
  }
  return 30;
}

long toFixed(int y, int m, int d) {
  long f = newYear(y) + d - 1;
  if (m < TISHREI) {
    for (int mm = TISHREI; mm <= monthsInYear(y); mm++) f += daysInMonth(mm, y);
    for (int mm = NISAN; mm < m; mm++) f += daysInMonth(mm, y);
  } else {
    for (int mm = TISHREI; mm < m; mm++) f += daysInMonth(mm, y);
  }
  return f;
}

HDate fromFixed(long date) {
  int y = (int)((date - EPOCH) / (35975351.0 / 98496.0)); // approximate year minus one
  while (newYear(y + 1) <= date) y++;
  int m = (date < toFixed(y, NISAN, 1)) ? TISHREI : NISAN;
  while (date > toFixed(y, m, daysInMonth(m, y))) m++;
  return {y, m, (int)(date - toFixed(y, m, 1)) + 1};
}

static long shabbatOnOrBefore(long f) { return f - (dayOfWeek(f) + 1) % 7; }
static long shabbatOnOrAfter(long f) { return f + (6 - dayOfWeek(f)) % 7; }

// ---------------- Text ----------------

const char *monthName(int m, int y) {
  static const char *names[] = {"", "ניסן", "אייר", "סיון", "תמוז", "אב", "אלול", "תשרי",
                                "חשון", "כסלו", "טבת", "שבט", "אדר", "אדר ב'"};
  if (m == ADAR && isLeap(y)) return "אדר א'";
  return (m >= NISAN && m <= ADAR2) ? names[m] : "";
}

const char *dayName(int dow) {
  static const char *names[] = {"ראשון", "שני", "שלישי", "רביעי", "חמישי", "שישי", "שבת"};
  return names[((dow % 7) + 7) % 7];
}

void numberToHebrew(int n, char *out, size_t len) {
  static const char *ones[] = {"", "א", "ב", "ג", "ד", "ה", "ו", "ז", "ח", "ט"};
  static const char *tens[] = {"", "י", "כ", "ל", "מ", "נ", "ס", "ע", "פ", "צ"};
  static const char *hundreds[] = {"", "ק", "ר", "ש", "ת"};
  const char *parts[12];
  int k = 0;
  n %= 1000;
  int h = n / 100;
  while (h > 4) {
    parts[k++] = "ת";
    h -= 4;
  }
  if (h) parts[k++] = hundreds[h];
  int r = n % 100;
  if (r == 15 || r == 16) { // written ט"ו / ט"ז to avoid spelling the divine name
    parts[k++] = "ט";
    parts[k++] = (r == 15) ? "ו" : "ז";
  } else {
    if (r / 10) parts[k++] = tens[r / 10];
    if (r % 10) parts[k++] = ones[r % 10];
  }
  out[0] = 0;
  for (int i = 0; i < k; i++) {
    if (k > 1 && i == k - 1) append(out, len, "\"");
    append(out, len, parts[i]);
  }
  if (k == 1) append(out, len, "'");
}

void yearToHebrew(int year, bool thousands, char *out, size_t len) {
  char buf[24];
  out[0] = 0;
  if (thousands && year >= 1000) {
    static const char *ones[] = {"", "א", "ב", "ג", "ד", "ה", "ו", "ז", "ח", "ט"};
    append(out, len, ones[(year / 1000) % 10]);
    append(out, len, "'");
  }
  numberToHebrew(year % 1000, buf, sizeof buf);
  append(out, len, buf);
}

void formatDate(const HDate &d, bool withYear, char *out, size_t len) {
  char day[16], year[24];
  numberToHebrew(d.day, day, sizeof day);
  if (withYear) {
    yearToHebrew(d.year, false, year, sizeof year);
    snprintf(out, len, "%s %s %s", day, monthName(d.month, d.year), year);
  } else {
    snprintf(out, len, "%s %s", day, monthName(d.month, d.year));
  }
}

// ---------------- Events ----------------

static const struct {
  const char *key;
  uint8_t flags;
  const char *name;
} EVENT_INFO[EV_COUNT] = {
    {"none", 0, ""},
    {"rosh_hashana", F_YOMTOV, "ראש השנה"},
    {"yom_kippur", F_YOMTOV | F_FAST, "יום|הכפורים"},
    {"sukkot", F_YOMTOV, "חג|הסוכות"},
    {"shmini_atzeret", F_YOMTOV, "שמיני|עצרת"},
    {"simchat_torah", F_YOMTOV, "שמחת|תורה"},
    {"pesach", F_YOMTOV, "חג|הפסח"},
    {"shavuot", F_YOMTOV, "חג|השבועות"},
    {"chol_hamoed_sukkot", F_CHOLHAMOED, "סוכות"},
    {"hoshana_raba", F_CHOLHAMOED, "הושענא|רבא"},
    {"chol_hamoed_pesach", F_CHOLHAMOED, "פסח"},
    {"erev_rosh_hashana", F_EREV, "ערב|ראש השנה"},
    {"erev_yom_kippur", F_EREV, "ערב|יום כיפור"},
    {"erev_sukkot", F_EREV, "ערב|סוכות"},
    {"erev_pesach", F_EREV, "ערב|פסח"},
    {"erev_shavuot", F_EREV, "ערב|שבועות"},
    {"erev_tisha_bav", F_EREV, "ערב|תשעה באב"},
    {"chanukah", F_MINOR, "חנוכה"},
    {"tu_bishvat", F_MINOR, "ט\"ו|בשבט"},
    {"purim", F_MINOR, "פורים"},
    {"shushan_purim", F_MINOR, "שושן|פורים"},
    {"purim_meshulash", F_MINOR, "פורים|משולש"},
    {"purim_katan", F_MINOR, "פורים|קטן"},
    {"shushan_purim_katan", F_MINOR, "שושן פורים|קטן"},
    {"pesach_sheni", F_MINOR, "פסח|שני"},
    {"lag_baomer", F_MINOR, "ל\"ג|בעומר"},
    {"tu_bav", F_MINOR, "ט\"ו|באב"},
    {"isru_chag", F_MINOR, "אסרו|חג"},
    {"tzom_gedaliah", F_FAST, "צום|גדליה"},
    {"asara_btevet", F_FAST, "צום|עשרה בטבת"},
    {"taanit_esther", F_FAST, "תענית|אסתר"},
    {"taanit_bechorot", F_FAST, "תענית|בכורות"},
    {"tzom_tamuz", F_FAST, "צום|י\"ז בתמוז"},
    {"tisha_bav", F_FAST, "צום|תשעה באב"},
    {"yom_hashoah", F_MODERN, "יום|השואה"},
    {"yom_hazikaron", F_MODERN, "יום|הזכרון"},
    {"yom_haatzmaut", F_MODERN, "יום|העצמאות"},
    {"yom_yerushalayim", F_MODERN, "יום|ירושלים"},
    {"sigd", F_MODERN, "חג|הסיגד"},
    {"rosh_chodesh", F_ROSHCHODESH, "ראש חודש"},
    {"shabbat_shuva", F_SHABBAT, "שבת|שובה"},
    {"shabbat_shira", F_SHABBAT, "שבת|שירה"},
    {"shabbat_shekalim", F_SHABBAT, "שבת|שקלים"},
    {"shabbat_zachor", F_SHABBAT, "שבת|זכור"},
    {"shabbat_parah", F_SHABBAT, "שבת|פרה"},
    {"shabbat_hachodesh", F_SHABBAT, "שבת|החודש"},
    {"shabbat_hagadol", F_SHABBAT, "שבת|הגדול"},
    {"shabbat_chazon", F_SHABBAT, "שבת|חזון"},
    {"shabbat_nachamu", F_SHABBAT, "שבת|נחמו"},
    {"shabbat_mevarchim", F_SHABBAT, "מברכין"},
    {"tal_umatar", 0, "ותן|טל ומטר"},
    {"bedikat_chametz", 0, "בדיקת חמץ|בערב"},
};

uint8_t eventFlags(uint8_t id) { return id < EV_COUNT ? EVENT_INFO[id].flags : 0; }
const char *eventKey(uint8_t id) { return id < EV_COUNT ? EVENT_INFO[id].key : "none"; }

void eventName(const Event &e, int hebYear, char *out, size_t len) {
  char num[16];
  const char *base = e.id < EV_COUNT ? EVENT_INFO[e.id].name : "";
  switch (e.id) {
  case EV_ROSH_HASHANA:
    snprintf(out, len, e.arg == 2 ? "%s|יום ב'" : "ראש|השנה", base);
    break;
  case EV_SUKKOT:
    snprintf(out, len, e.arg == 2 ? "סוכות|יו\"ט שני" : "%s", base);
    break;
  case EV_SHAVUOT:
    snprintf(out, len, e.arg == 2 ? "שבועות|יו\"ט שני" : "%s", base);
    break;
  case EV_PESACH:
    if (e.arg == 7) snprintf(out, len, "שביעי|של פסח");
    else if (e.arg == 8) snprintf(out, len, "אחרון|של פסח");
    else snprintf(out, len, e.arg == 2 ? "פסח|יו\"ט שני" : "%s", base);
    break;
  case EV_CHOL_HAMOED_SUKKOT:
  case EV_CHOL_HAMOED_PESACH: // as printed: "א' דחוה"מ"
    numberToHebrew(e.arg, num, sizeof num);
    snprintf(out, len, "%s דחוה\"מ|%s", num, base);
    break;
  case EV_CHANUKAH: // as printed: "ג' דחנוכה"
    numberToHebrew(e.arg, num, sizeof num);
    snprintf(out, len, "%s|יום %s", base, num);
    break;
  case EV_ROSH_CHODESH: {
    int kind = e.arg >> 4;
    const char *month = monthName(e.arg & 15, hebYear);
    if (kind) snprintf(out, len, "%s דר\"ח|%s", kind == 1 ? "א'" : "ב'", month);
    else snprintf(out, len, "%s|%s", base, month);
    break;
  }
  case EV_SHABBAT_MEVARCHIM:
    snprintf(out, len, "%s|חודש %s", base, monthName(e.arg, hebYear));
    break;
  default:
    snprintf(out, len, "%s", base);
  }
}

int omerDay(long fixed) {
  HDate h = fromFixed(fixed);
  if (h.month != NISAN && h.month != IYAR && h.month != SIVAN) return 0;
  long n = fixed - toFixed(h.year, NISAN, 15);
  return (n >= 1 && n <= 49) ? (int)n : 0;
}

static bool isBeshalach(long shabbat, bool israel); // defined with the weekly portions below

static int nextMonth(int m, int y) {
  if (m == monthsInYear(y)) return NISAN;
  return m + 1; // Elul never has a 30th day, so it never reaches here needing Tishrei
}

int events(long fixed, bool israel, Event *out, int max) {
  int n = 0;
  auto add = [&](uint8_t id, uint8_t arg = 0) {
    if (n < max) out[n++] = {id, arg};
  };
  HDate h = fromFixed(fixed);
  int dow = dayOfWeek(fixed);
  int y = h.year, m = h.month, d = h.day;
  int purimMonth = isLeap(y) ? ADAR2 : ADAR;

  switch (m) {
  case TISHREI:
    if (d == 1 || d == 2) add(EV_ROSH_HASHANA, d);
    if ((d == 3 && dow != 6) || (d == 4 && dow == 0)) add(EV_TZOM_GEDALIAH);
    if (d == 9) add(EV_EREV_YOM_KIPPUR);
    if (d == 10) add(EV_YOM_KIPPUR);
    if (d == 14) add(EV_EREV_SUKKOT);
    if (d == 15) add(EV_SUKKOT, 1);
    if (d == 16) israel ? add(EV_CHOL_HAMOED_SUKKOT, 1) : add(EV_SUKKOT, 2);
    if (d >= 17 && d <= 20) add(EV_CHOL_HAMOED_SUKKOT, d - (israel ? 15 : 16));
    if (d == 21) add(EV_HOSHANA_RABA);
    if (d == 22) add(EV_SHMINI_ATZERET, israel ? 1 : 0);
    if (d == 23) add(israel ? EV_ISRU_CHAG : EV_SIMCHAT_TORAH);
    if (d == 24 && !israel) add(EV_ISRU_CHAG);
    break;
  case HESHVAN:
    if (d == 7 && israel) add(EV_TAL_UMATAR);
    // Sigd: 29 Heshvan, brought forward to Thursday when it falls on Shabbat
    if ((d == 29 && dow != 6) || (d == 27 && dow == 4)) add(EV_SIGD);
    break;
  case TEVET:
    if (d == 10) add(EV_ASARA_BTEVET);
    break;
  case SHVAT:
    if (d == 15) add(EV_TU_BISHVAT);
    break;
  case NISAN:
    // Fast of the firstborn: 14 Nisan, or Thursday the 12th when the 14th is Shabbat
    if ((d == 14 && dow != 6) || (d == 12 && dow == 4)) add(EV_TAANIT_BECHOROT);
    // The search for chametz: the evening before the 14th, or Thursday evening when the 14th is Shabbat
    if ((d == 13 && dow != 5) || (d == 12 && dow == 4)) add(EV_BEDIKAT_CHAMETZ);
    if (d == 14) add(EV_EREV_PESACH);
    if (d == 15) add(EV_PESACH, 1);
    if (d == 16) israel ? add(EV_CHOL_HAMOED_PESACH, 1) : add(EV_PESACH, 2);
    if (d >= 17 && d <= 20) add(EV_CHOL_HAMOED_PESACH, d - (israel ? 15 : 16));
    if (d == 21) add(EV_PESACH, 7);
    if (d == 22) israel ? add(EV_ISRU_CHAG) : add(EV_PESACH, 8);
    if (d == 23 && !israel) add(EV_ISRU_CHAG);
    // Yom HaShoah: 27 Nisan, moved off Friday (to Thursday) and off Sunday (to Monday)
    if ((d == 27 && dow != 5 && dow != 0) || (d == 26 && dow == 4) || (d == 28 && dow == 1)) add(EV_YOM_HASHOAH);
    break;
  case IYAR: {
    // Independence day: 5 Iyar, moved so that neither it nor Memorial day touches Shabbat
    int dow5 = dayOfWeek(toFixed(y, IYAR, 5));
    int atz = (dow5 == 5) ? 4 : (dow5 == 6) ? 3 : (dow5 == 1) ? 6 : 5;
    if (d == atz - 1) add(EV_YOM_HAZIKARON);
    if (d == atz) add(EV_YOM_HAATZMAUT);
    if (d == 14) add(EV_PESACH_SHENI);
    if (d == 18) add(EV_LAG_BAOMER);
    if (d == 28) add(EV_YOM_YERUSHALAYIM);
    break;
  }
  case SIVAN:
    if (d == 5) add(EV_EREV_SHAVUOT);
    if (d == 6) add(EV_SHAVUOT, 1);
    if (d == 7) israel ? add(EV_ISRU_CHAG) : add(EV_SHAVUOT, 2);
    if (d == 8 && !israel) add(EV_ISRU_CHAG);
    break;
  case TAMUZ:
    if ((d == 17 && dow != 6) || (d == 18 && dow == 0)) add(EV_TZOM_TAMUZ);
    break;
  case AV:
    if ((d == 8 && dow != 5) || (d == 9 && dow == 6)) add(EV_EREV_TISHA_BAV);
    if ((d == 9 && dow != 6) || (d == 10 && dow == 0)) add(EV_TISHA_BAV);
    if (d == 15) add(EV_TU_BAV);
    break;
  case ELUL:
    if (d == 29) add(EV_EREV_ROSH_HASHANA);
    break;
  }

  if (m == purimMonth) {
    if ((d == 13 && dow != 6) || (d == 11 && dow == 4)) add(EV_TAANIT_ESTHER);
    if (d == 14) add(EV_PURIM);
    if (d == 15) add(EV_SHUSHAN_PURIM);
    if (d == 16 && dow == 0) add(EV_PURIM_MESHULASH);
  } else if (m == ADAR) { // Adar I of a leap year
    if (d == 14) add(EV_PURIM_KATAN);
    if (d == 15) add(EV_SHUSHAN_PURIM_KATAN);
  }

  if (m == KISLEV || m == TEVET) {
    long day = fixed - toFixed(y, KISLEV, 25) + 1; // Kislev and Tevet share a Hebrew year
    if (day >= 1 && day <= 8) add(EV_CHANUKAH, (uint8_t)day);
  }

  if (d == 30) add(EV_ROSH_CHODESH, nextMonth(m, y) | (1 << 4));
  else if (d == 1 && m != TISHREI) add(EV_ROSH_CHODESH, m | (heb::fromFixed(fixed - 1).day == 30 ? 2 << 4 : 0));

  if (dow == 6) {
    if (m == TISHREI && d >= 3 && d <= 9) add(EV_SHABBAT_SHUVA);
    if (m == SHVAT && isBeshalach(fixed, israel)) add(EV_SHABBAT_SHIRA);
    if (m == SHVAT || m == ADAR || m == ADAR2 || m == NISAN) {
      long hachodesh = shabbatOnOrBefore(toFixed(y, NISAN, 1));
      if (fixed == shabbatOnOrBefore(toFixed(y, purimMonth, 1))) add(EV_SHABBAT_SHEKALIM);
      if (fixed == shabbatOnOrBefore(toFixed(y, purimMonth, 13))) add(EV_SHABBAT_ZACHOR);
      if (fixed == hachodesh - 7) add(EV_SHABBAT_PARAH);
      if (fixed == hachodesh) add(EV_SHABBAT_HACHODESH);
      if (fixed == shabbatOnOrBefore(toFixed(y, NISAN, 14))) add(EV_SHABBAT_HAGADOL);
    }
    if (m == AV) {
      long chazon = shabbatOnOrBefore(toFixed(y, AV, 9));
      if (fixed == chazon) add(EV_SHABBAT_CHAZON);
      if (fixed == chazon + 7) add(EV_SHABBAT_NACHAMU);
    }
    // Blessing the new month: the last Shabbat before Rosh Chodesh (never for Tishrei)
    if (m != ELUL && d >= 23 && d <= 29) add(EV_SHABBAT_MEVARCHIM, nextMonth(m, y));
  }
  return n;
}

bool isYomTov(long fixed, bool israel) {
  Event ev[8];
  int n = events(fixed, israel, ev, 8);
  for (int i = 0; i < n; i++)
    if (eventFlags(ev[i].id) & F_YOMTOV) return true;
  return false;
}

bool isRestDay(long fixed, bool israel) { return dayOfWeek(fixed) == 6 || isYomTov(fixed, israel); }

// ---------------- Weekly Torah portion ----------------

static const char *PARASHA_NAMES[55] = {
    "",        "בראשית", "נח",       "לך לך",   "וירא",   "חיי שרה", "תולדות", "ויצא",    "וישלח",  "וישב",   "מקץ",
    "ויגש",    "ויחי",   "שמות",     "וארא",    "בא",     "בשלח",    "יתרו",   "משפטים",  "תרומה",  "תצוה",   "כי תשא",
    "ויקהל",   "פקודי",  "ויקרא",    "צו",      "שמיני",  "תזריע",   "מצורע",  "אחרי מות", "קדושים", "אמור",   "בהר",
    "בחוקותי", "במדבר",  "נשא",      "בהעלותך", "שלח לך", "קרח",     "חקת",    "בלק",     "פנחס",   "מטות",   "מסעי",
    "דברים",   "ואתחנן", "עקב",      "ראה",     "שופטים", "כי תצא",  "כי תבוא", "נצבים",   "וילך",   "האזינו", "וזאת הברכה"};

const char *parashaName(int i) { return (i >= 1 && i <= 54) ? PARASHA_NAMES[i] : ""; }

void formatParasha(const Parasha &p, char *out, size_t len) {
  if (!p.first) out[0] = 0;
  else if (p.second) snprintf(out, len, "%s-%s", parashaName(p.first), parashaName(p.second));
  else snprintf(out, len, "%s", parashaName(p.first));
}

// True when a festival reading displaces the weekly portion on this Shabbat.
static bool festivalShabbat(long f, bool israel) {
  HDate h = fromFixed(f);
  if (h.month == TISHREI) return h.day == 1 || h.day == 2 || h.day == 10 || (h.day >= 15 && h.day <= (israel ? 22 : 23));
  if (h.month == NISAN) return h.day >= 15 && h.day <= (israel ? 21 : 22);
  if (h.month == SIVAN) return h.day == 6 || (h.day == 7 && !israel);
  return false;
}

// One Hebrew year of readings, built on demand and remembered.
static struct {
  int year = 0;
  bool israel;
  uint8_t scheme;
  int n;
  long date[58];
  Parasha p[58];
} sedra;

static int freeShabbatot(long from, long to, bool israel, long *out) {
  int n = 0;
  for (long s = shabbatOnOrAfter(from); s <= to; s += 7)
    if (!festivalShabbat(s, israel)) out[n++] = s;
  return n;
}

// Gives portions next..last to `count` Shabbatot, joining the listed pairs (in priority order)
// only as far as needed to make them fit.
static void assign(const long *dates, int count, int &next, int last, const uint8_t *pairs, int nPairs) {
  int need = (last - next + 1) - count;
  if (need < 0) need = 0;
  if (need > nPairs) need = nPairs;
  for (int i = 0; i < count; i++) {
    bool joined = false;
    for (int k = 0; k < need; k++)
      if (pairs[k] == next) joined = true;
    sedra.date[sedra.n] = dates[i];
    sedra.p[sedra.n++] = {(uint8_t)next, (uint8_t)(joined ? next + 1 : 0), 0};
    next += joined ? 2 : 1;
  }
}

static void buildSedra(int y, bool israel, uint8_t scheme) {
  sedra.year = y;
  sedra.israel = israel;
  sedra.scheme = scheme;
  sedra.n = 0;
  long s[32];
  int simchatDay = israel ? 22 : 23;

  // Tishrei up to Simchat Torah closes the previous cycle: Vayelech and/or Haazinu
  long rh = toFixed(y, TISHREI, 1), simchat = toFixed(y, TISHREI, simchatDay);
  int n = freeShabbatot(rh, simchat - 1, israel, s);
  if (n == 2) {
    sedra.date[sedra.n] = s[0];
    sedra.p[sedra.n++] = {52, 0, 0};
  }
  if (n >= 1) {
    sedra.date[sedra.n] = s[n - 1];
    sedra.p[sedra.n++] = {53, 0, 0};
  }

  long pesach = toFixed(y, NISAN, 15), shavuot = toFixed(y, SIVAN, 6);
  long chazon = shabbatOnOrBefore(toFixed(y, AV, 9));
  long nextRh = toFixed(y + 1, TISHREI, 1);
  int next = 1;

  static const uint8_t beforePesach[] = {22};      // Vayakhel-Pekudei, so that Tzav precedes Pesach
  static const uint8_t beforeShavuot[] = {27, 29, 32}; // Tazria-Metzora, Acharei-Kedoshim, Behar-Bechukotai
  static const uint8_t summerStd[] = {42, 39};     // Matot-Masei first, Chukat-Balak only abroad
  static const uint8_t summerYemen[] = {39, 42};   // Baladi: Chukat-Balak first

  n = freeShabbatot(simchat + 1, pesach - 1, israel, s);
  assign(s, n, next, 25, beforePesach, 1);
  n = freeShabbatot(pesach, shavuot - 1, israel, s);
  assign(s, n, next, 34, beforeShavuot, 3);
  int summerStart = sedra.n;
  n = freeShabbatot(shavuot, chazon, israel, s);
  assign(s, n, next, 44, scheme == SCHEME_STANDARD ? summerStd : summerYemen, 2);
  if (scheme == SCHEME_DARDAI) {
    // Instead of Chukat-Balak: Korach + first part of Chukat, then second part of Chukat + Balak
    for (int i = summerStart + 1; i < sedra.n; i++)
      if (sedra.p[i].first == 39 && sedra.p[i].second == 40 && sedra.p[i - 1].first == 38 && !sedra.p[i - 1].second) {
        sedra.p[i - 1] = {38, 39, 1};
        sedra.p[i] = {39, 40, 2};
      }
  }
  n = freeShabbatot(chazon + 1, nextRh - 1, israel, s);
  assign(s, n, next, 51, nullptr, 0);

  // Nitzavim takes Vayelech with it when next Tishrei has only one free Shabbat before Sukkot
  long t[4];
  int nextTishrei = freeShabbatot(nextRh, toFixed(y + 1, TISHREI, simchatDay) - 1, israel, t);
  if (nextTishrei == 1 && sedra.n && sedra.p[sedra.n - 1].first == 51) sedra.p[sedra.n - 1].second = 52;
}

Parasha parasha(long shabbat, bool israel, uint8_t scheme) {
  int y = fromFixed(shabbat).year;
  if (sedra.year != y || sedra.israel != israel || sedra.scheme != scheme) buildSedra(y, israel, scheme);
  for (int i = 0; i < sedra.n; i++)
    if (sedra.date[i] == shabbat) return sedra.p[i];
  return {0, 0, 0};
}

// Shabbat Shira. The winter portions are the same in every custom, so whichever table is
// already built will do (rebuilding it for another custom would only waste time).
static bool isBeshalach(long shabbat, bool israel) {
  int y = fromFixed(shabbat).year;
  uint8_t scheme = (sedra.year == y && sedra.israel == israel) ? sedra.scheme : (uint8_t)SCHEME_STANDARD;
  return parasha(shabbat, israel, scheme).first == 16;
}

// ---------------- Molad ----------------

Molad molad(int year, int month) {
  long long months = floorDiv(235LL * year - 234, 19); // months before Tishrei of this year
  months += (month >= TISHREI) ? month - TISHREI : month + monthsInYear(year) - 7;
  // Parts (1/1080 hour) since midnight at the start of fixed day 0
  long long parts = (long long)EPOCH * 25920 - 876 + months * 765433LL;
  long long day = floorDiv(parts, 25920), rem = mod(parts, 25920);
  Molad mo;
  mo.fixed = (long)day;
  mo.dow = (int)mod(day, 7);
  mo.hour = (int)(rem / 1080);
  mo.minute = (int)((rem % 1080) / 18);
  mo.chalakim = (int)(rem % 18);
  return mo;
}

Molad kiddushLevanaEnd(int year, int month) {
  Molad mo = molad(year, month);
  // Half of 29 days 12 hours 793 parts is 14 days 18 hours 396.5 parts; the half part is dropped
  long long parts = ((long long)mo.hour * 1080 + mo.minute * 18 + mo.chalakim) + 18 * 1080 + 396;
  mo.fixed += 14 + (long)(parts / 25920);
  parts %= 25920;
  mo.dow = (int)mod(mo.fixed, 7);
  mo.hour = (int)(parts / 1080);
  mo.minute = (int)((parts % 1080) / 18);
  mo.chalakim = (int)(parts % 18);
  return mo;
}

} // namespace heb
