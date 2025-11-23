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
 * @brief Create and initialize Lua context with standard libraries and plugin loading
 *
 * @param ctx [out] Pointer to store the created context
 * @param options Options for Lua context creation (can be NULL for default options)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_create(struct gcmz_lua_context **const ctx,
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
 * @param x Mouse X coordinate
 * @param y Mouse Y coordinate
 * @param key_state Keyboard and mouse button state flags
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_drag_enter(struct gcmz_lua_context const *const ctx,
                                        struct gcmz_file_list *const file_list,
                                        int x,
                                        int y,
                                        uint32_t key_state,
                                        struct ov_error *const err);

/**
 * @brief Call drag_over hook on all loaded modules in priority order
 *
 * @param ctx Lua context instance
 * @param x Mouse X coordinate
 * @param y Mouse Y coordinate
 * @param key_state Keyboard and mouse button state flags
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_drag_over(
    struct gcmz_lua_context const *const ctx, int x, int y, uint32_t key_state, struct ov_error *const err);

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
 * @param x Mouse X coordinate
 * @param y Mouse Y coordinate
 * @param key_state Keyboard and mouse button state flags
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_call_drop(struct gcmz_lua_context const *const ctx,
                                  struct gcmz_file_list *const file_list,
                                  int x,
                                  int y,
                                  uint32_t key_state,
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
