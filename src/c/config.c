#include "config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shlobj.h>

#include <ovarray.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/file.h>
#include <ovl/os.h>
#include <ovl/path.h>
#include <ovl/source.h>
#include <ovl/source/file.h>

#include "json.h"

struct gcmz_config {
  NATIVE_CHAR **save_paths;
  enum gcmz_processing_mode processing_mode;
  bool allow_create_directories;
  bool external_api;
  bool show_debug_menu;
  gcmz_project_path_provider_fn project_path_getter;
  void *project_path_userdata;
};

#ifdef _WIN32
#  define STRLEN wcslen
#  define STRCHR wcschr
#  define STRNCMP wcsncmp
#else
#  define STRLEN strlen
#  define STRCHR strchr
#  define STRNCMP strncmp
#endif

static bool test_file_creation(NATIVE_CHAR const *const dir_path, bool const create_dir, struct ov_error *const err) {
  if (!dir_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  NATIVE_CHAR *test_file_path = NULL;
  HANDLE test_file = INVALID_HANDLE_VALUE;
  bool result = false;

  // Create directory if allowed
  if (create_dir) {
    int const ret = SHCreateDirectoryExW(NULL, dir_path, NULL);
    if (ret != ERROR_SUCCESS && ret != ERROR_ALREADY_EXISTS) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(ret));
      goto cleanup;
    }
  }

  // Build test file path
  {
    // Check if dir_path already ends with a path separator
    size_t dir_len = STRLEN(dir_path);
    bool const has_trailing_sep =
        dir_len > 0 && (dir_path[dir_len - 1] == NSTR('\\') || dir_path[dir_len - 1] == NSTR('/'));
    if (has_trailing_sep) {
      dir_len--;
    }

    NATIVE_CHAR const test_filename[] = NSTR("\\_gcmz_.tmp");
    size_t const test_filename_len = sizeof(test_filename) / sizeof(test_filename[0]) - 1;

    if (!OV_ARRAY_GROW(&test_file_path, dir_len + test_filename_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    memcpy(test_file_path, dir_path, dir_len * sizeof(NATIVE_CHAR));
    memcpy(test_file_path + dir_len, test_filename, test_filename_len * sizeof(NATIVE_CHAR));
    test_file_path[dir_len + test_filename_len] = NSTR('\0');
  }

  // Try to create test file
  test_file = CreateFileW(
      test_file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
  if (test_file == INVALID_HANDLE_VALUE) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  result = true;

cleanup:
  if (test_file != INVALID_HANDLE_VALUE) {
    CloseHandle(test_file); // FILE_FLAG_DELETE_ON_CLOSE will delete the file
  }
  if (test_file_path) {
    OV_ARRAY_DESTROY(&test_file_path);
  }
  return result;
}

static bool get_dll_directory(NATIVE_CHAR **const dir, struct ov_error *const err) {
  if (!dir) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  NATIVE_CHAR *module_path = NULL;
  void *hinstance = NULL;
  bool result = false;

  {
    if (!ovl_os_get_hinstance_from_fnptr((void *)get_dll_directory, &hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!ovl_path_get_module_name(&module_path, hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    NATIVE_CHAR const *last_slash = ovl_path_find_last_path_sep(module_path);
    if (!last_slash) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "No directory separator found in module path");
      goto cleanup;
    }

    size_t const dir_len = (size_t)(last_slash - module_path);
    if (!OV_ARRAY_GROW(dir, dir_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    memcpy(*dir, module_path, dir_len * sizeof(NATIVE_CHAR));
    (*dir)[dir_len] = NSTR('\0');
  }

  result = true;

cleanup:
  if (module_path) {
    OV_ARRAY_DESTROY(&module_path);
  }
  return result;
}

struct gcmz_config *gcmz_config_create(struct gcmz_config_options const *const options, struct ov_error *const err) {
  struct gcmz_config *cfg = NULL;
  struct gcmz_config *result = NULL;

  if (!OV_REALLOC(&cfg, 1, sizeof(struct gcmz_config))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  *cfg = (struct gcmz_config){
      .external_api = true,
      .project_path_getter = options ? options->project_path_provider : NULL,
      .project_path_userdata = options ? options->project_path_provider_userdata : NULL,
  };

  // Set default save_paths to %PROJECTDIR%
  {
    NATIVE_CHAR const *const default_path = NSTR("%PROJECTDIR%");
    if (!gcmz_config_set_save_paths(cfg, &default_path, 1, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = cfg;
  cfg = NULL;

cleanup:
  if (cfg) {
    gcmz_config_destroy(&cfg);
  }
  return result;
}

enum { placeholder_buffer_size = 512 };

typedef size_t (*expand_vars_replace_fn)(NATIVE_CHAR const *const var_name,
                                         size_t const var_name_len,
                                         NATIVE_CHAR replacement_buf[placeholder_buffer_size],
                                         void *const userdata);

/**
 * Expands placeholders in a path string by replacing %NAME% patterns with values from a callback.
 *
 * The function processes the path from left to right, replacing each %NAME% placeholder with the string
 * returned by the callback. If the callback returns SIZE_MAX, the placeholder is left as a literal '%' character.
 *
 * @param path The input path string containing placeholders in the format %NAME%
 * @param callback Function called for each placeholder to provide replacement text (up to placeholder_buffer_size chars
 * including null terminator)
 * @param userdata User-defined data passed to the callback function
 * @param expanded_path Output parameter receiving the expanded path string. Reuses existing buffer if non-NULL.
 * @param err Error information if the function fails
 * @return true on success, false on failure
 */
static bool expand_vars(NATIVE_CHAR const *const path,
                        expand_vars_replace_fn const callback,
                        void *const userdata,
                        NATIVE_CHAR **const expanded_path,
                        struct ov_error *const err) {
  if (!path || !expanded_path || !callback) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  NATIVE_CHAR *const initial_expanded_path = *expanded_path;
  bool result = false;

  {
    size_t total_length = STRLEN(path);
    if (!OV_ARRAY_GROW(expanded_path, total_length + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    NATIVE_CHAR replacement_buf[placeholder_buffer_size];
    NATIVE_CHAR const *src = path;
    NATIVE_CHAR const *src_end = src + STRLEN(path);
    size_t dest_pos = 0;
    while (src < src_end) {
      NATIVE_CHAR const *percent_start = STRCHR(src, NSTR('%'));
      size_t const copy_len = (size_t)(percent_start ? percent_start - src : src_end - src);
      if (copy_len > 0) {
        memcpy(*expanded_path + dest_pos, src, copy_len * sizeof(NATIVE_CHAR));
        dest_pos += copy_len;
      }
      if (!percent_start) {
        break;
      }

      NATIVE_CHAR const *const percent_end = STRCHR(percent_start + 1, NSTR('%'));
      size_t const var_name_len = (size_t)(percent_end - percent_start - 1);
      if (!percent_end || var_name_len == 0) {
        (*expanded_path)[dest_pos++] = *percent_start;
        src = percent_start + 1;
        continue;
      }

      replacement_buf[0] = NSTR('\0');
      size_t const replacement_len = callback(percent_start + 1, var_name_len, replacement_buf, userdata);
      if (replacement_len == SIZE_MAX) {
        (*expanded_path)[dest_pos++] = *percent_start;
        src = percent_start + 1;
        continue;
      }
      size_t const placeholder_len = (size_t)(percent_end - percent_start + 1);
      total_length = total_length - placeholder_len + replacement_len;
      if (total_length + 1 > OV_ARRAY_CAPACITY(*expanded_path)) {
        if (!OV_ARRAY_GROW(expanded_path, (total_length + 1) * 2)) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
          goto cleanup;
        }
      }
      memcpy(*expanded_path + dest_pos, replacement_buf, replacement_len * sizeof(NATIVE_CHAR));
      dest_pos += replacement_len;
      src = percent_end + 1; // Skip past closing %
    }

    (*expanded_path)[dest_pos] = NSTR('\0');
  }

  result = true;

cleanup:
  if (!result && !initial_expanded_path) {
    OV_ARRAY_DESTROY(expanded_path);
  }
  return result;
}

struct placeholder_callback_data {
  struct gcmz_config const *config;
  SYSTEMTIME st;
};

static SYSTEMTIME const *placeholder_callback_get_local_time(struct placeholder_callback_data *const data) {
  if (data->st.wYear == 0) {
    GetLocalTime(&data->st);
  }
  return &data->st;
}

static int placeholder_expand_projectdir(struct gcmz_config const *const config,
                                         NATIVE_CHAR replacement_buf[placeholder_buffer_size],
                                         struct ov_error *err) {
  int result = -1;

  {
    NATIVE_CHAR const *const project_path =
        config->project_path_getter ? config->project_path_getter(config->project_path_userdata) : NULL;
    if (!project_path) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    NATIVE_CHAR const *const last_sep = ovl_path_find_last_path_sep(project_path);
    size_t const dir_len = (size_t)(last_sep - project_path);
    if (dir_len >= placeholder_buffer_size) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    memcpy(replacement_buf, project_path, dir_len * sizeof(NATIVE_CHAR));
    replacement_buf[dir_len] = NSTR('\0');
    result = (int)dir_len;
  }

cleanup:
  return result;
}

static int placeholder_expand_shareddir(NATIVE_CHAR replacement_buf[placeholder_buffer_size], struct ov_error *err) {
  static NATIVE_CHAR const shared_folder_name[] = NSTR("GCMZShared");
  static size_t const folder_name_len = sizeof(shared_folder_name) / sizeof(NATIVE_CHAR) - 1;

  NATIVE_CHAR *dll_dir = NULL;
  int result = -1;

  {
    if (!get_dll_directory(&dll_dir, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    size_t const dll_dir_len = STRLEN(dll_dir);
    size_t const total_len = dll_dir_len + 1 + folder_name_len;

    // Check if total_len + 1 (for null terminator) fits in buffer
    if (total_len + 1 > placeholder_buffer_size) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    memcpy(replacement_buf, dll_dir, dll_dir_len * sizeof(NATIVE_CHAR));
    replacement_buf[dll_dir_len] = NSTR('\\');
    memcpy(replacement_buf + dll_dir_len + 1, shared_folder_name, folder_name_len * sizeof(NATIVE_CHAR));
    replacement_buf[total_len] = NSTR('\0');
    result = (int)total_len;
  }

cleanup:
  if (dll_dir) {
    OV_ARRAY_DESTROY(&dll_dir);
  }
  return result;
}

static size_t placeholder_callback(NATIVE_CHAR const *const var_name,
                                   size_t const var_name_len,
                                   NATIVE_CHAR replacement_buf[placeholder_buffer_size],
                                   void *const userdata) {
  static NATIVE_CHAR const project_dir_name[] = NSTR("PROJECTDIR");
  static size_t const project_dir_name_len = sizeof(project_dir_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const shared_dir_name[] = NSTR("SHAREDDIR");
  static size_t const shared_dir_name_len = sizeof(shared_dir_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const year_name[] = NSTR("YEAR");
  static size_t const year_name_len = sizeof(year_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const month_name[] = NSTR("MONTH");
  static size_t const month_name_len = sizeof(month_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const day_name[] = NSTR("DAY");
  static size_t const day_name_len = sizeof(day_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const hour_name[] = NSTR("HOUR");
  static size_t const hour_name_len = sizeof(hour_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const minute_name[] = NSTR("MINUTE");
  static size_t const minute_name_len = sizeof(minute_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const second_name[] = NSTR("SECOND");
  static size_t const second_name_len = sizeof(second_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const millisecond_name[] = NSTR("MILLISECOND");
  static size_t const millisecond_name_len = sizeof(millisecond_name) / sizeof(NATIVE_CHAR) - 1;
  static NATIVE_CHAR const ph02d[] = NSTR("%02d");
  static NATIVE_CHAR const ph03d[] = NSTR("%03d");
  static NATIVE_CHAR const ph04d[] = NSTR("%04d");
  static NATIVE_CHAR const bad_var[] = NSTR("***");
  static size_t const bad_var_len = sizeof(bad_var) / sizeof(NATIVE_CHAR) - 1;

  struct ov_error err = {0};
  struct placeholder_callback_data *const data = (struct placeholder_callback_data *)userdata;
  int written = -1;
  if (var_name_len == project_dir_name_len && STRNCMP(var_name, project_dir_name, project_dir_name_len) == 0) {
    written = placeholder_expand_projectdir(data->config, replacement_buf, &err);
  } else if (var_name_len == shared_dir_name_len && STRNCMP(var_name, shared_dir_name, shared_dir_name_len) == 0) {
    written = placeholder_expand_shareddir(replacement_buf, &err);
  } else if (var_name_len == year_name_len && STRNCMP(var_name, year_name, year_name_len) == 0) {
    int const year = placeholder_callback_get_local_time(data)->wYear;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph04d, ph04d, year);
  } else if (var_name_len == month_name_len && STRNCMP(var_name, month_name, month_name_len) == 0) {
    int const month = placeholder_callback_get_local_time(data)->wMonth;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph02d, ph02d, month);
  } else if (var_name_len == day_name_len && STRNCMP(var_name, day_name, day_name_len) == 0) {
    int const day = placeholder_callback_get_local_time(data)->wDay;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph02d, ph02d, day);
  } else if (var_name_len == hour_name_len && STRNCMP(var_name, hour_name, hour_name_len) == 0) {
    int const hour = placeholder_callback_get_local_time(data)->wHour;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph02d, ph02d, hour);
  } else if (var_name_len == minute_name_len && STRNCMP(var_name, minute_name, minute_name_len) == 0) {
    int const minute = placeholder_callback_get_local_time(data)->wMinute;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph02d, ph02d, minute);
  } else if (var_name_len == second_name_len && STRNCMP(var_name, second_name, second_name_len) == 0) {
    int const second = placeholder_callback_get_local_time(data)->wSecond;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph02d, ph02d, second);
  } else if (var_name_len == millisecond_name_len && STRNCMP(var_name, millisecond_name, millisecond_name_len) == 0) {
    int const milliseconds = placeholder_callback_get_local_time(data)->wMilliseconds;
    written = ov_snprintf_wchar(replacement_buf, placeholder_buffer_size, ph03d, ph03d, milliseconds);
  } else {
    return SIZE_MAX; // Unknown variable
  }
  if (written < 0) {
    OV_ERROR_DESTROY(&err);
    memcpy(replacement_buf, bad_var, sizeof(bad_var));
    return bad_var_len;
  }
  return (size_t)written;
}

void gcmz_config_destroy(struct gcmz_config **const config) {
  if (!config || !*config) {
    return;
  }

  struct gcmz_config *cfg = *config;

  if (cfg->save_paths) {
    size_t const count = OV_ARRAY_LENGTH(cfg->save_paths);
    for (size_t i = 0; i < count; ++i) {
      if (cfg->save_paths[i]) {
        OV_ARRAY_DESTROY(&cfg->save_paths[i]);
      }
    }
    OV_ARRAY_DESTROY(&cfg->save_paths);
  }

  OV_FREE(config);
}

static bool get_config_file_path(NATIVE_CHAR **const config_path, struct ov_error *const err) {
  if (!config_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  NATIVE_CHAR const last_part[] = NSTR("\\GCMZDrops.json");
  NATIVE_CHAR *dll_dir = NULL;
  bool result = false;

  if (!get_dll_directory(&dll_dir, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  {
    size_t const dll_dir_len = STRLEN(dll_dir);
    size_t const last_part_len = STRLEN(last_part);
    if (!OV_ARRAY_GROW(config_path, dll_dir_len + last_part_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    memcpy(*config_path, dll_dir, dll_dir_len * sizeof(NATIVE_CHAR));
    memcpy((*config_path) + dll_dir_len, last_part, last_part_len * sizeof(NATIVE_CHAR));
    (*config_path)[dll_dir_len + last_part_len] = NSTR('\0');
  }

  result = true;

cleanup:
  if (dll_dir) {
    OV_ARRAY_DESTROY(&dll_dir);
  }
  return result;
}

static char const g_json_key_version[] = "version";
static char const g_json_key_processing_mode[] = "processing_mode";
static char const g_json_key_allow_create_directories[] = "allow_create_directories";
static char const g_json_key_external_api[] = "external_api";
static char const g_json_key_show_debug_menu[] = "show_debug_menu";
static char const g_json_key_save_paths[] = "save_paths";

static bool load_save_paths_from_json(struct gcmz_config *const config,
                                      yyjson_val *const save_paths_array,
                                      struct ov_error *const err) {
  if (!config || !save_paths_array) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  size_t const array_size = yyjson_arr_size(save_paths_array);
  if (array_size == 0) {
    return true; // Empty array, nothing to do
  }

  NATIVE_CHAR **temp_paths = NULL;
  size_t valid_count = 0;
  bool result = false;

  if (!OV_ARRAY_GROW(&temp_paths, array_size)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  memset(temp_paths, 0, array_size * sizeof(NATIVE_CHAR *));

  for (size_t i = 0; i < array_size; ++i) {
    yyjson_val *item_val = yyjson_arr_get(save_paths_array, i);
    if (!item_val || !yyjson_is_str(item_val)) {
      continue;
    }

    char const *item_utf8 = yyjson_get_str(item_val);
    if (!item_utf8) {
      continue;
    }

    size_t const item_utf8_len = strlen(item_utf8);
    if (item_utf8_len >= 32768) {
      continue; // Path too long
    }

    size_t const item_wchar_len = ov_utf8_to_wchar_len(item_utf8, item_utf8_len);
    if (item_wchar_len == 0) {
      continue;
    }

    if (!OV_ARRAY_GROW(&temp_paths[valid_count], item_wchar_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const item_converted =
        ov_utf8_to_wchar(item_utf8, item_utf8_len, temp_paths[valid_count], item_wchar_len + 1, NULL);
    if (item_converted == 0) {
      continue;
    }

    valid_count++;
  }

  if (!gcmz_config_set_save_paths(config, (NATIVE_CHAR const *const *)temp_paths, valid_count, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (temp_paths) {
    for (size_t i = 0; i < array_size; ++i) {
      if (temp_paths[i]) {
        OV_ARRAY_DESTROY(&temp_paths[i]);
      }
    }
    OV_ARRAY_DESTROY(&temp_paths);
  }
  return result;
}

bool gcmz_config_load(struct gcmz_config *const config, struct ov_error *const err) {
  if (!config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  NATIVE_CHAR *config_path = NULL;
  struct ovl_source *source = NULL;
  char *json_str = NULL;
  yyjson_doc *doc = NULL;
  bool result = false;

  {
    if (!get_config_file_path(&config_path, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!ovl_source_file_create(config_path, &source, err)) {
      if (ov_error_is(err, ov_error_type_hresult, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) {
        OV_ERROR_DESTROY(err);
        result = true; // Use default settings
        goto cleanup;
      }
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const file_size = ovl_source_size(source);
    if (file_size == UINT64_MAX) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (file_size > SIZE_MAX) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    size_t const size = (size_t)file_size;
    if (!OV_ARRAY_GROW(&json_str, size + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const bytes_read = ovl_source_read(source, json_str, 0, size);
    if (bytes_read != size) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    json_str[size] = '\0';

    doc = yyjson_read(json_str, strlen(json_str), 0);
    if (!doc) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    yyjson_val *processing_mode_val = yyjson_obj_get(root, g_json_key_processing_mode);
    if (processing_mode_val && yyjson_is_str(processing_mode_val)) {
      config->processing_mode = gcmz_processing_mode_from_string(yyjson_get_str(processing_mode_val));
    }

    yyjson_val *create_dirs_val = yyjson_obj_get(root, g_json_key_allow_create_directories);
    if (create_dirs_val && yyjson_is_bool(create_dirs_val)) {
      config->allow_create_directories = yyjson_get_bool(create_dirs_val);
    }

    yyjson_val *external_api_val = yyjson_obj_get(root, g_json_key_external_api);
    if (external_api_val && yyjson_is_bool(external_api_val)) {
      config->external_api = yyjson_get_bool(external_api_val);
    }

    yyjson_val *show_debug_menu_val = yyjson_obj_get(root, g_json_key_show_debug_menu);
    if (show_debug_menu_val && yyjson_is_bool(show_debug_menu_val)) {
      config->show_debug_menu = yyjson_get_bool(show_debug_menu_val);
    }

    yyjson_val *save_paths_array = yyjson_obj_get(root, g_json_key_save_paths);
    if (save_paths_array) {
      if (yyjson_is_arr(save_paths_array)) {
        if (!load_save_paths_from_json(config, save_paths_array, err)) {
          OV_ERROR_ADD_TRACE(err);
          goto cleanup;
        }
      }
    }
  }

  result = true;

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  if (doc) {
    yyjson_doc_free(doc);
  }
  OV_ARRAY_DESTROY(&json_str);
  OV_ARRAY_DESTROY(&config_path);
  return result;
}

bool gcmz_config_save(struct gcmz_config const *const config, struct ov_error *const err) {
  if (!config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  NATIVE_CHAR *config_path = NULL;
  yyjson_mut_doc *doc = NULL;
  char *json_str = NULL;
  char *path_utf8 = NULL;
  struct ovl_file *file = NULL;
  bool result = false;

  {
    if (!get_config_file_path(&config_path, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "Failed to create JSON document");
      goto cleanup;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, g_json_key_version, "1.0");
    yyjson_mut_obj_add_str(
        doc, root, g_json_key_processing_mode, gcmz_processing_mode_to_string(config->processing_mode));
    yyjson_mut_obj_add_bool(doc, root, g_json_key_allow_create_directories, config->allow_create_directories);
    yyjson_mut_obj_add_bool(doc, root, g_json_key_external_api, config->external_api);
    yyjson_mut_obj_add_bool(doc, root, g_json_key_show_debug_menu, config->show_debug_menu);

    yyjson_mut_val *save_paths_array = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, g_json_key_save_paths, save_paths_array);
    size_t const count = OV_ARRAY_LENGTH(config->save_paths);
    for (size_t i = 0; i < count; ++i) {
      if (!config->save_paths[i]) {
        continue;
      }
      size_t path_wchar_len = wcslen(config->save_paths[i]);
      size_t path_utf8_len = ov_wchar_to_utf8_len(config->save_paths[i], path_wchar_len);
      if (path_utf8_len == 0) {
        continue; // Skip invalid paths
      }
      if (!OV_ARRAY_GROW(&path_utf8, path_utf8_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      size_t path_converted =
          ov_wchar_to_utf8(config->save_paths[i], path_wchar_len, path_utf8, path_utf8_len + 1, NULL);
      if (path_converted == 0) {
        continue; // Skip conversion failures
      }
      yyjson_mut_arr_add_str(doc, save_paths_array, path_utf8);
    }

    json_str = yyjson_mut_write_opts(doc, YYJSON_WRITE_PRETTY, gcmz_json_get_alc(), NULL, NULL);
    if (!json_str) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    if (!ovl_file_create(config_path, &file, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    size_t const json_len = strlen(json_str);
    size_t written = 0;
    if (!ovl_file_write(file, json_str, json_len, &written, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (written != json_len) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (file) {
    ovl_file_close(file);
  }
  if (doc) {
    yyjson_mut_doc_free(doc);
  }
  if (json_str) {
    OV_FREE(&json_str);
  }
  OV_ARRAY_DESTROY(&path_utf8);
  OV_ARRAY_DESTROY(&config_path);
  return result;
}

static bool try_save_path(struct gcmz_config const *const config,
                          NATIVE_CHAR const *const save_path,
                          NATIVE_CHAR const *const filename,
                          size_t const filename_len,
                          bool const create_directories,
                          NATIVE_CHAR **const result_path,
                          struct ov_error *const err) {
  if (!config || !save_path || !filename || !result_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  if (!expand_vars(
          save_path, placeholder_callback, &(struct placeholder_callback_data){.config = config}, result_path, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!test_file_creation(*result_path, create_directories, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    // Append filename to the expanded directory path
    size_t const dir_path_len = STRLEN(*result_path);
    if (!OV_ARRAY_GROW(result_path, dir_path_len + 1 + filename_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    (*result_path)[dir_path_len] = NSTR('\\');
    memcpy(*result_path + dir_path_len + 1, filename, filename_len * sizeof(NATIVE_CHAR));
    (*result_path)[dir_path_len + 1 + filename_len] = NSTR('\0');
  }

  result = true;

cleanup:
  return result;
}

NATIVE_CHAR *gcmz_config_get_save_path(struct gcmz_config const *const config,
                                       NATIVE_CHAR const *const filename,
                                       struct ov_error *const err) {
  if (!config || !filename) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  NATIVE_CHAR *path = NULL;
  NATIVE_CHAR *result = NULL;

  size_t const filename_len = STRLEN(filename);

  // Try each save_path in order until one works
  for (size_t i = 0, n = OV_ARRAY_LENGTH(config->save_paths); i < n; ++i) {
    if (!try_save_path(
            config, config->save_paths[i], filename, filename_len, config->allow_create_directories, &path, err)) {
      OV_ERROR_DESTROY(err);
      continue;
    }
    result = path;
    path = NULL;
    goto cleanup;
  }

  // Fallback to shared folder using placeholder
  if (!try_save_path(config, gcmz_config_get_fallback_save_path(), filename, filename_len, true, &path, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  result = path;
  path = NULL;

cleanup:
  if (path) {
    OV_ARRAY_DESTROY(&path);
  }
  return result;
}

bool gcmz_config_get_processing_mode(struct gcmz_config const *const config,
                                     enum gcmz_processing_mode *const mode,
                                     struct ov_error *const err) {
  if (!config || !mode) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *mode = config->processing_mode;
  return true;
}

bool gcmz_config_set_processing_mode(struct gcmz_config *const config,
                                     enum gcmz_processing_mode const mode,
                                     struct ov_error *const err) {
  if (!config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (mode < gcmz_processing_mode_auto || mode > gcmz_processing_mode_copy) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  config->processing_mode = mode;
  return true;
}

bool gcmz_config_get_allow_create_directories(struct gcmz_config const *const config,
                                              bool *const allow_create_directories,
                                              struct ov_error *const err) {
  if (!config || !allow_create_directories) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *allow_create_directories = config->allow_create_directories;
  return true;
}

bool gcmz_config_set_allow_create_directories(struct gcmz_config *const config,
                                              bool const allow_create_directories,
                                              struct ov_error *const err) {
  if (!config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  config->allow_create_directories = allow_create_directories;
  return true;
}

bool gcmz_config_get_external_api(struct gcmz_config const *const config,
                                  bool *const external_api,
                                  struct ov_error *const err) {
  if (!config || !external_api) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *external_api = config->external_api;
  return true;
}

bool gcmz_config_set_external_api(struct gcmz_config *const config,
                                  bool const external_api,
                                  struct ov_error *const err) {
  if (!config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  config->external_api = external_api;
  return true;
}

bool gcmz_config_get_show_debug_menu(struct gcmz_config const *const config,
                                     bool *const show_debug_menu,
                                     struct ov_error *const err) {
  if (!config || !show_debug_menu) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *show_debug_menu = config->show_debug_menu;
  return true;
}

bool gcmz_config_set_show_debug_menu(struct gcmz_config *const config,
                                     bool const show_debug_menu,
                                     struct ov_error *const err) {
  if (!config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  config->show_debug_menu = show_debug_menu;
  return true;
}

NATIVE_CHAR const *const *gcmz_config_get_save_paths(struct gcmz_config const *const config) {
  if (!config) {
    return NULL;
  }

  return (NATIVE_CHAR const *const *)config->save_paths;
}

bool gcmz_config_set_save_paths(struct gcmz_config *const config,
                                NATIVE_CHAR const *const *const paths,
                                size_t const num_paths,
                                struct ov_error *const err) {
  if (!config || (num_paths > 0 && !paths)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  for (size_t i = 0; i < num_paths; ++i) {
    if (!paths[i]) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
      return false;
    }
  }

  bool result = false;

  size_t const cur_num = OV_ARRAY_LENGTH(config->save_paths);
  if (cur_num > num_paths) {
    for (size_t i = num_paths; i < cur_num; ++i) {
      if (config->save_paths[i]) {
        OV_ARRAY_DESTROY(&config->save_paths[i]);
      }
    }
  } else if (cur_num < num_paths) {
    if (!OV_ARRAY_GROW(&config->save_paths, num_paths)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    for (size_t i = cur_num; i < num_paths; ++i) {
      config->save_paths[i] = NULL;
    }
  }
  for (size_t i = 0; i < num_paths; ++i) {
    size_t const new_path_len = STRLEN(paths[i]);
    if (!OV_ARRAY_GROW(&config->save_paths[i], new_path_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    memcpy(config->save_paths[i], paths[i], new_path_len * sizeof(NATIVE_CHAR));
    config->save_paths[i][new_path_len] = NSTR('\0');
  }

  OV_ARRAY_SET_LENGTH(config->save_paths, num_paths);

  result = true;

cleanup:
  return result;
}

NATIVE_CHAR const *gcmz_config_get_fallback_save_path(void) {
  static NATIVE_CHAR const shared_folder_placeholder[] = NSTR("%SHAREDDIR%\\%YEAR%");
  return shared_folder_placeholder;
}

bool gcmz_config_expand_placeholders(struct gcmz_config const *const config,
                                     NATIVE_CHAR const *const path,
                                     NATIVE_CHAR **const expanded_path,
                                     struct ov_error *const err) {
  if (!config || !path || !expanded_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  return expand_vars(
      path, placeholder_callback, &(struct placeholder_callback_data){.config = config}, expanded_path, err);
}

#undef STRLEN
#undef STRCHR
#undef STRNCMP
