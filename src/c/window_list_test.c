#include <ovtest.h>

#include "window_list.h"

static bool check_window_list_update(struct gcmz_window_list *wl, void **windows, size_t count, ov_tribool want) {
  ov_tribool got = gcmz_window_list_update(wl, windows, count, NULL);
  if (got == ov_indeterminate) {
    return TEST_CHECK(got == want);
  }
  return TEST_CHECK(got == want);
}

static void test_create_destroy(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  gcmz_window_list_destroy(&wl);
  TEST_CHECK(wl == NULL);
}

static void test_update_empty(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  void *windows[] = {NULL};
  check_window_list_update(wl, windows, 0, ov_false);

  gcmz_window_list_destroy(&wl);
}

static void test_update_change_detected(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  // First update - should detect change from empty
  void *windows1[] = {
      (void *)0x1000,
      (void *)0x2000,
  };
  enum { windows1_count = sizeof(windows1) / sizeof(windows1[0]) };
  check_window_list_update(wl, windows1, windows1_count, ov_true);

  // Second update with different windows - should detect change
  void *windows2[] = {
      (void *)0x3000,
      (void *)0x4000,
  };
  enum { windows2_count = sizeof(windows2) / sizeof(windows2[0]) };
  check_window_list_update(wl, windows2, windows2_count, ov_true);

  gcmz_window_list_destroy(&wl);
}

static void test_update_no_change(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  void *windows[] = {
      (void *)0x1000,
      (void *)0x2000,
  };
  enum { windows_count = sizeof(windows) / sizeof(windows[0]) };

  check_window_list_update(wl, windows, windows_count, ov_true);

  // Update with same windows - should detect no change
  check_window_list_update(wl, windows, windows_count, ov_false);

  gcmz_window_list_destroy(&wl);
}

static void test_update_size_change(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  void *windows1[] = {
      (void *)0x1000,
      (void *)0x2000,
  };
  enum { windows1_count = sizeof(windows1) / sizeof(windows1[0]) };

  check_window_list_update(wl, windows1, windows1_count, ov_true);

  // Update with same windows - should detect no change
  void *windows2[] = {
      (void *)0x1000,
      (void *)0x2000,
  };
  enum { windows2_count = sizeof(windows2) / sizeof(windows2[0]) };
  check_window_list_update(wl, windows2, windows2_count, ov_false);

  gcmz_window_list_destroy(&wl);
}

static void test_update_too_many_windows(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  enum { num_windows = 17 };
  void *windows[num_windows];
  for (size_t i = 0; i < num_windows; ++i) {
    windows[i] = (void *)(uintptr_t)(0x1000 + i * 0x1000);
  }

  TEST_FAILED_WITH(gcmz_window_list_update(wl, windows, num_windows, &err) != ov_indeterminate,
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_fail);

  gcmz_window_list_destroy(&wl);
}

static void test_update_invalid_args(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_ASSERT_SUCCEEDED(wl != NULL, &err);

  void *windows[] = {
      (void *)0x1000,
  };
  enum { windows_count = sizeof(windows) / sizeof(windows[0]) };

  // NULL window list
  TEST_FAILED_WITH(gcmz_window_list_update(NULL, windows, windows_count, &err) != ov_indeterminate,
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  // NULL windows array
  TEST_FAILED_WITH(gcmz_window_list_update(wl, NULL, windows_count, &err) != ov_indeterminate,
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  gcmz_window_list_destroy(&wl);
}

TEST_LIST = {
    {"test_create_destroy", test_create_destroy},
    {"test_update_empty", test_update_empty},
    {"test_update_change_detected", test_update_change_detected},
    {"test_update_no_change", test_update_no_change},
    {"test_update_size_change", test_update_size_change},
    {"test_update_too_many_windows", test_update_too_many_windows},
    {"test_update_invalid_args", test_update_invalid_args},
    {NULL, NULL},
};
