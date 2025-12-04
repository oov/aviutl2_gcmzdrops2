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
 * @param file_manage_fn File management function for copying/managing files (optional, can be NULL)
 * @param callback_userdata Shared user data for all callback functions
 * @param lua_context Lua context for scripting hooks
 * @param err [out] Error information on failure
 * @return Drop context pointer on success, NULL on failure
 */
struct gcmz_drop *gcmz_drop_create(gcmz_drop_dataobj_extract_fn const extract_fn,
                                   gcmz_drop_cleanup_temp_file_fn const cleanup_fn,
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
 * @brief Create IDataObject from file list with CF_HDROP format
 *
 * Creates a new IDataObject containing file paths in CF_HDROP format.
 * The returned object must be released by the caller using IDataObject_Release.
 *
 * @param file_list File list to include in the data object
 * @param x X coordinate to store in DROPFILES structure
 * @param y Y coordinate to store in DROPFILES structure
 * @param err [out] Error information on failure
 * @return IDataObject pointer (as void*) on success, NULL on failure
 */
void *gcmz_drop_create_file_list_dataobj(struct gcmz_file_list const *const file_list,
                                         int const x,
                                         int const y,
                                         struct ov_error *const err);

/**
 * @brief Context for drop completion
 *
 * Contains all parameters needed for IDropTarget::Drop.
 * Fields can be modified before calling complete function.
 */
struct gcmz_drop_complete_context {
  // Processed data (read-only)
  struct gcmz_file_list *final_files; ///< Processed file list after Lua hooks and file management

  // Drop parameters (modifiable)
  void *window;           ///< Target window handle
  int x;                  ///< Drop X coordinate (screen coordinates)
  int y;                  ///< Drop Y coordinate (screen coordinates)
  uint32_t key_state;     ///< Key state flags (MK_CONTROL, MK_SHIFT, etc.)
  uint32_t modifier_keys; ///< Additional modifier keys (gcmz_modifier_key_flags)
  uint32_t drop_effect;   ///< Allowed drop effects (DROPEFFECT_*)

  // User data
  void *userdata; ///< User data passed via completion_userdata parameter
};

/**
 * @brief Complete function type for finishing drop operation
 *
 * This function MUST be called exactly once after the completion callback is invoked.
 * It performs either the actual drop or cancellation, and releases all resources.
 * Can be called synchronously within the callback or asynchronously later.
 *
 * @param ctx Complete context (will be invalidated and freed after this call)
 * @param execute_drop true to execute IDropTarget::Drop, false to call DragLeave (cancel)
 */
typedef void (*gcmz_drop_complete_func)(struct gcmz_drop_complete_context *ctx, bool execute_drop);

/**
 * @brief Callback invoked after drop processing is complete
 *
 * This callback is called after all Lua hooks and file management have been executed.
 * The callback receives the complete context and a complete function.
 *
 * IMPORTANT: The complete function MUST be called exactly once, either:
 * - Immediately within this callback, or
 * - Later asynchronously (e.g., after showing a dialog, network operation, etc.)
 *
 * @param ctx Complete context (valid until complete is called)
 * @param complete Function to complete the drop operation (MUST be called exactly once)
 * @param userdata User data passed via completion_userdata parameter
 */
typedef void (*gcmz_drop_completion_callback)(struct gcmz_drop_complete_context *ctx,
                                              gcmz_drop_complete_func complete,
                                              void *userdata);

/**
 * @brief Simulate file drop operation with IDataObject
 *
 * Simulates a drag-and-drop operation by passing the IDataObject to the
 * specified window's drop target at the given coordinates.
 *
 * @param ctx Drop context
 * @param window Window handle where drop is simulated (must be registered)
 * @param dataobj IDataObject pointer to drop
 * @param x Drop X coordinate (client coordinates)
 * @param y Drop Y coordinate (client coordinates)
 * @param use_exo_converter Whether to enable EXO conversion
 * @param from_external_api Whether this drop originated from external API
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_drop_simulate_drop(struct gcmz_drop *const ctx,
                             void *const window,
                             void *const dataobj,
                             int const x,
                             int const y,
                             bool const use_exo_converter,
                             bool const from_external_api,
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

/**
 * @brief Simulate file drop operation for external API with pre-processed Lua hooks
 *
 * This function is specifically designed for external API integration where Lua
 * handlers need to be called independently from the normal hook-based drop flow.
 *
 * Unlike gcmz_drop_simulate_drop which uses the wrapped IDropTarget hook chain,
 * this function:
 * 1. Extracts files from the IDataObject
 * 2. Performs EXO conversion if enabled
 * 3. Calls Lua handlers (drag_enter, drop) in sequence
 * 4. Applies file management (copying, etc.)
 * 5. Calls completion callback with processed file list
 * 6. Completion callback decides whether to drop or cancel
 *
 * This allows the completion callback to receive the fully processed file list
 * and modify drop parameters before the actual drop occurs.
 *
 * @param ctx Drop context
 * @param window Window handle where drop is simulated (must be registered)
 * @param dataobj IDataObject pointer to drop
 * @param x Drop X coordinate (client coordinates)
 * @param y Drop Y coordinate (client coordinates)
 * @param use_exo_converter Whether to enable EXO conversion
 * @param completion_callback Callback for drop completion (required, must not be NULL)
 * @param completion_userdata User data passed to completion callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_drop_simulate_drop_external(struct gcmz_drop *const ctx,
                                      void *const window,
                                      void *const dataobj,
                                      int const x,
                                      int const y,
                                      bool const use_exo_converter,
                                      gcmz_drop_completion_callback const completion_callback,
                                      void *const completion_userdata,
                                      struct ov_error *const err);
