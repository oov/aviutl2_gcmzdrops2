#pragma once

#include <ovbase.h>

struct gcmz_file_list;

/**
 * @brief Initialize the delayed cleanup system
 *
 * Creates and starts a dedicated worker thread for delayed file deletion.
 * Files scheduled for deletion will be removed after a configurable delay
 * to ensure they are no longer in use.
 *
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for details)
 *
 * @note This function must be called before any other delayed cleanup functions.
 *       Call gcmz_delayed_cleanup_exit() to properly shut down the system.
 */
NODISCARD bool gcmz_delayed_cleanup_init(struct ov_error *const err);

/**
 * @brief Shutdown the delayed cleanup system
 *
 * Stops the worker thread and cleans up all resources. Any remaining
 * scheduled files will be processed before shutdown. This function
 * blocks until the worker thread has fully stopped.
 *
 * @note This function is safe to call multiple times or without prior
 *       initialization. After calling this function, gcmz_delayed_cleanup_init()
 *       must be called again before using other functions.
 */
void gcmz_delayed_cleanup_exit(void);

/**
 * @brief Schedule a single file for delayed deletion
 *
 * Adds a file to the deletion queue. The file will be deleted after
 * the configured delay period (default: 30 seconds) from the time
 * this function is called.
 *
 * @param file_path Wide character file path string. Must not be NULL and must be null-terminated.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for details)
 *
 * @note The file path is copied internally, so the caller retains ownership.
 *       If the file does not exist when deletion time arrives, the operation
 *       is considered successful (no error is reported).
 * @note This function is thread-safe and can be called from any thread.
 */
NODISCARD bool gcmz_delayed_cleanup_schedule_file(wchar_t const *const file_path, struct ov_error *const err);

/**
 * @brief Schedule all temporary files in a file list for delayed deletion
 *
 * Iterates through the file list and schedules all files marked as temporary
 * for delayed deletion. Non-temporary files are ignored. After scheduling,
 * the temporary flags are cleared to prevent double-deletion.
 *
 * @param files Pointer to file list. Must not be NULL.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for details)
 *
 * @note This function modifies the file list by clearing temporary flags.
 *       It is the recommended way to migrate from immediate deletion to delayed deletion.
 * @note This function is thread-safe and can be called from any thread.
 */
NODISCARD bool gcmz_delayed_cleanup_schedule_temporary_files(struct gcmz_file_list *const files,
                                                             struct ov_error *const err);
