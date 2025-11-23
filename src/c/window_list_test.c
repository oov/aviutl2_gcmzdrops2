#include <ovtest.h>

#include "gcmz_types.h"
#include "window_list.h"

static void test_create_destroy(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  size_t num_windows = 999;
  struct gcmz_window_info const *items = gcmz_window_list_get(wl, &num_windows);
  TEST_CHECK(items != NULL);
  TEST_CHECK(num_windows == 0);

  gcmz_window_list_destroy(&wl);
  TEST_CHECK(wl == NULL);
}

static void test_update_empty(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  struct gcmz_window_info windows[] = {0};
  TEST_CHECK(gcmz_window_list_update(wl, windows, 0, &err) == ov_false);

  size_t num_windows = 999;
  gcmz_window_list_get(wl, &num_windows);
  TEST_CHECK(num_windows == 0);

  gcmz_window_list_destroy(&wl);
}

static void test_update_change_detected(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  // First update - should detect change from empty
  struct gcmz_window_info windows1[] = {
      {.window = (void *)0x1000, .width = 100, .height = 200},
      {.window = (void *)0x2000, .width = 150, .height = 250},
  };
  enum { windows1_count = sizeof(windows1) / sizeof(windows1[0]) };
  TEST_CHECK(gcmz_window_list_update(wl, windows1, windows1_count, &err) == ov_true);

  // Second update with different windows - should detect change
  struct gcmz_window_info windows2[] = {
      {.window = (void *)0x3000, .width = 100, .height = 200},
      {.window = (void *)0x4000, .width = 150, .height = 250},
  };
  enum { windows2_count = sizeof(windows2) / sizeof(windows2[0]) };
  TEST_CHECK(gcmz_window_list_update(wl, windows2, windows2_count, &err) == ov_true);

  gcmz_window_list_destroy(&wl);
}

static void test_update_no_change(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  struct gcmz_window_info windows[] = {
      {.window = (void *)0x1000, .width = 100, .height = 200},
      {.window = (void *)0x2000, .width = 150, .height = 250},
  };
  enum { windows_count = sizeof(windows) / sizeof(windows[0]) };

  TEST_CHECK(gcmz_window_list_update(wl, windows, windows_count, &err) == ov_true);

  // Update with same windows - should detect no change
  TEST_CHECK(gcmz_window_list_update(wl, windows, windows_count, &err) == ov_false);

  gcmz_window_list_destroy(&wl);
}

static void test_update_size_change(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  struct gcmz_window_info windows1[] = {
      {.window = (void *)0x1000, .width = 100, .height = 200},
      {.window = (void *)0x2000, .width = 150, .height = 250},
  };
  enum { windows1_count = sizeof(windows1) / sizeof(windows1[0]) };

  TEST_CHECK(gcmz_window_list_update(wl, windows1, windows1_count, &err) == ov_true);

  // Update with different sizes but same windows
  struct gcmz_window_info windows2[] = {
      {.window = (void *)0x1000, .width = 200, .height = 300},
      {.window = (void *)0x2000, .width = 250, .height = 350},
  };
  enum { windows2_count = sizeof(windows2) / sizeof(windows2[0]) };
  TEST_CHECK(gcmz_window_list_update(wl, windows2, windows2_count, &err) == ov_false);

  // Verify sizes were updated
  size_t num_windows = 0;
  struct gcmz_window_info const *items = gcmz_window_list_get(wl, &num_windows);
  TEST_CHECK(num_windows == windows1_count);
  // Items are sorted by window handle
  if (items[0].window == (void *)0x1000) {
    TEST_CHECK(items[0].width == 200);
    TEST_CHECK(items[0].height == 300);
    TEST_CHECK(items[1].width == 250);
    TEST_CHECK(items[1].height == 350);
  } else {
    TEST_CHECK(items[1].width == 200);
    TEST_CHECK(items[1].height == 300);
    TEST_CHECK(items[0].width == 250);
    TEST_CHECK(items[0].height == 350);
  }

  gcmz_window_list_destroy(&wl);
}

static void test_update_too_many_windows(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  enum { num_windows = 9 };
  struct gcmz_window_info windows[num_windows];
  for (size_t i = 0; i < num_windows; ++i) {
    windows[i] = (struct gcmz_window_info){
        .window = (void *)(uintptr_t)(0x1000 + i * 0x1000),
        .width = 100,
        .height = 200,
    };
  }

  TEST_CHECK(gcmz_window_list_update(wl, windows, num_windows, &err) == ov_indeterminate);
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_fail));

  gcmz_window_list_destroy(&wl);
  OV_ERROR_DESTROY(&err);
}

static void test_update_invalid_args(void) {
  struct ov_error err = {0};
  struct gcmz_window_list *wl = gcmz_window_list_create(&err);
  TEST_CHECK(wl != NULL);

  struct gcmz_window_info windows[] = {
      {.window = (void *)0x1000, .width = 100, .height = 200},
  };
  enum { windows_count = sizeof(windows) / sizeof(windows[0]) };

  // NULL window list
  TEST_CHECK(gcmz_window_list_update(NULL, windows, windows_count, &err) == ov_indeterminate);
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
  OV_ERROR_DESTROY(&err);

  // NULL windows array
  TEST_CHECK(gcmz_window_list_update(wl, NULL, windows_count, &err) == ov_indeterminate);
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));

  gcmz_window_list_destroy(&wl);
  OV_ERROR_DESTROY(&err);
}

static void test_get_invalid_args(void) {
  struct gcmz_window_list *wl = NULL;
  size_t num_windows = 0;

  // NULL window list
  struct gcmz_window_info const *items = gcmz_window_list_get(NULL, &num_windows);
  TEST_CHECK(items == NULL);

  // NULL num_windows
  items = gcmz_window_list_get(wl, NULL);
  TEST_CHECK(items == NULL);
}

TEST_LIST = {
    {"test_create_destroy", test_create_destroy},
    {"test_update_empty", test_update_empty},
    {"test_update_change_detected", test_update_change_detected},
    {"test_update_no_change", test_update_no_change},
    {"test_update_size_change", test_update_size_change},
    {"test_update_too_many_windows", test_update_too_many_windows},
    {"test_update_invalid_args", test_update_invalid_args},
    {"test_get_invalid_args", test_get_invalid_args},
    {NULL, NULL},
};
