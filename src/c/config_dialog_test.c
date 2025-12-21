#include <ovtest.h>

#include "config.h"
#include "config_dialog.h"

static void test_config_dialog_show_with_null_config(void) {
  struct ov_error err = {0};
  TEST_FAILED_WITH(gcmz_config_dialog_show(NULL, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);
}

static void test_config_dialog_show_with_valid_error(void) {
  struct ov_error err = {0};
  struct gcmz_config *config = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  TEST_FAILED_WITH(gcmz_config_dialog_show(&(struct gcmz_config_dialog_options){.config = config}, &err),
                   &err,
                   ov_error_type_hresult,
                   HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND));

  gcmz_config_destroy(&config);
}

TEST_LIST = {
    {"config_dialog_show_with_null_config", test_config_dialog_show_with_null_config},
    {"config_dialog_show_with_valid_error", test_config_dialog_show_with_valid_error},
    {NULL, NULL},
};
