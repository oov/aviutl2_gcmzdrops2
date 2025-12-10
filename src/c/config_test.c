#include "config.h"

#include <ovarray.h>
#include <ovprintf.h>

#include "config.c"

#ifdef _WIN32
#  define STRLEN wcslen
#  define STRCMP wcscmp
#  define STRNCMP wcsncmp
#else
#  define STRLEN strlen
#  define STRCMP strcmp
#  define STRNCMP strncmp
#endif

static wchar_t const *g_test_project_path = NULL;
static wchar_t *mock_get_project_path(void *userdata) {
  (void)userdata;
  if (!g_test_project_path) {
    return NULL;
  }
  size_t const len = wcslen(g_test_project_path);
  wchar_t *result = NULL;
  if (!OV_ARRAY_GROW(&result, len + 1)) {
    return NULL;
  }
  wcscpy(result, g_test_project_path);
  return result;
}

static void test_init(void) {
  wchar_t json_path[MAX_PATH * 2];
  GetModuleFileNameW(NULL, json_path, MAX_PATH);
  wchar_t *last_slash = wcsrchr(json_path, L'\\');
  if (last_slash) {
    *last_slash = L'\0';
  }
  wcscat(json_path, L"\\gcmz.json");
  DeleteFileW(json_path);
}

static void test_cleanup(void) {
  test_init();
  g_test_project_path = NULL;
}

#define TEST_MY_INIT test_init()
#define TEST_MY_FINI test_cleanup()

#include <ovtest.h>

static void test_config_create_destroy(void) {
  struct gcmz_config *config = NULL;
  struct ov_error err = {0};
  config = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }
  gcmz_config_destroy(&config);
  TEST_CHECK(config == NULL);
}

static void test_config_default_values(void) {
  struct gcmz_config *config = NULL;
  enum gcmz_processing_mode mode;
  struct ov_error err = {0};

  config = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_get_processing_mode(config, &mode, &err), &err)) {
    goto cleanup;
  }
  TEST_CHECK(mode == gcmz_processing_mode_auto);

cleanup:
  gcmz_config_destroy(&config);
}

static void test_config_processing_mode_getset(void) {
  struct gcmz_config *config = NULL;
  enum gcmz_processing_mode mode;
  struct ov_error err = {0};

  config = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_processing_mode(config, gcmz_processing_mode_direct, &err), &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_get_processing_mode(config, &mode, &err), &err)) {
    goto cleanup;
  }
  TEST_CHECK(mode == gcmz_processing_mode_direct);

  if (!TEST_SUCCEEDED(gcmz_config_set_processing_mode(config, gcmz_processing_mode_copy, &err), &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_get_processing_mode(config, &mode, &err), &err)) {
    goto cleanup;
  }
  TEST_CHECK(mode == gcmz_processing_mode_copy);

cleanup:
  gcmz_config_destroy(&config);
}

static void test_config_save_load(void) {
  struct gcmz_config *config1 = NULL;
  struct gcmz_config *config2 = NULL;
  enum gcmz_processing_mode mode;
  struct ov_error err = {0};

  config1 = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config1 != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_processing_mode(config1, gcmz_processing_mode_direct, &err), &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_save(config1, &err), &err)) {
    goto cleanup;
  }

  config2 = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config2 != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_load(config2, &err), &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_get_processing_mode(config2, &mode, &err), &err)) {
    goto cleanup;
  }
  TEST_CHECK(mode == gcmz_processing_mode_direct);

cleanup:
  gcmz_config_destroy(&config1);
  gcmz_config_destroy(&config2);
}

static void test_config_get_save_path_with_save_paths(void) {
  struct gcmz_config *config = NULL;
  wchar_t *save_path = NULL;
  struct ov_error err = {0};
  wchar_t temp_dir[MAX_PATH];
  wchar_t test_path[MAX_PATH];
  wchar_t const *test_paths[1];
  wchar_t expected_path[MAX_PATH];

  config = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_allow_create_directories(config, true, &err), &err)) {
    goto cleanup;
  }

  GetTempPathW(MAX_PATH, temp_dir);
  ov_snprintf_wchar(test_path, MAX_PATH, NULL, L"%lsTestSavePaths", temp_dir);

  test_paths[0] = test_path;
  if (!TEST_SUCCEEDED(gcmz_config_set_save_paths(config, test_paths, 1, &err), &err)) {
    goto cleanup;
  }

  save_path = gcmz_config_get_save_path(config, L"test.png", &err);
  if (!TEST_SUCCEEDED(save_path != NULL, &err)) {
    goto cleanup;
  }

  ov_snprintf_wchar(expected_path, MAX_PATH, NULL, L"%ls\\test.png", test_path);

  TEST_CHECK(STRCMP(save_path, expected_path) == 0);
  TEST_MSG("Expected: %ls", expected_path);
  TEST_MSG("Actual  : %ls", save_path);

cleanup:
  OV_ARRAY_DESTROY(&save_path);
  gcmz_config_destroy(&config);
}

static void test_config_get_save_path_nonexistent_dir_no_create(void) {
  struct gcmz_config *config = NULL;
  wchar_t *save_path = NULL;
  struct ov_error err = {0};
  wchar_t const *test_paths[] = {L"C:\\NonExistentTestPath"};

  config = gcmz_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_allow_create_directories(config, false, &err), &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_save_paths(config, test_paths, 1, &err), &err)) {
    goto cleanup;
  }

  save_path = gcmz_config_get_save_path(config, L"test.png", &err);
  if (!TEST_SUCCEEDED(save_path != NULL, &err)) {
    goto cleanup;
  }
  TEST_CHECK(wcsstr(save_path, L"GCMZShared\\") != NULL);
  TEST_CHECK(wcsstr(save_path, L"\\test.png") != NULL);
  TEST_MSG("Should fallback to shared folder when directory doesn't exist and create_directories is false");
  TEST_MSG("Actual path: %ls", save_path);

cleanup:
  if (save_path) {
    OV_ARRAY_DESTROY(&save_path);
  }
  gcmz_config_destroy(&config);
}

static void test_config_get_save_path_project_based(void) {
  struct gcmz_config *config = NULL;
  wchar_t *save_path = NULL;
  struct ov_error err = {0};
  wchar_t temp_dir[MAX_PATH];
  wchar_t mock_project_path[MAX_PATH];
  wchar_t const *test_paths[] = {L"%PROJECTDIR%"};
  wchar_t expected_path[MAX_PATH];

  GetTempPathW(MAX_PATH, temp_dir);
  ov_snprintf_wchar(mock_project_path, MAX_PATH, NULL, L"%lsProjects\\MyProject.aup", temp_dir);
  g_test_project_path = mock_project_path;

  config = gcmz_config_create(
      &(struct gcmz_config_options){
          .project_path_provider = mock_get_project_path,
          .project_path_provider_userdata = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_allow_create_directories(config, true, &err), &err)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_config_set_save_paths(config, test_paths, 1, &err), &err)) {
    goto cleanup;
  }

  save_path = gcmz_config_get_save_path(config, L"test.png", &err);
  if (!TEST_SUCCEEDED(save_path != NULL, &err)) {
    goto cleanup;
  }

  ov_snprintf_wchar(expected_path, MAX_PATH, NULL, L"%lsProjects\\test.png", temp_dir);

  TEST_CHECK(STRCMP(save_path, expected_path) == 0);
  TEST_MSG("Expected: %ls", expected_path);
  TEST_MSG("Actual  : %ls", save_path);

cleanup:
  OV_ARRAY_DESTROY(&save_path);
  gcmz_config_destroy(&config);
  g_test_project_path = NULL;
}

static void test_config_get_save_path_fallback_to_shared(void) {
  struct gcmz_config *config = NULL;
  wchar_t *save_path = NULL;
  struct ov_error err = {0};

  g_test_project_path = NULL;

  config = gcmz_config_create(
      &(struct gcmz_config_options){
          .project_path_provider = mock_get_project_path,
          .project_path_provider_userdata = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    goto cleanup;
  }

  // Default save_paths contains %PROJECTDIR%, but project_path_provider returns NULL
  // so it should fallback to shared folder
  save_path = gcmz_config_get_save_path(config, L"test.png", &err);
  if (!TEST_SUCCEEDED(save_path != NULL, &err)) {
    goto cleanup;
  }
  TEST_CHECK(wcsstr(save_path, L"GCMZShared\\") != NULL);
  TEST_CHECK(wcsstr(save_path, L"\\test.png") != NULL);

cleanup:
  if (save_path) {
    OV_ARRAY_DESTROY(&save_path);
  }
  gcmz_config_destroy(&config);
}

struct test_callback_data {
  wchar_t const *const *var_names;
  wchar_t const *const *var_values;
  size_t var_count;
};

static size_t test_expand_vars_callback(NATIVE_CHAR const *const var_name,
                                        size_t const var_name_len,
                                        NATIVE_CHAR *const replacement_buf,
                                        void *const userdata) {
  struct test_callback_data const *const data = (struct test_callback_data const *)userdata;

  for (size_t i = 0; i < data->var_count; ++i) {
    size_t const name_len = STRLEN(data->var_names[i]);
    if (var_name_len == name_len && STRNCMP(var_name, data->var_names[i], var_name_len) == 0) {
      size_t const value_len = STRLEN(data->var_values[i]);
      if (value_len >= 511) {
        return SIZE_MAX; // Too long for buffer
      }
      memcpy(replacement_buf, data->var_values[i], value_len * sizeof(NATIVE_CHAR));
      replacement_buf[value_len] = NSTR('\0');
      return value_len;
    }
  }
  return SIZE_MAX; // Variable not found
}

static void test_config_expand_vars_single_variable(void) {
  wchar_t *expanded = NULL;
  struct ov_error err = {0};

  wchar_t const *var_names[] = {L"PROJECTDIR"};
  wchar_t const *var_values[] = {L"C:\\Projects\\MyProject"};
  struct test_callback_data data = {var_names, var_values, 1};

  if (!TEST_SUCCEEDED(expand_vars(L"%PROJECTDIR%\\files\\data.txt", test_expand_vars_callback, &data, &expanded, &err),
                      &err)) {
  }
  TEST_ASSERT(expanded != NULL);
  TEST_CHECK(STRCMP(expanded, L"C:\\Projects\\MyProject\\files\\data.txt") == 0);

  OV_ARRAY_DESTROY(&expanded);
}

static void test_config_expand_vars_multiple_variables(void) {
  wchar_t *expanded = NULL;
  struct ov_error err = {0};

  wchar_t const *var_names[] = {L"PROJECTDIR", L"USERNAME"};
  wchar_t const *var_values[] = {L"C:\\Projects\\MyProject", L"user"};
  struct test_callback_data data = {var_names, var_values, 2};

  if (!TEST_SUCCEEDED(
          expand_vars(L"%PROJECTDIR%\\files\\%USERNAME%\\data.txt", test_expand_vars_callback, &data, &expanded, &err),
          &err)) {
  }
  TEST_ASSERT(expanded != NULL);
  TEST_CHECK(STRCMP(expanded, L"C:\\Projects\\MyProject\\files\\user\\data.txt") == 0);

  OV_ARRAY_DESTROY(&expanded);
}

static void test_config_expand_vars_multiple_occurrences(void) {
  wchar_t *expanded = NULL;
  struct ov_error err = {0};

  wchar_t const *var_names[] = {L"DIR"};
  wchar_t const *var_values[] = {L"test"};
  struct test_callback_data data = {var_names, var_values, 1};

  if (!TEST_SUCCEEDED(expand_vars(L"%DIR%\\%DIR%\\file.txt", test_expand_vars_callback, &data, &expanded, &err),
                      &err)) {
  }
  TEST_ASSERT(expanded != NULL);
  TEST_CHECK(STRCMP(expanded, L"test\\test\\file.txt") == 0);

  OV_ARRAY_DESTROY(&expanded);
}

static size_t empty_callback(NATIVE_CHAR const *const var_name,
                             size_t const var_name_len,
                             NATIVE_CHAR *const replacement_buf,
                             void *const userdata) {
  (void)var_name;
  (void)var_name_len;
  (void)replacement_buf;
  (void)userdata;
  return SIZE_MAX; // No variables supported
}

static void test_config_expand_vars_no_variables(void) {
  wchar_t *expanded = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(expand_vars(L"C:\\simple\\path\\file.txt", empty_callback, NULL, &expanded, &err), &err)) {
  }
  TEST_ASSERT(expanded != NULL);
  TEST_CHECK(STRCMP(expanded, L"C:\\simple\\path\\file.txt") == 0);

  OV_ARRAY_DESTROY(&expanded);
}

static void test_config_expand_vars_undefined_variables(void) {
  wchar_t *expanded = NULL;
  struct ov_error err = {0};

  wchar_t const *var_names[] = {L"KNOWN"};
  wchar_t const *var_values[] = {L"value"};
  struct test_callback_data data = {var_names, var_values, 1};

  if (!TEST_SUCCEEDED(expand_vars(L"%KNOWN%\\%UNKNOWN%\\file.txt", test_expand_vars_callback, &data, &expanded, &err),
                      &err)) {
  }
  TEST_ASSERT(expanded != NULL);
  TEST_CHECK(STRCMP(expanded, L"value\\%UNKNOWN%\\file.txt") == 0);

  OV_ARRAY_DESTROY(&expanded);
}

static void test_config_error_handling(void) {
  struct gcmz_config *config = NULL;
  enum gcmz_processing_mode mode;
  wchar_t *path = NULL;
  struct ov_error err = {0};

  config = gcmz_config_create(NULL, &err);
  TEST_ASSERT(config != NULL);

  TEST_FAILED_WITH(gcmz_config_get_processing_mode(NULL, &mode, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_config_get_processing_mode(config, NULL, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_config_set_processing_mode(NULL, gcmz_processing_mode_direct, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_config_get_save_path(NULL, L"test.png", &err) != NULL,
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_config_get_save_path(config, NULL, &err) != NULL,
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(
      expand_vars(NULL, NULL, NULL, &path, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(
      expand_vars(L"test", NULL, NULL, &path, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(expand_vars(L"test", empty_callback, NULL, NULL, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_config_load(NULL, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_config_save(NULL, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);
  gcmz_config_destroy(&config);
}

TEST_LIST = {
    {"config_create_destroy", test_config_create_destroy},
    {"config_default_values", test_config_default_values},
    {"config_processing_mode_getset", test_config_processing_mode_getset},
    {"config_save_load", test_config_save_load},
    {"config_get_save_path_with_save_paths", test_config_get_save_path_with_save_paths},
    {"config_get_save_path_nonexistent_dir_no_create", test_config_get_save_path_nonexistent_dir_no_create},
    {"config_get_save_path_project_based", test_config_get_save_path_project_based},
    {"config_get_save_path_fallback_to_shared", test_config_get_save_path_fallback_to_shared},
    {"config_expand_vars_single_variable", test_config_expand_vars_single_variable},
    {"config_expand_vars_multiple_variables", test_config_expand_vars_multiple_variables},
    {"config_expand_vars_multiple_occurrences", test_config_expand_vars_multiple_occurrences},
    {"config_expand_vars_no_variables", test_config_expand_vars_no_variables},
    {"config_expand_vars_undefined_variables", test_config_expand_vars_undefined_variables},
    {"config_error_handling", test_config_error_handling},
    {NULL, NULL},
};
