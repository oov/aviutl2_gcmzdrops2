#include "isotime.h"

#include <string.h>
#include <time.h>

static uint64_t const microseconds_per_second = 1000000ULL;
static int const month_lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// These helpers are adapted from Howard Hinnant's algorithms:
// https://howardhinnant.github.io/date_algorithms.html

static int days_from_civil(int const year, int const month, int const day) {
  int const adjusted_year = year - (month <= 2);
  int const era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
  int const yoe = adjusted_year - era * 400;
  int const mp = month + (month > 2 ? -3 : 9);
  int const doy = (153 * mp + 2) / 5 + day - 1;
  int const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(int const days, int *const year, int *const month, int *const day) {
  int const z = days + 719468; // Offset to the algorithm's epoch (0000-03-01)
  int const era = (z >= 0 ? z : z - 146096) / 146097;
  int const doe = z - era * 146097;                                      // [0, 146096]
  int const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
  int const yoe_year = yoe + era * 400;
  int const doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
  int const mp = (5 * doy + 2) / 153;                      // [0, 11]
  int const day_local = doy - (153 * mp + 2) / 5 + 1;
  int const month_local = mp + (mp < 10 ? 3 : -9);
  int const year_local = yoe_year + (month_local <= 2);
  if (year) {
    *year = year_local;
  }
  if (month) {
    *month = month_local;
  }
  if (day) {
    *day = day_local;
  }
}

static bool parse_2digits(char const *const str, int *const value) {
  if (!str || !value) {
    return false;
  }
  unsigned int const d0 = (unsigned int)(str[0] - '0');
  unsigned int const d1 = (unsigned int)(str[1] - '0');
  if (d0 > 9U || d1 > 9U) {
    return false;
  }
  *value = (int)(d0 * 10U + d1);
  return true;
}

static bool parse_4digits(char const *const str, int *const value) {
  if (!str || !value) {
    return false;
  }
  unsigned int const d0 = (unsigned int)(str[0] - '0');
  unsigned int const d1 = (unsigned int)(str[1] - '0');
  unsigned int const d2 = (unsigned int)(str[2] - '0');
  unsigned int const d3 = (unsigned int)(str[3] - '0');
  if (d0 > 9U || d1 > 9U || d2 > 9U || d3 > 9U) {
    return false;
  }
  *value = (int)(d0 * 1000U + d1 * 100U + d2 * 10U + d3);
  return true;
}

static void to_2digit(int const value, char buf[2]) {
  buf[0] = (char)('0' + (value / 10));
  buf[1] = (char)('0' + (value % 10));
}

static void to_4digit(int const value, char buf[4]) {
  buf[0] = (char)('0' + (value / 1000));
  buf[1] = (char)('0' + ((value / 100) % 10));
  buf[2] = (char)('0' + ((value / 10) % 10));
  buf[3] = (char)('0' + (value % 10));
}

bool isotime_parse(char const *const str,
                   size_t const len,
                   uint64_t *const timestamp_us,
                   int32_t *const tz_offset_sec) {
  if (!str || !timestamp_us) {
    return false;
  }

  char const *pos = str;
  char const *const end = str + len;
  if ((size_t)(end - pos) < 10) {
    return false;
  }

  int year = 0;
  if ((size_t)(end - pos) < 4 || !parse_4digits(pos, &year)) {
    return false;
  }
  pos += 4;

  if (pos == end || pos[0] != '-') {
    return false;
  }
  pos++;

  int month = 0;
  if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &month)) {
    return false;
  }
  pos += 2;

  if (pos == end || pos[0] != '-') {
    return false;
  }
  pos++;

  int day = 0;
  if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &day)) {
    return false;
  }
  pos += 2;

  int hour = 0;
  int minute = 0;
  int second = 0;
  int microsecond = 0;

  if (pos != end && pos[0] == 'T') {
    pos++;
    if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &hour)) {
      return false;
    }
    pos += 2;

    if (pos != end && pos[0] == ':') {
      pos++;
      if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &minute)) {
        return false;
      }
      pos += 2;

      if (pos != end && pos[0] == ':') {
        pos++;
        if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &second)) {
          return false;
        }
        pos += 2;

        if (pos != end && pos[0] == '.') {
          pos++;
          if (pos == end || (unsigned int)(pos[0] - '0') > 9U) {
            return false;
          }

          int digits = 0;
          microsecond = 0;
          while (pos != end && (unsigned int)(pos[0] - '0') <= 9U && digits < 6) {
            microsecond = microsecond * 10 + (pos[0] - '0');
            pos++;
            digits++;
          }
          while (digits < 6) {
            microsecond *= 10;
            digits++;
          }
          while (pos != end && (unsigned int)(pos[0] - '0') <= 9U) {
            pos++;
          }
        }
      }
    }
  }

  int tz_offset_seconds = 0;
  if (pos != end) {
    char const tz_char = pos[0];
    if (tz_char == 'Z' || tz_char == 'z') {
      pos++;
    } else if ((tz_char == '+' || tz_char == '-') && (size_t)(end - pos) >= 3) {
      int const sign = (tz_char == '+') ? 1 : -1;
      pos++;

      int tz_hours = 0;
      if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &tz_hours)) {
        return false;
      }
      pos += 2;

      int tz_minutes = 0;
      if (pos != end) {
        if ((size_t)(end - pos) < 3 || pos[0] != ':') {
          return false;
        }
        pos++;
        if ((size_t)(end - pos) < 2 || !parse_2digits(pos, &tz_minutes)) {
          return false;
        }
        pos += 2;
      }

      if (tz_hours < 0 || tz_hours > 23 || tz_minutes < 0 || tz_minutes > 59) {
        return false;
      }
      tz_offset_seconds = sign * (tz_hours * 3600 + tz_minutes * 60);
    } else {
      return false;
    }
  }

  if (pos != end) {
    return false;
  }

  if (year < 1970 || year > 9999 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59 || microsecond < 0 || microsecond > 999999) {
    return false;
  }

  bool const is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  int const max_day = month_lengths[month - 1] + ((month == 2 && is_leap_year) ? 1 : 0);
  if (day < 1 || day > max_day) {
    return false;
  }

  int const days = days_from_civil(year, month, day);
  uint64_t total_seconds = (uint64_t)days * 24ULL * 3600ULL + (uint64_t)(hour * 3600 + minute * 60 + second);

  if (tz_offset_seconds >= 0) {
    total_seconds -= (uint64_t)tz_offset_seconds;
  } else {
    total_seconds += (uint64_t)(-tz_offset_seconds);
  }

  if (tz_offset_sec) {
    *tz_offset_sec = tz_offset_seconds;
  }

  *timestamp_us = total_seconds * microseconds_per_second + (uint64_t)microsecond;
  return true;
}

char *isotime_format(uint64_t const timestamp_us, char buf[26], int32_t const tz_offset_sec) {
  if (!buf) {
    return NULL;
  }

  static uint64_t const max_supported_seconds = 253402300799ULL; // 9999-12-31T23:59:59
  uint64_t total_seconds = timestamp_us / microseconds_per_second;
  if (total_seconds > max_supported_seconds) {
    total_seconds = max_supported_seconds;
  }

  static uint64_t const seconds_per_day = 60 * 60 * 24;
  int const days = (int)(total_seconds / seconds_per_day);
  int const seconds_today = (int)(total_seconds % seconds_per_day);

  static int const seconds_per_hour = 60 * 60;
  int const hour = seconds_today / seconds_per_hour;
  int const seconds_this_hour = seconds_today % seconds_per_hour;

  static int const seconds_per_minute = 60;
  int const minute = seconds_this_hour / seconds_per_minute;
  int const second = seconds_this_hour % seconds_per_minute;

  int year = 0;
  int month = 0;
  int day = 0;
  civil_from_days(days, &year, &month, &day);

  // Format base: YYYY-MM-DDTHH:MM:SS
  // Position: 0123456789...
  //           YYYY-MM-DDTHH:MM:SS
  to_4digit(year, buf + 0);
  buf[4] = '-';
  to_2digit(month, buf + 5);
  buf[7] = '-';
  to_2digit(day, buf + 8);
  buf[10] = 'T';
  to_2digit(hour, buf + 11);
  buf[13] = ':';
  to_2digit(minute, buf + 14);
  buf[16] = ':';
  to_2digit(second, buf + 17);

  // Format timezone
  if (tz_offset_sec == 0) {
    buf[19] = 'Z';
    buf[20] = '\0';
  } else {
    // Format: +HH:MM or -HH:MM
    int32_t offset = tz_offset_sec;
    char const sign = (offset > 0) ? '+' : '-';
    if (offset < 0) {
      offset = -offset;
    }

    int const tz_hours = (int)(offset / 3600);
    int const tz_minutes = (int)((offset % 3600) / 60);

    buf[19] = sign;
    to_2digit(tz_hours, buf + 20);
    buf[22] = ':';
    to_2digit(tz_minutes, buf + 23);
    buf[25] = '\0';
  }

  return buf;
}

uint64_t isotime_now(void) {
  struct timespec ts = {0};
  timespec_get(&ts, TIME_UTC);

  uint64_t sec = (uint64_t)ts.tv_sec;
  uint64_t us = (uint64_t)ts.tv_nsec / 1000;
  return sec * 1000000ULL + us;
}
