#include "timefmt.h"

// strftime is not used here: "%a %d %b" gives "Sun 13 Sep" — lower case, and
// with a leading zero on the day.
static const char *const WDAY[7] = {"SUN", "MON", "TUE", "WED",
                                    "THU", "FRI", "SAT"};
static const char *const MONTH[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static bool hm_valid(const struct tm *t) {
  return t != NULL && t->tm_hour >= 0 && t->tm_hour <= 23 && t->tm_min >= 0 &&
         t->tm_min <= 59;
}

static bool date_valid(const struct tm *t) {
  return t != NULL && t->tm_wday >= 0 && t->tm_wday <= 6 && t->tm_mon >= 0 &&
         t->tm_mon <= 11 && t->tm_mday >= 1 && t->tm_mday <= 31;
}

void timefmt_hm(char *buf, size_t size, const struct tm *t, bool use_24h) {
  if (!hm_valid(t)) {
    snprintf(buf, size, "--:--");
    return;
  }
  if (use_24h) {
    snprintf(buf, size, "%02d:%02d", t->tm_hour, t->tm_min);
    return;
  }
  // Dropping the leading zero frees the column the marker needs.
  int hour = t->tm_hour % 12;
  if (hour == 0) hour = 12;
  snprintf(buf, size, "%d:%02d%c", hour, t->tm_min,
           t->tm_hour < 12 ? 'A' : 'P');
}

void timefmt_date(char *buf, size_t size, const struct tm *t) {
  if (!date_valid(t)) {
    if (size > 0) buf[0] = '\0';
    return;
  }
  snprintf(buf, size, "%s %d %s", WDAY[t->tm_wday], t->tm_mday,
           MONTH[t->tm_mon]);
}
