#include "lua_script_module_param.h"

#include <ovarray.h>

#include <aviutl2_module2.h>

#include <lauxlib.h>
#include <lua.h>

#include <string.h>

struct script_module_param_context {
  lua_State *L;
  int base;
  int num_args;
  int num_pushed;
  bool has_error;
  char *error_msg;
};

static struct script_module_param_context *g_ctx = NULL;

static int param_get_num(void) {
  if (!g_ctx) {
    return 0;
  }
  return g_ctx->num_args;
}

static int param_get_int(int index) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return 0;
  }
  return (int)lua_tointeger(g_ctx->L, g_ctx->base + index);
}

static double param_get_double(int index) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return 0.0;
  }
  return lua_tonumber(g_ctx->L, g_ctx->base + index);
}

static char const *param_get_string(int index) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return NULL;
  }
  return lua_tostring(g_ctx->L, g_ctx->base + index);
}

static void *param_get_data(int index) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return NULL;
  }
  if (lua_islightuserdata(g_ctx->L, g_ctx->base + index)) {
    return lua_touserdata(g_ctx->L, g_ctx->base + index);
  }
  return NULL;
}

static bool param_get_boolean(int index) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return false;
  }
  return lua_toboolean(g_ctx->L, g_ctx->base + index) != 0;
}

static int param_get_table_int(int index, char const *key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args || !key) {
    return 0;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return 0;
  }
  lua_getfield(g_ctx->L, stack_index, key);
  int const result = (int)lua_tointeger(g_ctx->L, -1);
  lua_pop(g_ctx->L, 1);
  return result;
}

static double param_get_table_double(int index, char const *key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args || !key) {
    return 0.0;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return 0.0;
  }
  lua_getfield(g_ctx->L, stack_index, key);
  double const result = lua_tonumber(g_ctx->L, -1);
  lua_pop(g_ctx->L, 1);
  return result;
}

static char const *param_get_table_string(int index, char const *key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args || !key) {
    return NULL;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return NULL;
  }
  lua_getfield(g_ctx->L, stack_index, key);
  char const *const result = lua_tostring(g_ctx->L, -1);
  lua_pop(g_ctx->L, 1);
  return result;
}

static bool param_get_table_boolean(int index, char const *key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args || !key) {
    return false;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return false;
  }
  lua_getfield(g_ctx->L, stack_index, key);
  bool const result = lua_toboolean(g_ctx->L, -1) != 0;
  lua_pop(g_ctx->L, 1);
  return result;
}

static int param_get_array_num(int index) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return 0;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return 0;
  }
  return (int)lua_objlen(g_ctx->L, stack_index);
}

static int param_get_array_int(int index, int key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return 0;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return 0;
  }
  lua_rawgeti(g_ctx->L, stack_index, key + 1); // Lua arrays are 1-based
  int const result = (int)lua_tointeger(g_ctx->L, -1);
  lua_pop(g_ctx->L, 1);
  return result;
}

static double param_get_array_double(int index, int key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return 0.0;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return 0.0;
  }
  lua_rawgeti(g_ctx->L, stack_index, key + 1);
  double const result = lua_tonumber(g_ctx->L, -1);
  lua_pop(g_ctx->L, 1);
  return result;
}

static char const *param_get_array_string(int index, int key) {
  if (!g_ctx || index < 0 || index >= g_ctx->num_args) {
    return NULL;
  }
  int const stack_index = g_ctx->base + index;
  if (!lua_istable(g_ctx->L, stack_index)) {
    return NULL;
  }
  lua_rawgeti(g_ctx->L, stack_index, key + 1);
  char const *const result = lua_tostring(g_ctx->L, -1);
  lua_pop(g_ctx->L, 1);
  return result;
}

static void param_push_int(int value) {
  if (!g_ctx) {
    return;
  }
  lua_pushinteger(g_ctx->L, value);
  g_ctx->num_pushed++;
}

static void param_push_double(double value) {
  if (!g_ctx) {
    return;
  }
  lua_pushnumber(g_ctx->L, value);
  g_ctx->num_pushed++;
}

static void param_push_string(char const *value) {
  if (!g_ctx) {
    return;
  }
  if (value) {
    lua_pushstring(g_ctx->L, value);
  } else {
    lua_pushnil(g_ctx->L);
  }
  g_ctx->num_pushed++;
}

static void param_push_data(void *value) {
  if (!g_ctx) {
    return;
  }
  lua_pushlightuserdata(g_ctx->L, value);
  g_ctx->num_pushed++;
}

static void param_push_boolean(bool value) {
  if (!g_ctx) {
    return;
  }
  lua_pushboolean(g_ctx->L, value ? 1 : 0);
  g_ctx->num_pushed++;
}

static void param_push_table_int(char const **keys, int *values, int num) {
  if (!g_ctx || !keys || !values || num <= 0) {
    return;
  }
  lua_createtable(g_ctx->L, 0, num);
  for (int i = 0; i < num; i++) {
    if (keys[i]) {
      lua_pushinteger(g_ctx->L, values[i]);
      lua_setfield(g_ctx->L, -2, keys[i]);
    }
  }
  g_ctx->num_pushed++;
}

static void param_push_table_double(char const **keys, double *values, int num) {
  if (!g_ctx || !keys || !values || num <= 0) {
    return;
  }
  lua_createtable(g_ctx->L, 0, num);
  for (int i = 0; i < num; i++) {
    if (keys[i]) {
      lua_pushnumber(g_ctx->L, values[i]);
      lua_setfield(g_ctx->L, -2, keys[i]);
    }
  }
  g_ctx->num_pushed++;
}

static void param_push_table_string(char const **keys, char const **values, int num) {
  if (!g_ctx || !keys || !values || num <= 0) {
    return;
  }
  lua_createtable(g_ctx->L, 0, num);
  for (int i = 0; i < num; i++) {
    if (keys[i]) {
      if (values[i]) {
        lua_pushstring(g_ctx->L, values[i]);
      } else {
        lua_pushnil(g_ctx->L);
      }
      lua_setfield(g_ctx->L, -2, keys[i]);
    }
  }
  g_ctx->num_pushed++;
}

static void param_push_array_int(int *values, int num) {
  if (!g_ctx || !values || num <= 0) {
    return;
  }
  lua_createtable(g_ctx->L, num, 0);
  for (int i = 0; i < num; i++) {
    lua_pushinteger(g_ctx->L, values[i]);
    lua_rawseti(g_ctx->L, -2, i + 1);
  }
  g_ctx->num_pushed++;
}

static void param_push_array_double(double *values, int num) {
  if (!g_ctx || !values || num <= 0) {
    return;
  }
  lua_createtable(g_ctx->L, num, 0);
  for (int i = 0; i < num; i++) {
    lua_pushnumber(g_ctx->L, values[i]);
    lua_rawseti(g_ctx->L, -2, i + 1);
  }
  g_ctx->num_pushed++;
}

static void param_push_array_string(char const **values, int num) {
  if (!g_ctx || !values || num <= 0) {
    return;
  }
  lua_createtable(g_ctx->L, num, 0);
  for (int i = 0; i < num; i++) {
    if (values[i]) {
      lua_pushstring(g_ctx->L, values[i]);
    } else {
      lua_pushnil(g_ctx->L);
    }
    lua_rawseti(g_ctx->L, -2, i + 1);
  }
  g_ctx->num_pushed++;
}

static void param_set_error(char const *message) {
  if (!g_ctx) {
    return;
  }
  g_ctx->has_error = true;
  if (g_ctx->error_msg) {
    OV_ARRAY_DESTROY(&g_ctx->error_msg);
  }
  if (message) {
    size_t const len = strlen(message);
    if (OV_ARRAY_GROW(&g_ctx->error_msg, len + 1)) {
      memcpy(g_ctx->error_msg, message, len + 1);
    }
  }
}

int script_module_param_call(lua_State *const L, void (*func)(struct aviutl2_script_module_param *)) {
  if (!func) {
    return luaL_error(L, "script module function is invalid");
  }

  // Set up parameter context
  struct script_module_param_context ctx = {
      .L = L,
      .base = 1, // Arguments start at index 1
      .num_args = lua_gettop(L),
      .num_pushed = 0,
      .has_error = false,
      .error_msg = NULL,
  };
  g_ctx = &ctx;

  // Build the parameter interface
  struct aviutl2_script_module_param param = {
      .get_param_num = param_get_num,
      .get_param_int = param_get_int,
      .get_param_double = param_get_double,
      .get_param_string = param_get_string,
      .get_param_data = param_get_data,
      .get_param_boolean = param_get_boolean,
      .get_param_table_int = param_get_table_int,
      .get_param_table_double = param_get_table_double,
      .get_param_table_string = param_get_table_string,
      .get_param_table_boolean = param_get_table_boolean,
      .get_param_array_num = param_get_array_num,
      .get_param_array_int = param_get_array_int,
      .get_param_array_double = param_get_array_double,
      .get_param_array_string = param_get_array_string,
      .push_result_int = param_push_int,
      .push_result_double = param_push_double,
      .push_result_string = param_push_string,
      .push_result_data = param_push_data,
      .push_result_boolean = param_push_boolean,
      .push_result_table_int = param_push_table_int,
      .push_result_table_double = param_push_table_double,
      .push_result_table_string = param_push_table_string,
      .push_result_array_int = param_push_array_int,
      .push_result_array_double = param_push_array_double,
      .push_result_array_string = param_push_array_string,
      .set_error = param_set_error,
  };

  // Call the function
  func(&param);

  int result = ctx.num_pushed;

  // Check for errors
  if (ctx.has_error) {
    if (ctx.error_msg) {
      // Copy error message to stack before destroying (luaL_error does longjmp)
      lua_pushstring(L, ctx.error_msg);
      OV_ARRAY_DESTROY(&ctx.error_msg);
      g_ctx = NULL;
      return lua_error(L);
    }
    g_ctx = NULL;
    return luaL_error(L, "script module function failed");
  }

  if (ctx.error_msg) {
    OV_ARRAY_DESTROY(&ctx.error_msg);
  }
  g_ctx = NULL;
  return result;
}
