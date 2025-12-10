#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovtest.h>

#include "tray.h"

static int g_callback_called = 0;
static wchar_t const *g_menu_label = NULL;
static bool g_menu_enabled = false;

static void test_callback(void *const userdata, struct gcmz_tray_callback_event *const event) {
  (void)userdata;
  switch (event->type) {
  case gcmz_tray_callback_query_info:
    event->result.query_info.label = g_menu_label;
    event->result.query_info.enabled = g_menu_enabled;
    break;
  case gcmz_tray_callback_clicked:
    g_callback_called++;
    break;
  }
}

static void reset_test_state(void) {
  g_callback_called = 0;
  g_menu_label = NULL;
  g_menu_enabled = false;
}

static void test_tray_create_destroy(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    return;
  }
  gcmz_tray_destroy(&tray);
  TEST_CHECK(tray == NULL);
}

static void test_tray_window_exists(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    return;
  }
  TEST_CHECK(FindWindowW(L"GCMZDropsTrayWindow", NULL) != NULL);
  gcmz_tray_destroy(&tray);
  TEST_CHECK(FindWindowW(L"GCMZDropsTrayWindow", NULL) == NULL);
}

static void test_tray_add_menu_item(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = NULL;

  reset_test_state();
  g_menu_label = L"Test Menu Item";
  g_menu_enabled = true;

  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(gcmz_tray_add_menu_item(tray, test_callback, NULL, &err), &err)) {
    goto cleanup;
  }

cleanup:
  gcmz_tray_destroy(&tray);
}

static void test_tray_add_multiple_menu_items(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = NULL;

  reset_test_state();
  g_menu_label = L"Menu Item";
  g_menu_enabled = true;

  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  for (int i = 0; i < 10; i++) {
    if (!TEST_SUCCEEDED(gcmz_tray_add_menu_item(tray, test_callback, NULL, &err), &err)) {
      goto cleanup;
    }
  }

cleanup:
  gcmz_tray_destroy(&tray);
}

static void test_tray_remove_menu_item(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = NULL;

  reset_test_state();
  g_menu_label = L"Test Menu Item";
  g_menu_enabled = true;

  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(gcmz_tray_add_menu_item(tray, test_callback, NULL, &err), &err)) {
    goto cleanup;
  }
  gcmz_tray_remove_menu_item(tray, test_callback);

cleanup:
  gcmz_tray_destroy(&tray);
}

static void test_callback_2(void *const userdata, struct gcmz_tray_callback_event *const event) {
  (void)userdata;
  (void)event;
  // Different callback for testing multiple items
}

static void test_tray_remove_specific_menu_item(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = NULL;

  reset_test_state();
  g_menu_label = L"Test Menu Item";
  g_menu_enabled = true;

  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(gcmz_tray_add_menu_item(tray, test_callback, NULL, &err), &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(gcmz_tray_add_menu_item(tray, test_callback_2, NULL, &err), &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(gcmz_tray_add_menu_item(tray, test_callback, NULL, &err), &err)) {
    goto cleanup;
  }
  gcmz_tray_remove_menu_item(tray, test_callback);
  gcmz_tray_remove_menu_item(tray, test_callback_2);
  gcmz_tray_remove_menu_item(tray, test_callback);

cleanup:
  gcmz_tray_destroy(&tray);
}

static void test_tray_invalid_arguments(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = NULL;

  TEST_CASE("NULL tray");
  TEST_FAILED_WITH(gcmz_tray_add_menu_item(NULL, test_callback, NULL, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  TEST_CASE("NULL callback");
  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  TEST_FAILED_WITH(
      gcmz_tray_add_menu_item(tray, NULL, NULL, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);
  gcmz_tray_destroy(&tray);

  TEST_CASE("NULL tray for remove");
  gcmz_tray_remove_menu_item(NULL, test_callback);

  TEST_CASE("NULL callback for remove");
  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  gcmz_tray_remove_menu_item(tray, NULL);

cleanup:
  gcmz_tray_destroy(&tray);
}

static void test_tray_destroy_null(void) {
  gcmz_tray_destroy(NULL);
  struct gcmz_tray *tray = NULL;
  gcmz_tray_destroy(&tray);
}

static void test_tray_rapid_create_destroy(void) {
  struct ov_error err = {0};

  for (int i = 0; i < 5; i++) {
    struct gcmz_tray *tray = gcmz_tray_create(NULL, &err);
    if (!TEST_SUCCEEDED(tray != NULL, &err)) {
      return;
    }
    TEST_CHECK(FindWindowW(L"GCMZDropsTrayWindow", NULL) != NULL);
    gcmz_tray_destroy(&tray);
    TEST_CHECK(tray == NULL);
  }
}

static void test_tray_create_with_null_err(void) {
  struct gcmz_tray *tray = gcmz_tray_create(NULL, NULL);
  if (TEST_CHECK(tray != NULL)) {
    gcmz_tray_destroy(&tray);
    TEST_CHECK(tray == NULL);
  }
}

static void test_tray_add_menu_item_with_null_err(void) {
  struct ov_error err = {0};
  struct gcmz_tray *tray = NULL;

  reset_test_state();
  g_menu_label = L"Test Menu Item";
  g_menu_enabled = true;

  tray = gcmz_tray_create(NULL, &err);
  if (!TEST_SUCCEEDED(tray != NULL, &err)) {
    goto cleanup;
  }
  TEST_CHECK(gcmz_tray_add_menu_item(tray, test_callback, NULL, NULL));

cleanup:
  gcmz_tray_destroy(&tray);
}

TEST_LIST = {
    {"test_tray_create_destroy", test_tray_create_destroy},
    {"test_tray_window_exists", test_tray_window_exists},
    {"test_tray_add_menu_item", test_tray_add_menu_item},
    {"test_tray_add_multiple_menu_items", test_tray_add_multiple_menu_items},
    {"test_tray_remove_menu_item", test_tray_remove_menu_item},
    {"test_tray_remove_specific_menu_item", test_tray_remove_specific_menu_item},
    {"test_tray_invalid_arguments", test_tray_invalid_arguments},
    {"test_tray_destroy_null", test_tray_destroy_null},
    {"test_tray_rapid_create_destroy", test_tray_rapid_create_destroy},
    {"test_tray_create_with_null_err", test_tray_create_with_null_err},
    {"test_tray_add_menu_item_with_null_err", test_tray_add_menu_item_with_null_err},
    {NULL, NULL},
};
