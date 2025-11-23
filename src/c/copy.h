#pragma once

#include <ovbase.h>

#include "gcmz_types.h"

/**
 * @brief Callback function for retrieving save path for file management
 *
 * @param filename Hash-based filename to save (e.g., "image.12345678.png")
 * @param userdata User-provided context data
 * @param err [out] Error information on failure
 * @return Allocated full path where file should be saved, or NULL on error
 */
typedef wchar_t *(*gcmz_copy_get_save_path_fn)(wchar_t const *filename, void *userdata, struct ov_error *err);

/**
 * @brief Manage file processing including hash-based caching and copying
 *
 * This function determines whether a file needs to be copied based on processing mode,
 * calculates file hash if copy is needed, searches for existing cached file with same hash,
 * and copies the file to cache directory if no existing file is found.
 *
 * @param source_file Source file path to process
 * @param processing_mode Processing mode (auto/direct/copy) determining copy behavior
 * @param get_save_path Callback function to get destination path for file
 * @param userdata User data passed to get_save_path callback
 * @param final_file [out] Allocated final file path to use (either original or cached copy)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_copy(wchar_t const *const source_file,
                         enum gcmz_processing_mode processing_mode,
                         gcmz_copy_get_save_path_fn get_save_path,
                         void *userdata,
                         wchar_t **const final_file,
                         struct ov_error *const err);
