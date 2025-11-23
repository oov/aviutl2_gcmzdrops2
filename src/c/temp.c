#include "temp.h"

#include <ovarray.h>
#include <ovbase.h>
#include <ovl/file.h>
#include <ovl/os.h>
#include <ovl/path.h>
#include <ovmo.h>
#include <ovnum.h>
#include <ovprintf.h>
#include <ovrand.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE g_temp_dir_handle = INVALID_HANDLE_VALUE;
static wchar_t const g_folder_prefix[] = L"gcmzdrops";

static bool build_temp_directory_path(wchar_t **const dest, DWORD const process_id, struct ov_error *const err) {
  if (!dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *temp_dir = NULL;
  bool success = false;

  if (!ovl_path_get_temp_directory(&temp_dir, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    size_t temp_len = wcslen(temp_dir);
    if (!OV_ARRAY_GROW(dest, temp_len + 32)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    wcscpy(*dest, temp_dir);
    wcscat(*dest, g_folder_prefix);
    wchar_t buf[32];
    wcscat(*dest, ov_utoa_wchar(process_id, buf));
  }

  success = true;

cleanup:
  if (temp_dir) {
    OV_ARRAY_DESTROY(&temp_dir);
  }
  return success;
}

static bool temp_remove_directory_by_process_id(DWORD process_id, struct ov_error *const err) {
  wchar_t *temp_dir = NULL;
  wchar_t *search_pattern = NULL;
  wchar_t *file_path = NULL;
  HANDLE h = INVALID_HANDLE_VALUE;
  bool success = false;

  if (!build_temp_directory_path(&temp_dir, process_id, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!OV_ARRAY_GROW(&search_pattern, wcslen(temp_dir) + 4)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  wcscpy(search_pattern, temp_dir);
  wcscat(search_pattern, L"\\*");

  WIN32_FIND_DATAW find_data;
  h = FindFirstFileW(search_pattern, &find_data);
  if (h == INVALID_HANDLE_VALUE) {
    HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    if (hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
      // No directory found, nothing to remove - this is success
      success = true;
      goto cleanup;
    }
    OV_ERROR_SET_HRESULT(err, hr);
    goto cleanup;
  }

  do {
    if (wcscmp(find_data.cFileName, L".") != 0 && wcscmp(find_data.cFileName, L"..") != 0) {
      if (!OV_ARRAY_GROW(&file_path, wcslen(temp_dir) + wcslen(find_data.cFileName) + 2)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(file_path, temp_dir);
      wcscat(file_path, L"\\");
      wcscat(file_path, find_data.cFileName);
      if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        RemoveDirectoryW(file_path);
      } else {
        DeleteFileW(file_path);
      }
    }
  } while (FindNextFileW(h, &find_data));

  success = true;

cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    FindClose(h);
    h = INVALID_HANDLE_VALUE;
  }
  if (success) {
    RemoveDirectoryW(temp_dir);
  }
  if (file_path) {
    OV_ARRAY_DESTROY(&file_path);
  }
  if (search_pattern) {
    OV_ARRAY_DESTROY(&search_pattern);
  }
  if (temp_dir) {
    OV_ARRAY_DESTROY(&temp_dir);
  }
  return success;
}

bool gcmz_temp_cleanup_stale_directories(gcmz_temp_cleanup_callback_fn callback,
                                         void *userdata,
                                         struct ov_error *const err) {
  wchar_t *temp_dir = NULL;
  wchar_t *search_pattern = NULL;
  wchar_t *dir_path = NULL;
  HANDLE h = INVALID_HANDLE_VALUE;
  bool success = false;

  if (!ovl_path_get_temp_directory(&temp_dir, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    size_t const temp_len = wcslen(temp_dir);
    if (!OV_ARRAY_GROW(&search_pattern, temp_len + wcslen(g_folder_prefix) + 2)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(search_pattern, temp_dir);
    wcscat(search_pattern, g_folder_prefix);
    wcscat(search_pattern, L"*");
  }

  {
    WIN32_FIND_DATAW find_data = {0};
    h = FindFirstFileW(search_pattern, &find_data);
    if (h == INVALID_HANDLE_VALUE) {
      HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
      if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        success = true; // No matching directories found
        goto cleanup;
      }
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }
    DWORD const current_pid = GetCurrentProcessId();
    do {
      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        continue;
      }
      if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
        continue;
      }
      // Extract process ID from directory name (gcmzdrops{pid})
      uint64_t pid = 0;
      if (!ov_atou_wchar(find_data.cFileName + wcslen(g_folder_prefix), &pid, true)) {
        continue;
      }
      if ((DWORD)pid == current_pid) {
        continue;
      }
      if (!OV_ARRAY_GROW(&dir_path, wcslen(temp_dir) + wcslen(find_data.cFileName) + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(dir_path, temp_dir);
      wcscat(dir_path, find_data.cFileName);
      HANDLE hdir = CreateFileW(dir_path, DELETE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (hdir == INVALID_HANDLE_VALUE) {
        continue; // Directory is locked, skip it
      }
      CloseHandle(hdir);

      // Proceed with deletion
      struct ov_error temp_err = {0};
      if (temp_remove_directory_by_process_id((DWORD)pid, &temp_err)) {
        if (callback) {
          callback(dir_path, userdata);
        }
      } else {
        // Failed to remove - just ignore
        OV_ERROR_DESTROY(&temp_err);
      }
    } while (FindNextFileW(h, &find_data));
  }

  success = true;

cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    FindClose(h);
    h = INVALID_HANDLE_VALUE;
  }
  if (dir_path) {
    OV_ARRAY_DESTROY(&dir_path);
  }
  if (search_pattern) {
    OV_ARRAY_DESTROY(&search_pattern);
  }
  if (temp_dir) {
    OV_ARRAY_DESTROY(&temp_dir);
  }
  return success;
}

bool gcmz_temp_create_directory(struct ov_error *const err) {
  wchar_t *tmp = NULL;
  bool success = false;

  if (!build_temp_directory_path(&tmp, GetCurrentProcessId(), err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!CreateDirectoryW(tmp, NULL)) {
    DWORD const last_error = GetLastError();
    if (last_error != ERROR_ALREADY_EXISTS) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(last_error));
      goto cleanup;
    }
  }

  g_temp_dir_handle = CreateFileW(
      tmp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

  if (g_temp_dir_handle == INVALID_HANDLE_VALUE) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  success = true;

cleanup:
  OV_ARRAY_DESTROY(&tmp);
  return success;
}

void gcmz_temp_remove_directory(void) {
  if (g_temp_dir_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(g_temp_dir_handle);
    g_temp_dir_handle = INVALID_HANDLE_VALUE;
  }
  struct ov_error err = {0};
  if (!temp_remove_directory_by_process_id(GetCurrentProcessId(), &err)) {
    OV_ERROR_REPORT(&err, gettext("failed to remove temporary directory"));
  }
}

bool gcmz_temp_build_path(wchar_t **const dest, wchar_t const *const filename, struct ov_error *const err) {
  if (!dest || !filename) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *temp_dir = NULL;
  bool success = false;

  if (!build_temp_directory_path(&temp_dir, GetCurrentProcessId(), err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!OV_ARRAY_GROW(dest, wcslen(temp_dir) + wcslen(filename) + 2)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  wcscpy(*dest, temp_dir);
  wcscat(*dest, L"\\");
  wcscat(*dest, filename);

  success = true;

cleanup:
  OV_ARRAY_DESTROY(&temp_dir);
  return success;
}

static size_t extract_file_name(wchar_t const *path) {
  if (!path) {
    return 0;
  }

  wchar_t const *bslash = wcsrchr(path, L'\\');
  wchar_t const *slash = wcsrchr(path, L'/');
  wchar_t const *separator = NULL;

  if (bslash == NULL && slash == NULL) {
    return 0;
  } else if (bslash != NULL && slash != NULL) {
    separator = bslash > slash ? bslash : slash;
  } else {
    separator = bslash != NULL ? bslash : slash;
  }

  return (size_t)(separator + 1 - path);
}

static size_t extract_file_extension(wchar_t const *filename) {
  if (!filename) {
    return 0;
  }

  size_t len = wcslen(filename);
  size_t filename_start = extract_file_name(filename);

  // Look for extension in filename part only
  for (size_t i = len; i > filename_start; --i) {
    if (filename[i - 1] == L'.') {
      // Don't treat dot at start of filename as extension (e.g., ".gitignore")
      if (i - 1 > filename_start) {
        return i - 1;
      }
    }
  }

  return len; // No extension found
}

static void format_hex_string_wchar(wchar_t *dest, uint64_t value) {
  for (int i = 0; i < 16; ++i) {
    wchar_t hex_char = (value >> (60 - i * 4)) & 0xF;
    dest[i] = hex_char < 10 ? L'0' + hex_char : L'a' + hex_char - 10;
  }
}

bool gcmz_temp_create_unique_file(wchar_t const *const filename,
                                  wchar_t **const dest_path,
                                  struct ov_error *const err) {
  if (!dest_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  // Use default filename if filename is NULL or empty
  wchar_t const *actual_filename = filename;
  if (!filename || filename[0] == L'\0') {
    actual_filename = L"tmp.bin";
  }

  wchar_t *temp_dir = NULL;
  HANDLE h = INVALID_HANDLE_VALUE;
  bool success = false;

  if (!build_temp_directory_path(&temp_dir, GetCurrentProcessId(), err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    size_t const temp_dir_len = wcslen(temp_dir);
    size_t const actual_filename_len = wcslen(actual_filename);
    size_t const ext_pos = extract_file_extension(actual_filename);
    size_t const ext_len = actual_filename_len - ext_pos;

    wchar_t const *basename = actual_filename;
    size_t basename_len = ext_pos;
    if (actual_filename_len > 0 && basename_len == 0) {
      basename = L"tmp";
      basename_len = 3;
    }

    // Build path: temp_dir + "\\" + basename + "_" + 16-hex + extension
    size_t const path_len = temp_dir_len + 1 + basename_len + 1 + 16 + ext_len;
    if (!OV_ARRAY_GROW(&temp_dir, path_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    temp_dir[temp_dir_len] = L'\\';
    wcsncpy(temp_dir + temp_dir_len + 1, basename, basename_len);
    temp_dir[temp_dir_len + 1 + basename_len] = L'_';
    wcscpy(temp_dir + temp_dir_len + 1 + basename_len + 1 + 16, actual_filename + ext_pos);

    wchar_t *digits = temp_dir + temp_dir_len + 1 + basename_len + 1;
    uint64_t rng_state = ov_rand_splitmix64(ov_rand_get_global_hint() + GetTickCount64());
    for (int attempt = 0; attempt < 5; ++attempt) {
      rng_state = ov_rand_splitmix64_next(rng_state);
      format_hex_string_wchar(digits, rng_state);
      h = CreateFileW(temp_dir, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
      if (h != INVALID_HANDLE_VALUE) {
        *dest_path = temp_dir;
        OV_ARRAY_SET_LENGTH(temp_dir, path_len);
        temp_dir = NULL; // Transfer ownership
        success = true;
        goto cleanup;
      }
      HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
      if (hr != HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)) {
        OV_ERROR_SET_HRESULT(err, hr);
        goto cleanup;
      }
    }
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("failed to create unique temporary file"));
  }

cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
  if (temp_dir) {
    OV_ARRAY_DESTROY(&temp_dir);
  }
  return success;
}
