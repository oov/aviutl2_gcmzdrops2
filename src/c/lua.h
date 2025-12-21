#pragma once

#include <ovbase.h>

struct gcmz_file_list;
struct gcmz_lua_context;
struct lua_State;
struct aviutl2_script_module_table;

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
 * @param script Lua script string that returns a module table
 * @param script_len Length of the script in bytes
 * @param source Source path indicating where the script came from
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_add_handler_script(struct gcmz_lua_context *const ctx,
                                           char const *const script,
                                           size_t const script_len,
                                           char const *const source,
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

/**
 * @brief Callback function type for enumerating handler modules
 *
 * @param name Handler name (UTF-8)
 * @param priority Handler priority value
 * @param source Source path where the handler was registered from (UTF-8)
 * @param userdata User-defined context
 * @return true to continue enumeration, false to stop
 */
typedef bool (*gcmz_lua_handler_enum_callback)(char const *name, int priority, char const *source, void *userdata);

/**
 * @brief Enumerate all registered handler modules
 *
 * Calls the callback function for each registered handler module.
 * The callback receives the handler name, priority, and source path.
 *
 * @param ctx Lua context instance
 * @param callback Callback function to call for each handler
 * @param userdata User-defined context passed to callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_enum_handlers(struct gcmz_lua_context const *const ctx,
                                      gcmz_lua_handler_enum_callback callback,
                                      void *userdata,
                                      struct ov_error *const err);

/**
 * @brief Get the registry key for script modules table
 *
 * Returns a pointer to the static string used as the Lua registry key
 * for the script modules table.
 *
 * @return Registry key string
 */
char const *gcmz_lua_get_script_modules_key(void);

/**
 * @brief Callback function type for enumerating script modules
 *
 * @param name Module name (UTF-8)
 * @param source Source path where the module was registered from (UTF-8)
 * @param userdata User-defined context
 * @return true to continue enumeration, false to stop
 */
typedef bool (*gcmz_lua_script_module_enum_callback)(char const *name, char const *source, void *userdata);

/**
 * @brief Enumerate all registered script modules
 *
 * Calls the callback function for each registered script module.
 * The callback receives the module name and source path.
 *
 * @param ctx Lua context instance
 * @param callback Callback function to call for each module
 * @param userdata User-defined context passed to callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_enum_script_modules(struct gcmz_lua_context const *const ctx,
                                            gcmz_lua_script_module_enum_callback callback,
                                            void *userdata,
                                            struct ov_error *const err);

/**
 * @brief Register a script module with the Lua context
 *
 * @param ctx Lua context instance
 * @param table Script module table from external DLL
 * @param module_name Module name (UTF-8)
 * @param source Source path indicating where the module came from (UTF-8)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_lua_register_script_module(struct gcmz_lua_context *const ctx,
                                               struct aviutl2_script_module_table *const table,
                                               char const *const module_name,
                                               char const *const source,
                                               struct ov_error *const err);
