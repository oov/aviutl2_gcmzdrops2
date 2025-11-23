#include <ovtest.h>

#include "lua_api.h"

#include <ovarray.h>

#include <stdio.h>
#include <string.h>

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
static bool mock_get_project_data(struct gcmz_project_data *project_data, void *userdata, struct ov_error *err) {
  (void)userdata;
  (void)err;
  // Return mock project data
  *project_data = (struct gcmz_project_data){
      .width = 1920,
      .height = 1080,
      .video_rate = 30,
      .video_scale = 1,
      .sample_rate = 48000,
      .audio_ch = 2,
      .cursor_frame = 0,
      .display_frame = 0,
      .display_layer = 0,
      .display_zoom = 10000,
      .project_path = NULL,
  };
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
  TEST_CHECK(gcmz_lua_api_register(L, NULL, &err));

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
  TEST_CHECK(gcmz_lua_api_register(L, NULL, &err));

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
  TEST_CHECK(!gcmz_lua_api_register(NULL, NULL, &err));
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
  if (!TEST_CHECK(gcmz_lua_api_register(L, NULL, &err))) {
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

TEST_LIST = {
    {"api_register", test_api_register},
    {"convert_encoding", test_convert_encoding},
    {"decode_exo_text", test_decode_exo_text},
    {"register_invalid_args", test_register_invalid_args},
    {NULL, NULL},
};
