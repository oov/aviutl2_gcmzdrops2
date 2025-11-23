#pragma once

#include <ovbase.h>

struct aviutl2_log_handle;

/**
 * @brief Set log handle with priority management
 *
 * This function manages log handle assignment with a priority system:
 * - The first non-NULL handle set is retained (highest priority)
 * - Subsequent non-NULL handles are ignored
 * - NULL can always be passed to reset the handle
 *
 * @param handle Logger handle to set, or NULL to reset
 */
void gcmz_logf_set_handle(struct aviutl2_log_handle *handle);

/**
 * @brief Output verbose level log message
 *
 * @param err Error information to include (can be NULL)
 * @param reference Reference format string (can be NULL)
 * @param format Printf-style format string (can be NULL if err is provided)
 */
void gcmz_logf_verbose(struct ov_error const *const err, char const *const reference, char const *const format, ...);

/**
 * @brief Output info level log message
 *
 * @param err Error information to include (can be NULL)
 * @param reference Reference format string (can be NULL)
 * @param format Printf-style format string (can be NULL if err is provided)
 */
void gcmz_logf_info(struct ov_error const *const err, char const *const reference, char const *const format, ...);

/**
 * @brief Output warning level log message
 *
 * @param err Error information to include (can be NULL)
 * @param reference Reference format string (can be NULL)
 * @param format Printf-style format string (can be NULL if err is provided)
 */
void gcmz_logf_warn(struct ov_error const *const err, char const *const reference, char const *const format, ...);

/**
 * @brief Output error level log message
 *
 * @param err Error information to include (can be NULL)
 * @param reference Reference format string (can be NULL)
 * @param format Printf-style format string (can be NULL if err is provided)
 */
void gcmz_logf_error(struct ov_error const *const err, char const *const reference, char const *const format, ...);
