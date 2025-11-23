#pragma once

#include <ovbase.h>

#include "gcmz_types.h"

struct gcmz_analyze;
struct gcmz_project_data;

/**
 * @brief Analysis status codes indicating success or specific failure reason
 */
enum gcmz_analyze_status {
  gcmz_analyze_status_invalid = 0,
  gcmz_analyze_status_success = 1,
  gcmz_analyze_status_zoom_bar_not_found = 2,
  gcmz_analyze_status_layer_window_not_found = 3,
  gcmz_analyze_status_effective_area_calculation_failed = 4,
  gcmz_analyze_status_cursor_detection_area_calculation_failed = 5,
};

/**
 * @brief Rectangle structure representing position and dimensions
 */
struct gcmz_analyze_rect {
  int x;
  int y;
  int width;
  int height;
};

/**
 * @brief Result structure from UI element detection analysis
 *
 * Contains positions of detected UI elements and analysis status.
 * Rectangle structures are only valid when status is gcmz_analyze_status_success.
 */
struct gcmz_analyze_result {
  struct gcmz_analyze_rect zoom_bar;
  struct gcmz_analyze_rect layer_window;
  struct gcmz_analyze_rect effective_area;
  struct gcmz_analyze_rect cursor_detection_area;
  struct gcmz_analyze_rect cursor; ///< Zeroed if cursor not detected
  enum gcmz_analyze_status status;
  int layer_height;
  void *window;
};

/**
 * @brief Callback function type for retrieving window list
 *
 * @param windows [out] Array to store window information
 * @param window_len Maximum number of windows to store
 * @param userdata User data passed to callback
 * @param err [out] Error information on failure
 * @return Number of windows retrieved, SIZE_MAX on error
 */
typedef size_t (*gcmz_analyze_get_window_list_func)(struct gcmz_window_info *const windows,
                                                    size_t const window_len,
                                                    void *const userdata,
                                                    struct ov_error *const err);

/**
 * @brief Callback function type for capturing window bitmap data
 *
 * @param window Window handle to capture
 * @param data [in/out] Captured image data (caller must free with OV_ARRAY_DESTROY)
 *                      Can be pre-allocated for reuse, or NULL to allocate new buffer
 * @param width [out] Captured image width
 * @param height [out] Captured image height
 * @param userdata User data passed to callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_analyze_capture_func)(void *const window,
                                          uint8_t **const data,
                                          int *const width,
                                          int *const height,
                                          void *const userdata,
                                          struct ov_error *const err);

/**
 * @brief Style for UI element detection
 */
struct gcmz_analyze_style {
  struct gcmz_color active_normal;
  struct gcmz_color active_hover;
  struct gcmz_color inactive_normal;
  struct gcmz_color inactive_hover;
  struct gcmz_color background;
  struct gcmz_color frame_cursor;
  struct gcmz_color frame_cursor_wide;
  int time_gauge_height;
  int layer_header_width;
  int scroll_bar_size;
  int layer_height;
  // These are not defined in style.conf
  int zoom_bar_margin;
  int zoom_bar_block_width;
  int zoom_bar_block_gap;
};

/**
 * @brief Callback function type for retrieving style
 *
 * @param style [out] Style structure to fill
 * @param userdata User data passed to callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_analyze_get_style_func)(struct gcmz_analyze_style *const style,
                                            void *const userdata,
                                            struct ov_error *const err);

struct gcmz_analyze_save_context;

/**
 * @brief Callback function for saving captured image to file
 *
 * @param ctx Context containing mutable bitmap data
 * @param path File path to save image
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_analyze_save_to_file_func)(struct gcmz_analyze_save_context *const ctx,
                                               NATIVE_CHAR const *const path,
                                               struct ov_error *const err);

/**
 * @brief Callback function type for handling analyze completion
 *
 * @param ctx Opaque context for saving image (callback is invoked only when bitmap data is available)
 * @param save_to_file Function to save image to file for the captured bitmap
 * @param status Analysis status (success or specific error type)
 * @param userdata User data passed to callback
 * @param err [out] Error information (can be set by this callback)
 * @return true on success, false on failure
 */
typedef bool (*gcmz_analyze_complete_func)(struct gcmz_analyze_save_context *const ctx,
                                           gcmz_analyze_save_to_file_func save_to_file,
                                           enum gcmz_analyze_status const status,
                                           void *userdata,
                                           struct ov_error *const err);

/**
 * @brief Options for creating a analyze instance
 */
struct gcmz_analyze_options {
  gcmz_analyze_capture_func capture;
  gcmz_analyze_get_window_list_func get_window_list;
  gcmz_analyze_get_style_func get_style;
  void *userdata;
};

/**
 * @brief Create and initialize a analyze instance with options
 *
 * @param ap [out] Pointer to store the created analyze instance
 * @param options Options for analyze instance (NULL for defaults)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_analyze_create(struct gcmz_analyze **const ap,
                                   struct gcmz_analyze_options const *const options,
                                   struct ov_error *const err);

/**
 * @brief Destroy a analyze instance and free all associated resources
 *
 * @param ap Analyze instance to destroy
 */
void gcmz_analyze_destroy(struct gcmz_analyze **const ap);

/**
 * @brief Capture and analyze window layout information
 *
 * @param a Analyze instance
 * @param zoom Zoom value for validation (< 0 to skip validation)
 * @param result [out] Analysis results
 * @param on_analyze_complete Optional callback for handling completion (can be NULL)
 * @param complete_userdata User data passed to completion callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_analyze_run(struct gcmz_analyze *const a,
                                int const zoom,
                                struct gcmz_analyze_result *const result,
                                gcmz_analyze_complete_func on_analyze_complete,
                                void *const complete_userdata,
                                struct ov_error *const err);
