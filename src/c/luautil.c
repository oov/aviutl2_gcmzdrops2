#include "luautil.h"

#include <lauxlib.h>
#include <ovarray.h>
#include <ovprintf.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

static NODISCARD bool
gcmz_error_to_string(struct ov_error const *const e, char **const dest, struct ov_error *const err) {
  if (!e || !dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *dest = NULL;
  char *result = NULL;
  bool result_success = false;

  {
    // Use the built-in ov_error_to_string function from ovbase
    if (!ov_error_to_string(e, &result, true, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  *dest = result;
  result = NULL;
  result_success = true;

cleanup:
  if (result) {
    OV_ARRAY_DESTROY(&result);
  }
  return result_success;
}

int gcmz_luafn_err_(lua_State *const L, struct ov_error *const e, char const *const funcname) {
  if (!L || !funcname) {
    if (e) {
      OV_ERROR_DESTROY(e);
    }
    return lua_error(L);
  }

  struct ov_error err = {0};
  char *error_msg = NULL;

  luaL_where(L, 1);

  // Build function name part
  static char const prefix[] = "gcmz_";
  enum { prefix_len = sizeof(prefix) - 1 };
  if (strncmp(funcname, prefix, prefix_len) == 0) {
    lua_pushstring(L, "error on gcmz.");
    lua_pushstring(L, funcname + prefix_len);
  } else {
    lua_pushstring(L, "error on ");
    lua_pushstring(L, funcname);
  }
  lua_pushstring(L, "():\r\n");

  // Convert error to string
  if (!gcmz_error_to_string(e, &error_msg, &err)) {
    OV_ERROR_DESTROY(&err);
    lua_pushstring(L, "failed to build error message");
  } else {
    lua_pushstring(L, error_msg);
  }

  lua_concat(L, 5);

  // Cleanup
  if (error_msg) {
    OV_ARRAY_DESTROY(&error_msg);
  }
  OV_ERROR_DESTROY(e);

  return lua_error(L);
}

bool gcmz_lua_pcall(lua_State *const L, int nargs, int nresults, struct ov_error *const err) {
  if (!L) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  int const result = lua_pcall(L, nargs, nresults, 0);
  if (result != LUA_OK) {
    // Get error message from stack
    char const *const error_msg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown Lua error";
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, error_msg);
    lua_pop(L, 1); // Pop error message
    return false;
  }

  return true;
}
