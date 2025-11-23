#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovtest.h>

#include <ovarray.h>

#include "delayed_cleanup.h"
#include "file.h"
#include "temp.h"

static bool create_test_file(wchar_t const *const file_path) {
  HANDLE h = CreateFileW(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  CloseHandle(h);
  return true;
}

static bool file_exists(wchar_t const *const file_path) {
  DWORD const attrs = GetFileAttributesW(file_path);
  return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void test_delayed_cleanup_init_exit(void) {
  struct ov_error err = {0};
  if (TEST_CHECK(gcmz_delayed_cleanup_init(&err))) {
    gcmz_delayed_cleanup_exit();
  } else {
    OV_ERROR_DESTROY(&err);
    return;
  }

  if (TEST_CHECK(gcmz_delayed_cleanup_init(&err))) {
    gcmz_delayed_cleanup_exit();
  } else {
    OV_ERROR_DESTROY(&err);
  }
}

static void test_delayed_cleanup_schedule_file(void) {
  struct ov_error err = {0};
  wchar_t *test_file_path = NULL;

  if (!TEST_CHECK(gcmz_delayed_cleanup_init(&err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(gcmz_temp_create_directory(&err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(gcmz_temp_build_path(&test_file_path, L"test_delayed_cleanup.tmp", &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  TEST_CHECK(create_test_file(test_file_path));
  TEST_CHECK(file_exists(test_file_path));
  TEST_CHECK(gcmz_delayed_cleanup_schedule_file(test_file_path, &err));
  TEST_CHECK(file_exists(test_file_path));
  gcmz_delayed_cleanup_exit();
  TEST_CHECK(!file_exists(test_file_path));

cleanup:
  if (test_file_path) {
    OV_ARRAY_DESTROY(&test_file_path);
  }
  gcmz_temp_remove_directory();
}

static void test_delayed_cleanup_schedule_nonexistent_file(void) {
  struct ov_error err = {0};
  TEST_CHECK(gcmz_delayed_cleanup_init(&err));
  TEST_CHECK(gcmz_delayed_cleanup_schedule_file(L"C:\\nonexistent\\file.tmp", &err));
  gcmz_delayed_cleanup_exit();
}

static void test_delayed_cleanup_schedule_temporary_files(void) {
  struct ov_error err = {0};
  struct gcmz_file_list *files = NULL;
  wchar_t *test_file1_path = NULL;
  wchar_t *test_file2_path = NULL;
  wchar_t *test_file3_path = NULL;
  struct gcmz_file const *file1 = NULL;
  struct gcmz_file const *file2 = NULL;
  struct gcmz_file const *file3 = NULL;
  struct gcmz_file *mutable_file1 = NULL;
  struct gcmz_file *mutable_file2 = NULL;
  struct gcmz_file *mutable_file3 = NULL;

  if (!TEST_CHECK(gcmz_delayed_cleanup_init(&err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(gcmz_temp_create_directory(&err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  files = gcmz_file_list_create(&err);
  if (!TEST_CHECK(files != NULL)) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  if (!TEST_CHECK(gcmz_temp_build_path(&test_file1_path, L"test_temp1.tmp", &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_temp_build_path(&test_file2_path, L"test_temp2.tmp", &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_temp_build_path(&test_file3_path, L"test_regular.tmp", &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  TEST_CHECK(create_test_file(test_file1_path));
  TEST_CHECK(create_test_file(test_file2_path));
  TEST_CHECK(create_test_file(test_file3_path));
  TEST_CHECK(gcmz_file_list_add_temporary(files, test_file1_path, L"application/octet-stream", &err));
  TEST_CHECK(gcmz_file_list_add_temporary(files, test_file2_path, L"application/octet-stream", &err));
  TEST_CHECK(gcmz_file_list_add(files, test_file3_path, L"application/octet-stream", &err));
  TEST_CHECK(gcmz_file_list_count(files) == 3);
  file1 = gcmz_file_list_get(files, 0);
  file2 = gcmz_file_list_get(files, 1);
  file3 = gcmz_file_list_get(files, 2);
  TEST_CHECK(file1 && file1->temporary);
  TEST_CHECK(file2 && file2->temporary);
  TEST_CHECK(file3 && !file3->temporary);
  TEST_CHECK(gcmz_delayed_cleanup_schedule_temporary_files(files, &err));
  mutable_file1 = gcmz_file_list_get_mutable(files, 0);
  mutable_file2 = gcmz_file_list_get_mutable(files, 1);
  mutable_file3 = gcmz_file_list_get_mutable(files, 2);
  TEST_CHECK(mutable_file1 && !mutable_file1->temporary);
  TEST_CHECK(mutable_file2 && !mutable_file2->temporary);
  TEST_CHECK(mutable_file3 && !mutable_file3->temporary);
  TEST_CHECK(file_exists(test_file1_path));
  TEST_CHECK(file_exists(test_file2_path));
  TEST_CHECK(file_exists(test_file3_path));
  gcmz_delayed_cleanup_exit();
  TEST_CHECK(!file_exists(test_file1_path));
  TEST_CHECK(!file_exists(test_file2_path));
  TEST_CHECK(file_exists(test_file3_path));
  DeleteFileW(test_file3_path);

cleanup:
  if (test_file3_path) {
    OV_ARRAY_DESTROY(&test_file3_path);
  }
  if (test_file2_path) {
    OV_ARRAY_DESTROY(&test_file2_path);
  }
  if (test_file1_path) {
    OV_ARRAY_DESTROY(&test_file1_path);
  }
  if (files) {
    gcmz_file_list_destroy(&files);
  }
  gcmz_temp_remove_directory();
}

static void test_delayed_cleanup_invalid_arguments(void) {
  struct ov_error err = {0};
  if (TEST_CHECK(!gcmz_delayed_cleanup_schedule_file(NULL, &err))) {
    OV_ERROR_DESTROY(&err);
  }
  if (TEST_CHECK(!gcmz_delayed_cleanup_schedule_temporary_files(NULL, &err))) {
    OV_ERROR_DESTROY(&err);
  }
  if (TEST_CHECK(!gcmz_delayed_cleanup_schedule_file(L"some_file.tmp", &err))) {
    OV_ERROR_DESTROY(&err);
  }
}

TEST_LIST = {
    {"test_delayed_cleanup_init_exit", test_delayed_cleanup_init_exit},
    {"test_delayed_cleanup_schedule_file", test_delayed_cleanup_schedule_file},
    {"test_delayed_cleanup_schedule_nonexistent_file", test_delayed_cleanup_schedule_nonexistent_file},
    {"test_delayed_cleanup_schedule_temporary_files", test_delayed_cleanup_schedule_temporary_files},
    {"test_delayed_cleanup_invalid_arguments", test_delayed_cleanup_invalid_arguments},
    {NULL, NULL},
};
