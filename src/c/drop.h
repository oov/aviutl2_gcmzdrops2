#pragma once

#include <ovbase.h>

struct gcmz_file_list;
struct gcmz_lua_context;
struct gcmz_project_data;

struct gcmz_drop;

/**
 * @brief Data object extraction callback
 *
 * @param dataobj IDataObject pointer
 * @param dest File list to populate
 * @param userdata User data passed to the function
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_drop_dataobj_extract_fn)(void *dataobj,
                                             struct gcmz_file_list *dest,
                                             void *userdata,
                                             struct ov_error *const err);

/**
 * @brief Temporary file cleanup callback
 *
 * @param path File path to clean up
 * @param userdata User data passed to the function
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_drop_cleanup_temp_file_fn)(wchar_t const *const path, void *userdata, struct ov_error *const err);

/**
 * @brief Project data provider callback for EXO conversion
 *
 * @param project_data Project data structure to fill
 * @param userdata User data passed to the function
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_drop_project_data_provider_fn)(struct gcmz_project_data *project_data,
                                                   void *userdata,
                                                   struct ov_error *const err);

/**
 * @brief File management callback
 *
 * @param source_file Source file path to process
 * @param final_file [out] Final file path (caller must OV_ARRAY_DESTROY)
 * @param userdata User data passed to the function
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_drop_file_manage_fn)(wchar_t const *source_file,
                                         wchar_t **final_file,
                                         void *userdata,
                                         struct ov_error *const err);

/**
 * @brief Create and initialize drop context
 *
 * @param extract_fn Data object extraction function
 * @param cleanup_fn Temporary files cleanup function
 * @param project_data_fn Project data provider function for EXO conversion
 * @param file_manage_fn File management function for copying/managing files (optional, can be NULL)
 * @param callback_userdata Shared user data for all callback functions
 * @param lua_context Lua context for scripting hooks (optional, can be NULL)
 * @param err [out] Error information on failure
 * @return Drop context pointer on success, NULL on failure
 */
struct gcmz_drop *gcmz_drop_create(gcmz_drop_dataobj_extract_fn const extract_fn,
                                   gcmz_drop_cleanup_temp_file_fn const cleanup_fn,
                                   gcmz_drop_project_data_provider_fn const project_data_fn,
                                   gcmz_drop_file_manage_fn const file_manage_fn,
                                   void *const callback_userdata,
                                   struct gcmz_lua_context *const lua_context,
                                   struct ov_error *const err);

/**
 * @brief Register a window for drop target functionality
 *
 * @param ctx Drop context
 * @param window Window handle to register
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_drop_register_window(struct gcmz_drop *const ctx, void *const window, struct ov_error *const err);

/**
 * @brief Destroy drop context and free memory
 *
 * @param ctx Drop context pointer
 */
void gcmz_drop_destroy(struct gcmz_drop **const ctx);

/**
 * @brief Simulate file drop operation
 *
 * @param ctx Drop context
 * @param window Window handle where drop is simulated (must be registered)
 * @param file_list File list to drop
 * @param x Drop X coordinate
 * @param y Drop Y coordinate
 * @param use_exo_converter Whether to enable EXO conversion
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_drop_simulate_drop(struct gcmz_drop *const ctx,
                             void *const window,
                             struct gcmz_file_list const *const file_list,
                             int const x,
                             int const y,
                             bool const use_exo_converter,
                             struct ov_error *const err);

/**
 * @brief Create IDataObject from file list
 *
 * Creates a COM IDataObject containing the specified files. The caller must call
 * IDataObject_Release() on the returned object when done.
 *
 * @param ctx Drop context
 * @param file_list File list to include in the data object
 * @param use_exo_converter Whether to wrap with EXO converter
 * @param err [out] Error information on failure
 * @return IDataObject pointer on success (caller must Release), NULL on failure
 */
void *gcmz_drop_create_dataobject(struct gcmz_drop *const ctx,
                                  struct gcmz_file_list const *const file_list,
                                  bool const use_exo_converter,
                                  struct ov_error *const err);

/**
 * @brief Inject IDataObject to registered window
 *
 * Simulates a drag-and-drop operation by injecting the IDataObject to the
 * specified window's drop target at the given coordinates.
 *
 * @param ctx Drop context
 * @param window Window handle where drop is injected (must be registered)
 * @param dataobj IDataObject pointer to inject
 * @param x Drop X coordinate (client coordinates)
 * @param y Drop Y coordinate (client coordinates)
 * @param use_exo_converter Whether to enable EXO conversion
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_drop_inject_dataobject(struct gcmz_drop *const ctx,
                                 void *const window,
                                 void *const dataobj,
                                 int const x,
                                 int const y,
                                 bool const use_exo_converter,
                                 struct ov_error *const err);

/**
 * @brief Get the last right-click position across all registered windows
 *
 * Retrieves the window handle and client coordinates of the most recent
 * right-click event that occurred on any registered window.
 *
 * @param ctx Drop context
 * @param window [out] Window handle where last right-click occurred
 * @param x [out] X coordinate of last right-click (client coordinates)
 * @param y [out] Y coordinate of last right-click (client coordinates)
 * @param err [out] Error information on failure
 * @return true on success, false on failure (no right-click recorded yet)
 */
bool gcmz_drop_get_right_click_position(
    struct gcmz_drop *const ctx, void **const window, int *const x, int *const y, struct ov_error *const err);
