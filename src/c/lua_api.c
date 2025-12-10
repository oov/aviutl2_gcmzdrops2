// NOTE:
// When modifying Lua API functions, please also update the documentation in LUA.md

#include "lua_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovarray.h>
#include <ovl/file.h>
#include <ovmo.h>
#include <ovutf.h>

#include <aviutl2_plugin2.h>

#include "luautil.h"

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wreserved-macro-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#  endif
#endif // __GNUC__
#include <lauxlib.h>
#include <lua.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

static struct gcmz_lua_api_options g_lua_api_options = {0};

void gcmz_lua_api_set_options(struct gcmz_lua_api_options const *const options) {
  if (options) {
    g_lua_api_options = *options;
  } else {
    g_lua_api_options = (struct gcmz_lua_api_options){0};
  }
}

static int gcmz_lua_get_project_data(lua_State *L) {
  if (!g_lua_api_options.get_project_data) {
    return luaL_error(L, "get_project_data is not available (no project data provider configured)");
  }

  struct ov_error err = {0};
  struct aviutl2_edit_info edit_info = {0};
  char *project_path = NULL;
  int result = -1;

  {
    if (!g_lua_api_options.get_project_data(&edit_info, &project_path, g_lua_api_options.userdata, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    lua_createtable(L, 0, 10);

    lua_pushstring(L, "width");
    lua_pushinteger(L, edit_info.width);
    lua_settable(L, -3);

    lua_pushstring(L, "height");
    lua_pushinteger(L, edit_info.height);
    lua_settable(L, -3);

    lua_pushstring(L, "rate");
    lua_pushinteger(L, edit_info.rate);
    lua_settable(L, -3);

    lua_pushstring(L, "scale");
    lua_pushinteger(L, edit_info.scale);
    lua_settable(L, -3);

    lua_pushstring(L, "sample_rate");
    lua_pushinteger(L, edit_info.sample_rate);
    lua_settable(L, -3);

    if (project_path && *project_path) {
      lua_pushstring(L, "project_path");
      lua_pushstring(L, project_path);
      lua_settable(L, -3);
    }
  }

  result = 1;

cleanup:
  if (project_path) {
    OV_ARRAY_DESTROY(&project_path);
  }
  return result < 0 ? gcmz_luafn_result_err(L, &err) : result;
}

enum encoding_type {
  ENCODING_SJIS = 932,
  ENCODING_UTF8 = CP_UTF8,
  ENCODING_UTF16LE = 1200,
  ENCODING_UTF16BE = 1201,
  ENCODING_EUCJP = 20932,
  ENCODING_ISO2022JP = 50220,
  ENCODING_ANSI = CP_ACP
};

static int get_codepage_from_name(char const *name) {
  if (!name) {
    return -1;
  }

  if (strcmp(name, "sjis") == 0 || strcmp(name, "shift_jis") == 0) {
    return ENCODING_SJIS;
  }
  if (strcmp(name, "utf8") == 0 || strcmp(name, "utf-8") == 0) {
    return ENCODING_UTF8;
  }
  if (strcmp(name, "utf16le") == 0 || strcmp(name, "utf-16le") == 0) {
    return ENCODING_UTF16LE;
  }
  if (strcmp(name, "utf16be") == 0 || strcmp(name, "utf-16be") == 0) {
    return ENCODING_UTF16BE;
  }
  if (strcmp(name, "eucjp") == 0 || strcmp(name, "euc-jp") == 0) {
    return ENCODING_EUCJP;
  }
  if (strcmp(name, "iso2022jp") == 0 || strcmp(name, "iso-2022-jp") == 0) {
    return ENCODING_ISO2022JP;
  }
  if (strcmp(name, "ansi") == 0) {
    return ENCODING_ANSI;
  }

  return -1;
}

static bool convert_encoding_internal(char const *src_text,
                                      size_t src_len,
                                      int src_codepage,
                                      int dest_codepage,
                                      char **result,
                                      size_t *result_len,
                                      struct ov_error *const err) {
  if (!src_text || !result || !result_len) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *wide_text = NULL;
  bool success = false;

  {
    // Handle same codepage case
    if (src_codepage == dest_codepage) {
      if (!OV_ARRAY_GROW(result, src_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      memcpy(*result, src_text, src_len);
      (*result)[src_len] = '\0';
      *result_len = src_len;
      success = true;
      goto cleanup;
    }
    // Step 1: Convert source to wide characters
    int wide_len;
    if (src_codepage == ENCODING_UTF16LE) {
      // Source is already UTF-16LE
      wide_len = (int)(src_len / sizeof(wchar_t));
      if (!OV_ARRAY_GROW(&wide_text, (size_t)wide_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      memcpy(wide_text, src_text, src_len);
      wide_text[wide_len] = L'\0';
    } else if (src_codepage == ENCODING_UTF16BE) {
      // Convert UTF-16BE to UTF-16LE
      wide_len = (int)(src_len / sizeof(wchar_t));
      if (!OV_ARRAY_GROW(&wide_text, (size_t)wide_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      uint16_t const *src_words = (uint16_t const *)(void const *)src_text;
      for (int i = 0; i < wide_len; i++) {
        wide_text[i] = (wchar_t)((src_words[i] >> 8) | (src_words[i] << 8));
      }
      wide_text[wide_len] = L'\0';
    } else {
      // Convert from multibyte to wide
      wide_len = MultiByteToWideChar((UINT)src_codepage, 0, src_text, (int)src_len, NULL, 0);
      if (wide_len <= 0) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
      if (!OV_ARRAY_GROW(&wide_text, (size_t)wide_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      if (MultiByteToWideChar((UINT)src_codepage, 0, src_text, (int)src_len, wide_text, wide_len) != wide_len) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
      wide_text[wide_len] = L'\0';
    }

    // Step 2: Convert wide characters to destination
    if (dest_codepage == ENCODING_UTF16LE) {
      // Destination is UTF-16LE
      *result_len = (size_t)wide_len * sizeof(wchar_t);
      if (!OV_ARRAY_GROW(result, *result_len + sizeof(wchar_t))) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      memcpy(*result, wide_text, *result_len);
      memset(*result + *result_len, 0, sizeof(wchar_t)); // null terminate
    } else if (dest_codepage == ENCODING_UTF16BE) {
      // Convert UTF-16LE to UTF-16BE
      *result_len = (size_t)wide_len * sizeof(wchar_t);
      if (!OV_ARRAY_GROW(result, *result_len + sizeof(wchar_t))) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      uint16_t *dest_words = (uint16_t *)(void *)*result;
      for (int i = 0; i < wide_len; i++) {
        uint16_t val = (uint16_t)wide_text[i];
        dest_words[i] = (uint16_t)((val >> 8) | (val << 8)); // byte swap
      }
      memset(*result + *result_len, 0, sizeof(wchar_t)); // null terminate
    } else {
      // Convert wide to multibyte
      int dest_len = WideCharToMultiByte((UINT)dest_codepage, 0, wide_text, wide_len, NULL, 0, NULL, NULL);
      if (dest_len <= 0) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
      if (!OV_ARRAY_GROW(result, (size_t)dest_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      if (WideCharToMultiByte((UINT)dest_codepage, 0, wide_text, wide_len, *result, dest_len, NULL, NULL) != dest_len) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
      (*result)[dest_len] = '\0';
      *result_len = (size_t)dest_len;
    }
  }

  success = true;

cleanup:
  if (wide_text) {
    OV_ARRAY_DESTROY(&wide_text);
  }
  return success;
}

static int gcmz_lua_convert_encoding(lua_State *L) {
  char const *src_text = luaL_checkstring(L, 1);
  char const *src_encoding = luaL_checkstring(L, 2);
  char const *dest_encoding = luaL_checkstring(L, 3);

  if (!src_text || !src_encoding || !dest_encoding) {
    return luaL_error(L, "convert_encoding requires (text, src_encoding, dest_encoding)");
  }

  int src_codepage = get_codepage_from_name(src_encoding);
  int dest_codepage = get_codepage_from_name(dest_encoding);

  if (src_codepage == -1) {
    return luaL_error(L, "unsupported source encoding: %s", src_encoding);
  }
  if (dest_codepage == -1) {
    return luaL_error(L, "unsupported destination encoding: %s", dest_encoding);
  }

  struct ov_error err = {0};
  char *result = NULL;
  size_t result_len = 0;
  size_t src_len = strlen(src_text);

  if (!convert_encoding_internal(src_text, src_len, src_codepage, dest_codepage, &result, &result_len, &err)) {
    if (result) {
      OV_ARRAY_DESTROY(&result);
    }
    return gcmz_luafn_result_err(L, &err);
  }

  lua_pushlstring(L, result, result_len);

  if (result) {
    OV_ARRAY_DESTROY(&result);
  }

  return 1;
}

static int gcmz_lua_create_temp_file(lua_State *L) {
  char const *filename = luaL_checkstring(L, 1);
  if (!filename) {
    return luaL_error(L, "create_temp_file requires filename");
  }

  if (!g_lua_api_options.temp_file_provider) {
    return luaL_error(L, "create_temp_file is not available (no temp file provider configured)");
  }

  struct ov_error err = {0};
  char *dest_path = g_lua_api_options.temp_file_provider(g_lua_api_options.userdata, filename, &err);
  if (!dest_path) {
    return gcmz_luafn_result_err(L, &err);
  }

  lua_pushstring(L, dest_path);
  OV_ARRAY_DESTROY(&dest_path);
  return 1;
}

static int gcmz_lua_save_file(lua_State *L) {
  char const *src_path = luaL_checkstring(L, 1);
  char const *dest_filename = luaL_checkstring(L, 2);

  if (!src_path || !dest_filename) {
    return luaL_error(L, "save_file requires (src_path, dest_filename)");
  }

  if (!g_lua_api_options.save_path_provider) {
    return luaL_error(L, "save_file is not available (no save path provider configured)");
  }

  struct ov_error err = {0};
  char *dest_path = NULL;
  wchar_t *src_path_w = NULL;
  wchar_t *dest_path_w = NULL;
  struct ovl_file *src_file = NULL;
  struct ovl_file *dest_file = NULL;
  char *buffer = NULL;
  int result = -1;

  {
    dest_path = g_lua_api_options.save_path_provider(g_lua_api_options.userdata, dest_filename, &err);
    if (!dest_path) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    size_t const src_len = strlen(src_path);
    size_t const src_wlen = ov_utf8_to_wchar_len(src_path, src_len);
    if (src_wlen == 0) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&src_path_w, src_wlen + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_utf8_to_wchar(src_path, src_len, src_path_w, src_wlen + 1, NULL);

    size_t const dest_len = strlen(dest_path);
    size_t const dest_wlen = ov_utf8_to_wchar_len(dest_path, dest_len);
    if (dest_wlen == 0) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&dest_path_w, dest_wlen + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_utf8_to_wchar(dest_path, dest_len, dest_path_w, dest_wlen + 1, NULL);

    if (!ovl_file_open(src_path_w, &src_file, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    uint64_t file_size = 0;
    if (!ovl_file_size(src_file, &file_size, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!ovl_file_create(dest_path_w, &dest_file, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    static size_t const chunk_size = 64 * 1024;
    if (!OV_ARRAY_GROW(&buffer, chunk_size)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    uint64_t remaining = file_size;
    while (remaining > 0) {
      size_t const to_read = remaining > chunk_size ? chunk_size : (size_t)remaining;
      size_t bytes_read = 0;
      if (!ovl_file_read(src_file, buffer, to_read, &bytes_read, &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
      if (bytes_read == 0) {
        break;
      }
      size_t bytes_written = 0;
      if (!ovl_file_write(dest_file, buffer, bytes_read, &bytes_written, &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
      if (bytes_written != bytes_read) {
        OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
        goto cleanup;
      }
      remaining -= bytes_read;
    }
    lua_pushstring(L, dest_path);
  }

  result = 1;

cleanup:
  if (buffer) {
    OV_ARRAY_DESTROY(&buffer);
  }
  if (dest_file) {
    ovl_file_close(dest_file);
    dest_file = NULL;
  }
  if (src_file) {
    ovl_file_close(src_file);
    src_file = NULL;
  }
  if (dest_path_w) {
    OV_ARRAY_DESTROY(&dest_path_w);
  }
  if (src_path_w) {
    OV_ARRAY_DESTROY(&src_path_w);
  }
  if (dest_path) {
    OV_ARRAY_DESTROY(&dest_path);
  }
  return result < 0 ? gcmz_luafn_result_err(L, &err) : result;
}

static int parse_hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

static int gcmz_lua_get_versions(lua_State *L) {
  lua_createtable(L, 0, 2);

  lua_pushstring(L, "aviutl2_ver");
  lua_pushinteger(L, (lua_Integer)g_lua_api_options.aviutl2_ver);
  lua_settable(L, -3);

  lua_pushstring(L, "gcmz_ver");
  lua_pushinteger(L, (lua_Integer)g_lua_api_options.gcmz_ver);
  lua_settable(L, -3);

  return 1;
}

static int gcmz_lua_get_script_directory(lua_State *L) {
  if (!g_lua_api_options.script_dir_provider) {
    return luaL_error(L, "get_script_directory is not available (no script directory provider configured)");
  }

  struct ov_error err = {0};
  char *script_dir = NULL;
  int result = -1;

  {
    script_dir = g_lua_api_options.script_dir_provider(g_lua_api_options.userdata, &err);
    if (!script_dir) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    lua_pushstring(L, script_dir);
  }

  result = 1;

cleanup:
  if (script_dir) {
    OV_ARRAY_DESTROY(&script_dir);
  }
  return result < 0 ? gcmz_luafn_result_err(L, &err) : result;
}

static int gcmz_lua_get_media_info(lua_State *L) {
  if (!g_lua_api_options.get_media_info) {
    return luaL_error(L, "get_media_info is not available (no media info provider configured)");
  }

  char const *filepath = luaL_checkstring(L, 1);
  if (!filepath || !*filepath) {
    return luaL_error(L, "get_media_info requires filepath");
  }

  struct ov_error err = {0};
  struct gcmz_lua_api_media_info info = {0};
  int result = -1;

  {
    if (!g_lua_api_options.get_media_info(filepath, &info, g_lua_api_options.userdata, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    lua_createtable(L, 0, 5);

    // video_track_num and audio_track_num are always present (0 has meaning)
    lua_pushstring(L, "video_track_num");
    lua_pushinteger(L, info.video_track_num);
    lua_settable(L, -3);

    lua_pushstring(L, "audio_track_num");
    lua_pushinteger(L, info.audio_track_num);
    lua_settable(L, -3);

    // total_time is nil for still images (video_track_num > 0 but no duration)
    lua_pushstring(L, "total_time");
    if (info.video_track_num > 0 && info.total_time <= 0) {
      lua_pushnil(L); // still image
    } else {
      lua_pushnumber(L, info.total_time);
    }
    lua_settable(L, -3);

    // width and height are nil for audio-only files
    if (info.video_track_num > 0) {
      lua_pushstring(L, "width");
      lua_pushinteger(L, info.width);
      lua_settable(L, -3);

      lua_pushstring(L, "height");
      lua_pushinteger(L, info.height);
      lua_settable(L, -3);
    }
  }

  result = 1;

cleanup:
  return result < 0 ? gcmz_luafn_result_err(L, &err) : result;
}

// Decode EXO text field (hex-encoded UTF-16LE) to UTF-8
// EXO text format: each UTF-16LE code unit is represented as 4 hex digits (little-endian byte order)
// Example: "41004200" = "AB" (0x0041 = 'A', 0x0042 = 'B')
static int gcmz_lua_decode_exo_text(lua_State *L) {
  size_t hex_len = 0;
  char const *hex_str = luaL_checklstring(L, 1, &hex_len);
  if (!hex_str) {
    return luaL_error(L, "decode_exo_text requires hex string");
  }
  if (hex_len == 0) {
    lua_pushliteral(L, "");
    return 1;
  }

  struct ov_error err = {0};
  wchar_t *wide_chars = NULL;
  char *utf8_result = NULL;
  int result = -1;

  {
    if (hex_len % 4 != 0) {
      OV_ERROR_SET(&err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument,
                   "invalid hex string length (must be multiple of 4)");
      goto cleanup;
    }

    size_t const wide_count = hex_len / 4;
    if (!OV_ARRAY_GROW(&wide_chars, wide_count + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    // Parse hex pairs to UTF-16LE code units
    size_t actual_len = 0;
    for (size_t i = 0; i < wide_count; ++i) {
      // Parse 4 hex digits: low byte first (little-endian)
      int d0 = parse_hex_digit(hex_str[i * 4 + 0]);
      int d1 = parse_hex_digit(hex_str[i * 4 + 1]);
      int d2 = parse_hex_digit(hex_str[i * 4 + 2]);
      int d3 = parse_hex_digit(hex_str[i * 4 + 3]);

      if (d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
        OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_invalid_argument, "invalid hex character in string");
        goto cleanup;
      }

      uint8_t low_byte = (uint8_t)((d0 << 4) | d1);
      uint8_t high_byte = (uint8_t)((d2 << 4) | d3);
      wchar_t codepoint = (wchar_t)((high_byte << 8) | low_byte);

      // Stop at null terminator
      if (codepoint == 0) {
        break;
      }

      wide_chars[actual_len++] = codepoint;
    }
    wide_chars[actual_len] = L'\0';

    if (actual_len == 0) {
      lua_pushliteral(L, "");
      result = 1;
      goto cleanup;
    }
    size_t const utf8_len = ov_wchar_to_utf8_len(wide_chars, actual_len);
    if (utf8_len == 0) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&utf8_result, utf8_len + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_wchar_to_utf8(wide_chars, actual_len, utf8_result, utf8_len + 1, NULL);
    lua_pushlstring(L, utf8_result, utf8_len);
  }

  result = 1;

cleanup:
  if (utf8_result) {
    OV_ARRAY_DESTROY(&utf8_result);
  }
  if (wide_chars) {
    OV_ARRAY_DESTROY(&wide_chars);
  }
  return result < 0 ? gcmz_luafn_result_err(L, &err) : result;
}

/**
 * @brief Global Lua function: debug_print
 *
 * Outputs debug message via configured callback.
 */
static int global_lua_debug_print(lua_State *L) {
  char const *message = luaL_checkstring(L, 1);
  if (!message) {
    return 0;
  }

  if (g_lua_api_options.debug_print) {
    g_lua_api_options.debug_print(g_lua_api_options.userdata, message);
  }

  return 0;
}

// Registry keys for i18n cache
static char g_i18n_preferred_langs_key = 0; // Cached preferred LANGIDs (userdata)
static char g_i18n_toLCID_key = 0;          // Cached LocaleNameToLCID function pointer (lightuserdata)
static char g_i18n_kernel32_key = 0;        // Cached kernel32 handle (lightuserdata)

// Helper: Get LCID from locale name using Windows API
typedef LCID(WINAPI *LocaleNameToLCIDProc)(LPCWSTR lpName, DWORD dwFlags);

static size_t
choose_language(WORD const *const preferred, size_t const num_preferred, WORD const *const langs, size_t num_langs) {
  size_t candidate = 0;
  for (size_t i = 0; i < num_preferred; ++i) {
    WORD const prefered_primary = PRIMARYLANGID(preferred[i]);
    for (size_t j = 0; j < num_langs; ++j) {
      if (langs[j] == preferred[i]) {
        return j + 1;
      }
      if (!candidate && (prefered_primary == PRIMARYLANGID(langs[j]))) {
        candidate = j + 1;
      }
    }
  }
  return candidate;
}

// Structure to hold cached preferred languages
struct i18n_cache {
  size_t num_preferred;
  WORD preferred_langs[256];
};

// Initialize i18n cache (called once per Lua state)
static bool
i18n_init_cache(lua_State *L, struct i18n_cache **out_cache, LocaleNameToLCIDProc *out_toLCID, struct ov_error *err) {
  // Check if cache already exists
  lua_pushlightuserdata(L, (void *)&g_i18n_toLCID_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (!lua_isnil(L, -1)) {
    // Cache exists, retrieve values
    LocaleNameToLCIDProc toLCID = (LocaleNameToLCIDProc)lua_touserdata(L, -1);
    lua_pop(L, 1);

    lua_pushlightuserdata(L, (void *)&g_i18n_preferred_langs_key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    struct i18n_cache *cache = (struct i18n_cache *)lua_touserdata(L, -1);
    lua_pop(L, 1);

    *out_cache = cache;
    *out_toLCID = toLCID;
    return true;
  }
  lua_pop(L, 1); // Pop nil

  // Initialize cache
  wchar_t *preferred_w = NULL;
  bool result = false;

  {
    // Load kernel32.dll and get LocaleNameToLCID
    HMODULE hKernel32 = LoadLibraryW(L"kernel32.dll");
    if (!hKernel32) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    LocaleNameToLCIDProc toLCID = (LocaleNameToLCIDProc)(void *)GetProcAddress(hKernel32, "LocaleNameToLCID");
    if (!toLCID) {
      FreeLibrary(hKernel32);
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    // Store kernel32 handle in registry (prevent unloading)
    lua_pushlightuserdata(L, (void *)&g_i18n_kernel32_key);
    lua_pushlightuserdata(L, (void *)hKernel32);
    lua_rawset(L, LUA_REGISTRYINDEX);

    // Store toLCID function pointer
    lua_pushlightuserdata(L, (void *)&g_i18n_toLCID_key);
    lua_pushlightuserdata(L, (void *)toLCID);
    lua_rawset(L, LUA_REGISTRYINDEX);

    // Get preferred languages from system
    if (!mo_get_preferred_ui_languages(&preferred_w, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Create userdata for cache
    struct i18n_cache *cache = (struct i18n_cache *)lua_newuserdata(L, sizeof(struct i18n_cache));
    memset(cache, 0, sizeof(*cache));

    // Build preferred language list as LANGIDs
    for (wchar_t const *l = preferred_w; *l != L'\0'; l += wcslen(l) + 1) {
      WORD const lang = LANGIDFROMLCID(toLCID(l, 0));
      if (lang == 0) {
        continue;
      }
      if (cache->num_preferred >= 256) {
        break;
      }
      cache->preferred_langs[cache->num_preferred++] = lang;
    }

    // Store cache in registry
    lua_pushlightuserdata(L, (void *)&g_i18n_preferred_langs_key);
    lua_pushvalue(L, -2); // Push cache userdata
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1); // Pop cache userdata

    *out_cache = cache;
    *out_toLCID = toLCID;
  }

  result = true;

cleanup:
  if (preferred_w) {
    OV_ARRAY_DESTROY(&preferred_w);
  }
  return result;
}

/**
 * @brief Global Lua function: i18n
 *
 * Selects the most appropriate text from a table based on current language.
 * Table keys should be locale codes (e.g., "ja_JP", "en_US").
 * Optional second argument: preferred language code (e.g., "ja_JP") to override system preference.
 */
static int global_lua_i18n(lua_State *L) {
  if (!lua_istable(L, 1)) {
    return luaL_error(L, "i18n requires a table argument");
  }

  struct ov_error err = {0};
  wchar_t *key_w = NULL;
  wchar_t *override_lang_w = NULL;
  int result = -1;

  {
    // Get or initialize cache
    struct i18n_cache *cache = NULL;
    LocaleNameToLCIDProc toLCID = NULL;
    if (!i18n_init_cache(L, &cache, &toLCID, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Check for optional override language (second argument)
    WORD override_langs[1] = {0};
    size_t num_override = 0;
    if (lua_isstring(L, 2)) {
      char const *override_lang = lua_tostring(L, 2);
      size_t override_len = strlen(override_lang);
      if (override_len > 0 && override_len < 32) {
        size_t override_wlen = ov_utf8_to_wchar_len(override_lang, override_len);
        if (override_wlen > 0) {
          if (!OV_ARRAY_GROW(&override_lang_w, override_wlen + 1)) {
            OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
            goto cleanup;
          }
          ov_utf8_to_wchar(override_lang, override_len, override_lang_w, override_wlen + 1, NULL);
          // Replace '_' with '-' for Windows locale API
          for (size_t i = 0; i < override_wlen; ++i) {
            if (override_lang_w[i] == L'_') {
              override_lang_w[i] = L'-';
            }
          }
          WORD const lang = LANGIDFROMLCID(toLCID(override_lang_w, 0));
          if (lang != 0) {
            override_langs[0] = lang;
            num_override = 1;
          }
        }
      }
    }

    enum {
      buf_size = 256,
    };

    // Collect keys from table and convert to LANGIDs
    WORD table_langs[buf_size] = {0};
    size_t num_table_langs = 0;

    // We also need to track the actual key strings for later access
    lua_newtable(L); // Stack: tbl, [lang], keys_array
    int keys_array_idx = lua_gettop(L);

    lua_pushnil(L); // Stack: tbl, [lang], keys_array, nil
    while (lua_next(L, 1) != 0) {
      // Stack: tbl, [lang], keys_array, key, value
      lua_pop(L, 1); // Pop value, keep key

      if (lua_type(L, -1) != LUA_TSTRING) {
        continue;
      }

      if (num_table_langs >= buf_size) {
        continue;
      }

      char const *key = lua_tostring(L, -1);
      size_t key_len = strlen(key);
      if (key_len == 0 || key_len >= 32) {
        continue;
      }

      // Store key in keys_array
      lua_pushvalue(L, -1);                                     // Duplicate key
      lua_rawseti(L, keys_array_idx, (int)num_table_langs + 1); // keys_array[num_table_langs+1] = key

      // Convert key to wchar_t (replace '_' with '-' for Windows API)
      size_t key_wlen = ov_utf8_to_wchar_len(key, key_len);
      if (key_wlen == 0) {
        continue;
      }
      if (!OV_ARRAY_GROW(&key_w, key_wlen + 1)) {
        OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      ov_utf8_to_wchar(key, key_len, key_w, key_wlen + 1, NULL);

      // Replace '_' with '-' for Windows locale API
      for (size_t i = 0; i < key_wlen; ++i) {
        if (key_w[i] == L'_') {
          key_w[i] = L'-';
        }
      }

      WORD const lang = LANGIDFROMLCID(toLCID(key_w, 0));
      table_langs[num_table_langs++] = lang;
    }
    // Stack: tbl, [lang], keys_array

    if (num_table_langs == 0) {
      // No valid keys found, return nil
      lua_pushnil(L);
      result = 1;
      goto cleanup;
    }

    // Find the best match
    size_t en_us_idx = 0, first_idx = 0;
    for (size_t i = 0; i < num_table_langs && (!en_us_idx || !first_idx); ++i) {
      if (!en_us_idx && table_langs[i] == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) {
        en_us_idx = i + 1;
      }
      if (!first_idx && table_langs[i]) {
        first_idx = i + 1;
      }
    }

    // Try override language first, then fall back to system preferences
    size_t chosen_idx = 0;
    if (num_override > 0) {
      chosen_idx = choose_language(override_langs, num_override, table_langs, num_table_langs);
    }
    if (!chosen_idx) {
      chosen_idx = choose_language(cache->preferred_langs, cache->num_preferred, table_langs, num_table_langs);
    }
    if (!chosen_idx) {
      chosen_idx = en_us_idx;
    }
    if (!chosen_idx) {
      chosen_idx = first_idx;
    }

    if (!chosen_idx) {
      lua_pushnil(L);
      result = 1;
      goto cleanup;
    }

    // Get the key at chosen_idx from keys_array
    lua_rawgeti(L, keys_array_idx, (int)chosen_idx); // Stack: tbl, [lang], keys_array, chosen_key
    char const *chosen_key = lua_tostring(L, -1);

    // Get the value from original table
    lua_getfield(L, 1, chosen_key); // Stack: tbl, [lang], keys_array, chosen_key, value
  }

  result = 1;

cleanup:
  if (override_lang_w) {
    OV_ARRAY_DESTROY(&override_lang_w);
  }
  if (key_w) {
    OV_ARRAY_DESTROY(&key_w);
  }
  return result < 0 ? gcmz_luafn_result_err(L, &err) : result;
}

/**
 * @brief Register all gcmz Lua APIs to the given Lua state
 *
 * Creates a global 'gcmz' table with all available APIs.
 * NOTE: When adding or modifying API functions, please also update LUA.md
 *
 * @param L Lua state to register APIs to
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_lua_api_register(struct lua_State *const L, struct ov_error *const err) {
  if (!L) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  // Register gcmz namespace
  lua_newtable(L);
  lua_pushcfunction(L, gcmz_lua_create_temp_file);
  lua_setfield(L, -2, "create_temp_file");
  lua_pushcfunction(L, gcmz_lua_convert_encoding);
  lua_setfield(L, -2, "convert_encoding");
  lua_pushcfunction(L, gcmz_lua_decode_exo_text);
  lua_setfield(L, -2, "decode_exo_text");
  lua_pushcfunction(L, gcmz_lua_get_media_info);
  lua_setfield(L, -2, "get_media_info");
  lua_pushcfunction(L, gcmz_lua_get_project_data);
  lua_setfield(L, -2, "get_project_data");
  lua_pushcfunction(L, gcmz_lua_get_script_directory);
  lua_setfield(L, -2, "get_script_directory");
  lua_pushcfunction(L, gcmz_lua_get_versions);
  lua_setfield(L, -2, "get_versions");
  lua_pushcfunction(L, gcmz_lua_save_file);
  lua_setfield(L, -2, "save_file");
  lua_setglobal(L, "gcmz");

  // Register global helper functions
  lua_pushcfunction(L, global_lua_debug_print);
  lua_setglobal(L, "debug_print");
  lua_pushcfunction(L, global_lua_i18n);
  lua_setglobal(L, "i18n");

  return true;
}
