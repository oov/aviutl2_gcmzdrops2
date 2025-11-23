#include <ovtest.h>

#include "config.h"
#include "config_dialog.h"

static void test_config_dialog_show_with_null_config(void) {
  struct ov_error err = {0};
  bool result = gcmz_config_dialog_show(NULL, NULL, false, &err);
  if (TEST_CHECK(!result)) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }
}

static void test_config_dialog_show_with_valid_error(void) {
  struct ov_error err = {0};
  struct gcmz_config *config = gcmz_config_create(NULL, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  bool result = gcmz_config_dialog_show(config, NULL, false, &err);
  if (TEST_CHECK(!result)) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_hresult, HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND)));
    OV_ERROR_DESTROY(&err);
  }

  gcmz_config_destroy(&config);
}

TEST_LIST = {
    {"config_dialog_show_with_null_config", test_config_dialog_show_with_null_config},
    {"config_dialog_show_with_valid_error", test_config_dialog_show_with_valid_error},
    {NULL, NULL},
};
