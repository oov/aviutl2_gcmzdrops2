static void test_init(void);
static void test_fini(void);
#define TEST_MY_INIT test_init()
#define TEST_MY_FINI test_fini()
#include <ovtest.h>

#include "ini_reader.h"
#include "lua.h"
#include "lua_api.h"
#include "luautil.h"

#include <ovarray.h>
#include <ovl/file.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <string.h>

#include <aviutl2_plugin2.h>

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
#include <lualib.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#ifndef SOURCE_DIR
#  define SOURCE_DIR .
#endif

#define LSTR(x) L##x
#define LSTR2(x) LSTR(#x)
#define WSTRINGIZE(x) LSTR2(x)
#define STRINGIZE_HELPER(x) #x
#define STRINGIZE(x) STRINGIZE_HELPER(x)
#define TEST_PATH(relative_path) WSTRINGIZE(SOURCE_DIR) L"/test_data/exo/" relative_path
#define LUA_SCRIPT_PATH STRINGIZE(SOURCE_DIR) "/../lua"

static lua_State *g_L = NULL;

static bool
mock_get_project_data(struct aviutl2_edit_info *edit_info, char **project_path, void *userdata, struct ov_error *err) {
  (void)userdata;
  (void)err;
  *edit_info = (struct aviutl2_edit_info){0};
  if (project_path) {
    *project_path = NULL;
  }
  return true;
}

static char *mock_create_temp_file(void *userdata, char const *filename, struct ov_error *err) {
  (void)userdata;
  (void)err;
  char *path = NULL;
  char const *prefix = "test_temp_";
  size_t len = strlen(prefix) + strlen(filename) + 1;
  if (OV_ARRAY_GROW(&path, len)) {
    strcpy(path, prefix);
    strcat(path, filename);
  }
  return path;
}

static void test_init(void) {
  g_L = luaL_newstate();
  if (!g_L) {
    return;
  }
  luaL_openlibs(g_L);

  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .temp_file_provider = mock_create_temp_file,
      .aviutl2_ver = 0x02000001,
      .gcmz_ver = 0x02000001,
  });

  struct ov_error err = {0};
  if (!gcmz_lua_api_register(g_L, &err)) {
    OV_ERROR_REPORT(&err, NULL);
    lua_close(g_L);
    g_L = NULL;
    return;
  }

  if (luaL_dostring(g_L, "package.path = package.path .. ';" LUA_SCRIPT_PATH "/?.lua'") != LUA_OK) {
    lua_close(g_L);
    g_L = NULL;
    return;
  }

  if (luaL_dostring(g_L, "exo = require('exo')") != LUA_OK) {
    lua_close(g_L);
    g_L = NULL;
  }
}

static void test_fini(void) {
  if (g_L) {
    lua_close(g_L);
    g_L = NULL;
  }
  gcmz_lua_api_set_options(NULL);
}

static bool compare_sections(struct gcmz_ini_reader *want_ini,
                             char const *want_section,
                             struct gcmz_ini_reader *got_ini,
                             char const *got_section) {
  size_t want_count = gcmz_ini_reader_get_entry_count(want_ini, want_section);
  size_t got_count = gcmz_ini_reader_get_entry_count(got_ini, got_section);
  if (want_count != got_count) {
    return false;
  }

  struct gcmz_ini_iter iter = {0};
  while (gcmz_ini_reader_iter_entries(want_ini, want_section, &iter)) {
    char key[256] = {0};
    size_t key_len = iter.name_len < sizeof(key) - 1 ? iter.name_len : sizeof(key) - 1;
    memcpy(key, iter.name, key_len);

    struct gcmz_ini_value want_val = gcmz_ini_reader_get_value(want_ini, want_section, key);
    struct gcmz_ini_value got_val = gcmz_ini_reader_get_value(got_ini, got_section, key);

    if (got_val.ptr == NULL) {
      return false;
    }
    if (want_val.size != got_val.size || memcmp(want_val.ptr, got_val.ptr, want_val.size) != 0) {
      return false;
    }
  }
  return true;
}

static bool compare_ini_contents(char const *want, size_t want_len, char const *got, size_t got_len) {
  if (want_len == got_len && memcmp(want, got, want_len) == 0) {
    return true;
  }

  struct gcmz_ini_reader *want_ini = NULL;
  struct gcmz_ini_reader *got_ini = NULL;
  struct ov_error err = {0};
  bool result = false;
  bool *got_matched = NULL;

  {
    if (!gcmz_ini_reader_create(&want_ini, &err) || !gcmz_ini_reader_create(&got_ini, &err)) {
      OV_ERROR_DESTROY(&err);
      TEST_CHECK(false);
      TEST_MSG("failed to create ini readers for comparison");
      goto cleanup;
    }

    if (!gcmz_ini_reader_load_memory(want_ini, want, want_len, &err)) {
      OV_ERROR_DESTROY(&err);
      TEST_CHECK(false);
      TEST_MSG("failed to parse expected INI");
      goto cleanup;
    }

    if (!gcmz_ini_reader_load_memory(got_ini, got, got_len, &err)) {
      OV_ERROR_DESTROY(&err);
      TEST_CHECK(false);
      TEST_MSG("failed to parse converted INI");
      goto cleanup;
    }

    size_t want_count = gcmz_ini_reader_get_section_count(want_ini);
    size_t got_count = gcmz_ini_reader_get_section_count(got_ini);

    if (want_count != got_count) {
      TEST_CHECK(false);
      TEST_MSG("section count mismatch: want %zu, got %zu", want_count, got_count);
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&got_matched, got_count)) {
      TEST_CHECK(false);
      TEST_MSG("out of memory");
      goto cleanup;
    }
    memset(got_matched, 0, sizeof(bool) * got_count);

    struct gcmz_ini_iter want_iter = {0};
    while (gcmz_ini_reader_iter_sections(want_ini, &want_iter)) {
      char want_section[256] = {0};
      if (want_iter.name) {
        size_t len = want_iter.name_len < sizeof(want_section) - 1 ? want_iter.name_len : sizeof(want_section) - 1;
        memcpy(want_section, want_iter.name, len);
      }

      bool found_match = false;
      struct gcmz_ini_iter got_iter = {0};
      size_t got_idx = 0;
      while (gcmz_ini_reader_iter_sections(got_ini, &got_iter)) {
        if (got_matched[got_idx]) {
          got_idx++;
          continue;
        }

        char got_section[256] = {0};
        if (got_iter.name) {
          size_t len = got_iter.name_len < sizeof(got_section) - 1 ? got_iter.name_len : sizeof(got_section) - 1;
          memcpy(got_section, got_iter.name, len);
        }

        if (compare_sections(
                want_ini, want_iter.name ? want_section : NULL, got_ini, got_iter.name ? got_section : NULL)) {
          got_matched[got_idx] = true;
          found_match = true;
          break;
        }
        got_idx++;
      }

      if (!found_match) {
        TEST_CHECK(false);
        TEST_MSG("No matching section found for section '%s'", want_section);

        TEST_MSG("Content of want section '%s':", want_section);
        struct gcmz_ini_iter entry_iter = {0};
        while (gcmz_ini_reader_iter_entries(want_ini, want_section, &entry_iter)) {
          char key[256] = {0};
          size_t key_len = entry_iter.name_len < sizeof(key) - 1 ? entry_iter.name_len : sizeof(key) - 1;
          memcpy(key, entry_iter.name, key_len);
          struct gcmz_ini_value val = gcmz_ini_reader_get_value(want_ini, want_section, key);
          TEST_MSG("  %s = %.*s", key, (int)val.size, val.ptr);
        }

        struct gcmz_ini_iter debug_iter = {0};
        while (gcmz_ini_reader_iter_sections(got_ini, &debug_iter)) {
          char debug_section[256] = {0};
          if (debug_iter.name) {
            size_t len =
                debug_iter.name_len < sizeof(debug_section) - 1 ? debug_iter.name_len : sizeof(debug_section) - 1;
            memcpy(debug_section, debug_iter.name, len);
          }
          TEST_MSG("Available got section: '%s'", debug_section);

          struct gcmz_ini_iter got_entry_iter = {0};
          while (gcmz_ini_reader_iter_entries(got_ini, debug_section, &got_entry_iter)) {
            char key[256] = {0};
            size_t key_len = got_entry_iter.name_len < sizeof(key) - 1 ? got_entry_iter.name_len : sizeof(key) - 1;
            memcpy(key, got_entry_iter.name, key_len);
            struct gcmz_ini_value val = gcmz_ini_reader_get_value(got_ini, debug_section, key);
            TEST_MSG("  %s = %.*s", key, (int)val.size, val.ptr);
          }
        }

        goto cleanup;
      }
    }

    result = true;
  }

cleanup:
  if (got_matched) {
    OV_ARRAY_DESTROY(&got_matched);
  }
  gcmz_ini_reader_destroy(&want_ini);
  gcmz_ini_reader_destroy(&got_ini);
  return result;
}

static bool read_file(wchar_t const *path, char **data, size_t *len, struct ov_error *err) {
  struct ovl_file *file = NULL;
  char *buf = NULL;
  bool result = false;

  {
    if (!ovl_file_open(path, &file, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t size = 0;
    if (!ovl_file_size(file, &size, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&buf, (size_t)size + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t read = 0;
    if (!ovl_file_read(file, buf, (size_t)size, &read, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    buf[read] = '\0';
    *data = buf;
    *len = read;
    buf = NULL;
  }

  result = true;

cleanup:
  if (file) {
    ovl_file_close(file);
  }
  if (buf) {
    OV_ARRAY_DESTROY(&buf);
  }
  return result;
}

static void test_exo_convert(void) {
  TEST_ASSERT(g_L != NULL);

  static struct {
    wchar_t const *src;
    wchar_t const *dest;
  } const test_cases[] = {
      {TEST_PATH(L"1-src.exo"), TEST_PATH(L"1-dest.object")},
      {TEST_PATH(L"2-src.exo"), TEST_PATH(L"2-dest.object")},
  };

  for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i) {
    TEST_CASE_("%ls", test_cases[i].src);

    char *expected = NULL;
    char src_utf8[1024];
    size_t expected_len = 0;
    struct ov_error err = {0};

    {
      if (!TEST_CHECK(read_file(test_cases[i].dest, &expected, &expected_len, &err))) {
        OV_ERROR_DESTROY(&err);
        goto cleanup;
      }

      // Convert wide char path to UTF-8
      if (!ov_snprintf_char(src_utf8, sizeof(src_utf8) / sizeof(src_utf8[0]), NULL, "%ls", test_cases[i].src)) {
        TEST_MSG("Failed to convert filepath to UTF-8");
        goto cleanup;
      }

      // Create file list table: { {filepath = src_utf8} }
      lua_newtable(g_L); // files
      lua_newtable(g_L); // file entry
      lua_pushstring(g_L, "filepath");
      lua_pushstring(g_L, src_utf8);
      lua_settable(g_L, -3);
      lua_rawseti(g_L, -2, 1); // files[1] = entry

      lua_getglobal(g_L, "exo");
      lua_getfield(g_L, -1, "process_file_list");
      lua_pushvalue(g_L, -3); // push files table

      int pcall_result = lua_pcall(g_L, 1, 1, 0);
      if (!TEST_CHECK(pcall_result == LUA_OK)) {
        TEST_MSG(
            "want LUA_OK, got %d: %s", pcall_result, lua_isstring(g_L, -1) ? lua_tostring(g_L, -1) : "(not a string)");
        lua_pop(g_L, 3); // files, exo, error
        goto cleanup;
      }

      // The result is the modified files table (same as input table)
      // Get files[1].filepath
      lua_rawgeti(g_L, -1, 1); // entry
      lua_getfield(g_L, -1, "filepath");
      char const *converted_path = lua_tostring(g_L, -1);

      if (!TEST_CHECK(converted_path != NULL)) {
        TEST_MSG("converted_path is NULL");
        lua_pop(g_L, 5);
        goto cleanup;
      }

      // Read the converted file
      char *converted = NULL;
      size_t converted_len = 0;
      wchar_t *converted_path_w = NULL;

      size_t path_len = strlen(converted_path);
      size_t wlen = ov_utf8_to_wchar_len(converted_path, path_len);
      if (wlen > 0 && OV_ARRAY_GROW(&converted_path_w, wlen + 1)) {
        ov_utf8_to_wchar(converted_path, path_len, converted_path_w, wlen + 1, NULL);
        converted_path_w[wlen] = L'\0';

        if (read_file(converted_path_w, &converted, &converted_len, &err)) {
          compare_ini_contents(expected, expected_len, converted, converted_len);
          DeleteFileW(converted_path_w); // Cleanup
        } else {
          TEST_MSG("Failed to read converted file: %s", converted_path);
          OV_ERROR_REPORT(&err, NULL); // Report error if read failed
        }
        OV_ARRAY_DESTROY(&converted_path_w);
      } else {
        TEST_MSG("Failed to convert path to wchar_t");
      }

      if (converted) {
        OV_ARRAY_DESTROY(&converted);
      }

      lua_pop(g_L, 5); // filepath, entry, files, exo, result
    }

  cleanup:
    if (expected) {
      OV_ARRAY_DESTROY(&expected);
    }
  }
}

TEST_LIST = {
    {"exo_convert", test_exo_convert},
    {NULL, NULL},
};
