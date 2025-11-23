#include "lua_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovarray.h>
#include <ovl/file.h>
#include <ovutf.h>

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
  char *path_utf8 = NULL;
  int result = -1;

  {
    struct gcmz_project_data data = {0};
    if (!g_lua_api_options.get_project_data(&data, g_lua_api_options.userdata, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    lua_createtable(L, 0, 10);

    lua_pushstring(L, "width");
    lua_pushinteger(L, data.width);
    lua_settable(L, -3);

    lua_pushstring(L, "height");
    lua_pushinteger(L, data.height);
    lua_settable(L, -3);

    lua_pushstring(L, "video_rate");
    lua_pushinteger(L, data.video_rate);
    lua_settable(L, -3);

    lua_pushstring(L, "video_scale");
    lua_pushinteger(L, data.video_scale);
    lua_settable(L, -3);

    lua_pushstring(L, "sample_rate");
    lua_pushinteger(L, data.sample_rate);
    lua_settable(L, -3);

    // Convert project_path to UTF-8
    if (data.project_path) {
      size_t const path_wlen = wcslen(data.project_path);
      size_t const path_len = ov_wchar_to_utf8_len(data.project_path, path_wlen);
      if (path_len > 0) {
        if (!OV_ARRAY_GROW(&path_utf8, path_len + 1)) {
          OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
          goto cleanup;
        }
        ov_wchar_to_utf8(data.project_path, path_wlen, path_utf8, path_len + 1, NULL);
        lua_pushstring(L, "project_path");
        lua_pushstring(L, path_utf8);
        lua_settable(L, -3);
      }
    }
  }

  result = 1;

cleanup:
  if (path_utf8) {
    OV_ARRAY_DESTROY(&path_utf8);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
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
    return gcmz_luafn_err(L, &err);
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
    return gcmz_luafn_err(L, &err);
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
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

static int gcmz_lua_write_file(lua_State *L) {
  char const *filepath = luaL_checkstring(L, 1);
  size_t content_len = 0;
  char const *content = luaL_checklstring(L, 2, &content_len);

  if (!filepath) {
    return luaL_error(L, "write_file requires filepath");
  }

  struct ov_error err = {0};
  wchar_t *filepath_w = NULL;
  struct ovl_file *file = NULL;
  int result = -1;

  {
    size_t const path_len = strlen(filepath);
    size_t const path_wlen = ov_utf8_to_wchar_len(filepath, path_len);
    if (path_wlen == 0) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&filepath_w, path_wlen + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_utf8_to_wchar(filepath, path_len, filepath_w, path_wlen + 1, NULL);
    if (!ovl_file_create(filepath_w, &file, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    size_t bytes_written = 0;
    if (!ovl_file_write(file, content, content_len, &bytes_written, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (bytes_written != content_len) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
      goto cleanup;
    }

    lua_pushboolean(L, 1);
  }

  result = 1;

cleanup:
  if (file) {
    ovl_file_close(file);
    file = NULL;
  }
  if (filepath_w) {
    OV_ARRAY_DESTROY(&filepath_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

static int gcmz_lua_read_file(lua_State *L) {
  char const *filepath = luaL_checkstring(L, 1);
  if (!filepath) {
    return luaL_error(L, "read_file requires filepath");
  }

  struct ov_error err = {0};
  wchar_t *filepath_w = NULL;
  struct ovl_file *file = NULL;
  char *content = NULL;
  int result = -1;

  {
    size_t const path_len = strlen(filepath);
    size_t const path_wlen = ov_utf8_to_wchar_len(filepath, path_len);
    if (path_wlen == 0) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&filepath_w, path_wlen + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_utf8_to_wchar(filepath, path_len, filepath_w, path_wlen + 1, NULL);
    if (!ovl_file_open(filepath_w, &file, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    uint64_t file_size = 0;
    if (!ovl_file_size(file, &file_size, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (file_size > 1024 * 1024 * 100) { // Limit to 100MB
      OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "file too large");
      goto cleanup;
    }
    size_t const size = (size_t)file_size;
    if (!OV_ARRAY_GROW(&content, size + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    size_t bytes_read = 0;
    if (!ovl_file_read(file, content, size, &bytes_read, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (bytes_read != size) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
      goto cleanup;
    }
    content[size] = '\0';
    lua_pushlstring(L, content, size);
  }

  result = 1;

cleanup:
  if (content) {
    OV_ARRAY_DESTROY(&content);
  }
  if (file) {
    ovl_file_close(file);
    file = NULL;
  }
  if (filepath_w) {
    OV_ARRAY_DESTROY(&filepath_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
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
  if (hex_len % 4 != 0) {
    return luaL_error(L, "invalid hex string length (must be multiple of 4)");
  }

  struct ov_error err = {0};
  wchar_t *wide_chars = NULL;
  char *utf8_result = NULL;
  int result = -1;

  {
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
        OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
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
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

bool gcmz_lua_api_register(struct lua_State *const L, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!L) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  lua_newtable(L);
  lua_pushcfunction(L, gcmz_lua_create_temp_file);
  lua_setfield(L, -2, "create_temp_file");
  lua_pushcfunction(L, gcmz_lua_convert_encoding);
  lua_setfield(L, -2, "convert_encoding");
  lua_pushcfunction(L, gcmz_lua_decode_exo_text);
  lua_setfield(L, -2, "decode_exo_text");
  lua_pushcfunction(L, gcmz_lua_get_project_data);
  lua_setfield(L, -2, "get_project_data");
  lua_pushcfunction(L, gcmz_lua_read_file);
  lua_setfield(L, -2, "read_file");
  lua_pushcfunction(L, gcmz_lua_save_file);
  lua_setfield(L, -2, "save_file");
  lua_pushcfunction(L, gcmz_lua_write_file);
  lua_setfield(L, -2, "write_file");
  lua_setglobal(L, "gcmz");
  return true;
}
