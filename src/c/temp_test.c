#include "temp.h"

#include <ovarray.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <stdio.h>

static void putc_stderr(int c, void *ctx) {
  (void)ctx;
  fputc(c, stderr);
}

static void test_init(void) {
  struct ov_error err = {0};
  if (!gcmz_temp_create_directory(&err)) {
    char *msg = NULL;
    ov_error_to_string(&err, &msg, true, NULL);
    ov_pprintf_char(putc_stderr, NULL, NULL, "Failed to create temporary directory: %s\n", msg);
    OV_ARRAY_DESTROY(&msg);
    OV_ERROR_DESTROY(&err);
  }
}

static void test_cleanup(void) { gcmz_temp_remove_directory(); }

#define TEST_MY_INIT test_init()
#define TEST_MY_FINI test_cleanup()

#include <ovtest.h>

#include <ovarray.h>
#include <stdio.h>

static void test_gcmz_temp_create_unique_file_success(void) {
  wchar_t *temp_file = NULL;
  struct ov_error err = {0};
  if (!TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"temp.txt", &temp_file, &err), &err)) {
    return;
  }

  // Verify file path was returned
  if (TEST_CHECK(temp_file != NULL)) {
    // Verify file extension is correct
    TEST_CHECK(wcsstr(temp_file, L".txt") != NULL);

    // Verify filename contains base name with underscore
    TEST_CHECK(wcsstr(temp_file, L"temp_") != NULL);

    // Verify file exists
    HANDLE hFile = CreateFileW(temp_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile != INVALID_HANDLE_VALUE);
    CloseHandle(hFile);
  }

  // Clean up
  DeleteFileW(temp_file);
  OV_ARRAY_DESTROY(&temp_file);
}

static void test_gcmz_temp_create_unique_file_edge_cases(void) {
  wchar_t *file1 = NULL;
  wchar_t *file2 = NULL;
  wchar_t *file3 = NULL;
  wchar_t *file4 = NULL;
  struct ov_error err = {0};

  // Test very long extension
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"test.verylongextension", &file1, &err), &err)) {
    if (TEST_CHECK(file1 != NULL)) {
      TEST_CHECK(wcsstr(file1, L".verylongextension") != NULL);
      TEST_CHECK(wcsstr(file1, L"test_") != NULL);
    }
  }

  // Test no extension
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"noextension", &file2, &err), &err)) {
    if (TEST_CHECK(file2 != NULL)) {
      TEST_CHECK(wcsstr(file2, L"noextension_") != NULL);
    }
  }

  // Test very long filename
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"verylongfilenamethatexceedstypiclimits.txt", &file3, &err), &err)) {
    if (TEST_CHECK(file3 != NULL)) {
      TEST_CHECK(wcsstr(file3, L".txt") != NULL);
      TEST_CHECK(wcsstr(file3, L"verylongfilenamethatexceedstypiclimits_") != NULL);
    }
  }

  // Test single character filename
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"a.b", &file4, &err), &err)) {
    if (TEST_CHECK(file4 != NULL)) {
      TEST_CHECK(wcsstr(file4, L".b") != NULL);
      TEST_CHECK(wcsstr(file4, L"a_") != NULL);
    }
  }

  // Verify all files exist and have different names (if created successfully)
  if (file1) {
    HANDLE hFile1 = CreateFileW(file1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile1 != INVALID_HANDLE_VALUE);
    CloseHandle(hFile1);
  }
  if (file2) {
    HANDLE hFile2 = CreateFileW(file2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile2 != INVALID_HANDLE_VALUE);
    CloseHandle(hFile2);
  }
  if (file3) {
    HANDLE hFile3 = CreateFileW(file3, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile3 != INVALID_HANDLE_VALUE);
    CloseHandle(hFile3);
  }
  if (file4) {
    HANDLE hFile4 = CreateFileW(file4, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile4 != INVALID_HANDLE_VALUE);
    CloseHandle(hFile4);
  }

  // Verify files have different names (if they exist)
  if (file1 && file2) {
    TEST_CHECK(wcscmp(file1, file2) != 0);
  }
  if (file1 && file3) {
    TEST_CHECK(wcscmp(file1, file3) != 0);
  }
  if (file1 && file4) {
    TEST_CHECK(wcscmp(file1, file4) != 0);
  }
  if (file2 && file3) {
    TEST_CHECK(wcscmp(file2, file3) != 0);
  }
  if (file2 && file4) {
    TEST_CHECK(wcscmp(file2, file4) != 0);
  }
  if (file3 && file4) {
    TEST_CHECK(wcscmp(file3, file4) != 0);
  }

  // Clean up
  if (file1) {
    DeleteFileW(file1);
    OV_ARRAY_DESTROY(&file1);
  }
  if (file2) {
    DeleteFileW(file2);
    OV_ARRAY_DESTROY(&file2);
  }
  if (file3) {
    DeleteFileW(file3);
    OV_ARRAY_DESTROY(&file3);
  }
  if (file4) {
    DeleteFileW(file4);
    OV_ARRAY_DESTROY(&file4);
  }
}

static void test_gcmz_temp_create_unique_file_uniqueness(void) {
  // Create multiple files with same filename and verify they're all unique
  wchar_t *files[10] = {0};
  struct ov_error err = {0};

  for (int i = 0; i < 10; ++i) {
    if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"test.tmp", &files[i], &err), &err)) {
      if (TEST_CHECK(files[i] != NULL)) {
        // Verify file exists
        HANDLE hFile = CreateFileW(files[i], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        TEST_CHECK(hFile != INVALID_HANDLE_VALUE);
        CloseHandle(hFile);
      }
    }
  }

  // Verify all files have unique names
  for (int i = 0; i < 10; ++i) {
    if (!files[i]) {
      continue;
    }
    for (int j = i + 1; j < 10; ++j) {
      if (!files[j]) {
        continue;
      }
      TEST_CHECK(wcscmp(files[i], files[j]) != 0);
    }
  }

  // Clean up
  for (int i = 0; i < 10; ++i) {
    if (files[i]) {
      DeleteFileW(files[i]);
      OV_ARRAY_DESTROY(&files[i]);
    }
  }
}

static void test_gcmz_temp_create_unique_file_error_handling(void) {
  // Test with NULL dest_path
  struct ov_error err = {0};
  TEST_FAILED_WITH(gcmz_temp_create_unique_file(L"test.txt", NULL, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
}

static void test_gcmz_temp_create_unique_file_default_filename(void) {
  wchar_t *temp_file1 = NULL;
  wchar_t *temp_file2 = NULL;
  struct ov_error err = {0};

  // Test with NULL filename (should use "tmp.bin")
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(NULL, &temp_file1, &err), &err)) {
    if (TEST_CHECK(temp_file1 != NULL)) {
      TEST_CHECK(wcsstr(temp_file1, L"tmp_") != NULL);
      TEST_CHECK(wcsstr(temp_file1, L".bin") != NULL);
    }
  }

  // Test with empty string filename (should use "tmp.bin")
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L"", &temp_file2, &err), &err)) {
    if (TEST_CHECK(temp_file2 != NULL)) {
      TEST_CHECK(wcsstr(temp_file2, L"tmp_") != NULL);
      TEST_CHECK(wcsstr(temp_file2, L".bin") != NULL);
    }
  }

  // Verify files exist
  if (temp_file1) {
    HANDLE hFile1 = CreateFileW(temp_file1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile1 != INVALID_HANDLE_VALUE);
    CloseHandle(hFile1);
  }
  if (temp_file2) {
    HANDLE hFile2 = CreateFileW(temp_file2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile2 != INVALID_HANDLE_VALUE);
    CloseHandle(hFile2);
  }

  // Verify files have different names
  if (temp_file1 && temp_file2) {
    TEST_CHECK(wcscmp(temp_file1, temp_file2) != 0);
  }

  // Clean up
  if (temp_file1) {
    DeleteFileW(temp_file1);
    OV_ARRAY_DESTROY(&temp_file1);
  }
  if (temp_file2) {
    DeleteFileW(temp_file2);
    OV_ARRAY_DESTROY(&temp_file2);
  }
}

static void test_gcmz_temp_create_unique_file_dot_filenames(void) {
  wchar_t *temp_file = NULL;
  struct ov_error err = {0};

  // Test with dot at start (hidden file style)
  if (TEST_SUCCEEDED(gcmz_temp_create_unique_file(L".gitignore", &temp_file, &err), &err)) {
    if (TEST_CHECK(temp_file != NULL)) {
      // Should use ".gitignore" as basename since dot-prefixed files are treated as basenames
      TEST_CHECK(wcsstr(temp_file, L".gitignore_") != NULL);
    }
  }

  // Verify file exists
  if (temp_file) {
    HANDLE hFile = CreateFileW(temp_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_CHECK(hFile != INVALID_HANDLE_VALUE);
    CloseHandle(hFile);
  }

  // Clean up
  if (temp_file) {
    DeleteFileW(temp_file);
    OV_ARRAY_DESTROY(&temp_file);
  }
}

TEST_LIST = {
    {"test_gcmz_temp_create_unique_file_success", test_gcmz_temp_create_unique_file_success},
    {"test_gcmz_temp_create_unique_file_edge_cases", test_gcmz_temp_create_unique_file_edge_cases},
    {"test_gcmz_temp_create_unique_file_uniqueness", test_gcmz_temp_create_unique_file_uniqueness},
    {"test_gcmz_temp_create_unique_file_error_handling", test_gcmz_temp_create_unique_file_error_handling},
    {"test_gcmz_temp_create_unique_file_default_filename", test_gcmz_temp_create_unique_file_default_filename},
    {"test_gcmz_temp_create_unique_file_dot_filenames", test_gcmz_temp_create_unique_file_dot_filenames},
    {NULL, NULL},
};
