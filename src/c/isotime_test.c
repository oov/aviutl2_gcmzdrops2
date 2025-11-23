#include <ovtest.h>

#include "isotime.h"

#include <string.h>
#include <time.h>

static bool parse_literal(char const *literal, uint64_t *timestamp_us, int32_t *tz_offset_sec) {
  return isotime_parse(literal, strlen(literal), timestamp_us, tz_offset_sec);
}

static bool make_timestamp(int year, int month, int day, int hour, int minute, int second, uint64_t *timestamp_us) {
  struct tm tm = {0};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
#ifdef _WIN32
  time_t const sec = _mkgmtime(&tm);
#else
  time_t const sec = timegm(&tm);
#endif
  if (sec == (time_t)-1) {
    return false;
  }
  *timestamp_us = (uint64_t)sec * 1000000ULL;
  return true;
}

static bool build_expected_timestamp(int year,
                                     int month,
                                     int day,
                                     int hour,
                                     int minute,
                                     int second,
                                     int microsecond,
                                     int32_t offset_sec,
                                     uint64_t *timestamp_us) {
  uint64_t base = 0;
  if (!make_timestamp(year, month, day, hour, minute, second, &base)) {
    return false;
  }
  int64_t const adjusted = (int64_t)base - (int64_t)offset_sec * 1000000LL + (int64_t)microsecond;
  if (adjusted < 0) {
    return false;
  }
  *timestamp_us = (uint64_t)adjusted;
  return true;
}

static void test_parse_success_cases(void) {
  struct parse_case {
    char const *literal;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int microsecond;
    int32_t offset_sec;
    bool check_offset;
    bool use_tm;
    uint64_t expected_constant;
  };

  struct parse_case const cases[] = {
      {"2024-01-15T12:30:45Z", 2024, 1, 15, 12, 30, 45, 0, 0, true, true, 0ULL},
      {"2024-01-15T12:30:45+05:30", 2024, 1, 15, 12, 30, 45, 0, 19800, true, true, 0ULL},
      {"2024-01-15T12:30:45-08:00", 2024, 1, 15, 12, 30, 45, 0, -28800, true, true, 0ULL},
      {"2024-01-15T12:30:45.123456Z", 2024, 1, 15, 12, 30, 45, 123456, 0, true, true, 0ULL},
      {"2024-01-15T12:30:45z", 2024, 1, 15, 12, 30, 45, 0, 0, true, true, 0ULL},
      {"2024-01-15", 2024, 1, 15, 0, 0, 0, 0, 0, false, true, 0ULL},
      {"2024-01-15T12Z", 2024, 1, 15, 12, 0, 0, 0, 0, true, true, 0ULL},
      {"2024-01-15T12:30Z", 2024, 1, 15, 12, 30, 0, 0, 0, true, true, 0ULL},
      {"2024-02-29T12:30:45Z", 2024, 2, 29, 12, 30, 45, 0, 0, true, true, 0ULL},
      {"9999-12-31T23:59:59Z", 9999, 12, 31, 23, 59, 59, 0, 0, true, false, 253402300799000000ULL},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    struct parse_case const tc = cases[i];
    TEST_CASE_("literal=%s", tc.literal);
    uint64_t timestamp_us = 0;
    int32_t tz_offset = 0;
    if (!TEST_CHECK(parse_literal(tc.literal, &timestamp_us, &tz_offset))) {
      continue;
    }

    if (tc.use_tm) {
      uint64_t expected = 0;
      if (!TEST_CHECK(build_expected_timestamp(
              tc.year, tc.month, tc.day, tc.hour, tc.minute, tc.second, tc.microsecond, tc.offset_sec, &expected))) {
        continue;
      }
      TEST_CHECK(timestamp_us == expected);
    } else {
      TEST_CHECK(timestamp_us == tc.expected_constant);
    }
    if (tc.check_offset) {
      TEST_CHECK(tz_offset == tc.offset_sec);
    }
  }
}

static void test_parse_invalid_cases(void) {
  uint64_t timestamp_us = 0;
  int32_t tz_offset = 0;
  TEST_CHECK(!isotime_parse("2024-01-15", 5, &timestamp_us, &tz_offset));
  char const *const literals[] = {"2024/01/15T12:30:45Z",
                                  "2024-13-15T12:30:45Z",
                                  "2024-02-30T12:30:45Z",
                                  "2024-01-15T25:30:45Z",
                                  "2023-02-29T12:30:45Z"};
  for (size_t i = 0; i < sizeof(literals) / sizeof(literals[0]); i++) {
    TEST_CASE_("literal=%s", literals[i]);
    TEST_CHECK(!parse_literal(literals[i], &timestamp_us, &tz_offset));
  }
}

static void test_parse_null_params(void) {
  uint64_t timestamp_us = 0;
  TEST_CHECK(!isotime_parse(NULL, 20, &timestamp_us, NULL));
  TEST_CHECK(!parse_literal("2024-01-15T12:30:45Z", NULL, NULL));
}

static void test_parse_tz_offset_null(void) {
  uint64_t timestamp_us = 0;
  TEST_CHECK(parse_literal("2024-01-15T12:30:45+05:30", &timestamp_us, NULL));
}

static void test_parse_bounds(void) {
  uint64_t timestamp_us = 0;
  TEST_CHECK(parse_literal("1970-01-01T00:00:00Z", &timestamp_us, NULL));
  TEST_CHECK(timestamp_us == 0);
  TEST_CHECK(!parse_literal("1969-12-31T23:59:59Z", &timestamp_us, NULL));
  TEST_CHECK(parse_literal("9999-12-31T23:59:59Z", &timestamp_us, NULL));
  TEST_CHECK(timestamp_us == 253402300799000000ULL);
  TEST_CHECK(!parse_literal("10000-01-01T00:00:00Z", &timestamp_us, NULL));
}

static void test_roundtrip_utc(void) {
  char const *literal = "2024-01-15T12:30:45Z";
  uint64_t timestamp_us = 0;
  TEST_CHECK(isotime_parse(literal, strlen(literal), &timestamp_us, NULL));
  char buf[26];
  TEST_CHECK(isotime_format(timestamp_us, buf, 0) == buf);
  uint64_t parsed = 0;
  TEST_CHECK(isotime_parse(buf, strlen(buf), &parsed, NULL));
  TEST_CHECK(timestamp_us == parsed);
}

static void test_roundtrip_with_tz(void) {
  uint64_t const timestamp_us = 1705318245000000ULL;
  int32_t const tz_offset = 19800;
  char buf[26];
  TEST_CHECK(isotime_format(timestamp_us, buf, tz_offset) == buf);
  TEST_CHECK(strlen(buf) == 25);
  TEST_CHECK(buf[19] == '+');
  uint64_t parsed_timestamp = 0;
  int32_t parsed_offset = 0;
  TEST_CHECK(isotime_parse(buf, strlen(buf), &parsed_timestamp, &parsed_offset));
  TEST_CHECK(parsed_offset == tz_offset);
}

static void test_microsecond_roundtrip(void) {
  char const *literal = "2024-01-15T12:30:45.999999Z";
  uint64_t timestamp_us = 0;
  TEST_CHECK(isotime_parse(literal, strlen(literal), &timestamp_us, NULL));
  TEST_CHECK(timestamp_us % 1000000ULL == 999999ULL);
  char buf[26];
  TEST_CHECK(isotime_format(timestamp_us, buf, 0) == buf);
  uint64_t parsed = 0;
  TEST_CHECK(isotime_parse(buf, strlen(buf), &parsed, NULL));
  TEST_CHECK(timestamp_us / 1000000ULL == parsed / 1000000ULL);
}

static void test_now(void) {
  uint64_t const timestamp_us = isotime_now();
  TEST_CHECK(timestamp_us > 1577836800000000ULL);
  char buf[26];
  TEST_CHECK(isotime_format(timestamp_us, buf, 0) == buf);
  TEST_CHECK(strlen(buf) == 20);
  TEST_CHECK(buf[19] == 'Z');
}

static void test_format_null_buffer(void) { TEST_CHECK(isotime_format(1000000000000000ULL, NULL, 0) == NULL); }

static void test_format_offsets(void) {
  struct format_case {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int32_t offset;
    char const *expected;
  };
  struct format_case const cases[] = {
      {2024, 1, 15, 12, 30, 45, 0, "2024-01-15T12:30:45Z"},
      {2024, 1, 15, 12, 30, 45, 19800, "2024-01-15T12:30:45+05:30"},
      {2024, 1, 15, 12, 30, 45, -28800, "2024-01-15T12:30:45-08:00"},
      {2024, 1, 15, 12, 30, 45, 20700, "2024-01-15T12:30:45+05:45"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    TEST_CASE_("offset=%d", cases[i].offset);
    uint64_t timestamp_us = 0;
    TEST_CHECK_(make_timestamp(cases[i].year,
                               cases[i].month,
                               cases[i].day,
                               cases[i].hour,
                               cases[i].minute,
                               cases[i].second,
                               &timestamp_us),
                "offset: %d",
                cases[i].offset);
    char buf[26];
    char *formatted = isotime_format(timestamp_us, buf, cases[i].offset);
    if (!TEST_CHECK_(formatted == buf, "offset: %d", cases[i].offset)) {
      continue;
    }
    TEST_CHECK_(strcmp(buf, cases[i].expected) == 0, "offset: %d", cases[i].offset);
  }
}

static void test_format_max_timestamp(void) {
  char buf[26];
  TEST_CHECK(isotime_format(UINT64_MAX, buf, 0) == buf);
  TEST_CHECK(strcmp(buf, "9999-12-31T23:59:59Z") == 0);
  uint64_t parsed = 0;
  TEST_CHECK(isotime_parse(buf, strlen(buf), &parsed, NULL));
  TEST_CHECK(parsed == 253402300799000000ULL);
}

static void test_format_vs_strftime(void) {
  struct strftime_case {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    char const *note;
  };

  struct strftime_case const cases[] = {
      {1970, 1, 1, 0, 0, 0, "epoch"},
      {1970, 12, 31, 23, 59, 59, "first year end"},
      {2000, 1, 1, 0, 0, 0, "y2k"},
      {2000, 2, 29, 12, 0, 0, "leap 2000"},
      {2004, 2, 29, 12, 0, 0, "leap 2004"},
      {2100, 2, 28, 12, 0, 0, "non-leap 2100"},
      {2023, 12, 31, 23, 59, 59, "recent year end"},
      {2024, 1, 1, 0, 0, 0, "recent year start"},
      {2024, 2, 29, 12, 30, 45, "recent leap"},
      {9999, 12, 31, 23, 59, 59, "max"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    struct strftime_case const tc = cases[i];
    TEST_CASE_("case=%s", tc.note);
    uint64_t timestamp_us = 0;
    if (!make_timestamp(tc.year, tc.month, tc.day, tc.hour, tc.minute, tc.second, &timestamp_us)) {
      continue;
    }
    char buf[26];
    char *formatted = isotime_format(timestamp_us, buf, 0);
    if (!TEST_CHECK_(formatted == buf, "case: %s", tc.note)) {
      continue;
    }
    time_t const timestamp_sec = (time_t)(timestamp_us / 1000000ULL);
    struct tm *utc_time = gmtime(&timestamp_sec);
    if (!utc_time) {
      continue;
    }
    char expected[26];
    size_t const len = strftime(expected, sizeof(expected), "%Y-%m-%dT%H:%M:%SZ", utc_time);
    TEST_CHECK_(len == 20, "case: %s", tc.note);
    TEST_CHECK_(strcmp(buf, expected) == 0, "case: %s", tc.note);
  }
}

TEST_LIST = {
    {"test_parse_success_cases", test_parse_success_cases},
    {"test_parse_invalid_cases", test_parse_invalid_cases},
    {"test_parse_null_params", test_parse_null_params},
    {"test_parse_tz_offset_null", test_parse_tz_offset_null},
    {"test_parse_bounds", test_parse_bounds},
    {"test_roundtrip_utc", test_roundtrip_utc},
    {"test_roundtrip_with_tz", test_roundtrip_with_tz},
    {"test_microsecond_roundtrip", test_microsecond_roundtrip},
    {"test_now", test_now},
    {"test_format_null_buffer", test_format_null_buffer},
    {"test_format_offsets", test_format_offsets},
    {"test_format_max_timestamp", test_format_max_timestamp},
    {"test_format_vs_strftime", test_format_vs_strftime},
    {NULL, NULL},
};
