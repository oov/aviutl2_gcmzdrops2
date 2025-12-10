#include "lua.h"

#include "file.h"
#include "gcmz_types.h"
#include "logf.h"
#include "luautil.h"

#include <ovarray.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovutf.h>

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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct gcmz_lua_context {
  lua_State *L;
  gcmz_lua_schedule_cleanup_callback schedule_cleanup_callback;
  gcmz_lua_create_temp_file_callback create_temp_file_callback;
  void *userdata;
  int entrypoint_ref; // Lua registry reference for entrypoint module
};

#define LUA_SET_STRING_FIELD(L, key, value)                                                                            \
  do {                                                                                                                 \
    lua_pushstring((L), (key));                                                                                        \
    lua_pushstring((L), (value) ? (value) : "");                                                                       \
    lua_settable((L), -3);                                                                                             \
  } while (0)

#define LUA_SET_BOOL_FIELD(L, key, value)                                                                              \
  do {                                                                                                                 \
    lua_pushstring((L), (key));                                                                                        \
    lua_pushboolean((L), (value));                                                                                     \
    lua_settable((L), -3);                                                                                             \
  } while (0)

/**
 * @brief Create a Lua table from gcmz_file_list
 *
 * Creates a table with structure:
 * @code
 * {
 *   {filepath = "C:\\Path\\To\\File1.ext", mimetype = "image/png"},
 *   {filepath = "C:\\Path\\To\\File2.ext", mimetype = "audio/wav"},
 *   ...
 * }
 * @endcode
 *
 * @param L Lua state
 * @param file_list Source file list
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool create_files_table(lua_State *L, struct gcmz_file_list const *const file_list, struct ov_error *const err) {
  if (!L || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  char *buffer = NULL;
  bool result = false;

  size_t const file_count = gcmz_file_list_count(file_list);
  lua_createtable(L, (int)file_count, 0);

  for (size_t i = 0; i < file_count; i++) {
    struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
    if (!file) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    lua_createtable(L, 0, 2);

    if (!gcmz_wchar_to_utf8(file->path, &buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    LUA_SET_STRING_FIELD(L, "filepath", buffer);

    if (!gcmz_wchar_to_utf8(file->mime_type, &buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    LUA_SET_STRING_FIELD(L, "mimetype", buffer);

    lua_rawseti(L, -2, (int)(i + 1));
  }
  result = true;

cleanup:
  if (buffer) {
    OV_ARRAY_DESTROY(&buffer);
  }
  if (!result) {
    lua_pop(L, 1);
  }
  return result;
}

/**
 * @brief Get string field from Lua table at stack top
 *
 * @param L Lua state
 * @param key Field name
 * @return String value or NULL if not present/empty
 */
static char const *lua_get_string_field(lua_State *L, char const *const key) {
  lua_pushstring(L, key);
  lua_gettable(L, -2);
  char const *const value = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
  lua_pop(L, 1);
  return (value && value[0] != '\0') ? value : NULL;
}

/**
 * @brief Get boolean field from Lua table at stack top
 *
 * @param L Lua state
 * @param key Field name
 * @param default_value Value to return if field is not present
 * @return Boolean value
 */
static bool lua_get_bool_field(lua_State *L, char const *const key, bool default_value) {
  lua_pushstring(L, key);
  lua_gettable(L, -2);
  bool const value = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : default_value;
  lua_pop(L, 1);
  return value;
}

/**
 * @brief Free collected paths array
 */
static void free_collected_paths(wchar_t ***const paths, size_t count) {
  if (!paths || !*paths) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    if ((*paths)[i]) {
      OV_ARRAY_DESTROY(&(*paths)[i]);
    }
  }
  OV_ARRAY_DESTROY(paths);
}

/**
 * @brief Collect file paths from Lua table
 *
 * @param L Lua state
 * @param table_index Stack index of the Lua table
 * @param out_paths [out] Array of collected paths
 * @param out_count [out] Number of paths collected
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool collect_paths_from_table(
    lua_State *L, int table_index, wchar_t ***const out_paths, size_t *const out_count, struct ov_error *const err) {
  size_t const table_len = lua_objlen(L, table_index);
  if (table_len == 0) {
    *out_paths = NULL;
    *out_count = 0;
    return true;
  }

  wchar_t **paths = NULL;
  size_t count = 0;
  bool result = false;

  if (!OV_ARRAY_GROW(&paths, table_len)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    return false;
  }
  for (size_t i = 0; i < table_len; i++) {
    paths[i] = NULL;
  }

  for (size_t i = 1; i <= table_len; i++) {
    lua_rawgeti(L, table_index, (int)i);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }

    char const *const filepath = lua_get_string_field(L, "filepath");
    lua_pop(L, 1); // Pop file entry table

    if (!filepath) {
      continue;
    }

    wchar_t *path_w = NULL;
    if (!gcmz_utf8_to_wchar(filepath, &path_w, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (path_w) {
      paths[count++] = path_w;
    }
  }

  result = true;

cleanup:
  if (result) {
    *out_paths = paths;
    *out_count = count;
  } else {
    free_collected_paths(&paths, count);
  }
  return result;
}

/**
 * @brief Check if path exists in path array
 */
static bool path_exists_in_array(wchar_t const *const path, wchar_t **const paths, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (paths[i] && wcscmp(path, paths[i]) == 0) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Schedule cleanup for removed temporary files
 *
 * @param file_list Existing file list
 * @param new_paths New collected paths
 * @param new_paths_count Number of new collected paths
 * @param callback Cleanup scheduling callback
 * @param userdata User data passed to callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool schedule_removed_temp_files_cleanup(struct gcmz_file_list const *const file_list,
                                                wchar_t **const new_paths,
                                                size_t new_paths_count,
                                                gcmz_lua_schedule_cleanup_callback callback,
                                                void *userdata,
                                                struct ov_error *const err) {
  if (!callback) {
    return true;
  }

  size_t const existing_count = gcmz_file_list_count(file_list);
  for (size_t i = 0; i < existing_count; i++) {
    struct gcmz_file const *const file = gcmz_file_list_get(file_list, i);
    if (!file || !file->temporary || !file->path) {
      continue;
    }

    if (!path_exists_in_array(file->path, new_paths, new_paths_count)) {
      if (!callback(file->path, userdata, err)) {
        OV_ERROR_ADD_TRACE(err);
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Parse single file entry from Lua table and add to file list
 *
 * @param L Lua state (file entry table at stack top)
 * @param file_list File list to add to
 * @param path_buffer [in/out] Buffer for path wchar_t conversion (reused)
 * @param mime_buffer [in/out] Buffer for mime wchar_t conversion (reused)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool parse_and_add_file_entry(lua_State *L,
                                     struct gcmz_file_list *const file_list,
                                     wchar_t **const path_buffer,
                                     wchar_t **const mime_buffer,
                                     struct ov_error *const err) {
  char const *const filepath = lua_get_string_field(L, "filepath");
  if (!filepath) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  wchar_t *mime = NULL;

  {
    if (!gcmz_utf8_to_wchar(filepath, path_buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!*path_buffer) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
      goto cleanup;
    }
    char const *const mimetype = lua_get_string_field(L, "mimetype");
    if (mimetype) {
      if (!gcmz_utf8_to_wchar(mimetype, mime_buffer, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      mime = *mime_buffer;
    }
    if (lua_get_bool_field(L, "temporary", false)) {
      if (!gcmz_file_list_add_temporary(file_list, *path_buffer, mime, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
    } else {
      if (!gcmz_file_list_add(file_list, *path_buffer, mime, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
    }
  }

  result = true;

cleanup:
  return result;
}

/**
 * @brief Synchronize file_list with a Lua table
 *
 * Expects a table with structure:
 * @code
 * {
 *   {filepath = "C:\\Path\\To\\File1.ext", mimetype = "image/png", temporary = false},
 *   {filepath = "C:\\Path\\To\\File2.ext", mimetype = "audio/wav", temporary = true},
 *   ...
 * }
 * @endcode
 *
 * Temporary files removed from the list are scheduled for delayed cleanup.
 *
 * @param L Lua state
 * @param table_index Stack index of the Lua table
 * @param file_list File list to update
 * @param schedule_cleanup_callback Callback for scheduling cleanup (can be NULL)
 * @param userdata User data passed to callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool update_file_list_from_table(lua_State *L,
                                        int table_index,
                                        struct gcmz_file_list *const file_list,
                                        gcmz_lua_schedule_cleanup_callback schedule_cleanup_callback,
                                        void *userdata,
                                        struct ov_error *const err) {
  if (!L || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (!lua_istable(L, table_index)) {
    return true; // Not a table, no update needed
  }

  wchar_t **new_paths = NULL;
  size_t new_paths_count = 0;
  wchar_t *path_buffer = NULL;
  wchar_t *mime_buffer = NULL;
  bool result = false;

  if (!collect_paths_from_table(L, table_index, &new_paths, &new_paths_count, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  if (!schedule_removed_temp_files_cleanup(
          file_list, new_paths, new_paths_count, schedule_cleanup_callback, userdata, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  gcmz_file_list_clear(file_list);
  for (size_t i = 1, len = lua_objlen(L, table_index); i <= len; ++i) {
    lua_rawgeti(L, table_index, (int)i);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    if (!parse_and_add_file_entry(L, file_list, &path_buffer, &mime_buffer, err)) {
      OV_ERROR_ADD_TRACE(err);
      lua_pop(L, 1);
      goto cleanup;
    }
    lua_pop(L, 1);
  }

  result = true;

cleanup:
  if (path_buffer) {
    OV_ARRAY_DESTROY(&path_buffer);
  }
  if (mime_buffer) {
    OV_ARRAY_DESTROY(&mime_buffer);
  }
  free_collected_paths(&new_paths, new_paths_count);
  return result;
}

/**
 * @brief Create a Lua table from key state
 *
 * @param L Lua state
 * @param key_state Windows key state flags
 * @param modifier_keys Additional modifier keys (gcmz_modifier_key_flags)
 * @param from_external_api Whether this drop originated from external API
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool create_state_table(
    lua_State *L, uint32_t key_state, uint32_t modifier_keys, bool from_external_api, struct ov_error *const err) {
  if (!L) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  lua_createtable(L, 0, 9);

  LUA_SET_BOOL_FIELD(L, "control", (key_state & MK_CONTROL) != 0);
  LUA_SET_BOOL_FIELD(L, "shift", (key_state & MK_SHIFT) != 0);
  LUA_SET_BOOL_FIELD(L, "alt", (modifier_keys & gcmz_modifier_alt) != 0);
  LUA_SET_BOOL_FIELD(L, "win", (modifier_keys & gcmz_modifier_win) != 0);
  LUA_SET_BOOL_FIELD(L, "lbutton", (key_state & MK_LBUTTON) != 0);
  LUA_SET_BOOL_FIELD(L, "mbutton", (key_state & MK_MBUTTON) != 0);
  LUA_SET_BOOL_FIELD(L, "rbutton", (key_state & MK_RBUTTON) != 0);
  LUA_SET_BOOL_FIELD(L, "from_external_api", from_external_api);

  return true;
}

/**
 * @brief Setup plugin loading paths and load modules from script directory
 *
 * Collects .lua file paths and calls entrypoint.load_handlers to load them.
 * Note: package.path and package.cpath should already be configured before calling this.
 *
 * @param ctx Lua context (must have entrypoint_ref set)
 * @param script_dir Directory containing script modules
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool setup_plugin_loading(struct gcmz_lua_context *ctx, wchar_t const *script_dir, struct ov_error *const err) {
  if (!ctx || !ctx->L || !script_dir || ctx->entrypoint_ref == LUA_NOREF) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);
  size_t script_dir_len = wcslen(script_dir);
  wchar_t *filepath = NULL;
  char *utf8_path = NULL;
  HANDLE find_handle = INVALID_HANDLE_VALUE;
  bool result = false;
  int file_count = 0;

  {
    // Get entrypoint.load_handlers function
    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
      goto cleanup;
    }
    lua_getfield(L, -1, "load_handlers");
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 2);
      OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
      goto cleanup;
    }
    lua_remove(L, -2); // Remove entrypoint, keep function

    // Create table for file paths
    lua_newtable(L);

    // Create search pattern: script_dir\*.lua
    static wchar_t const pattern_suffix[] = L"\\*.lua";
    static_assert(sizeof(pattern_suffix) / sizeof(wchar_t) <= MAX_PATH, "pattern_suffix is too large");

    // MAX_PATH is enough to avoid unnecessary reallocs
    if (!OV_ARRAY_GROW(&filepath, script_dir_len + MAX_PATH)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(filepath, script_dir);
    wcscpy(filepath + script_dir_len, pattern_suffix);

    WIN32_FIND_DATAW find_data;
    find_handle = FindFirstFileW(filepath, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
      // No files found, call load_handlers with empty table
      if (!gcmz_lua_pcall(L, 1, 0, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      result = true;
      goto cleanup;
    }

    do {
      if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        continue;
      }

      // Build full filepath
      size_t const filepath_len = script_dir_len + 1 + wcslen(find_data.cFileName) + 1;
      if (!OV_ARRAY_GROW(&filepath, filepath_len)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(filepath, script_dir);
      filepath[script_dir_len] = L'\\';
      wcscpy(filepath + script_dir_len + 1, find_data.cFileName);

      // Convert to UTF-8
      if (!gcmz_wchar_to_utf8(filepath, &utf8_path, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }

      // Add to table
      lua_pushstring(L, utf8_path);
      lua_rawseti(L, -2, ++file_count);
    } while (FindNextFileW(find_handle, &find_data));

    // Call load_handlers(filepaths)
    if (!gcmz_lua_pcall(L, 1, 0, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  lua_settop(L, base_top);
  if (find_handle != INVALID_HANDLE_VALUE) {
    FindClose(find_handle);
  }
  if (filepath) {
    OV_ARRAY_DESTROY(&filepath);
  }
  if (utf8_path) {
    OV_ARRAY_DESTROY(&utf8_path);
  }
  return result;
}

NODISCARD bool gcmz_lua_create(struct gcmz_lua_context **const ctx, struct ov_error *const err) {
  if (!ctx || *ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_lua_context *c = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&c, 1, sizeof(struct gcmz_lua_context))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    *c = (struct gcmz_lua_context){.entrypoint_ref = LUA_NOREF};

    c->L = luaL_newstate();
    if (!c->L) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    luaL_openlibs(c->L);
    gcmz_lua_setup_utf8_funcs(c->L);
  }

  *ctx = c;
  c = NULL;
  result = true;

cleanup:
  if (c) {
    if (c->L) {
      lua_close(c->L);
    }
    OV_FREE(&c);
  }
  return result;
}

void gcmz_lua_destroy(struct gcmz_lua_context **const ctx) {
  if (!ctx || !*ctx) {
    return;
  }

  struct gcmz_lua_context *c = *ctx;
  if (c->L) {
    if (c->entrypoint_ref != LUA_NOREF) {
      luaL_unref(c->L, LUA_REGISTRYINDEX, c->entrypoint_ref);
    }
    lua_close(c->L);
  }
  OV_FREE(ctx);
}

NODISCARD bool gcmz_lua_setup(struct gcmz_lua_context *const ctx,
                              struct gcmz_lua_options const *const options,
                              struct ov_error *const err) {
  if (!ctx || !ctx->L || !options || !options->script_dir || options->script_dir[0] == L'\0') {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  char *utf8_dir = NULL;

  ctx->schedule_cleanup_callback = options->schedule_cleanup_callback;
  ctx->create_temp_file_callback = options->create_temp_file_callback;
  ctx->userdata = options->userdata;

  if (options->api_register_callback) {
    if (!options->api_register_callback(ctx->L, options->userdata, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  // Setup package.path and package.cpath first (needed for require to work)
  if (!gcmz_wchar_to_utf8(options->script_dir, &utf8_dir, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  lua_getglobal(ctx->L, "package");
  if (lua_istable(ctx->L, -1)) {
    lua_getfield(ctx->L, -1, "path");
    char const *const current_path = lua_tostring(ctx->L, -1);
    lua_pop(ctx->L, 1);
    lua_pushfstring(ctx->L, "%s;%s\\?.lua", current_path ? current_path : "", utf8_dir);
    lua_setfield(ctx->L, -2, "path");

    lua_getfield(ctx->L, -1, "cpath");
    char const *const current_cpath = lua_tostring(ctx->L, -1);
    lua_pop(ctx->L, 1);
    lua_pushfstring(ctx->L, "%s;%s\\?.dll", current_cpath ? current_cpath : "", utf8_dir);
    lua_setfield(ctx->L, -2, "cpath");
  }
  lua_pop(ctx->L, 1); // Pop package table

  // Load entrypoint module and store in registry (before loading plugins)
  lua_getglobal(ctx->L, "require");
  lua_pushstring(ctx->L, "entrypoint");
  if (!gcmz_lua_pcall(ctx->L, 1, 1, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  if (!lua_istable(ctx->L, -1)) {
    lua_pop(ctx->L, 1);
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "entrypoint module must return a table");
    goto cleanup;
  }
  ctx->entrypoint_ref = luaL_ref(ctx->L, LUA_REGISTRYINDEX);
  if (!setup_plugin_loading(ctx, options->script_dir, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (utf8_dir) {
    OV_ARRAY_DESTROY(&utf8_dir);
  }
  return result;
}

struct lua_State *gcmz_lua_get_state(struct gcmz_lua_context const *const ctx) {
  if (!ctx) {
    return NULL;
  }
  return ctx->L;
}

/**
 * Call drag_enter hook via Lua entrypoint module
 */
NODISCARD bool gcmz_lua_call_drag_enter(struct gcmz_lua_context const *const ctx,
                                        struct gcmz_file_list *const file_list,
                                        uint32_t key_state,
                                        uint32_t modifier_keys,
                                        bool from_external_api,
                                        struct ov_error *const err) {
  if (!ctx || !ctx->L || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (ctx->entrypoint_ref == LUA_NOREF) {
    return true; // No entrypoint loaded, nothing to call
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);
  bool result = false;

  // Get entrypoint.drag_enter from registry
  lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    result = true;
    goto cleanup;
  }
  lua_getfield(L, -1, "drag_enter");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    result = true;
    goto cleanup;
  }
  lua_remove(L, -2); // Remove entrypoint, keep function

  // Create files table
  if (!create_files_table(L, file_list, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Create state table
  if (!create_state_table(L, key_state, modifier_keys, from_external_api, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Call drag_enter(files, state) -> files
  if (!gcmz_lua_pcall(L, 2, 1, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Update file_list from returned files table
  if (!update_file_list_from_table(L, -1, file_list, ctx->schedule_cleanup_callback, ctx->userdata, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  lua_settop(L, base_top);
  return result;
}

/**
 * Call drag_leave hook via Lua entrypoint module
 */
NODISCARD bool gcmz_lua_call_drag_leave(struct gcmz_lua_context const *const ctx, struct ov_error *const err) {
  if (!ctx || !ctx->L) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (ctx->entrypoint_ref == LUA_NOREF) {
    return true; // No entrypoint loaded, nothing to call
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);

  // Get entrypoint.drag_leave from registry
  lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
  if (!lua_istable(L, -1)) {
    lua_settop(L, base_top);
    return true;
  }
  lua_getfield(L, -1, "drag_leave");
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, base_top);
    return true;
  }
  lua_remove(L, -2); // Remove entrypoint, keep function

  // Call drag_leave()
  if (!gcmz_lua_pcall(L, 0, 0, err)) {
    OV_ERROR_ADD_TRACE(err);
    lua_settop(L, base_top);
    return false;
  }

  lua_settop(L, base_top);
  return true;
}

/**
 * Call drop hook via Lua entrypoint module
 */
NODISCARD bool gcmz_lua_call_drop(struct gcmz_lua_context const *const ctx,
                                  struct gcmz_file_list *const file_list,
                                  uint32_t key_state,
                                  uint32_t modifier_keys,
                                  bool from_external_api,
                                  struct ov_error *const err) {
  if (!ctx || !ctx->L || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (ctx->entrypoint_ref == LUA_NOREF) {
    return true; // No entrypoint loaded, nothing to call
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);
  bool result = false;

  // Get entrypoint.drop from registry
  lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    result = true;
    goto cleanup;
  }
  lua_getfield(L, -1, "drop");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    result = true;
    goto cleanup;
  }
  lua_remove(L, -2); // Remove entrypoint, keep function

  // Create files table
  if (!create_files_table(L, file_list, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Create state table
  if (!create_state_table(L, key_state, modifier_keys, from_external_api, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Call drop(files, state) -> files
  if (!gcmz_lua_pcall(L, 2, 1, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Update file_list from returned files table
  if (!update_file_list_from_table(L, -1, file_list, ctx->schedule_cleanup_callback, ctx->userdata, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  lua_settop(L, base_top);
  return result;
}

/**
 * Convert EXO files to object files via Lua entrypoint module
 */
NODISCARD bool gcmz_lua_call_exo_convert(struct gcmz_lua_context const *const ctx,
                                         struct gcmz_file_list *const file_list,
                                         struct ov_error *const err) {
  if (!ctx || !ctx->L || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (ctx->entrypoint_ref == LUA_NOREF) {
    return true; // No entrypoint loaded, nothing to call
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);
  bool result = false;

  // Get entrypoint.exo_convert from registry
  lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    result = true;
    goto cleanup;
  }
  lua_getfield(L, -1, "exo_convert");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    result = true;
    goto cleanup;
  }
  lua_remove(L, -2); // Remove entrypoint, keep function

  // Create files table
  if (!create_files_table(L, file_list, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Call exo_convert(files) -> files
  if (!gcmz_lua_pcall(L, 1, 1, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Update file_list from returned files table
  if (!update_file_list_from_table(L, -1, file_list, ctx->schedule_cleanup_callback, ctx->userdata, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  lua_settop(L, base_top);
  return result;
}

NODISCARD bool gcmz_lua_add_handler_script(struct gcmz_lua_context *const ctx,
                                           char const *const name,
                                           char const *const script,
                                           size_t const script_len,
                                           struct ov_error *const err) {
  if (!ctx || !ctx->L || !name || !script || !script_len || ctx->entrypoint_ref == LUA_NOREF) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);
  bool result = false;

  {
    // Call entrypoint.add_module_from_string(name, script)
    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
    lua_getfield(L, -1, "add_module_from_string");
    lua_remove(L, -2); // Remove entrypoint, keep function
    lua_pushstring(L, name);
    lua_pushlstring(L, script, script_len);
    if (!gcmz_lua_pcall(L, 2, 2, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    // Returns (true, nil) on success, (false, errmsg) on failure
    if (!lua_toboolean(L, -2)) {
      char const *const errmsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown error";
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, errmsg);
      lua_pop(L, 2);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  lua_settop(L, base_top);
  return result;
}

NODISCARD bool gcmz_lua_add_handler_script_file(struct gcmz_lua_context *const ctx,
                                                NATIVE_CHAR const *const filepath,
                                                struct ov_error *const err) {
  if (!ctx || !ctx->L || !filepath || ctx->entrypoint_ref == LUA_NOREF) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  lua_State *L = ctx->L;
  int base_top = lua_gettop(L);
  char *utf8_path = NULL;
  bool result = false;

  {
    // Convert filepath to UTF-8
    if (!gcmz_wchar_to_utf8(filepath, &utf8_path, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Call entrypoint.add_module_from_file(filepath)
    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->entrypoint_ref);
    lua_getfield(L, -1, "add_module_from_file");
    lua_remove(L, -2); // Remove entrypoint, keep function
    lua_pushstring(L, utf8_path);
    if (!gcmz_lua_pcall(L, 1, 2, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    // Returns (true, nil) on success, (false, errmsg) on failure
    if (!lua_toboolean(L, -2)) {
      char const *const errmsg = lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown error";
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, errmsg);
      lua_pop(L, 2);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  lua_settop(L, base_top);
  if (utf8_path) {
    OV_ARRAY_DESTROY(&utf8_path);
  }
  return result;
}
