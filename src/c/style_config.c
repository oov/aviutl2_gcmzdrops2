#include "style_config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shlobj.h>

#include <ovarray.h>

#include <ovl/os.h>
#include <ovl/path.h>

#include "ini_reader.h"

static wchar_t const g_style_config_name[] = L"style.conf";

struct gcmz_style_config {
  struct gcmz_ini_reader *base_config;
  struct gcmz_ini_reader *override_config;
  struct gcmz_color background_color;
};

/**
 * Check if color has alpha channel (not fully opaque)
 * @param c Color value to check
 * @return true if color has alpha channel (a < 255), false otherwise
 */
static inline bool has_alpha(struct gcmz_style_config_color const c) { return c.a < 255; }

/**
 * Get executable directory path
 * @param err [out] Error information on failure
 * @return Executable directory path on success, NULL on failure
 */
static NODISCARD wchar_t *get_executable_directory(struct ov_error *const err) {
  HMODULE module = NULL;
  NATIVE_CHAR *path = NULL;
  NATIVE_CHAR *result = NULL;

  module = GetModuleHandleW(NULL);
  if (!module) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  if (!ovl_path_get_module_name(&path, module, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    NATIVE_CHAR const *const last_slash = ovl_path_find_last_path_sep(path);
    if (!last_slash) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    size_t const dir_len = (size_t)(last_slash - path);
    path[dir_len] = L'\0';
    OV_ARRAY_SET_LENGTH(path, dir_len);
  }

  result = path;
  path = NULL;

cleanup:
  if (path) {
    OV_ARRAY_DESTROY(&path);
  }
  return result;
}

/**
 * Get ProgramData directory path
 * @param err [out] Error information on failure
 * @return ProgramData directory path on success, NULL on failure
 */
static NODISCARD wchar_t *get_program_data_directory(struct ov_error *const err) {
  wchar_t *path_from_api = NULL;
  wchar_t *path = NULL;
  wchar_t *result = NULL;

  HRESULT hr = SHGetKnownFolderPath(&FOLDERID_ProgramData, 0, NULL, &path_from_api);
  if (FAILED(hr)) {
    OV_ERROR_SET_HRESULT(err, hr);
    goto cleanup;
  }

  {
    size_t const path_len = wcslen(path_from_api);
    if (!OV_ARRAY_GROW(&path, path_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(path, path_from_api);
    OV_ARRAY_SET_LENGTH(path, path_len);
  }

  result = path;
  path = NULL;

cleanup:
  if (path) {
    OV_ARRAY_DESTROY(&path);
  }
  if (path_from_api) {
    CoTaskMemFree(path_from_api);
  }
  return result;
}

/**
 * Get base configuration file path (executable directory)
 * @param base_config_path [out] Path to base configuration file
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static NODISCARD bool get_base_config_path(wchar_t **const base_config_path, struct ov_error *const err) {
  if (!base_config_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *exe_dir = NULL;
  bool result = false;

  exe_dir = get_executable_directory(err);
  if (!exe_dir) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    size_t const exe_len = wcslen(exe_dir);
    size_t const config_len = wcslen(g_style_config_name);
    size_t const total_len = exe_len + 1 + config_len;
    if (!OV_ARRAY_GROW(base_config_path, total_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(*base_config_path, exe_dir);
    (*base_config_path)[exe_len] = L'\\';
    wcscpy(*base_config_path + exe_len + 1, g_style_config_name);
    OV_ARRAY_SET_LENGTH(*base_config_path, total_len);
  }

  result = true;

cleanup:
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
  return result;
}

/**
 * Get local override configuration file path (exe directory)
 * @param local_override_config_path [out] Path to local override configuration file
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static NODISCARD bool get_local_override_config_path(wchar_t **const local_override_config_path,
                                                     struct ov_error *const err) {
  static wchar_t const g_data_dir_name[] = L"data";

  if (!local_override_config_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *exe_dir = NULL;
  bool result = false;

  exe_dir = get_executable_directory(err);
  if (!exe_dir) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    size_t const exe_len = wcslen(exe_dir);
    size_t const data_dir_len = wcslen(g_data_dir_name);
    size_t const config_len = wcslen(g_style_config_name);
    size_t const total_len = exe_len + 1 + data_dir_len + 1 + config_len;
    if (!OV_ARRAY_GROW(local_override_config_path, total_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(*local_override_config_path, exe_dir);
    (*local_override_config_path)[exe_len] = L'\\';
    wcscpy(*local_override_config_path + exe_len + 1, g_data_dir_name);
    (*local_override_config_path)[exe_len + 1 + data_dir_len] = L'\\';
    wcscpy(*local_override_config_path + exe_len + 1 + data_dir_len + 1, g_style_config_name);
    OV_ARRAY_SET_LENGTH(*local_override_config_path, total_len);
  }

  result = true;

cleanup:
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
  return result;
}

/**
 * Get override configuration file path (ProgramData)
 * @param override_config_path [out] Path to override configuration file
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static NODISCARD bool get_override_config_path(wchar_t **const override_config_path, struct ov_error *const err) {
  static wchar_t const g_dir_name[] = L"aviutl2";

  if (!override_config_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *program_data_dir = NULL;
  bool result = false;

  program_data_dir = get_program_data_directory(err);
  if (!program_data_dir) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  {
    size_t const program_data_len = wcslen(program_data_dir);
    size_t const dir_name_len = wcslen(g_dir_name);
    size_t const config_len = wcslen(g_style_config_name);
    size_t const total_len = program_data_len + 1 + dir_name_len + 1 + config_len;
    if (!OV_ARRAY_GROW(override_config_path, total_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(*override_config_path, program_data_dir);
    (*override_config_path)[program_data_len] = L'\\';
    wcscpy(*override_config_path + program_data_len + 1, g_dir_name);
    (*override_config_path)[program_data_len + 1 + dir_name_len] = L'\\';
    wcscpy(*override_config_path + program_data_len + 1 + dir_name_len + 1, g_style_config_name);
    OV_ARRAY_SET_LENGTH(*override_config_path, total_len);
  }

  result = true;

cleanup:
  if (program_data_dir) {
    OV_ARRAY_DESTROY(&program_data_dir);
  }
  return result;
}

/**
 * Parse single hex digit
 * @param c Hex character
 * @param value [out] Parsed value (0-15)
 * @return true if valid hex digit, false otherwise
 */
static bool parse_hex_digit(char const c, uint8_t *const value) {
  if (!value) {
    return false;
  }

  if (c >= '0' && c <= '9') {
    *value = (uint8_t)(c - '0');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *value = (uint8_t)(c - 'A' + 10);
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *value = (uint8_t)(c - 'a' + 10);
    return true;
  }
  return false;
}

/**
 * Parse two hex digits into a byte value
 * @param hex_str Pointer to two hex characters
 * @param value [out] Parsed byte value (0-255)
 * @return true if successful, false otherwise
 */
static bool parse_hex_byte(char const *const hex_str, uint8_t *const value) {
  if (!hex_str || !value) {
    return false;
  }

  uint8_t high, low;
  if (!parse_hex_digit(hex_str[0], &high) || !parse_hex_digit(hex_str[1], &low)) {
    return false;
  }

  *value = (uint8_t)((high << 4) | low);
  return true;
}

/**
 * Parse hex color string (RGB or RGBA format)
 * @param hex_string Color string in hex format (6 or 8 characters)
 * @param len Length of hex_string
 * @param color [out] Parsed color value
 * @return true if parsing succeeded, false otherwise
 */
static bool
parse_color_value(char const *const hex_string, size_t const len, struct gcmz_style_config_color *const color) {
  if (!hex_string || !color) {
    return false;
  }
  if (len != 6 && len != 8) {
    return false;
  }
  if (!parse_hex_byte(hex_string, &color->r) || !parse_hex_byte(hex_string + 2, &color->g) ||
      !parse_hex_byte(hex_string + 4, &color->b)) {
    return false;
  }
  if (len == 8) {
    if (!parse_hex_byte(hex_string + 6, &color->a)) {
      return false;
    }
  } else {
    color->a = 255;
  }
  return true;
}

/**
 * Blend RGBA color with background using alpha compositing
 * Formula: result = (alpha * foreground + (1-alpha) * background)
 * @param foreground Foreground color with alpha
 * @param background Background color
 * @return Blended RGB color result (without alpha)
 */
static NODISCARD struct gcmz_color blend_rgba_with_background(struct gcmz_style_config_color const *const foreground,
                                                              struct gcmz_color const *const background) {
  if (!foreground || !background) {
    return (struct gcmz_color){0};
  }
  if (!has_alpha(*foreground)) {
    return (struct gcmz_color){
        .r = foreground->r,
        .g = foreground->g,
        .b = foreground->b,
    };
  }
  uint32_t const alpha = (uint32_t)foreground->a;
  uint32_t const inv_alpha = 255 - alpha;
  return (struct gcmz_color){
      .r = (uint8_t)(((alpha * foreground->r + inv_alpha * background->r) * 32897) >> 23),
      .g = (uint8_t)(((alpha * foreground->g + inv_alpha * background->g) * 32897) >> 23),
      .b = (uint8_t)(((alpha * foreground->b + inv_alpha * background->b) * 32897) >> 23),
  };
}

/**
 * Check if character is a decimal digit
 * @param c Character to check
 * @return true if character is 0-9, false otherwise
 */
static bool is_decimal_digit(char const c) { return c >= '0' && c <= '9'; }

/**
 * Check if character is whitespace (space or tab)
 * @param c Character to check
 * @return true if character is whitespace, false otherwise
 */
static bool is_whitespace(char const c) { return c == ' ' || c == '\t'; }

/**
 * Parse integer string value
 * @param str String to parse
 * @param len Length of string
 * @param value [out] Parsed integer value
 * @return true if parsing succeeded, false otherwise
 */
static bool parse_integer_value(char const *const str, size_t const len, int64_t *const value) {
  if (!str || len == 0 || !value) {
    return false;
  }

  char const *p = str;
  char const *end = str + len;
  while (p < end && is_whitespace(*p)) {
    p++;
  }
  while (end > p && is_whitespace(*(end - 1))) {
    end--;
  }
  if (p >= end) {
    return false;
  }

  bool is_negative = false;
  if (*p == '-') {
    is_negative = true;
    p++;
  } else if (*p == '+') {
    p++;
  }
  if (p >= end || !is_decimal_digit(*p)) {
    return false;
  }

  uint64_t v = 0;
  while (p < end) {
    if (!is_decimal_digit(*p)) {
      return false;
    }
    uint64_t const prev = v;
    v = v * 10 + (uint64_t)(*p - '0');
    if (v < prev) {
      return false;
    }
    p++;
  }

  if (is_negative) {
    if (v > (uint64_t)INT64_MAX + 1) {
      return false;
    }
    *value = -(int64_t)v;
  } else {
    if (v > INT64_MAX) {
      return false;
    }
    *value = (int64_t)v;
  }

  return true;
}

/**
 * Load configuration file if it exists
 * @param filepath Path to configuration file
 * @param reader [out] Loaded INI reader (NULL if file doesn't exist)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static NODISCARD bool load_config_file_if_exists(wchar_t const *const filepath,
                                                 struct gcmz_ini_reader **const reader,
                                                 struct ov_error *const err) {
  if (!filepath || !reader) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *reader = NULL;
  bool result = false;

  DWORD const attributes = GetFileAttributesW(filepath);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return true;
  }
  struct gcmz_ini_reader *r = NULL;
  if (!gcmz_ini_reader_create(&r, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  if (!gcmz_ini_reader_load_file(r, filepath, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  *reader = r;
  r = NULL;
  result = true;

cleanup:
  if (r) {
    gcmz_ini_reader_destroy(&r);
  }
  return result;
}

NODISCARD struct gcmz_style_config *gcmz_style_config_create(struct gcmz_style_config_options const *const options,
                                                             struct ov_error *const err) {
  struct gcmz_style_config *c = NULL;
  wchar_t *buffer = NULL;
  struct gcmz_style_config *result = NULL;
  wchar_t const *path = NULL;

  if (!OV_REALLOC(&c, 1, sizeof(struct gcmz_style_config))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  *c = (struct gcmz_style_config){0};

  // Load base configuration
  if (options && options->base_config_path) {
    path = options->base_config_path;
  } else {
    if (!get_base_config_path(&buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    path = buffer;
  }
  if (!load_config_file_if_exists(path, &c->base_config, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Load local override configuration (from data\ folder)
  if (options && options->override_config_path) {
    path = options->override_config_path;
  } else {
    if (!get_local_override_config_path(&buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    path = buffer;
  }
  if (!load_config_file_if_exists(path, &c->override_config, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // If local override was not found, try ProgramData override
  if (!c->override_config) {
    if (!get_override_config_path(&buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    path = buffer;
    if (!load_config_file_if_exists(path, &c->override_config, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  c->background_color = gcmz_style_config_get_color_background(c);

  result = c;
  c = NULL;

cleanup:
  if (buffer) {
    OV_ARRAY_DESTROY(&buffer);
  }
  if (c) {
    gcmz_style_config_destroy(&c);
  }
  return result;
}

void gcmz_style_config_destroy(struct gcmz_style_config **const config) {
  if (!config || !*config) {
    return;
  }
  struct gcmz_style_config *const c = *config;
  if (c->base_config) {
    gcmz_ini_reader_destroy(&c->base_config);
  }
  if (c->override_config) {
    gcmz_ini_reader_destroy(&c->override_config);
  }
  OV_FREE(config);
}

NODISCARD bool gcmz_style_config_get_raw_color(struct gcmz_style_config const *const config,
                                               char const *const section,
                                               char const *const key,
                                               struct gcmz_style_config_color *const value,
                                               struct ov_error *const err) {
  if (!config || !key || !value) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_ini_value v = {NULL, 0};
  bool result = false;

  if (config->override_config) {
    v = gcmz_ini_reader_get_value(config->override_config, section, key);
    if (v.ptr && v.size && parse_color_value(v.ptr, v.size, value)) {
      result = true;
      goto cleanup;
    }
  }

  if (!v.ptr && config->base_config) {
    v = gcmz_ini_reader_get_value(config->base_config, section, key);
    if (v.ptr && v.size && parse_color_value(v.ptr, v.size, value)) {
      result = true;
      goto cleanup;
    }
  }

  OV_ERROR_SET_GENERIC(err, ov_error_generic_not_found);

cleanup:
  return result;
}

NODISCARD struct gcmz_style_config_color
gcmz_style_config_get_raw_color_fallback(struct gcmz_style_config const *const config,
                                         char const *const section,
                                         char const *const key,
                                         struct gcmz_style_config_color const default_color) {
  struct gcmz_style_config_color result = {0};
  if (!gcmz_style_config_get_raw_color(config, section, key, &result, NULL)) {
    result = default_color;
  }
  return result;
}

NODISCARD bool gcmz_style_config_get_blended_color(struct gcmz_style_config const *const config,
                                                   char const *const section,
                                                   char const *const key,
                                                   struct gcmz_color *const value,
                                                   struct ov_error *const err) {
  if (!value) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_style_config_color raw_color = {0};
  if (!gcmz_style_config_get_raw_color(config, section, key, &raw_color, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }

  if (has_alpha(raw_color)) {
    *value = blend_rgba_with_background(&raw_color, &config->background_color);
  } else {
    *value = (struct gcmz_color){.r = raw_color.r, .g = raw_color.g, .b = raw_color.b};
  }
  return true;
}

NODISCARD struct gcmz_color gcmz_style_config_get_blended_color_fallback(struct gcmz_style_config const *const config,
                                                                         char const *const section,
                                                                         char const *const key,
                                                                         struct gcmz_color const default_color) {
  struct gcmz_color result = {0};
  if (!gcmz_style_config_get_blended_color(config, section, key, &result, NULL)) {
    result = default_color;
  }
  return result;
}

NODISCARD bool gcmz_style_config_get_integer(struct gcmz_style_config const *const config,
                                             char const *const section,
                                             char const *const key,
                                             int64_t *const value,
                                             struct ov_error *const err) {
  if (!config || !key || !value) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_ini_value v = {0};
  bool result = false;

  if (config->override_config) {
    v = gcmz_ini_reader_get_value(config->override_config, section, key);
    if (v.ptr && v.size && parse_integer_value(v.ptr, v.size, value)) {
      result = true;
      goto cleanup;
    }
  }
  if (config->base_config) {
    v = gcmz_ini_reader_get_value(config->base_config, section, key);
    if (v.ptr && v.size && parse_integer_value(v.ptr, v.size, value)) {
      result = true;
      goto cleanup;
    }
  }
  OV_ERROR_SET_GENERIC(err, ov_error_generic_not_found);
cleanup:
  return result;
}

NODISCARD int64_t gcmz_style_config_get_integer_fallback(struct gcmz_style_config const *const config,
                                                         char const *const section,
                                                         char const *const key,
                                                         int64_t const default_value) {
  int64_t value;
  if (gcmz_style_config_get_integer(config, section, key, &value, NULL)) {
    return value;
  }
  return default_value;
}

static char const g_section_layout[] = "Layout";
static char const g_section_color[] = "Color";

NODISCARD int gcmz_style_config_get_layout_scroll_bar_size(struct gcmz_style_config const *const config) {
  static int const default_value = 20;
  return (int)gcmz_style_config_get_integer_fallback(config, g_section_layout, "ScrollBarSize", default_value);
}

NODISCARD int gcmz_style_config_get_layout_time_gauge_height(struct gcmz_style_config const *const config) {
  static int const default_value = 16;
  return (int)gcmz_style_config_get_integer_fallback(config, g_section_layout, "TimeGaugeHeight", default_value);
}

NODISCARD int gcmz_style_config_get_layout_layer_header_width(struct gcmz_style_config const *const config) {
  static int const default_value = 100;
  return (int)gcmz_style_config_get_integer_fallback(config, g_section_layout, "LayerHeaderWidth", default_value);
}

NODISCARD int gcmz_style_config_get_layout_layer_height(struct gcmz_style_config const *const config) {
  static int const default_value = 24;
  return (int)gcmz_style_config_get_integer_fallback(config, g_section_layout, "LayerHeight", default_value);
}

NODISCARD struct gcmz_color gcmz_style_config_get_color_zoom_gauge(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {96, 160, 255};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "ZoomGauge", default_value);
}

NODISCARD struct gcmz_color gcmz_style_config_get_color_zoom_gauge_hover(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {128, 192, 255};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "ZoomGaugeHover", default_value);
}

NODISCARD struct gcmz_color gcmz_style_config_get_color_zoom_gauge_off(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {32, 64, 128};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "ZoomGaugeOff", default_value);
}

NODISCARD struct gcmz_color
gcmz_style_config_get_color_zoom_gauge_off_hover(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {48, 96, 160};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "ZoomGaugeOffHover", default_value);
}

NODISCARD struct gcmz_color gcmz_style_config_get_color_background(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {32, 32, 32};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "Background", default_value);
}

NODISCARD struct gcmz_color gcmz_style_config_get_color_frame_cursor(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {200, 48, 48};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "FrameCursor", default_value);
}

NODISCARD struct gcmz_color
gcmz_style_config_get_color_frame_cursor_wide(struct gcmz_style_config const *const config) {
  static struct gcmz_color const default_value = {200, 48, 48};
  return gcmz_style_config_get_blended_color_fallback(config, g_section_color, "FrameCursorWide", default_value);
}
