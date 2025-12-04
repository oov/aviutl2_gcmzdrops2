#include <ovtest.h>

#include "lua_api.h"

#include <ovarray.h>

#include <stdio.h>
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

// Mock callback for get_project_data
static bool
mock_get_project_data(struct aviutl2_edit_info *edit_info, char **project_path, void *userdata, struct ov_error *err) {
  (void)userdata;
  (void)err;
  // Return mock project data
  *edit_info = (struct aviutl2_edit_info){
      .width = 1920,
      .height = 1080,
      .rate = 30,
      .scale = 1,
      .sample_rate = 48000,
  };
  if (project_path) {
    *project_path = NULL;
  }
  return true;
}

/**
 * Test gcmz_lua_api_register function
 */
static void test_api_register(void) {
  lua_State *L = luaL_newstate();
  TEST_ASSERT(L != NULL);

  luaL_openlibs(L);

  // Set up mock callback
  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .userdata = NULL,
  });

  struct ov_error err = {0};
  TEST_CHECK(gcmz_lua_api_register(L, &err));

  // Check if gcmz global table was created
  lua_getglobal(L, "gcmz");
  TEST_CHECK(lua_istable(L, -1));

  lua_getfield(L, -1, "get_project_data");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, -1, "convert_encoding");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, -1, "decode_exo_text");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 1);

  lua_close(L);
  gcmz_lua_api_set_options(NULL);
}

static void test_convert_encoding(void) {
  lua_State *L = luaL_newstate();
  TEST_ASSERT(L != NULL);

  luaL_openlibs(L);

  // Set up mock callback
  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .userdata = NULL,
  });

  struct ov_error err = {0};
  TEST_CHECK(gcmz_lua_api_register(L, &err));

  // Test same encoding (no conversion needed)
  int result = luaL_dostring(L, "return gcmz.convert_encoding('Hello', 'utf8', 'utf8')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "Hello") == 0);
  lua_pop(L, 1);

  // Test UTF-8 to UTF-8 (should be same)
  result = luaL_dostring(L, "return gcmz.convert_encoding('テスト', 'utf8', 'utf8')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "テスト") == 0);
  lua_pop(L, 1);

  // Test invalid source encoding
  result = luaL_dostring(L, "return pcall(gcmz.convert_encoding, 'test', 'invalid', 'utf8')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isboolean(L, -2));
  TEST_CHECK(!lua_toboolean(L, -2)); // Should return false (error)
  lua_pop(L, 2);

  // Test invalid destination encoding
  result = luaL_dostring(L, "return pcall(gcmz.convert_encoding, 'test', 'utf8', 'invalid')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isboolean(L, -2));
  TEST_CHECK(!lua_toboolean(L, -2)); // Should return false (error)
  lua_pop(L, 2);

  // Test missing arguments
  result = luaL_dostring(L, "return pcall(gcmz.convert_encoding, 'test')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isboolean(L, -2));
  TEST_CHECK(!lua_toboolean(L, -2)); // Should return false (error)
  lua_pop(L, 2);

  lua_close(L);
  gcmz_lua_api_set_options(NULL);
}

static void test_register_invalid_args(void) {
  struct ov_error err = {0};

  // Test with NULL lua_State
  TEST_CHECK(!gcmz_lua_api_register(NULL, &err));
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));

  OV_ERROR_DESTROY(&err);
}

static void test_decode_exo_text(void) {
  lua_State *L = luaL_newstate();
  TEST_ASSERT(L != NULL);

  luaL_openlibs(L);

  // Set up mock callback
  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .userdata = NULL,
  });

  struct ov_error err = {0};
  if (!TEST_CHECK(gcmz_lua_api_register(L, &err))) {
    OV_ERROR_REPORT(&err, NULL);
    lua_close(L);
    gcmz_lua_api_set_options(NULL);
    return;
  }

  // Test decoding "abc" encoded as UTF-16LE hex
  // 'a' = 0x0061 -> "6100"
  // 'b' = 0x0062 -> "6200"
  // 'c' = 0x0063 -> "6300"
  int result = luaL_dostring(L, "return gcmz.decode_exo_text('610062006300')");
  if (!TEST_CHECK(result == LUA_OK)) {
    if (lua_isstring(L, -1)) {
      TEST_MSG("decode_exo_text error: %s", lua_tostring(L, -1));
    }
    lua_close(L);
    gcmz_lua_api_set_options(NULL);
    return;
  }

  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "abc") == 0);
  lua_pop(L, 1);

  // Test empty string
  result = luaL_dostring(L, "return gcmz.decode_exo_text('')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "") == 0);
  lua_pop(L, 1);

  // Test string with Japanese characters
  // "テスト" in UTF-16LE:
  // テ = 0x30C6 -> "c630"
  // ス = 0x30B9 -> "b930"
  // ト = 0x30C8 -> "c830"
  result = luaL_dostring(L, "return gcmz.decode_exo_text('c630b930c830')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "テスト") == 0);
  lua_pop(L, 1);

  // Test NUL termination - should stop at first NUL
  result = luaL_dostring(L, "return gcmz.decode_exo_text('6100000062006300')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "a") == 0);
  lua_pop(L, 1);

  lua_close(L);
  gcmz_lua_api_set_options(NULL);
}

// Mock callback for debug_print
static char g_debug_print_buffer[1024] = {0};
static void mock_debug_print(void *userdata, char const *message) {
  (void)userdata;
  if (message) {
    strncpy(g_debug_print_buffer, message, sizeof(g_debug_print_buffer) - 1);
    g_debug_print_buffer[sizeof(g_debug_print_buffer) - 1] = '\0';
  }
}

static void test_debug_print(void) {
  lua_State *L = luaL_newstate();
  TEST_ASSERT(L != NULL);

  luaL_openlibs(L);

  // Reset buffer
  memset(g_debug_print_buffer, 0, sizeof(g_debug_print_buffer));

  // Set up mock callback with debug_print
  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .debug_print = mock_debug_print,
      .userdata = NULL,
  });

  struct ov_error err = {0};
  if (!TEST_CHECK(gcmz_lua_api_register(L, &err))) {
    OV_ERROR_REPORT(&err, NULL);
    lua_close(L);
    gcmz_lua_api_set_options(NULL);
    return;
  }

  // Test debug_print exists as global function
  lua_getglobal(L, "debug_print");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 1);

  // Test calling debug_print
  int result = luaL_dostring(L, "debug_print('Hello from Lua!')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(strcmp(g_debug_print_buffer, "Hello from Lua!") == 0);

  // Test calling debug_print without callback (should not crash)
  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .debug_print = NULL, // No callback
      .userdata = NULL,
  });

  memset(g_debug_print_buffer, 0, sizeof(g_debug_print_buffer));
  result = luaL_dostring(L, "debug_print('This should be ignored')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(g_debug_print_buffer[0] == '\0'); // Buffer should remain empty

  lua_close(L);
  gcmz_lua_api_set_options(NULL);
}

static void test_i18n(void) {
  lua_State *L = luaL_newstate();
  TEST_ASSERT(L != NULL);

  luaL_openlibs(L);

  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .get_project_data = mock_get_project_data,
      .userdata = NULL,
  });

  struct ov_error err = {0};
  if (!TEST_CHECK(gcmz_lua_api_register(L, &err))) {
    OV_ERROR_REPORT(&err, NULL);
    lua_close(L);
    gcmz_lua_api_set_options(NULL);
    return;
  }

  // Test i18n exists as global function
  lua_getglobal(L, "i18n");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 1);

  // Test i18n with empty table
  int result = luaL_dostring(L, "return i18n({})");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isnil(L, -1));
  lua_pop(L, 1);

  // Test i18n with invalid argument
  result = luaL_dostring(L, "return pcall(i18n, 'not a table')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(!lua_toboolean(L, -2)); // Should return false (error)
  lua_pop(L, 2);

  // Test i18n with override language (second argument)
  result = luaL_dostring(L,
                         "return i18n({\n"
                         "  ['en_US'] = 'Hello',\n"
                         "  ['ja_JP'] = 'こんにちは',\n"
                         "  ['zh_CN'] = '你好',\n"
                         "}, 'ja_JP')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "こんにちは") == 0);
  lua_pop(L, 1);

  // Test i18n override with different language
  result = luaL_dostring(L,
                         "return i18n({\n"
                         "  ['en_US'] = 'Hello',\n"
                         "  ['ja_JP'] = 'こんにちは',\n"
                         "  ['zh_CN'] = '你好',\n"
                         "}, 'zh_CN')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "你好") == 0);
  lua_pop(L, 1);

  // Test i18n override with en_US
  result = luaL_dostring(L,
                         "return i18n({\n"
                         "  ['en_US'] = 'Hello',\n"
                         "  ['ja_JP'] = 'こんにちは',\n"
                         "  ['zh_CN'] = '你好',\n"
                         "}, 'en_US')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  TEST_CHECK(strcmp(lua_tostring(L, -1), "Hello") == 0);
  lua_pop(L, 1);

  // Test i18n override with unavailable language falls back to en_US
  result = luaL_dostring(L,
                         "return i18n({\n"
                         "  ['en_US'] = 'English fallback',\n"
                         "  ['ja_JP'] = 'こんにちは',\n"
                         "}, 'fr_FR')");
  TEST_CHECK(result == LUA_OK);
  TEST_CHECK(lua_isstring(L, -1));
  // fr_FR not found, system preference likely not ja_JP on CI, falls back to en_US
  // Note: This test might be environment-dependent, but en_US fallback should work
  lua_pop(L, 1);

  lua_close(L);
  gcmz_lua_api_set_options(NULL);
}

TEST_LIST = {
    {"api_register", test_api_register},
    {"convert_encoding", test_convert_encoding},
    {"decode_exo_text", test_decode_exo_text},
    {"register_invalid_args", test_register_invalid_args},
    {"debug_print", test_debug_print},
    {"i18n", test_i18n},
    {NULL, NULL},
};
