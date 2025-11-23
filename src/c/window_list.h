#pragma once

#include <ovbase.h>

struct gcmz_window_info;
struct gcmz_window_list;

/**
 * @brief Create window list
 *
 * Creates a new window list container for tracking window information.
 *
 * @param err [out] Error information
 * @return Pointer to new window list on success, NULL on failure
 */
NODISCARD struct gcmz_window_list *gcmz_window_list_create(struct ov_error *const err);

/**
 * @brief Destroy window list and free memory
 *
 * @param wl Pointer to window list pointer
 */
void gcmz_window_list_destroy(struct gcmz_window_list **const wl);

/**
 * @brief Update window list with new window information
 *
 * Updates the internal list with new window information.
 * Returns whether the list contents changed (windows added/removed/reordered).
 *
 * @param wl Window list to update
 * @param windows Array of new window information
 * @param num_windows Number of windows in array
 * @param err [out] Error information
 * @return ov_true if list changed, ov_false if unchanged, ov_indeterminate on error
 */
NODISCARD ov_tribool gcmz_window_list_update(struct gcmz_window_list *const wl,
                                             struct gcmz_window_info const *const windows,
                                             size_t const num_windows,
                                             struct ov_error *const err);

/**
 * @brief Get window information from list
 *
 * Returns the current window information array and its count.
 *
 * @param wl Window list
 * @param num_windows [out] Number of windows in returned array
 * @return Pointer to window information array, NULL on error
 */
struct gcmz_window_info const *gcmz_window_list_get(struct gcmz_window_list const *const wl, size_t *const num_windows);
