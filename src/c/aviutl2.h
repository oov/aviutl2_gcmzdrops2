#pragma once

#include <ovbase.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct gcmz_project_data;
struct aviutl2_edit_handle;
struct aviutl2_log_handle;

/**
 * @brief Status codes for AviUtl2 version detection and initialization
 */
enum gcmz_aviutl2_status {
  gcmz_aviutl2_status_error,            ///< Fatal error (err will be set)
  gcmz_aviutl2_status_success,          ///< Version detected + signature verified
  gcmz_aviutl2_status_signature_failed, ///< Version detected + signature verification failed
  gcmz_aviutl2_status_unknown_binary,   ///< Unknown binary (version not detected, limited functionality)
};

/**
 * @brief Initialize AviUtl2 integration
 *
 * Initializes AviUtl2 integration layer and sets up internal state.
 * Even if initialization fails to detect the binary version, basic functionality
 * like finding manager windows may still be available.
 *
 * @param err [out] Error information on failure (only set for gcmz_aviutl2_status_error)
 * @return gcmz_aviutl2_status_success on full success
 *         gcmz_aviutl2_status_signature_failed if INI signature verification failed
 *         gcmz_aviutl2_status_unknown_binary if binary version not found (limited functionality)
 *         gcmz_aviutl2_status_error on fatal error (err will be set)
 */
NODISCARD enum gcmz_aviutl2_status gcmz_aviutl2_init(struct ov_error *const err);

/**
 * @brief Clean up AviUtl2 integration
 *
 * Cleans up resources allocated by gcmz_aviutl2_init().
 */
void gcmz_aviutl2_cleanup(void);

/**
 * @brief Get main AviUtl2 window handle
 *
 * @return Window handle to AviUtl2 main window, or NULL if unavailable
 */
NODISCARD HWND gcmz_aviutl2_get_main_window(void);

/**
 * @brief Find all aviutl2Manager windows in the current process
 *
 * @param window [out] Array to store found window handles
 * @param window_len Maximum number of handles to store
 * @param err [out] Error information on failure
 * @return Number of windows found, SIZE_MAX on error
 */
NODISCARD size_t gcmz_aviutl2_find_manager_windows(void **window, size_t window_len, struct ov_error *const err);

/**
 * @brief Get current project file path
 *
 * @return Project file path, or NULL if no project is open (do not free, internal pointer)
 */
wchar_t const *gcmz_aviutl2_get_project_path(void);

/**
 * @brief Get extended project information not provided by official API
 *
 * @param display_frame [out] Current display frame, can be NULL if not needed
 * @param display_layer [out] Current display layer, can be NULL if not needed
 * @param display_zoom [out] Current display zoom, can be NULL if not needed
 * @param project_path [out] Project path (do not free, internal pointer), can be NULL if not needed
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_aviutl2_get_extended_project_info(int *display_frame,
                                                      int *display_layer,
                                                      int *display_zoom,
                                                      wchar_t const **project_path,
                                                      struct ov_error *const err);

/**
 * @brief Set current cursor frame position
 *
 * @param frame Frame number to set
 */
void gcmz_aviutl2_set_cursor_frame(int frame);

/**
 * @brief Set current display layer
 *
 * @param layer Layer number to set
 */
void gcmz_aviutl2_set_display_layer(int layer);

/**
 * @brief Set current display zoom level
 *
 * @param zoom Zoom level to set
 */
void gcmz_aviutl2_set_display_zoom(int zoom);

/**
 * @brief Create simulated aviutl2_edit_handle for accessing project info
 *
 * @return Pointer to aviutl2_edit_handle, or NULL if unavailable
 */
struct aviutl2_edit_handle *gcmz_aviutl2_create_simulated_edit_handle(void);

/**
 * @brief Create simulated aviutl2_log_handle
 *
 * @return Pointer to aviutl2_log_handle, or NULL if unavailable
 */
struct aviutl2_log_handle *gcmz_aviutl2_create_simulated_log_handle(void);

/**
 * @brief Get detected AviUtl2 version name
 *
 * @return Version name string, or NULL if version was not detected (do not free, internal pointer)
 */
char const *gcmz_aviutl2_get_detected_version(void);

/**
 * @brief Get detected AviUtl2 version as uint32_t
 *
 * @return Version number as uint32_t (e.g., 2002100 for 2.0beta21), or 0 if version was not detected
 */
uint32_t gcmz_aviutl2_get_detected_version_uint32(void);
