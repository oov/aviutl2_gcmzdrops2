#include <ovtest.h>

#include <ovthreads.h>

#include "do.h"

static HWND g_test_window = NULL;
static bool g_window_created = false;

static void setup_test_window(void) {
  if (!g_window_created) {
    g_test_window = CreateWindowExW(
        0, L"STATIC", L"Test Window", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    g_window_created = (g_test_window != NULL);
  }
}

static void cleanup_test_window(void) {
  if (g_window_created && g_test_window) {
    DestroyWindow(g_test_window);
    g_test_window = NULL;
    g_window_created = false;
  }
}

static void test_init_with_null_window(void) {
  cleanup_test_window();
  struct ov_error err = {0};

  TEST_FAILED_WITH(
      gcmz_do_init(&(struct gcmz_do_init_option){0}, &err), &err, ov_error_type_generic, ov_error_generic_fail);

  cleanup_test_window();
}

static void test_init_with_null_error(void) {
  cleanup_test_window();
  setup_test_window();

  TEST_CHECK(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, NULL));

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_init_with_invalid_window(void) {
  cleanup_test_window();
  struct ov_error err = {0};

  TEST_FAILED_WITH(gcmz_do_init(&(struct gcmz_do_init_option){.window = (void *)0x12345678}, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_fail);

  cleanup_test_window();
}

static void test_init_success(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_double_init(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_exit_without_init(void) {
  cleanup_test_window();
  gcmz_do_exit();
  cleanup_test_window();
}

static void test_exit_after_init(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_double_exit(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  gcmz_do_exit();
  gcmz_do_exit();

  cleanup_test_window();
}

static int g_test_func_call_count = 0;
static void *g_test_func_data = NULL;

static void test_func(void *data) {
  g_test_func_call_count++;
  g_test_func_data = data;
}

static void test_do_without_init(void) {
  cleanup_test_window();
  g_test_func_call_count = 0;
  g_test_func_data = NULL;

  gcmz_do(test_func, (void *)0x12345);

  TEST_CHECK(g_test_func_call_count == 0);
  TEST_CHECK(g_test_func_data == NULL);

  cleanup_test_window();
}

static void test_do_with_null_func(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  g_test_func_call_count = 0;
  gcmz_do(NULL, (void *)0x12345);

  TEST_CHECK(g_test_func_call_count == 0);

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_do_same_thread(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  g_test_func_call_count = 0;
  g_test_func_data = NULL;

  void *test_data = (void *)0x12345;
  gcmz_do(test_func, test_data);

  TEST_CHECK(g_test_func_call_count == 1);
  TEST_CHECK(g_test_func_data == test_data);

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_do_blocking_without_init(void) {
  cleanup_test_window();
  g_test_func_call_count = 0;
  g_test_func_data = NULL;

  gcmz_do_blocking(test_func, (void *)0x12345);

  TEST_CHECK(g_test_func_call_count == 0);
  TEST_CHECK(g_test_func_data == NULL);

  cleanup_test_window();
}

static void test_do_blocking_with_null_func(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  g_test_func_call_count = 0;
  gcmz_do_blocking(NULL, (void *)0x12345);

  TEST_CHECK(g_test_func_call_count == 0);

  gcmz_do_exit();
  cleanup_test_window();
}

static void test_do_blocking_same_thread(void) {
  cleanup_test_window();
  setup_test_window();

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_do_init(&(struct gcmz_do_init_option){.window = g_test_window}, &err), &err);

  g_test_func_call_count = 0;
  g_test_func_data = NULL;

  void *test_data = (void *)0x12345;
  gcmz_do_blocking(test_func, test_data);

  TEST_CHECK(g_test_func_call_count == 1);
  TEST_CHECK(g_test_func_data == test_data);

  gcmz_do_exit();
  cleanup_test_window();
}

TEST_LIST = {
    {"init_with_null_window", test_init_with_null_window},
    {"init_with_null_error", test_init_with_null_error},
    {"init_with_invalid_window", test_init_with_invalid_window},
    {"init_success", test_init_success},
    {"double_init", test_double_init},
    {"exit_without_init", test_exit_without_init},
    {"exit_after_init", test_exit_after_init},
    {"double_exit", test_double_exit},
    {"do_without_init", test_do_without_init},
    {"do_with_null_func", test_do_with_null_func},
    {"do_same_thread", test_do_same_thread},
    {"do_blocking_without_init", test_do_blocking_without_init},
    {"do_blocking_with_null_func", test_do_blocking_with_null_func},
    {"do_blocking_same_thread", test_do_blocking_same_thread},
    {NULL, NULL},
};
