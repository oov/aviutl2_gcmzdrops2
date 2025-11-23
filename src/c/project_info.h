#pragma once

#include <ovbase.h>

struct aviutl2_edit_handle;
struct gcmz_project_data;

/**
 * @brief Callback for retrieving extended project information
 *
 * @param display_frame [out] Current display frame, can be NULL if not needed
 * @param display_layer [out] Current display layer, can be NULL if not needed
 * @param display_zoom [out] Current display zoom, can be NULL if not needed
 * @param project_path [out] Project path (do not free, internal pointer), can be NULL if not needed
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
typedef bool (*gcmz_extended_project_info_getter)(int *display_frame,
                                                  int *display_layer,
                                                  int *display_zoom,
                                                  wchar_t const **project_path,
                                                  struct ov_error *const err);

/**
 * @brief Set edit handle for official project information retrieval
 *
 * The first non-NULL handle set is retained. Passing NULL resets the handle.
 *
 * @param handle Edit handle to set
 */
void gcmz_project_info_set_handle(struct aviutl2_edit_handle *handle);

/**
 * @brief Set callback for retrieving extended project information
 *
 * The first non-NULL callback set is retained. Passing NULL resets the callback.
 *
 * @param getter Callback function to set
 */
void gcmz_project_info_set_extended_getter(gcmz_extended_project_info_getter getter);

/**
 * @brief Get current project data
 *
 * Retrieves project data using the official edit handle when available,
 * falling back to legacy access methods if needed.
 *
 * @param data [out] Project data structure to fill
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_project_info_get(struct gcmz_project_data *const data, struct ov_error *const err);
