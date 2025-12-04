#pragma once

#include <ovbase.h>

struct gcmz_file_list;
struct gcmz_lua_context;
struct lua_State;

/**
 * @brief Callback function type for registering Lua APIs
 *
 * @param L Lua state to register APIs to
 * @param userdata User-defined context passed to gcmz_lua_options
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef NODISCARD bool (*gcmz_lua_api_register_callback)(struct lua_State *const L,
                                                         void *userdata,
                                                         struct ov_error *const err);

/**
 * @brief Callback function type for scheduling temporary file cleanup
 *
 * Called when a temporary file is removed from the file list during Lua table synchronization.
 * The callback should schedule the file for delayed deletion.
 *
 * @param path File path to schedule for cleanup
 * @param userdata User-defined context passed to gcmz_lua_options
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef NODISCARD bool (*gcmz_lua_schedule_cleanup_callback)(wchar_t const *const path,
                                                             void *userdata,
                                                             struct ov_error *const err);

/**
 * @brief Callback function type for creating temporary files
 *
 * Creates a temporary file with the given filename hint and returns the full path.
 *
 * @param userdata User-defined context passed to gcmz_lua_options
 * @param filename Suggested filename for the temporary file (UTF-8)
 * @param err [out] Error information on failure
 * @return Created file path (UTF-8, caller must OV_ARRAY_DESTROY), NULL on failure
 */
typedef char *(*gcmz_lua_create_temp_file_callback)(void *userdata, char const *filename, struct ov_error *err);

/**
 * @brief Options for creating Lua context
 */
struct gcmz_lua_options {
  wchar_t const *script_dir; ///< Directory path to scan for requireable modules (can be NULL for no plugins)
  gcmz_lua_api_register_callback
      api_register_callback; ///< Callback function to register APIs (can be NULL to skip API registration)
  gcmz_lua_schedule_cleanup_callback
      schedule_cleanup_callback; ///< Callback for scheduling delayed cleanup (can be NULL to skip cleanup scheduling)
  gcmz_lua_create_temp_file_callback
      create_temp_file_callback; ///< Callback for creating temporary files (required for EXO conversion)
  void *userdata;                ///< User data passed to all callback functions
};

/**
 * @brief Create and initialize Lua context with standard libraries
 *
 * @param ctx [out] Pointer to store the created context
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_create(struct gcmz_lua_context **const ctx, struct ov_error *const err);

/**
 * @brief Setup Lua context with additional options
 *
 * @param ctx Lua context instance
 * @param options Options for Lua context setup
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_setup(struct gcmz_lua_context *const ctx,
                              struct gcmz_lua_options const *const options,
                              struct ov_error *const err);

/**
 * @brief Cleanup and destroy Lua context, freeing all resources
 *
 * @param ctx Lua context instance to destroy
 */
void gcmz_lua_destroy(struct gcmz_lua_context **const ctx);

/**
 * @brief Get the underlying lua_State for script execution
 *
 * @param ctx Lua context instance
 * @return lua_State pointer or NULL if context is invalid
 */
struct lua_State *gcmz_lua_get_state(struct gcmz_lua_context const *const ctx);

/**
 * @brief Call drag_enter hook on all loaded modules in priority order
 *
 * Modules can modify the file list by returning a new file table.
 *
 * @param ctx Lua context instance
 * @param file_list File list from drop operation (will be modified in-place if modules return new files)
 * @param key_state Keyboard and mouse button state flags
 * @param modifier_keys Additional modifier keys (gcmz_modifier_key_flags)
 * @param from_external_api Whether this drop originated from external API
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_drag_enter(struct gcmz_lua_context const *const ctx,
                                        struct gcmz_file_list *const file_list,
                                        uint32_t key_state,
                                        uint32_t modifier_keys,
                                        bool from_external_api,
                                        struct ov_error *const err);

/**
 * @brief Call drag_leave hook on all loaded modules in priority order
 *
 * @param ctx Lua context instance
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_drag_leave(struct gcmz_lua_context const *const ctx, struct ov_error *const err);

/**
 * @brief Call drop hook on all loaded modules in priority order
 *
 * Modules can modify the file list by returning a new file table.
 *
 * @param ctx Lua context instance
 * @param file_list File list from drop operation (will be modified in-place if modules return new files)
 * @param key_state Keyboard and mouse button state flags
 * @param modifier_keys Additional modifier keys (gcmz_modifier_key_flags)
 * @param from_external_api Whether this drop originated from external API
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_drop(struct gcmz_lua_context const *const ctx,
                                  struct gcmz_file_list *const file_list,
                                  uint32_t key_state,
                                  uint32_t modifier_keys,
                                  bool from_external_api,
                                  struct ov_error *const err);

/**
 * @brief Convert EXO files to object files using Lua exo module
 *
 * Processes each file in the file list and converts EXO files to AviUtl2 object format.
 * Converted files replace the original EXO files in the list.
 *
 * @param ctx Lua context instance
 * @param file_list File list to process (will be modified in-place)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_exo_convert(struct gcmz_lua_context const *const ctx,
                                         struct gcmz_file_list *const file_list,
                                         struct ov_error *const err);

/**
 * @brief Add a handler script from a Lua script string
 *
 * Loads and registers a handler module from a Lua script string.
 * The script should return a table with handler functions (drag_enter, drag_leave, drop)
 * and optionally a priority value.
 *
 * This function can be called anytime after gcmz_lua_create, including before full initialization.
 *
 * @param ctx Lua context instance
 * @param name Module name for identification (UTF-8)
 * @param script Lua script string (UTF-8) that returns a module table
 * @param script_len Length of the script in bytes
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_add_handler_script(struct gcmz_lua_context *const ctx,
                                           char const *const name,
                                           char const *const script,
                                           size_t const script_len,
                                           struct ov_error *const err);

/**
 * @brief Add a handler script from a Lua script file
 *
 * Loads and registers a handler module from a Lua script file.
 * The script should return a table with handler functions (drag_enter, drag_leave, drop)
 * and optionally a priority value.
 *
 * This function can be called anytime after gcmz_lua_create, including before full initialization.
 *
 * @param ctx Lua context instance
 * @param filepath Path to the Lua script file (native character encoding)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_add_handler_script_file(struct gcmz_lua_context *const ctx,
                                                NATIVE_CHAR const *const filepath,
                                                struct ov_error *const err);
