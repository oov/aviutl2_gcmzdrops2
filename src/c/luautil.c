#include "luautil.h"

#include <lauxlib.h>

#include <ovarray.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/file.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

int gcmz_luafn_err_(struct lua_State *const L, struct ov_error *const e, char const *const funcname) {
  if (!L || !funcname) {
    if (e) {
      OV_ERROR_DESTROY(e);
    }
    return lua_error(L);
  }

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
  if (!ov_error_to_string(e, &error_msg, true, NULL)) {
    lua_pushstring(L, "failed to build error message");
  } else {
    lua_pushstring(L, error_msg);
  }

  lua_concat(L, 5);

  if (error_msg) {
    OV_ARRAY_DESTROY(&error_msg);
  }
  OV_ERROR_DESTROY(e);
  return lua_error(L);
}

int gcmz_luafn_result_err_(struct lua_State *const L, struct ov_error *const e, char const *const funcname) {
  if (!L || !funcname) {
    if (e) {
      OV_ERROR_DESTROY(e);
    }
    lua_pushnil(L);
    lua_pushstring(L, "internal error");
    return 2;
  }

  char *error_msg = NULL;

  // Build function name part
  static char const prefix[] = "gcmz_";
  enum { prefix_len = sizeof(prefix) - 1 };
  if (strncmp(funcname, prefix, prefix_len) == 0) {
    lua_pushstring(L, "gcmz.");
    lua_pushstring(L, funcname + prefix_len);
  } else {
    lua_pushstring(L, funcname);
    lua_pushstring(L, "");
  }
  lua_pushstring(L, "(): ");

  // Convert error to string
  if (!ov_error_to_string(e, &error_msg, true, NULL)) {
    lua_pushstring(L, "failed to build error message");
  } else {
    lua_pushstring(L, error_msg);
  }

  lua_concat(L, 4);

  if (error_msg) {
    OV_ARRAY_DESTROY(&error_msg);
  }
  OV_ERROR_DESTROY(e);

  // Return (nil, errmsg)
  lua_pushnil(L);
  lua_insert(L, -2); // Move nil before error message
  return 2;
}

bool gcmz_lua_pcall(struct lua_State *const L, int nargs, int nresults, struct ov_error *const err) {
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

bool gcmz_utf8_to_wchar(char const *const src, wchar_t **const dest, struct ov_error *const err) {
  if (!dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!src || src[0] == '\0') {
    if (*dest) {
      (*dest)[0] = L'\0';
    }
    return true;
  }

  bool result = false;
  size_t const src_len = strlen(src);
  size_t const dest_len = ov_utf8_to_wchar_len(src, src_len);
  if (dest_len == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  if (!OV_ARRAY_GROW(dest, dest_len + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  if (ov_utf8_to_wchar(src, src_len, *dest, dest_len + 1, NULL) == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  result = true;

cleanup:
  if (!result && *dest) {
    OV_ARRAY_DESTROY(dest);
  }
  return result;
}

bool gcmz_wchar_to_utf8(wchar_t const *const src, char **const dest, struct ov_error *const err) {
  if (!dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!src || src[0] == L'\0') {
    if (*dest) {
      (*dest)[0] = '\0';
    }
    return true;
  }

  bool result = false;
  size_t const src_len = wcslen(src);
  size_t const dest_len = ov_wchar_to_utf8_len(src, src_len);
  if (dest_len == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  if (!OV_ARRAY_GROW(dest, dest_len + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  if (ov_wchar_to_utf8(src, src_len, *dest, dest_len + 1, NULL) == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  result = true;

cleanup:
  if (!result && *dest) {
    OV_ARRAY_DESTROY(dest);
  }
  return result;
}

struct lua_reader_state {
  struct ovl_file *file;
  char buffer[4096];
  bool first_read;
};

static char const *lua_file_reader(struct lua_State *const L, void *const ud, size_t *const size) {
  (void)L;
  struct lua_reader_state *const state = (struct lua_reader_state *)ud;
  struct ov_error err = {0};
  size_t read = 0;

  if (!ovl_file_read(state->file, state->buffer, sizeof(state->buffer), &read, &err)) {
    OV_ERROR_REPORT(&err, NULL);
    *size = 0;
    return NULL;
  }

  // Skip UTF-8 BOM on first read
  char const *buf = state->buffer;
  if (state->first_read) {
    state->first_read = false;
    if (read >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
      buf += 3;
      read -= 3;
    }
  }

  *size = read;
  return buf;
}

static bool lua_loadfile_w(struct lua_State *const L, NATIVE_CHAR const *const filepath, struct ov_error *const err) {
  if (!L || !filepath || filepath[0] == L'\0') {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_file *file = NULL;
  char *chunkname = NULL;
  bool result = false;

  {
    if (!ovl_file_open(filepath, &file, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Build chunk name "@filepath" in one allocation
    size_t const filepath_len = wcslen(filepath);
    size_t const utf8_len = ov_wchar_to_utf8_len(filepath, filepath_len);
    if (utf8_len == 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&chunkname, 1 + utf8_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    chunkname[0] = '@';
    if (ov_wchar_to_utf8(filepath, filepath_len, chunkname + 1, utf8_len + 1, NULL) == 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    struct lua_reader_state state = {
        .file = file,
        .first_read = true,
    };

    int const load_result = lua_load(L, lua_file_reader, &state, chunkname);
    if (load_result != LUA_OK) {
      char const *const lua_err = lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown Lua error";
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, lua_err);
      lua_pop(L, 1);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (chunkname) {
    OV_ARRAY_DESTROY(&chunkname);
  }
  if (file) {
    ovl_file_close(file);
    file = NULL;
  }
  return result;
}

/**
 * @brief Check if file exists using wchar_t path
 */
static bool file_exists_w(wchar_t const *const path) {
  DWORD const attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/**
 * @brief loadfile(filename [, mode [, env]]) - UTF-8 aware version
 *
 * Loads a Lua chunk from file without executing it.
 * Returns the compiled chunk as a function, or nil plus error message on failure.
 */
static int lua_loadfile_utf8(struct lua_State *L) {
  char const *const filename = luaL_optstring(L, 1, NULL);
  // mode parameter (arg 2) is ignored - we always load as text
  // env parameter (arg 3) would require setfenv which is complex

  wchar_t *filepath_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    if (!filename) {
      // No filename means load from stdin - not supported in this implementation
      lua_pushnil(L);
      lua_pushstring(L, "loading from stdin is not supported");
      result = 2;
      goto cleanup;
    }

    if (!gcmz_utf8_to_wchar(filename, &filepath_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!lua_loadfile_w(L, filepath_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    result = 1;
  }

cleanup:
  if (filepath_w) {
    OV_ARRAY_DESTROY(&filepath_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief dofile(filename) - UTF-8 aware version
 *
 * Opens the named file and executes its contents as a Lua chunk.
 */
static int lua_dofile_utf8(struct lua_State *L) {
  char const *const filename = luaL_optstring(L, 1, NULL);

  wchar_t *filepath_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    if (!filename) {
      OV_ERROR_SET(
          &err, ov_error_type_generic, ov_error_generic_invalid_argument, "loading from stdin is not supported");
      goto cleanup;
    }

    if (!gcmz_utf8_to_wchar(filename, &filepath_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!lua_loadfile_w(L, filepath_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Call the loaded chunk
    lua_call(L, 0, LUA_MULTRET);
    result = lua_gettop(L) - 1; // Return all results
  }

cleanup:
  if (filepath_w) {
    OV_ARRAY_DESTROY(&filepath_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief Convert module name to path format
 *
 * Replaces '.' with directory separator in module name.
 * If len is 0, uses strlen(modname).
 *
 * @param modname Module name
 * @param len Length of module name to convert (0 for full string)
 * @param dest [in/out] Pointer to destination buffer (can be reused)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool modname_to_path(char const *modname, size_t len, char **dest, struct ov_error *err) {
  if (!modname || !dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (len == 0) {
    len = strlen(modname);
  }
  if (!OV_ARRAY_GROW(dest, len + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    (*dest)[i] = (modname[i] == '.') ? '\\' : modname[i];
  }
  (*dest)[len] = '\0';
  return true;
}

/**
 * @brief Get package.path or package.cpath
 *
 * @param L Lua state
 * @param field "path" or "cpath"
 * @return Path string or NULL if not found (pushes error message on stack if NULL)
 */
static char const *get_package_field(struct lua_State *L, char const *field) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, field);
  char const *const path = lua_tostring(L, -1);
  if (!path) {
    lua_pushfstring(L, "\n\tno package.%s", field);
  }
  return path;
}

/**
 * @brief Search for a file in package.path/cpath pattern
 *
 * Replaces '?' with module name and checks if file exists.
 *
 * @param L Lua state
 * @param modname Module name (with '.' replaced by separator)
 * @param path_pattern Search path pattern (e.g., "?.lua;?/init.lua")
 * @param found_path [in/out] Buffer to store found path (reusable)
 * @return true if file found, false otherwise
 */
static bool search_path(struct lua_State *L, char const *modname, char const *path_pattern, wchar_t **found_path) {
  if (!path_pattern || !modname || !found_path) {
    return false;
  }

  // Build error message for "not found" paths
  luaL_Buffer tried;
  luaL_buffinit(L, &tried);

  char const *p = path_pattern;
  while (*p) {
    // Find end of this template
    char const *template_end = strchr(p, ';');
    if (!template_end) {
      template_end = p + strlen(p);
    }

    // Build the filename by replacing '?' with modname
    luaL_Buffer path_buf;
    luaL_buffinit(L, &path_buf);

    char const *q = p;
    while (q < template_end) {
      if (*q == '?') {
        luaL_addstring(&path_buf, modname);
      } else {
        luaL_addchar(&path_buf, *q);
      }
      q++;
    }
    luaL_pushresult(&path_buf);

    char const *const filepath = lua_tostring(L, -1);

    // Convert to wchar_t and check existence
    if (gcmz_utf8_to_wchar(filepath, found_path, NULL) && file_exists_w(*found_path)) {
      // Clean up: remove filepath string and tried buffer
      lua_pop(L, 1); // Pop filepath string
      luaL_pushresult(&tried);
      lua_pop(L, 1); // Discard tried buffer
      return true;
    }

    // Add to "tried" list
    luaL_addstring(&tried, "\n\tno file '");
    luaL_addstring(&tried, filepath);
    luaL_addchar(&tried, '\'');

    lua_pop(L, 1); // Pop filepath string

    // Move to next template
    p = (*template_end == ';') ? template_end + 1 : template_end;
  }

  // Push the "tried" message for error reporting
  luaL_pushresult(&tried);
  return false;
}

/**
 * @brief package.loaders[2] - Lua file searcher with UTF-8 support
 *
 * Searches for a Lua module in package.path and loads it.
 */
static int lua_searcher_utf8(struct lua_State *L) {
  char const *const modname = luaL_checkstring(L, 1);

  wchar_t *found_path = NULL;
  char *modname_path = NULL;
  char *utf8_path = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    char const *const path = get_package_field(L, "path");
    if (!path) {
      result = 1;
      goto cleanup;
    }

    if (!modname_to_path(modname, 0, &modname_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!search_path(L, modname_path, path, &found_path)) {
      result = 1;
      goto cleanup;
    }

    if (!lua_loadfile_w(L, found_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!gcmz_wchar_to_utf8(found_path, &utf8_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    lua_pushstring(L, utf8_path);

    result = 2;
  }

cleanup:
  if (found_path) {
    OV_ARRAY_DESTROY(&found_path);
  }
  if (modname_path) {
    OV_ARRAY_DESTROY(&modname_path);
  }
  if (utf8_path) {
    OV_ARRAY_DESTROY(&utf8_path);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief Build luaopen function name for a module
 *
 * Converts "foo.bar.baz" to "luaopen_foo_bar_baz"
 *
 * @param modname Module name
 * @param dest [in/out] Pointer to destination buffer (can be reused)
 * @return true on success, false on failure
 */
static bool build_luaopen_name(char const *modname, char **dest) {
  if (!modname || !dest) {
    return false;
  }
  size_t const modname_len = strlen(modname);
  // "luaopen_" + modname (with . replaced by _) + null
  if (!OV_ARRAY_GROW(dest, 8 + modname_len + 1)) {
    return false;
  }
  memcpy(*dest, "luaopen_", 8);
  for (size_t i = 0; i <= modname_len; i++) {
    char c = modname[i];
    (*dest)[8 + i] = (c == '.') ? '_' : c;
  }
  return true;
}

/**
 * @brief Registry key for loaded C module handles table
 */
static char const loaded_c_module_handles_key[] = "gcmz_loaded_c_module_handles";

/**
 * @brief Registry key for HMODULE userdata metatable
 */
static char const c_module_metatable_key[] = "gcmz_c_hmodule_mt";

/**
 * @brief __gc metamethod for HMODULE userdata
 *
 * Called when the userdata is garbage collected, frees the library.
 */
static int lua_c_handle_gc(struct lua_State *L) {
  HMODULE *handle_ptr = (HMODULE *)lua_touserdata(L, 1);
  if (handle_ptr && *handle_ptr) {
    FreeLibrary(*handle_ptr);
    *handle_ptr = NULL;
  }
  return 0;
}

/**
 * @brief Register loaded C module handle for cleanup
 *
 * Creates a full userdata for HMODULE with __gc metamethod for automatic cleanup.
 *
 * @param L Lua state
 * @param hmodule Module handle to register
 * @param modname Module name for tracking
 */
static void register_c_module_handle(struct lua_State *L, HMODULE hmodule, char const *modname) {
  if (!L || !hmodule) {
    return;
  }

  // Get loaded C handles table from registry
  lua_getfield(L, LUA_REGISTRYINDEX, loaded_c_module_handles_key);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return; // Table should exist, something went wrong
  }

  // Create full userdata to hold HMODULE
  HMODULE *handle_ptr = (HMODULE *)lua_newuserdata(L, sizeof(HMODULE));
  *handle_ptr = hmodule;

  // Set metatable with __gc
  lua_getfield(L, LUA_REGISTRYINDEX, c_module_metatable_key);
  lua_setmetatable(L, -2);

  // Store userdata in table[modname]
  lua_setfield(L, -2, modname);

  lua_pop(L, 1); // Pop loaded C handles table
}

/**
 * @brief Load C library and find luaopen function
 *
 * Common routine for loading C libraries with UTF-8 path support.
 *
 * @param L Lua state
 * @param modname Full module name (for luaopen function name)
 * @param found_path Wide-char path to the DLL
 * @param try_short_name If true, try short function name (last component) on failure
 * @param hmodule_out [out] Loaded module handle (caller must free on error)
 * @return luaopen function on success, NULL on failure (pushes error message on stack)
 */
static lua_CFunction load_c_library(
    struct lua_State *L, char const *modname, wchar_t const *found_path, bool try_short_name, HMODULE *hmodule_out) {
  char *funcname = NULL;

  HMODULE hmodule = LoadLibraryW(found_path);
  if (!hmodule) {
    DWORD const error_code = GetLastError();
    lua_pushfstring(L, "\n\terror loading module '%s': LoadLibrary failed (error %d)", modname, (int)error_code);
    return NULL;
  }

  if (!build_luaopen_name(modname, &funcname)) {
    FreeLibrary(hmodule);
    lua_pushstring(L, "\n\tout of memory");
    return NULL;
  }

  lua_CFunction luaopen_func = (lua_CFunction)(void *)GetProcAddress(hmodule, funcname);

  if (!luaopen_func && try_short_name) {
    char const *lastdot = strrchr(modname, '.');
    if (lastdot && build_luaopen_name(lastdot + 1, &funcname)) {
      luaopen_func = (lua_CFunction)(void *)GetProcAddress(hmodule, funcname);
    }
  }

  if (!luaopen_func) {
    build_luaopen_name(modname, &funcname);
    lua_pushfstring(
        L, "\n\terror loading module '%s': %s not found in DLL", modname, funcname ? funcname : "luaopen_?");
    OV_ARRAY_DESTROY(&funcname);
    FreeLibrary(hmodule);
    return NULL;
  }

  OV_ARRAY_DESTROY(&funcname);
  *hmodule_out = hmodule;
  return luaopen_func;
}

/**
 * @brief package.loaders[3] - C library searcher with UTF-8 support
 *
 * Searches for a C library in package.cpath and loads it using LoadLibraryW.
 */
static int lua_c_searcher_utf8(struct lua_State *L) {
  char const *const modname = luaL_checkstring(L, 1);

  wchar_t *found_path = NULL;
  char *modname_path = NULL;
  char *utf8_path = NULL;
  HMODULE hmodule = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    char const *const cpath = get_package_field(L, "cpath");
    if (!cpath) {
      result = 1;
      goto cleanup;
    }

    if (!modname_to_path(modname, 0, &modname_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!search_path(L, modname_path, cpath, &found_path)) {
      result = 1;
      goto cleanup;
    }

    lua_CFunction luaopen_func = load_c_library(L, modname, found_path, true, &hmodule);
    if (!luaopen_func) {
      result = 1;
      goto cleanup;
    }

    register_c_module_handle(L, hmodule, modname);
    lua_pushcfunction(L, luaopen_func);
    hmodule = NULL;

    if (!gcmz_wchar_to_utf8(found_path, &utf8_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    lua_pushstring(L, utf8_path);

    result = 2;
  }

cleanup:
  if (hmodule) {
    FreeLibrary(hmodule);
    hmodule = NULL;
  }
  if (found_path) {
    OV_ARRAY_DESTROY(&found_path);
  }
  if (modname_path) {
    OV_ARRAY_DESTROY(&modname_path);
  }
  if (utf8_path) {
    OV_ARRAY_DESTROY(&utf8_path);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief package.loaders[4] - All-in-one C library searcher with UTF-8 support
 *
 * Searches for a C submodule in an already loaded C library.
 * For example, require("a.b.c") will look for "a" library and call luaopen_a_b_c.
 */
static int lua_c_root_searcher_utf8(struct lua_State *L) {
  char const *const modname = luaL_checkstring(L, 1);

  char const *dot = strchr(modname, '.');
  if (!dot) {
    lua_pushstring(L, "\n\tno root module found");
    return 1;
  }

  wchar_t *found_path = NULL;
  char *root_path = NULL;
  char *utf8_path = NULL;
  HMODULE hmodule = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    size_t const root_len = (size_t)(dot - modname);

    char const *const cpath = get_package_field(L, "cpath");
    if (!cpath) {
      result = 1;
      goto cleanup;
    }

    if (!modname_to_path(modname, root_len, &root_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!search_path(L, root_path, cpath, &found_path)) {
      result = 1;
      goto cleanup;
    }

    // All-in-one loader doesn't try short names - it must find full luaopen_a_b_c
    lua_CFunction luaopen_func = load_c_library(L, modname, found_path, false, &hmodule);
    if (!luaopen_func) {
      result = 1;
      goto cleanup;
    }

    register_c_module_handle(L, hmodule, modname);
    lua_pushcfunction(L, luaopen_func);
    hmodule = NULL;

    if (!gcmz_wchar_to_utf8(found_path, &utf8_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    lua_pushstring(L, utf8_path);

    result = 2;
  }

cleanup:
  if (hmodule) {
    FreeLibrary(hmodule);
    hmodule = NULL;
  }
  if (found_path) {
    OV_ARRAY_DESTROY(&found_path);
  }
  if (root_path) {
    OV_ARRAY_DESTROY(&root_path);
  }
  if (utf8_path) {
    OV_ARRAY_DESTROY(&utf8_path);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief Registry key for file handle metatable
 */
static char const io_file_handle_key[] = "gcmz_io_file";

/**
 * @brief File handle type
 */
enum io_file_type {
  io_file_type_normal,
  io_file_type_popen,
};

/**
 * @brief File handle structure stored as userdata
 *
 * This structure is used for both io.open and io.popen file handles.
 * Flags are packed into a single byte using bit fields.
 */
struct io_file {
  enum io_file_type type;
  HANDLE handle;
  bool is_closed;
  bool is_read;
  bool is_write;
  bool is_binary;
  union {
    struct {
      HANDLE process_handle;
    } popen;
  } u;
};

/**
 * @brief Check if file handle is open and valid
 */
static inline bool io_file_is_open(struct io_file const *f) {
  return f && !f->is_closed && f->handle != INVALID_HANDLE_VALUE;
}

/**
 * @brief Get file handle from userdata, push nil + error message if invalid
 * @return File handle if valid and open, NULL otherwise (nil + error pushed)
 */
static struct io_file *get_file_handle_soft(struct lua_State *L, int index) {
  struct io_file *f = (struct io_file *)luaL_checkudata(L, index, io_file_handle_key);
  if (!io_file_is_open(f)) {
    lua_pushnil(L);
    lua_pushstring(L, "attempt to use a closed file");
    return NULL;
  }
  return f;
}

/**
 * @brief Check if file handle is valid and open, throw error if not
 */
static struct io_file *check_file_handle(struct lua_State *L, int index, char const *func_name) {
  struct io_file *f = (struct io_file *)luaL_checkudata(L, index, io_file_handle_key);
  if (!io_file_is_open(f)) {
    luaL_error(L, "attempt to use a closed file in %s", func_name);
    return NULL;
  }
  return f;
}

/**
 * @brief Get file handle from userdata without validity check
 */
static struct io_file *get_file_handle(struct lua_State *L, int index) {
  return (struct io_file *)luaL_testudata(L, index, io_file_handle_key);
}

/**
 * @brief file:close() method
 */
static int io_file_close(struct lua_State *L) {
  struct io_file *f = (struct io_file *)luaL_checkudata(L, 1, io_file_handle_key);

  if (!f) {
    lua_pushnil(L);
    lua_pushstring(L, "invalid file handle");
    return 2;
  }
  if (!io_file_is_open(f)) {
    lua_pushnil(L);
    lua_pushstring(L, "attempt to use a closed file");
    return 2;
  }

  if (f->type == io_file_type_normal) {
    if (!CloseHandle(f->handle)) {
      lua_pushnil(L);
      lua_pushfstring(L, "close failed (error %d)", (int)GetLastError());
      return 2;
    }
  } else {
    CloseHandle(f->handle);
    if (f->u.popen.process_handle != INVALID_HANDLE_VALUE) {
      WaitForSingleObject(f->u.popen.process_handle, INFINITE);
      CloseHandle(f->u.popen.process_handle);
      f->u.popen.process_handle = INVALID_HANDLE_VALUE;
    }
  }

  f->handle = INVALID_HANDLE_VALUE;
  f->is_closed = true;
  lua_pushboolean(L, 1);
  return 1;
}

/**
 * @brief __gc metamethod for file handle
 */
static int io_file_gc(struct lua_State *L) {
  struct io_file *f = get_file_handle(L, 1);

  if (!io_file_is_open(f)) {
    return 0;
  }

  CloseHandle(f->handle);
  f->handle = INVALID_HANDLE_VALUE;

  if (f->type == io_file_type_popen && f->u.popen.process_handle != INVALID_HANDLE_VALUE) {
    TerminateProcess(f->u.popen.process_handle, 1);
    CloseHandle(f->u.popen.process_handle);
    f->u.popen.process_handle = INVALID_HANDLE_VALUE;
  }

  f->is_closed = true;
  return 0;
}

/**
 * @brief __tostring metamethod for file handle
 */
static int io_file_tostring(struct lua_State *L) {
  struct io_file *f = get_file_handle(L, 1);

  if (!f) {
    lua_pushstring(L, "file (invalid)");
    return 1;
  }
  if (!io_file_is_open(f)) {
    lua_pushstring(L, "file (closed)");
    return 1;
  }

  if (f->type == io_file_type_popen) {
    lua_pushfstring(L, "file (popen %p)", (void *)f->handle);
  } else {
    lua_pushfstring(L, "file (%p)", (void *)f->handle);
  }
  return 1;
}

/**
 * @brief file:flush() method
 */
static int io_file_flush(struct lua_State *L) {
  struct io_file *f = check_file_handle(L, 1, "flush");
  if (!f) {
    return 2;
  }

  if (f->type == io_file_type_normal) {
    if (!FlushFileBuffers(f->handle)) {
      lua_pushnil(L);
      lua_pushfstring(L, "flush failed (error %d)", (int)GetLastError());
      return 2;
    }
  } else if (!f->is_read) {
    FlushFileBuffers(f->handle);
  }

  lua_pushboolean(L, 1);
  return 1;
}

/**
 * @brief Read a line from file
 *
 * @param f File handle
 * @param B Lua buffer for output
 * @param keep_newline Whether to keep the newline character
 * @return true if read something, false on EOF
 */
static bool read_line(struct io_file *f, luaL_Buffer *B, bool keep_newline) {
  bool has_data = false;
  char ch;
  DWORD bytes_read;

  while (ReadFile(f->handle, &ch, 1, &bytes_read, NULL) && bytes_read > 0) {
    has_data = true;
    if (ch == '\n') {
      if (keep_newline) {
        luaL_addchar(B, '\n');
      }
      break;
    }
    if (ch == '\r') {
      if (f->type == io_file_type_popen) {
        break;
      }
      char next_ch;
      DWORD next_read;
      if (ReadFile(f->handle, &next_ch, 1, &next_read, NULL) && next_read > 0) {
        if (next_ch != '\n') {
          SetFilePointer(f->handle, -1, NULL, FILE_CURRENT);
        } else if (keep_newline) {
          luaL_addchar(B, '\n');
        }
      }
      break;
    }
    luaL_addchar(B, ch);
  }

  return has_data;
}

/**
 * @brief Read entire file content
 */
static int read_all(struct lua_State *L, struct io_file *f) {
  luaL_Buffer B;
  luaL_buffinit(L, &B);

  char buffer[4096];
  DWORD bytes_read;
  while (ReadFile(f->handle, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
    luaL_addlstring(&B, buffer, bytes_read);
  }

  luaL_pushresult(&B);
  return 1;
}

/**
 * @brief Read specified number of bytes
 */
static int read_bytes(struct lua_State *L, struct io_file *f, size_t n) {
  if (n == 0) {
    // Special case: check EOF
    DWORD file_pos = SetFilePointer(f->handle, 0, NULL, FILE_CURRENT);
    DWORD file_size = GetFileSize(f->handle, NULL);
    if (file_pos >= file_size) {
      lua_pushnil(L);
    } else {
      lua_pushliteral(L, "");
    }
    return 1;
  }

  char *buffer = NULL;
  if (!OV_ARRAY_GROW(&buffer, n)) {
    lua_pushnil(L);
    lua_pushstring(L, "out of memory");
    return 2;
  }

  DWORD bytes_read;
  if (!ReadFile(f->handle, buffer, (DWORD)n, &bytes_read, NULL)) {
    OV_ARRAY_DESTROY(&buffer);
    lua_pushnil(L);
    lua_pushfstring(L, "read failed (error %d)", (int)GetLastError());
    return 2;
  }

  if (bytes_read == 0) {
    OV_ARRAY_DESTROY(&buffer);
    lua_pushnil(L);
    return 1;
  }

  lua_pushlstring(L, buffer, bytes_read);
  OV_ARRAY_DESTROY(&buffer);
  return 1;
}

/**
 * @brief Read a number from file
 */
static int read_number(struct lua_State *L, struct io_file *f) {
  luaL_Buffer B;
  luaL_buffinit(L, &B);

  char ch;
  DWORD bytes_read;
  bool has_digits = false;
  bool in_number = false;

  // Skip leading whitespace
  while (ReadFile(f->handle, &ch, 1, &bytes_read, NULL) && bytes_read > 0) {
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      continue;
    }
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
      luaL_addchar(&B, ch);
      in_number = true;
      if (ch >= '0' && ch <= '9') {
        has_digits = true;
      }
      break;
    }
    // Non-number character
    SetFilePointer(f->handle, -1, NULL, FILE_CURRENT);
    lua_pushnil(L);
    return 1;
  }

  if (!in_number) {
    lua_pushnil(L);
    return 1;
  }

  // Read rest of number
  while (ReadFile(f->handle, &ch, 1, &bytes_read, NULL) && bytes_read > 0) {
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' || ch == '-' || ch == '+') {
      luaL_addchar(&B, ch);
      if (ch >= '0' && ch <= '9') {
        has_digits = true;
      }
    } else {
      SetFilePointer(f->handle, -1, NULL, FILE_CURRENT);
      break;
    }
  }

  luaL_pushresult(&B);

  if (!has_digits) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }

  char const *str = lua_tostring(L, -1);
  lua_Number n = 0;
  if (sscanf(str, "%lf", &n) != 1) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  lua_pop(L, 1);
  lua_pushnumber(L, n);
  return 1;
}

/**
 * @brief file:read(...) method
 */
static int io_file_read(struct lua_State *L) {
  struct io_file *f = get_file_handle_soft(L, 1);
  if (!f) {
    return 2;
  }
  if (!f->is_read) {
    lua_pushnil(L);
    lua_pushstring(L, "file not opened for reading");
    return 2;
  }

  int nargs = lua_gettop(L) - 1;
  if (nargs == 0) {
    // Default: read a line
    luaL_Buffer B;
    luaL_buffinit(L, &B);
    if (read_line(f, &B, false)) {
      luaL_pushresult(&B);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }

  int nresults = 0;
  for (int i = 2; i <= nargs + 1; i++) {
    if (lua_type(L, i) == LUA_TNUMBER) {
      size_t n = (size_t)lua_tointeger(L, i);
      int r = read_bytes(L, f, n);
      nresults++;
      if (r == 2) {
        return r; // Error
      }
    } else {
      char const *fmt = luaL_checkstring(L, i);
      if (fmt[0] == '*') {
        fmt++;
      }
      switch (fmt[0]) {
      case 'n': // Number
        read_number(L, f);
        nresults++;
        break;
      case 'a': // All
        read_all(L, f);
        nresults++;
        break;
      case 'l': // Line without newline
      {
        luaL_Buffer B;
        luaL_buffinit(L, &B);
        if (read_line(f, &B, false)) {
          luaL_pushresult(&B);
        } else {
          lua_pushnil(L);
        }
        nresults++;
        break;
      }
      case 'L': // Line with newline
      {
        luaL_Buffer B;
        luaL_buffinit(L, &B);
        if (read_line(f, &B, true)) {
          luaL_pushresult(&B);
        } else {
          lua_pushnil(L);
        }
        nresults++;
        break;
      }
      default:
        luaL_argerror(L, i, "invalid format");
        break;
      }
    }
  }

  return nresults;
}

/**
 * @brief Write data to file handle
 *
 * @param f File handle
 * @param data Data to write
 * @param len Length of data
 * @return true on success, false on failure
 */
static bool write_data(struct io_file *f, void const *data, size_t len) {
  DWORD bytes_written;
  return WriteFile(f->handle, data, (DWORD)len, &bytes_written, NULL) && bytes_written == (DWORD)len;
}

/**
 * @brief file:write(...) method
 */
static int io_file_write(struct lua_State *L) {
  struct io_file *f = get_file_handle_soft(L, 1);
  if (!f) {
    return 2;
  }
  if (!f->is_write) {
    lua_pushnil(L);
    lua_pushstring(L, "file not opened for writing");
    return 2;
  }

  DWORD last_error = 0;
  int result = -1;

  int const nargs = lua_gettop(L);
  for (int i = 2; i <= nargs; i++) {
    size_t len;
    char const *str;
    if (lua_type(L, i) == LUA_TNUMBER) {
      str = lua_tolstring(L, i, &len);
    } else {
      str = luaL_checklstring(L, i, &len);
    }

    // Binary mode, popen, or no newline: direct write
    if (f->is_binary || f->type == io_file_type_popen || !memchr(str, '\n', len)) {
      if (!write_data(f, str, len)) {
        last_error = GetLastError();
        goto cleanup;
      }
      continue;
    }

    // Text mode: write with \n -> \r\n conversion
    char const *p = str;
    char const *end = str + len;
    while (p < end) {
      char const *next_nl = (char const *)memchr(p, '\n', (size_t)(end - p));
      if (!next_nl) {
        size_t remaining_len = (size_t)(end - p);
        if (remaining_len > 0 && !write_data(f, p, remaining_len)) {
          last_error = GetLastError();
          goto cleanup;
        }
        break;
      }
      ptrdiff_t chunk_len = next_nl - p;
      if (chunk_len > 0 && !write_data(f, p, (size_t)chunk_len)) {
        last_error = GetLastError();
        goto cleanup;
      }
      if (!write_data(f, "\r\n", 2)) {
        last_error = GetLastError();
        goto cleanup;
      }
      p = next_nl + 1;
    }
  }

  lua_pushvalue(L, 1); // Return the file handle for chaining

  result = 1;

cleanup:
  if (result < 0) {
    lua_pushnil(L);
    lua_pushfstring(L, "write failed (error %d)", (int)last_error);
    return 2;
  }
  return result;
}

/**
 * @brief file:seek([whence [, offset]]) method
 */
static int io_file_seek(struct lua_State *L) {
  struct io_file *f = check_file_handle(L, 1, "seek");
  if (!f) {
    return 2;
  }

  static char const *const mode_names[] = {"set", "cur", "end", NULL};
  static DWORD const modes[] = {FILE_BEGIN, FILE_CURRENT, FILE_END};

  int op = luaL_checkoption(L, 2, "cur", mode_names);
  lua_Integer offset = luaL_optinteger(L, 3, 0);

  LARGE_INTEGER li;
  li.QuadPart = offset;
  LARGE_INTEGER new_pos;

  if (!SetFilePointerEx(f->handle, li, &new_pos, modes[op])) {
    lua_pushnil(L);
    lua_pushfstring(L, "seek failed (error %d)", (int)GetLastError());
    return 2;
  }

  lua_pushinteger(L, (lua_Integer)new_pos.QuadPart);
  return 1;
}

/**
 * @brief file:setvbuf(mode [, size]) method
 *
 * This is a stub implementation since we use Win32 file handles directly.
 */
static int io_file_setvbuf(struct lua_State *L) {
  (void)check_file_handle(L, 1, "setvbuf");
  // Always returns success - buffering is handled by Windows
  lua_pushboolean(L, 1);
  return 1;
}

/**
 * @brief file:lines() iterator function
 */
static int io_file_lines_iterator(struct lua_State *L) {
  struct io_file *f = (struct io_file *)lua_touserdata(L, lua_upvalueindex(1));
  if (!io_file_is_open(f)) {
    return 0;
  }

  luaL_Buffer B;
  luaL_buffinit(L, &B);
  if (read_line(f, &B, false)) {
    luaL_pushresult(&B);
    return 1;
  }

  return 0;
}

/**
 * @brief file:lines() method
 */
static int io_file_lines(struct lua_State *L) {
  check_file_handle(L, 1, "lines");
  lua_pushvalue(L, 1);
  lua_pushcclosure(L, io_file_lines_iterator, 1);
  return 1;
}

/**
 * @brief Create a new file handle userdata
 */
static struct io_file *create_file_handle(struct lua_State *L, HANDLE h, bool is_read, bool is_write, bool is_binary) {
  struct io_file *f = (struct io_file *)lua_newuserdata(L, sizeof(struct io_file));
  f->type = io_file_type_normal;
  f->handle = h;
  f->is_closed = false;
  f->is_read = is_read;
  f->is_write = is_write;
  f->is_binary = is_binary;

  luaL_getmetatable(L, io_file_handle_key);
  lua_setmetatable(L, -2);

  return f;
}

/**
 * @brief Parse mode string for io.open
 *
 * @param mode Mode string
 * @param access [out] GENERIC_READ, GENERIC_WRITE, or both
 * @param creation [out] CREATE_ALWAYS, OPEN_EXISTING, etc.
 * @param is_read [out] Whether reading is allowed
 * @param is_write [out] Whether writing is allowed
 * @param is_append [out] Whether to append
 * @return true if mode is valid, false otherwise
 */
static bool parse_open_mode(
    char const *mode, DWORD *access, DWORD *creation, bool *is_read, bool *is_write, bool *is_append, bool *is_binary) {
  if (!mode || mode[0] == '\0') {
    return false;
  }

  *access = 0;
  *creation = 0;
  *is_read = false;
  *is_write = false;
  *is_append = false;

  bool has_plus = strchr(mode, '+') != NULL;
  bool has_b = strchr(mode, 'b') != NULL; // Binary mode
  *is_binary = has_b;

  switch (mode[0]) {
  case 'r':
    *is_read = true;
    if (has_plus) {
      *is_write = true;
      *access = GENERIC_READ | GENERIC_WRITE;
      *creation = OPEN_EXISTING;
    } else {
      *access = GENERIC_READ;
      *creation = OPEN_EXISTING;
    }
    break;
  case 'w':
    *is_write = true;
    if (has_plus) {
      *is_read = true;
      *access = GENERIC_READ | GENERIC_WRITE;
    } else {
      *access = GENERIC_WRITE;
    }
    *creation = CREATE_ALWAYS;
    break;
  case 'a':
    *is_write = true;
    *is_append = true;
    if (has_plus) {
      *is_read = true;
      *access = GENERIC_READ | GENERIC_WRITE;
    } else {
      *access = GENERIC_WRITE;
    }
    *creation = OPEN_ALWAYS;
    break;
  default:
    return false;
  }

  return true;
}

/**
 * @brief io.open(filename [, mode]) - UTF-8 aware version
 */
static int io_open_utf8(struct lua_State *L) {
  char const *const filename = luaL_checkstring(L, 1);
  char const *const mode = luaL_optstring(L, 2, "r");

  wchar_t *filepath_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    DWORD access, creation;
    bool is_read, is_write, is_append, is_binary;
    if (!parse_open_mode(mode, &access, &creation, &is_read, &is_write, &is_append, &is_binary)) {
      lua_pushnil(L);
      lua_pushstring(L, "invalid mode");
      result = 2;
      goto cleanup;
    }

    if (!gcmz_utf8_to_wchar(filename, &filepath_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    HANDLE h = CreateFileW(
        filepath_w, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
      DWORD error_code = GetLastError();
      lua_pushnil(L);
      if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND) {
        lua_pushfstring(L, "%s: No such file or directory", filename);
      } else if (error_code == ERROR_ACCESS_DENIED) {
        lua_pushfstring(L, "%s: Permission denied", filename);
      } else {
        lua_pushfstring(L, "%s: Cannot open file (error %d)", filename, (int)error_code);
      }
      result = 2;
      goto cleanup;
    }

    if (is_append) {
      SetFilePointer(h, 0, NULL, FILE_END);
    }

    create_file_handle(L, h, is_read, is_write, is_binary);
    result = 1;
  }

cleanup:
  if (filepath_w) {
    OV_ARRAY_DESTROY(&filepath_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief Store for io.input and io.output defaults
 */
static char const io_input_key[] = "gcmz_io_input";
static char const io_output_key[] = "gcmz_io_output";

/**
 * @brief io.input([file]) - UTF-8 aware version
 */
static int io_input_utf8(struct lua_State *L) {
  if (lua_gettop(L) == 0) {
    // Return current default input
    lua_getfield(L, LUA_REGISTRYINDEX, io_input_key);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushnil(L);
      lua_pushstring(L, "no default input file");
      return 2;
    }
    return 1;
  }

  if (lua_isstring(L, 1)) {
    // Open file for reading
    lua_pushcfunction(L, io_open_utf8);
    lua_pushvalue(L, 1);
    lua_pushliteral(L, "r");
    lua_call(L, 2, 2);
    if (lua_isnil(L, -2)) {
      return 2; // Return nil, error_message
    }
    lua_pop(L, 1); // Pop nil (second return value)
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, io_input_key);
    return 1;
  }

  // Set file as default input
  luaL_checkudata(L, 1, io_file_handle_key);
  lua_pushvalue(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, io_input_key);
  lua_pushvalue(L, 1);
  return 1;
}

/**
 * @brief io.output([file]) - UTF-8 aware version
 */
static int io_output_utf8(struct lua_State *L) {
  if (lua_gettop(L) == 0) {
    // Return current default output
    lua_getfield(L, LUA_REGISTRYINDEX, io_output_key);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushnil(L);
      lua_pushstring(L, "no default output file");
      return 2;
    }
    return 1;
  }

  if (lua_isstring(L, 1)) {
    // Open file for writing
    lua_pushcfunction(L, io_open_utf8);
    lua_pushvalue(L, 1);
    lua_pushliteral(L, "w");
    lua_call(L, 2, 2);
    if (lua_isnil(L, -2)) {
      return 2; // Return nil, error_message
    }
    lua_pop(L, 1); // Pop nil
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, io_output_key);
    return 1;
  }

  // Set file as default output
  luaL_checkudata(L, 1, io_file_handle_key);
  lua_pushvalue(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, io_output_key);
  lua_pushvalue(L, 1);
  return 1;
}

/**
 * @brief io.close([file]) - UTF-8 aware version
 */
static int io_close_utf8(struct lua_State *L) {
  if (lua_gettop(L) == 0) {
    // No arguments - close default output
    lua_getfield(L, LUA_REGISTRYINDEX, io_output_key);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushnil(L);
      lua_pushstring(L, "no default output file to close");
      return 2;
    }
    // Stack now has [default_output] at position 1, continue to io_file_close
  } else if (lua_isnil(L, 1)) {
    // First argument is nil - close default output
    lua_getfield(L, LUA_REGISTRYINDEX, io_output_key);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushnil(L);
      lua_pushstring(L, "no default output file to close");
      return 2;
    }
    // Stack now has [nil, default_output], remove nil
    lua_remove(L, 1);
  }
  // Position 1 should now have the file handle
  return io_file_close(L);
}

/**
 * @brief io.flush() - UTF-8 aware version
 */
static int io_flush_utf8(struct lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, io_output_key);
  if (lua_isnil(L, -1)) {
    lua_pushnil(L);
    lua_pushstring(L, "no default output file");
    return 2;
  }
  lua_insert(L, 1);
  return io_file_flush(L);
}

/**
 * @brief io.read(...) - UTF-8 aware version
 */
static int io_read_utf8(struct lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, io_input_key);
  if (lua_isnil(L, -1)) {
    return luaL_error(L, "no default input file");
  }
  // Check if the file is closed
  struct io_file *f = (struct io_file *)luaL_testudata(L, -1, io_file_handle_key);
  if (!io_file_is_open(f)) {
    return luaL_error(L, "default input file is closed");
  }
  lua_insert(L, 1);
  return io_file_read(L);
}

/**
 * @brief io.write(...) - UTF-8 aware version
 */
static int io_write_utf8(struct lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, io_output_key);
  if (lua_isnil(L, -1)) {
    return luaL_error(L, "no default output file");
  }
  // Check if the file is closed
  struct io_file *f = (struct io_file *)luaL_testudata(L, -1, io_file_handle_key);
  if (!io_file_is_open(f)) {
    return luaL_error(L, "default output file is closed");
  }
  lua_insert(L, 1);
  return io_file_write(L);
}

/**
 * @brief io.lines iterator state for file path version
 */
struct io_lines_state {
  HANDLE handle;
  bool should_close;
};

/**
 * @brief __gc for io.lines state
 */
static int io_lines_state_gc(struct lua_State *L) {
  struct io_lines_state *state = (struct io_lines_state *)lua_touserdata(L, 1);
  if (state && state->should_close && state->handle != INVALID_HANDLE_VALUE) {
    CloseHandle(state->handle);
    state->handle = INVALID_HANDLE_VALUE;
  }
  return 0;
}

/**
 * @brief Iterator function for io.lines(filename)
 */
static int io_lines_file_iterator(struct lua_State *L) {
  struct io_lines_state *state = (struct io_lines_state *)lua_touserdata(L, lua_upvalueindex(1));
  if (!state || state->handle == INVALID_HANDLE_VALUE) {
    return 0;
  }

  luaL_Buffer B;
  luaL_buffinit(L, &B);

  // Create temporary file struct for read_line
  struct io_file f = {
      .handle = state->handle,
      .is_closed = false,
      .is_read = true,
      .is_write = false,
  };

  if (read_line(&f, &B, false)) {
    luaL_pushresult(&B);
    return 1;
  }

  // EOF - close file if we own it
  if (state->should_close) {
    CloseHandle(state->handle);
    state->handle = INVALID_HANDLE_VALUE;
  }

  return 0;
}

/**
 * @brief io.lines([filename]) - UTF-8 aware version
 */
static int io_lines_utf8(struct lua_State *L) {
  if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
    // Use default input
    lua_getfield(L, LUA_REGISTRYINDEX, io_input_key);
    if (lua_isnil(L, -1)) {
      return luaL_error(L, "no default input file");
    }
    return io_file_lines(L);
  }

  char const *const filename = luaL_checkstring(L, 1);
  wchar_t *filepath_w = NULL;
  struct ov_error err = {0};

  if (!gcmz_utf8_to_wchar(filename, &filepath_w, &err)) {
    OV_ARRAY_DESTROY(&filepath_w);
    return gcmz_luafn_err(L, &err);
  }

  HANDLE h = CreateFileW(filepath_w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  OV_ARRAY_DESTROY(&filepath_w);

  if (h == INVALID_HANDLE_VALUE) {
    DWORD error_code = GetLastError();
    if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND) {
      return luaL_error(L, "%s: No such file or directory", filename);
    }
    return luaL_error(L, "%s: Cannot open file (error %d)", filename, (int)error_code);
  }

  // Create state userdata
  struct io_lines_state *state = (struct io_lines_state *)lua_newuserdata(L, sizeof(struct io_lines_state));
  state->handle = h;
  state->should_close = true;

  // Set metatable with __gc
  if (luaL_newmetatable(L, "gcmz_io_lines_state")) {
    lua_pushcfunction(L, io_lines_state_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);

  lua_pushcclosure(L, io_lines_file_iterator, 1);
  return 1;
}

/**
 * @brief Setup file handle metatable
 */
static void setup_io_file_metatable(struct lua_State *L) {
  luaL_newmetatable(L, io_file_handle_key);

  // Methods
  lua_pushcfunction(L, io_file_close);
  lua_setfield(L, -2, "close");
  lua_pushcfunction(L, io_file_flush);
  lua_setfield(L, -2, "flush");
  lua_pushcfunction(L, io_file_lines);
  lua_setfield(L, -2, "lines");
  lua_pushcfunction(L, io_file_read);
  lua_setfield(L, -2, "read");
  lua_pushcfunction(L, io_file_seek);
  lua_setfield(L, -2, "seek");
  lua_pushcfunction(L, io_file_setvbuf);
  lua_setfield(L, -2, "setvbuf");
  lua_pushcfunction(L, io_file_write);
  lua_setfield(L, -2, "write");

  // Metamethods
  lua_pushcfunction(L, io_file_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, io_file_tostring);
  lua_setfield(L, -2, "__tostring");

  // __index = self (methods accessible via obj:method())
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");

  lua_pop(L, 1);
}

/**
 * @brief io.type(obj) - UTF-8 aware version
 *
 * Returns "file" for open file handles, "closed file" for closed handles, nil otherwise.
 */
static int io_type_utf8(struct lua_State *L) {
  luaL_checkany(L, 1);

  struct io_file *f = get_file_handle(L, 1);
  if (f) {
    if (io_file_is_open(f)) {
      lua_pushliteral(L, "file");
    } else {
      lua_pushliteral(L, "closed file");
    }
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

/**
 * @brief io.popen(prog [, mode]) - UTF-8 aware version
 *
 * Opens a process and returns a file handle for reading or writing.
 */
static int io_popen_utf8(struct lua_State *L) {
  char const *const prog = luaL_checkstring(L, 1);
  char const *const mode = luaL_optstring(L, 2, "r");

  wchar_t *prog_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    bool is_read = (mode[0] == 'r');

    if (!gcmz_utf8_to_wchar(prog, &prog_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Create pipe
    SECURITY_ATTRIBUTES sa = {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .bInheritHandle = TRUE,
        .lpSecurityDescriptor = NULL,
    };

    HANDLE read_pipe = INVALID_HANDLE_VALUE;
    HANDLE write_pipe = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
      lua_pushnil(L);
      lua_pushfstring(L, "CreatePipe failed (error %d)", (int)GetLastError());
      result = 2;
      goto cleanup;
    }

    // Make one end of the pipe non-inheritable
    HANDLE our_pipe;
    HANDLE child_pipe;
    if (is_read) {
      our_pipe = read_pipe;
      child_pipe = write_pipe;
      SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    } else {
      our_pipe = write_pipe;
      child_pipe = read_pipe;
      SetHandleInformation(write_pipe, HANDLE_FLAG_INHERIT, 0);
    }

    // Build command line with cmd.exe /c
    size_t const cmd_prefix_len = wcslen(L"cmd.exe /c ");
    size_t const prog_w_len = wcslen(prog_w);
    wchar_t *cmdline = NULL;
    if (!OV_ARRAY_GROW(&cmdline, cmd_prefix_len + prog_w_len + 1)) {
      CloseHandle(read_pipe);
      CloseHandle(write_pipe);
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(cmdline, L"cmd.exe /c ");
    wcscat(cmdline, prog_w);

    STARTUPINFOW si = {
        .cb = sizeof(STARTUPINFOW),
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = is_read ? GetStdHandle(STD_INPUT_HANDLE) : child_pipe,
        .hStdOutput = is_read ? child_pipe : GetStdHandle(STD_OUTPUT_HANDLE),
        .hStdError = GetStdHandle(STD_ERROR_HANDLE),
    };

    PROCESS_INFORMATION pi = {0};

    BOOL created = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    OV_ARRAY_DESTROY(&cmdline);
    CloseHandle(child_pipe);

    if (!created) {
      DWORD error_code = GetLastError();
      CloseHandle(our_pipe);
      lua_pushnil(L);
      lua_pushfstring(L, "CreateProcess failed (error %d)", (int)error_code);
      result = 2;
      goto cleanup;
    }

    CloseHandle(pi.hThread);

    // Create popen file userdata
    struct io_file *f = (struct io_file *)lua_newuserdata(L, sizeof(struct io_file));
    f->type = io_file_type_popen;
    f->handle = our_pipe;
    f->is_closed = false;
    f->is_read = is_read;
    f->is_write = !is_read;
    f->is_binary = false;
    f->u.popen.process_handle = pi.hProcess;

    luaL_getmetatable(L, io_file_handle_key);
    lua_setmetatable(L, -2);

    result = 1;
  }

cleanup:
  if (prog_w) {
    OV_ARRAY_DESTROY(&prog_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief io.tmpfile() - UTF-8 aware version
 *
 * Creates a temporary file that is automatically deleted when closed.
 */
static int io_tmpfile_utf8(struct lua_State *L) {
  wchar_t temp_path[MAX_PATH];
  wchar_t temp_file[MAX_PATH];

  if (GetTempPathW(MAX_PATH, temp_path) == 0) {
    lua_pushnil(L);
    lua_pushfstring(L, "GetTempPath failed (error %d)", (int)GetLastError());
    return 2;
  }

  if (GetTempFileNameW(temp_path, L"lua", 0, temp_file) == 0) {
    lua_pushnil(L);
    lua_pushfstring(L, "GetTempFileName failed (error %d)", (int)GetLastError());
    return 2;
  }

  HANDLE h = CreateFileW(temp_file,
                         GENERIC_READ | GENERIC_WRITE,
                         0, // No sharing
                         NULL,
                         CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                         NULL);
  if (h == INVALID_HANDLE_VALUE) {
    DeleteFileW(temp_file);
    lua_pushnil(L);
    lua_pushfstring(L, "CreateFile failed (error %d)", (int)GetLastError());
    return 2;
  }

  create_file_handle(L, h, true, true, true);
  return 1;
}

/**
 * @brief os.clock() - Returns CPU time used by the program
 *
 * Returns an approximation of the amount of CPU time used by the program, in seconds.
 */
static int os_clock_impl(struct lua_State *L) {
  clock_t const c = clock();
  lua_pushnumber(L, (lua_Number)c / (lua_Number)CLOCKS_PER_SEC);
  return 1;
}

/**
 * @brief os.time([table]) - Returns current time or time from table
 *
 * Returns the current time when called without arguments, or a time representing
 * the date and time specified by the given table.
 */
static int os_time_impl(struct lua_State *L) {
  time_t t;
  if (lua_isnoneornil(L, 1)) {
    t = time(NULL);
  } else {
    struct tm ts;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "sec");
    ts.tm_sec = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "min");
    ts.tm_min = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "hour");
    ts.tm_hour = (int)luaL_optinteger(L, -1, 12);
    lua_pop(L, 1);
    lua_getfield(L, 1, "day");
    ts.tm_mday = (int)luaL_optinteger(L, -1, 1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "month");
    ts.tm_mon = (int)luaL_optinteger(L, -1, 1) - 1;
    lua_pop(L, 1);
    lua_getfield(L, 1, "year");
    ts.tm_year = (int)luaL_optinteger(L, -1, 1900) - 1900;
    lua_pop(L, 1);
    lua_getfield(L, 1, "isdst");
    ts.tm_isdst = lua_isnil(L, -1) ? -1 : (lua_toboolean(L, -1) ? 1 : 0);
    lua_pop(L, 1);
    t = mktime(&ts);
    if (t == (time_t)(-1)) {
      return luaL_error(L, "time result cannot be represented in this installation");
    }
  }
  lua_pushnumber(L, (lua_Number)t);
  return 1;
}

/**
 * @brief os.difftime(t2, t1) - Returns the difference in seconds between two times
 */
static int os_difftime_impl(struct lua_State *L) {
  lua_Number const t1 = luaL_checknumber(L, 1);
  lua_Number const t2 = luaL_optnumber(L, 2, 0);
  lua_pushnumber(L, difftime((time_t)t1, (time_t)t2));
  return 1;
}

/**
 * @brief Push tm fields to a table on the stack
 */
static void push_tm_table(struct lua_State *L, struct tm const *ts, int isdst) {
  lua_createtable(L, 0, 9);
  lua_pushinteger(L, ts->tm_sec);
  lua_setfield(L, -2, "sec");
  lua_pushinteger(L, ts->tm_min);
  lua_setfield(L, -2, "min");
  lua_pushinteger(L, ts->tm_hour);
  lua_setfield(L, -2, "hour");
  lua_pushinteger(L, ts->tm_mday);
  lua_setfield(L, -2, "day");
  lua_pushinteger(L, ts->tm_mon + 1);
  lua_setfield(L, -2, "month");
  lua_pushinteger(L, ts->tm_year + 1900);
  lua_setfield(L, -2, "year");
  lua_pushinteger(L, ts->tm_wday + 1);
  lua_setfield(L, -2, "wday");
  lua_pushinteger(L, ts->tm_yday + 1);
  lua_setfield(L, -2, "yday");
  lua_pushboolean(L, isdst);
  lua_setfield(L, -2, "isdst");
}

/**
 * @brief os.date([format [, time]]) - Returns formatted date/time string or table
 */
static int os_date_impl(struct lua_State *L) {
  char const *fmt = luaL_optstring(L, 1, "%c");
  time_t t;
  if (lua_isnoneornil(L, 2)) {
    t = time(NULL);
  } else {
    lua_Number const n = lua_tonumber(L, 2);
    t = (time_t)n;
  }
  struct tm *ts;
  bool use_utc = false;

  if (*fmt == '!') {
    use_utc = true;
    fmt++;
  }

  ts = use_utc ? gmtime(&t) : localtime(&t);
  if (ts == NULL) {
    return luaL_error(L, "date result cannot be represented in this installation");
  }

  if (strcmp(fmt, "*t") == 0) {
    push_tm_table(L, ts, ts->tm_isdst);
    return 1;
  }

  // Format string
  char buf[256];
  size_t const len = strftime(buf, sizeof(buf), fmt, ts);
  if (len == 0) {
    return luaL_error(L, "date format too long");
  }
  lua_pushlstring(L, buf, len);
  return 1;
}

/**
 * @brief os.exit([code [, close]]) - Terminates the host program
 *
 * Note: This is disabled for safety in plugin environment.
 */
static int os_exit_impl(struct lua_State *L) {
  (void)L;
  return luaL_error(L, "os.exit is disabled in plugin environment");
}

/**
 * @brief os.execute([command]) - UTF-8 aware version
 *
 * Executes a shell command. Returns true/nil, "exit"/"signal", exit_code.
 */
static int os_execute_utf8(struct lua_State *L) {
  char const *const cmd = luaL_optstring(L, 1, NULL);

  if (cmd == NULL) {
    // Check if shell is available
    lua_pushboolean(L, 1);
    return 1;
  }

  wchar_t *cmd_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    if (!gcmz_utf8_to_wchar(cmd, &cmd_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Build command line
    size_t const cmd_prefix_len = wcslen(L"cmd.exe /c ");
    size_t const cmd_w_len = wcslen(cmd_w);
    wchar_t *cmdline = NULL;
    if (!OV_ARRAY_GROW(&cmdline, cmd_prefix_len + cmd_w_len + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(cmdline, L"cmd.exe /c ");
    wcscat(cmdline, cmd_w);

    STARTUPINFOW si = {.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION pi = {0};

    BOOL created = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    OV_ARRAY_DESTROY(&cmdline);

    if (!created) {
      lua_pushnil(L);
      lua_pushstring(L, "exit");
      lua_pushinteger(L, -1);
      result = 3;
      goto cleanup;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    lua_pushboolean(L, exit_code == 0);
    lua_pushstring(L, "exit");
    lua_pushinteger(L, (lua_Integer)exit_code);
    result = 3;
  }

cleanup:
  if (cmd_w) {
    OV_ARRAY_DESTROY(&cmd_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief os.remove(filename) - UTF-8 aware version
 *
 * Deletes a file or empty directory.
 */
static int os_remove_utf8(struct lua_State *L) {
  char const *const filename = luaL_checkstring(L, 1);

  wchar_t *filename_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    if (!gcmz_utf8_to_wchar(filename, &filename_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Try to delete as file first
    if (DeleteFileW(filename_w)) {
      lua_pushboolean(L, 1);
      result = 1;
      goto cleanup;
    }

    DWORD error_code = GetLastError();

    // If it's a directory, try RemoveDirectory
    if (error_code == ERROR_ACCESS_DENIED) {
      DWORD attrs = GetFileAttributesW(filename_w);
      if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (RemoveDirectoryW(filename_w)) {
          lua_pushboolean(L, 1);
          result = 1;
          goto cleanup;
        }
        error_code = GetLastError();
      }
    }

    // Return nil, error_message
    lua_pushnil(L);
    switch (error_code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      lua_pushfstring(L, "%s: No such file or directory", filename);
      break;
    case ERROR_ACCESS_DENIED:
      lua_pushfstring(L, "%s: Permission denied", filename);
      break;
    case ERROR_DIR_NOT_EMPTY:
      lua_pushfstring(L, "%s: Directory not empty", filename);
      break;
    default:
      lua_pushfstring(L, "%s: Cannot remove (error %d)", filename, (int)error_code);
      break;
    }
    result = 2;
  }

cleanup:
  if (filename_w) {
    OV_ARRAY_DESTROY(&filename_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief os.rename(oldname, newname) - UTF-8 aware version
 *
 * Renames/moves a file or directory.
 */
static int os_rename_utf8(struct lua_State *L) {
  char const *const oldname = luaL_checkstring(L, 1);
  char const *const newname = luaL_checkstring(L, 2);

  wchar_t *oldname_w = NULL;
  wchar_t *newname_w = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    if (!gcmz_utf8_to_wchar(oldname, &oldname_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_utf8_to_wchar(newname, &newname_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (MoveFileW(oldname_w, newname_w)) {
      lua_pushboolean(L, 1);
      result = 1;
      goto cleanup;
    }

    DWORD error_code = GetLastError();

    lua_pushnil(L);
    switch (error_code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      lua_pushfstring(L, "%s: No such file or directory", oldname);
      break;
    case ERROR_ACCESS_DENIED:
      lua_pushfstring(L, "%s: Permission denied", oldname);
      break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
      lua_pushfstring(L, "%s: File exists", newname);
      break;
    default:
      lua_pushfstring(L, "%s: Cannot rename to %s (error %d)", oldname, newname, (int)error_code);
      break;
    }
    result = 2;
  }

cleanup:
  if (oldname_w) {
    OV_ARRAY_DESTROY(&oldname_w);
  }
  if (newname_w) {
    OV_ARRAY_DESTROY(&newname_w);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief os.tmpname() - UTF-8 aware version
 *
 * Returns a unique temporary filename as UTF-8 string.
 */
static int os_tmpname_utf8(struct lua_State *L) {
  wchar_t temp_path[MAX_PATH];
  wchar_t temp_file[MAX_PATH];

  if (GetTempPathW(MAX_PATH, temp_path) == 0) {
    return luaL_error(L, "unable to generate a unique filename");
  }

  if (GetTempFileNameW(temp_path, L"lua", 0, temp_file) == 0) {
    return luaL_error(L, "unable to generate a unique filename");
  }

  // Delete the file since tmpname just returns a name
  DeleteFileW(temp_file);

  char *utf8_path = NULL;
  if (!gcmz_wchar_to_utf8(temp_file, &utf8_path, NULL)) {
    return luaL_error(L, "unable to convert filename to UTF-8");
  }

  lua_pushstring(L, utf8_path);
  OV_ARRAY_DESTROY(&utf8_path);
  return 1;
}

/**
 * @brief os.getenv(varname) - UTF-8 aware version
 *
 * Returns the value of environment variable as UTF-8 string.
 */
static int os_getenv_utf8(struct lua_State *L) {
  char const *const varname = luaL_checkstring(L, 1);

  wchar_t *varname_w = NULL;
  wchar_t *value_w = NULL;
  char *value_utf8 = NULL;
  struct ov_error err = {0};
  int result = -1;

  {
    if (!gcmz_utf8_to_wchar(varname, &varname_w, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Get required buffer size
    DWORD size = GetEnvironmentVariableW(varname_w, NULL, 0);
    if (size == 0) {
      // Variable not found
      lua_pushnil(L);
      result = 1;
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&value_w, size)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (GetEnvironmentVariableW(varname_w, value_w, size) == 0) {
      lua_pushnil(L);
      result = 1;
      goto cleanup;
    }

    if (!gcmz_wchar_to_utf8(value_w, &value_utf8, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    lua_pushstring(L, value_utf8);
    result = 1;
  }

cleanup:
  if (varname_w) {
    OV_ARRAY_DESTROY(&varname_w);
  }
  if (value_w) {
    OV_ARRAY_DESTROY(&value_w);
  }
  if (value_utf8) {
    OV_ARRAY_DESTROY(&value_utf8);
  }
  return result < 0 ? gcmz_luafn_err(L, &err) : result;
}

/**
 * @brief os.setlocale() - Disabled version
 *
 * Calling setlocale can corrupt the UTF-8 environment, so this function
 * always returns an error to prevent accidental locale changes.
 */
static int os_setlocale_disabled(struct lua_State *L) {
  (void)L;
  return luaL_error(L, "os.setlocale is disabled to preserve UTF-8 environment");
}

/**
 * @brief Setup UTF-8 aware os library functions
 *
 * If the os library doesn't exist (e.g., LuaJIT built without os library),
 * this function creates the os table and provides all os functions.
 */
static void setup_os_utf8_funcs(struct lua_State *L) {
  lua_getglobal(L, "os");
  if (!lua_istable(L, -1)) {
    // os library doesn't exist, create it
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "os");
  }

  // Core time functions (always provide our implementation)
  lua_pushcfunction(L, os_clock_impl);
  lua_setfield(L, -2, "clock");

  lua_pushcfunction(L, os_time_impl);
  lua_setfield(L, -2, "time");

  lua_pushcfunction(L, os_date_impl);
  lua_setfield(L, -2, "date");

  lua_pushcfunction(L, os_difftime_impl);
  lua_setfield(L, -2, "difftime");

  lua_pushcfunction(L, os_exit_impl);
  lua_setfield(L, -2, "exit");

  // UTF-8 aware functions
  lua_pushcfunction(L, os_execute_utf8);
  lua_setfield(L, -2, "execute");

  lua_pushcfunction(L, os_remove_utf8);
  lua_setfield(L, -2, "remove");

  lua_pushcfunction(L, os_rename_utf8);
  lua_setfield(L, -2, "rename");

  lua_pushcfunction(L, os_tmpname_utf8);
  lua_setfield(L, -2, "tmpname");

  lua_pushcfunction(L, os_getenv_utf8);
  lua_setfield(L, -2, "getenv");

  lua_pushcfunction(L, os_setlocale_disabled);
  lua_setfield(L, -2, "setlocale");

  lua_pop(L, 1);
}

/**
 * @brief Setup UTF-8 aware io library functions
 */
static void setup_io_utf8_funcs(struct lua_State *L) {
  // Setup file handle metatables
  setup_io_file_metatable(L);

  // Replace io functions
  lua_getglobal(L, "io");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  lua_pushcfunction(L, io_open_utf8);
  lua_setfield(L, -2, "open");

  lua_pushcfunction(L, io_input_utf8);
  lua_setfield(L, -2, "input");

  lua_pushcfunction(L, io_output_utf8);
  lua_setfield(L, -2, "output");

  lua_pushcfunction(L, io_close_utf8);
  lua_setfield(L, -2, "close");

  lua_pushcfunction(L, io_flush_utf8);
  lua_setfield(L, -2, "flush");

  lua_pushcfunction(L, io_read_utf8);
  lua_setfield(L, -2, "read");

  lua_pushcfunction(L, io_write_utf8);
  lua_setfield(L, -2, "write");

  lua_pushcfunction(L, io_lines_utf8);
  lua_setfield(L, -2, "lines");

  lua_pushcfunction(L, io_type_utf8);
  lua_setfield(L, -2, "type");

  lua_pushcfunction(L, io_popen_utf8);
  lua_setfield(L, -2, "popen");

  lua_pushcfunction(L, io_tmpfile_utf8);
  lua_setfield(L, -2, "tmpfile");

  // Setup io.stdin, io.stdout, io.stderr using Windows console handles
  // These may be invalid in GUI applications but we create them for compatibility
  HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
  if (h_stdin != INVALID_HANDLE_VALUE && h_stdin != NULL) {
    // Duplicate handle so that Lua's close doesn't close the real stdin
    HANDLE h_dup = INVALID_HANDLE_VALUE;
    if (DuplicateHandle(GetCurrentProcess(), h_stdin, GetCurrentProcess(), &h_dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      create_file_handle(L, h_dup, true, false, true);
      lua_setfield(L, -2, "stdin");
      // Also set as default input
      lua_getfield(L, -1, "stdin");
      lua_setfield(L, LUA_REGISTRYINDEX, io_input_key);
    }
  }

  HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h_stdout != INVALID_HANDLE_VALUE && h_stdout != NULL) {
    HANDLE h_dup = INVALID_HANDLE_VALUE;
    if (DuplicateHandle(GetCurrentProcess(), h_stdout, GetCurrentProcess(), &h_dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      create_file_handle(L, h_dup, false, true, true);
      lua_setfield(L, -2, "stdout");
      // Also set as default output
      lua_getfield(L, -1, "stdout");
      lua_setfield(L, LUA_REGISTRYINDEX, io_output_key);
    }
  }

  HANDLE h_stderr = GetStdHandle(STD_ERROR_HANDLE);
  if (h_stderr != INVALID_HANDLE_VALUE && h_stderr != NULL) {
    HANDLE h_dup = INVALID_HANDLE_VALUE;
    if (DuplicateHandle(GetCurrentProcess(), h_stderr, GetCurrentProcess(), &h_dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      create_file_handle(L, h_dup, false, true, true);
      lua_setfield(L, -2, "stderr");
    }
  }

  lua_pop(L, 1);
}

void gcmz_lua_setup_utf8_funcs(struct lua_State *const L) {
  if (!L) {
    return;
  }

  // Create metatable for HMODULE userdata with __gc
  lua_newtable(L);
  lua_pushcfunction(L, lua_c_handle_gc);
  lua_setfield(L, -2, "__gc");
  lua_setfield(L, LUA_REGISTRYINDEX, c_module_metatable_key);

  // Create table to track loaded C modules and store in registry
  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, loaded_c_module_handles_key);

  // Replace loadfile
  lua_pushcfunction(L, lua_loadfile_utf8);
  lua_setglobal(L, "loadfile");

  // Replace dofile
  lua_pushcfunction(L, lua_dofile_utf8);
  lua_setglobal(L, "dofile");

  // Replace package.loaders[2], [3], and [4]
  lua_getglobal(L, "package");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "loaders");
    if (lua_istable(L, -1)) {
      // Replace loaders[2] (Lua searcher)
      lua_pushcfunction(L, lua_searcher_utf8);
      lua_rawseti(L, -2, 2);

      // Replace loaders[3] (C searcher)
      lua_pushcfunction(L, lua_c_searcher_utf8);
      lua_rawseti(L, -2, 3);

      // Replace loaders[4] (All-in-one C searcher)
      lua_pushcfunction(L, lua_c_root_searcher_utf8);
      lua_rawseti(L, -2, 4);
    }
    lua_pop(L, 1); // Pop loaders
  }
  lua_pop(L, 1); // Pop package

  setup_io_utf8_funcs(L);
  setup_os_utf8_funcs(L);
}
