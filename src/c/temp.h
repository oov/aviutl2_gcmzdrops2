#pragma once

#include <ovbase.h>

/**
 * @brief Callback function type for cleanup notifications
 *
 * Called when a stale temporary directory is successfully removed.
 *
 * @param dir_path Full path to the removed directory
 * @param userdata User-provided data passed to gcmz_temp_cleanup_stale_directories
 */
typedef void (*gcmz_temp_cleanup_callback_fn)(wchar_t const *dir_path, void *userdata);

/**
 * @brief Cleanup stale temporary directories
 *
 * Removes temporary directories that are not locked by running processes.
 * Simply attempts to delete directories - if locked, they will be skipped.
 *
 * @param callback Optional callback function called for each removed directory (can be NULL)
 * @param userdata User data passed to callback function
 * @param err [out] Error information
 * @return true on success, false on failure
 */
bool gcmz_temp_cleanup_stale_directories(gcmz_temp_cleanup_callback_fn callback,
                                         void *userdata,
                                         struct ov_error *const err);

/**
 * @brief Create temporary directory for GCMZDrops
 *
 * Creates a process-specific temporary directory under system temp path.
 * Directory name format: temp_dir/gcmzdrops{pid}
 *
 * @param err [out] Error information
 * @return true on success, false on failure
 */
bool gcmz_temp_create_directory(struct ov_error *const err);

/**
 * @brief Remove temporary directory
 *
 * Removes the temporary directory created by gcmz_temp_create_directory().
 */
void gcmz_temp_remove_directory(void);

/**
 * @brief Build path for temporary file
 *
 * Constructs a full path for a file in the temporary directory.
 *
 * @param dest [out] Full path to temporary file (caller must OV_ARRAY_DESTROY)
 * @param filename File name to place in temporary directory
 * @param err [out] Error information
 * @return true on success, false on failure
 */
bool gcmz_temp_build_path(wchar_t **const dest, wchar_t const *const filename, struct ov_error *const err);

/**
 * @brief Create unique temporary file
 *
 * Creates a temporary file with a unique name derived from the given filename.
 *
 * @param filename Base filename to create
 * @param dest_path [out] Full path to created file (caller must OV_ARRAY_DESTROY)
 * @param err [out] Error information
 * @return true on success, false on failure
 */
bool gcmz_temp_create_unique_file(wchar_t const *const filename, wchar_t **const dest_path, struct ov_error *const err);
