#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovtest.h>

#include <shlobj.h>

#include <ovarray.h>
#include <ovl/file.h>

#include "copy.c"

struct test_save_path_context {
  wchar_t const *base_dir;
};

static wchar_t *mock_get_save_path(wchar_t const *filename, void *userdata, struct ov_error *err) {
  if (!filename || !userdata) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  struct test_save_path_context *ctx = (struct test_save_path_context *)userdata;
  size_t const dir_len = wcslen(ctx->base_dir);
  size_t const filename_len = wcslen(filename);
  size_t const total_len = dir_len + 1 + filename_len + 1;

  wchar_t *result = NULL;
  if (!OV_ARRAY_GROW(&result, total_len)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    return NULL;
  }

  ov_snprintf_wchar(result, total_len, NULL, L"%ls\\%ls", ctx->base_dir, filename);
  return result;
}

static NATIVE_CHAR *create_test_file(NATIVE_CHAR const *filename, char const *data, struct ov_error *const err) {
  if (!filename || !data) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  struct ovl_file *f = NULL;
  NATIVE_CHAR *path = NULL;
  size_t const data_len = strlen(data);
  NATIVE_CHAR *result = NULL;

  if (!ovl_file_create_temp(filename, &f, &path, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (data && data_len > 0) {
    size_t written = 0;
    if (!ovl_file_write(f, data, data_len, &written, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (written != data_len) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to write all data to temp file");
      goto cleanup;
    }
  }

  result = path;
  path = NULL;

cleanup:
  if (f) {
    ovl_file_close(f);
    f = NULL;
  }
  if (path) {
    DeleteFileW(path);
    OV_ARRAY_DESTROY(&path);
  }
  return result;
}

static void test_hash_filename_generation(void) {
  wchar_t *temp_path = NULL;

  struct ov_error err = {0};
  wchar_t *hash_filename1 = NULL;
  wchar_t *hash_filename2 = NULL;
  static uint64_t const test_hash = 0x123456789abcdef0ULL;

  temp_path = create_test_file(L"gcmz_test.txt", "Hello, World!", &err);
  if (!TEST_SUCCEEDED(temp_path != NULL, &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(generate_hash_filename_from_hash(temp_path, test_hash, &hash_filename1, &err), &err)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(generate_hash_filename_from_hash(temp_path, test_hash, &hash_filename2, &err), &err)) {
    goto cleanup;
  }
  TEST_CHECK(wcscmp(hash_filename1, hash_filename2) == 0);
  TEST_CHECK(wcsstr(hash_filename1, L".") != NULL);

cleanup:
  OV_ARRAY_DESTROY(&hash_filename1);
  OV_ARRAY_DESTROY(&hash_filename2);
  if (temp_path) {
    DeleteFileW(temp_path);
    OV_ARRAY_DESTROY(&temp_path);
  }
}

static bool check_is_copy_needed(wchar_t const *path, enum gcmz_processing_mode mode, ov_tribool expected) {
  struct ov_error err = {0};
  ov_tribool result = is_copy_needed(path, mode, &err);
  if (result == ov_indeterminate) {
    OV_ERROR_DESTROY(&err);
    return false;
  }
  return result == expected;
}

static void test_copy_needs_determination(void) {
  wchar_t temp_path[MAX_PATH];
  wchar_t program_files_path[MAX_PATH];
  static wchar_t const *const users_bin = L"C:\\Users\\test.bin";
  static wchar_t const *const users_txt = L"C:\\Users\\test.txt";
  static wchar_t const *const temp_object = L"C:\\temp\\test.object";

  GetTempPathW(MAX_PATH, temp_path);
  wcscat(temp_path, L"test.bin");

  SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, program_files_path);
  wcscat(program_files_path, L"\\test.bin");

  TEST_CHECK(check_is_copy_needed(temp_path, gcmz_processing_mode_auto, ov_true));
  TEST_CHECK(check_is_copy_needed(program_files_path, gcmz_processing_mode_auto, ov_true));
  TEST_CHECK(check_is_copy_needed(users_bin, gcmz_processing_mode_auto, ov_false));
  TEST_CHECK(check_is_copy_needed(temp_path, gcmz_processing_mode_direct, ov_true));
  TEST_CHECK(check_is_copy_needed(program_files_path, gcmz_processing_mode_direct, ov_false));
  TEST_CHECK(check_is_copy_needed(users_bin, gcmz_processing_mode_copy, ov_true));
  TEST_CHECK(check_is_copy_needed(users_txt, gcmz_processing_mode_copy, ov_false));
  TEST_CHECK(check_is_copy_needed(temp_object, gcmz_processing_mode_auto, ov_false));
}

static void test_file_management_with_callback(void) {
  wchar_t temp_dir[MAX_PATH];
  wchar_t *source_file = NULL;
  wchar_t *final_file = NULL;
  struct ov_error err = {0};

  GetTempPathW(MAX_PATH, temp_dir);
  wcscat(temp_dir, L"gcmz_test_dir");
  CreateDirectoryW(temp_dir, NULL);

  source_file = create_test_file(L"gcmz_source_test.bin", "Test content for file management", &err);
  if (!TEST_SUCCEEDED(source_file != NULL, &err)) {
    goto cleanup;
  }

  {
    struct test_save_path_context ctx = {.base_dir = temp_dir};
    if (!TEST_SUCCEEDED(gcmz_copy(source_file, gcmz_processing_mode_copy, mock_get_save_path, &ctx, &final_file, &err),
                        &err)) {
      goto cleanup;
    }
  }

  if (TEST_CHECK(final_file != NULL)) {
    TEST_CHECK(wcsstr(final_file, temp_dir) != NULL);
    TEST_CHECK(GetFileAttributesW(final_file) != INVALID_FILE_ATTRIBUTES);
  }

cleanup:
  if (final_file) {
    DeleteFileW(final_file);
    OV_ARRAY_DESTROY(&final_file);
  }
  if (source_file) {
    DeleteFileW(source_file);
    OV_ARRAY_DESTROY(&source_file);
  }
  RemoveDirectoryW(temp_dir);
}

TEST_LIST = {
    {"hash_filename_generation", test_hash_filename_generation},
    {"copy_needs_determination", test_copy_needs_determination},
    {"file_management_with_callback", test_file_management_with_callback},
    {NULL, NULL},
};
