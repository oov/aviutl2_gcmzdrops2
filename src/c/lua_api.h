#pragma once

#include <ovbase.h>

#include "gcmz_types.h"

struct lua_State;
struct aviutl2_edit_info;

/**
 * @brief Callback function type for creating temporary file
 *
 * @param userdata User-provided data
 * @param filename Base file name (UTF-8)
 * @param err [out] Error information on failure
 * @return Created temp file path (UTF-8, caller must OV_ARRAY_DESTROY), NULL on failure
 */
typedef char *(*gcmz_lua_api_temp_file_provider_fn)(void *userdata, char const *filename, struct ov_error *err);

/**
 * @brief Callback function type for getting save path
 *
 * @param userdata User-provided data
 * @param filename File name to save (UTF-8)
 * @param err [out] Error information on failure
 * @return Save path (UTF-8, caller must OV_ARRAY_DESTROY), NULL on failure
 */
typedef char *(*gcmz_lua_api_save_path_provider_fn)(void *userdata, char const *filename, struct ov_error *err);

/**
 * @brief Callback function type for getting project data
 *
 * @param edit_info [out] Project data to fill
 * @param project_path [out] Allocated UTF-8 project path (caller must free with OV_ARRAY_DESTROY), can be NULL
 * @param userdata User-provided data
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef NODISCARD bool (*gcmz_lua_api_get_project_data_fn)(struct aviutl2_edit_info *edit_info,
                                                           char **project_path,
                                                           void *userdata,
                                                           struct ov_error *err);

/**
 * @brief Callback function type for debug print output
 *
 * @param userdata User-provided data
 * @param message UTF-8 message to print
 */
typedef void (*gcmz_lua_api_debug_print_fn)(void *userdata, char const *message);

/**
 * @brief Options for Lua API registration
 */
struct gcmz_lua_api_options {
  gcmz_lua_api_temp_file_provider_fn temp_file_provider;
  gcmz_lua_api_save_path_provider_fn save_path_provider;
  gcmz_lua_api_get_project_data_fn get_project_data;
  gcmz_lua_api_debug_print_fn debug_print;
  void *userdata;
  uint32_t aviutl2_ver;
  uint32_t gcmz_ver;
};

/**
 * @brief Set options for Lua API functions
 *
 * Must be called before gcmz_lua_api_register to enable save_file API.
 *
 * @param options Options structure pointer (can be NULL to clear)
 */
void gcmz_lua_api_set_options(struct gcmz_lua_api_options const *const options);

/**
 * @brief Register all gcmz Lua APIs to the given Lua state
 *
 * Creates a global 'gcmz' table with all available APIs.
 *
 * @param L Lua state to register APIs to
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_api_register(struct lua_State *const L, struct ov_error *const err);
