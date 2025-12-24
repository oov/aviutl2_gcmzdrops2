#include "copy.h"

#include <ovarray.h>
#include <ovcyrb64.h>
#include <ovl/file.h>
#include <ovl/path.h>
#include <ovmo.h>
#include <ovprintf.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shlobj.h>
#include <shlwapi.h>

static bool calc_file_hash(wchar_t const *const file_path, uint64_t *const hash, struct ov_error *const err) {
  if (!file_path || !hash) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  enum {
    buffer_size = 1024 * 1024, // 1MB
  };

  struct ovl_file *file = NULL;
  uint32_t *buffer = NULL;
  size_t remainder_count = 0;
  struct ov_cyrb64 ctx;
  bool result = false;

  ov_cyrb64_init(&ctx, 0);

  if (!OV_REALLOC(&buffer, buffer_size / sizeof(uint32_t), sizeof(uint32_t))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  if (!ovl_file_open(file_path, &file, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  for (;;) {
    size_t bytes_read = 0;
    if (!ovl_file_read(file, (uint8_t *)buffer + remainder_count, buffer_size - remainder_count, &bytes_read, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (bytes_read == 0) {
      break;
    }
    size_t const total_bytes = remainder_count + bytes_read;
    size_t const uint32_count = total_bytes / 4;
    size_t const new_remainder = total_bytes % 4;
    if (uint32_count > 0) {
      ov_cyrb64_update(&ctx, buffer, uint32_count);
    }
    remainder_count = new_remainder;
    if (remainder_count > 0) {
      uint8_t *const byte_buffer = (uint8_t *)buffer;
      memmove(byte_buffer, byte_buffer + uint32_count * 4, remainder_count);
    }
  }

  if (remainder_count > 0) {
    uint8_t *const byte_buffer = (uint8_t *)buffer;
    memset(byte_buffer + remainder_count, 0, 4 - remainder_count);
    ov_cyrb64_update(&ctx, buffer, 1);
  }

  *hash = ov_cyrb64_final(&ctx);

  result = true;

cleanup:
  if (file) {
    ovl_file_close(file);
    file = NULL;
  }
  if (buffer) {
    OV_FREE(&buffer);
  }
  return result;
}

static bool is_file_under_directory(wchar_t const *file_path, wchar_t const *directory_path) {
  if (!file_path || !directory_path) {
    return false;
  }
  wchar_t relative_path[MAX_PATH];
  if (!PathRelativePathToW(relative_path, directory_path, FILE_ATTRIBUTE_DIRECTORY, file_path, 0)) {
    return false;
  }
  if (wcsncmp(relative_path, L"..\\", 3) == 0 || wcsncmp(relative_path, L"../", 3) == 0) {
    return false;
  }
  return true;
}

static bool is_under_temp_directory(wchar_t const *file_path) {
  if (!file_path) {
    return false;
  }
  wchar_t temp_path[MAX_PATH];
  DWORD result = GetTempPathW(MAX_PATH, temp_path);
  if (result == 0 || result > MAX_PATH) {
    return false;
  }
  return is_file_under_directory(file_path, temp_path);
}

static bool is_under_system_directory(wchar_t const *file_path) {
  if (!file_path) {
    return false;
  }
  static int const csidls[] = {
      CSIDL_APPDATA,
      CSIDL_LOCAL_APPDATA,
      CSIDL_COMMON_APPDATA,
      CSIDL_COOKIES,
      CSIDL_INTERNET_CACHE,
      CSIDL_PROGRAM_FILES,
      CSIDL_PROGRAM_FILES_COMMON,
      CSIDL_STARTMENU,
      CSIDL_PROGRAMS,
      CSIDL_WINDOWS,
      CSIDL_SYSTEM,
      CSIDL_PROGRAM_FILESX86,
  };
  wchar_t system_path[MAX_PATH];
  size_t const n = sizeof(csidls) / sizeof(csidls[0]);
  for (size_t i = 0; i < n; ++i) {
    HRESULT const hr = SHGetFolderPathW(NULL, csidls[i], NULL, SHGFP_TYPE_CURRENT, system_path);
    if (FAILED(hr) || hr == S_FALSE) {
      continue;
    }
    if (is_file_under_directory(file_path, system_path)) {
      return true;
    }
  }
  return false;
}

static wchar_t const *get_extension_from_filename(wchar_t const *const filename) {
  wchar_t const *p = filename, *dot = NULL;
  while (*p) {
    if (*p == L'.') {
      dot = p;
    }
    p++;
  }
  return dot ? dot : p;
}

static void uint32_to_hex8(uint32_t value, wchar_t *buf8) {
  static wchar_t const hex_chars[] = L"0123456789abcdef";
  for (int i = 7; i >= 0; i--) {
    buf8[7 - i] = hex_chars[(value >> (i * 4)) & 0xf];
  }
}

static bool generate_hash_filename_from_hash(wchar_t const *const original_path,
                                             uint64_t hash,
                                             wchar_t **const hash_filename,
                                             struct ov_error *const err) {
  if (!original_path || !hash_filename) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *result = NULL;
  bool success = false;

  {
    wchar_t hash_hex[8];
    uint32_to_hex8(hash & 0xffffffff, hash_hex);
    wchar_t const *filename = ovl_path_extract_file_name(original_path);
    wchar_t const *ext_pos = get_extension_from_filename(filename);

    size_t const name_len = (size_t)(ext_pos - filename);
    size_t const ext_len = wcslen(ext_pos);
    size_t const hash_len = sizeof(hash_hex) / sizeof(wchar_t);
    size_t const total_len = name_len + 1 + hash_len + 1 + ext_len; // name.hash.ext + null

    if (!OV_ARRAY_GROW(&result, total_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    wchar_t *p = result;
    wcsncpy(p, filename, name_len);
    p += name_len;
    *p++ = L'.';
    wcsncpy(p, hash_hex, hash_len);
    p += hash_len;
    wcscpy(p, ext_pos);
    OV_ARRAY_SET_LENGTH(result, total_len);

    *hash_filename = result;
    result = NULL;
  }

  success = true;

cleanup:
  if (result) {
    OV_ARRAY_DESTROY(&result);
  }
  return success;
}

static ov_tribool
is_copy_needed(wchar_t const *const file_path, enum gcmz_processing_mode processing_mode, struct ov_error *const err) {
  if (!file_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return ov_indeterminate;
  }

  {
    static wchar_t const *const ignore_extensions[] = {
        // Files that are parsed and loaded
        L".txt",
        L".object",
        L".exo",
        // Script file types
        L".anm",
        L".anm2",
        L".obj",
        L".obj2",
        L".cam",
        L".cam2",
        L".scn",
        L".scn2",
        L".tra",
        L".tra2",
        // Plugins
        L".aui2",
        L".aul2",
        L".auo2",
        L".aup2",
        L".aux2",
    };
    wchar_t const *filename = ovl_path_extract_file_name(file_path);
    wchar_t const *ext = get_extension_from_filename(filename);
    size_t const ignore_count = sizeof(ignore_extensions) / sizeof(ignore_extensions[0]);
    for (size_t i = 0; i < ignore_count; ++i) {
      if (ovl_path_is_same_ext(ext, ignore_extensions[i])) {
        return ov_false;
      }
    }
  }

  switch (processing_mode) {
  case gcmz_processing_mode_copy:
    return ov_true;
  case gcmz_processing_mode_direct:
    return (is_under_temp_directory(file_path) ? ov_true : ov_false);
  case gcmz_processing_mode_auto:
    return ((is_under_temp_directory(file_path) || is_under_system_directory(file_path)) ? ov_true : ov_false);
  }
  OV_ERROR_SETF(err,
                ov_error_type_generic,
                ov_error_generic_unexpected,
                "%1$d",
                gettext("unknown processing mode: %1$d"),
                processing_mode);
  return ov_indeterminate;
}

static ov_tribool gcmz_file_find_existing_by_hash(wchar_t const *const directory,
                                                  wchar_t const *const hash_hex,
                                                  wchar_t const *const extension,
                                                  wchar_t **const found_file,
                                                  struct ov_error *const err) {
  if (!directory || !hash_hex || !extension || !found_file) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return ov_indeterminate;
  }

  wchar_t *search_pattern = NULL;
  WIN32_FIND_DATAW find_data;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  ov_tribool result = ov_indeterminate;

  {
    // Create search pattern: directory\*.hash.ext
    size_t const pattern_len = wcslen(directory) + 1 + 1 + 1 + wcslen(hash_hex) + wcslen(extension) + 1;
    if (!OV_ARRAY_GROW(&search_pattern, pattern_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_snprintf_wchar(search_pattern, pattern_len, NULL, L"%ls\\*.%ls%ls", directory, hash_hex, extension);
    hFind = FindFirstFileW(search_pattern, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
      HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
      if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        OV_ERROR_SET_HRESULT(err, hr);
        goto cleanup;
      }
      result = ov_false;
      goto cleanup;
    }
    size_t result_len = wcslen(directory) + 1 + wcslen(find_data.cFileName) + 1;
    if (!OV_ARRAY_GROW(found_file, result_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_snprintf_wchar(*found_file, result_len, NULL, L"%ls\\%ls", directory, find_data.cFileName);
  }

  result = ov_true;

cleanup:
  if (hFind != INVALID_HANDLE_VALUE) {
    FindClose(hFind);
    hFind = INVALID_HANDLE_VALUE;
  }
  if (search_pattern) {
    OV_ARRAY_DESTROY(&search_pattern);
  }
  return result;
}

bool gcmz_copy(wchar_t const *const source_file,
               enum gcmz_processing_mode processing_mode,
               gcmz_copy_get_save_path_fn get_save_path,
               void *userdata,
               wchar_t **const final_file,
               struct ov_error *const err) {
  if (!source_file || !get_save_path || !final_file) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *hash_filename = NULL;
  wchar_t *save_path = NULL;
  wchar_t *dir_path = NULL;
  uint64_t file_hash;
  bool result = false;

  {
    ov_tribool const needs_copy = is_copy_needed(source_file, processing_mode, err);
    if (needs_copy == ov_indeterminate) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!needs_copy) {
      size_t const path_len = wcslen(source_file) + 1;
      if (!OV_ARRAY_GROW(final_file, path_len)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(*final_file, source_file);
      result = true;
      goto cleanup;
    }
    if (!calc_file_hash(source_file, &file_hash, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!generate_hash_filename_from_hash(source_file, file_hash, &hash_filename, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    save_path = get_save_path(hash_filename, userdata, err);
    if (!save_path) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    wchar_t const *last_sep = ovl_path_find_last_path_sep(save_path);
    if (last_sep) {
      size_t const dir_len = (size_t)(last_sep - save_path);
      if (!OV_ARRAY_GROW(&dir_path, dir_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcsncpy(dir_path, save_path, dir_len);
      dir_path[dir_len] = L'\0';
      wchar_t hash_hex[9];
      uint32_to_hex8(file_hash & 0xffffffff, hash_hex);
      hash_hex[8] = L'\0';
      wchar_t const *extension = get_extension_from_filename(hash_filename);
      ov_tribool const found = gcmz_file_find_existing_by_hash(dir_path, hash_hex, extension, final_file, err);
      if (found == ov_indeterminate) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      if (found) {
        result = true;
        goto cleanup;
      }
    }
    if (!CopyFileW(source_file, save_path, FALSE)) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    // Return the saved path
    size_t const save_len = wcslen(save_path) + 1;
    if (!OV_ARRAY_GROW(final_file, save_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(*final_file, save_path);
  }

  result = true;

cleanup:
  if (hash_filename) {
    OV_ARRAY_DESTROY(&hash_filename);
  }
  if (dir_path) {
    OV_ARRAY_DESTROY(&dir_path);
  }
  if (save_path) {
    OV_ARRAY_DESTROY(&save_path);
  }
  return result;
}
