#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Detect MIME type and file extension from data
 *
 * Analyzes binary data to detect its MIME type and file extension
 * using WHATWG MIME Sniffing Standard. Supports various formats
 * including images, audio, video, and text.
 *
 * @param data Binary data to analyze. Must not be NULL.
 * @param len Size of data in bytes
 * @param mime [out] Pointer to MIME type string (wide character, pointer to static string)
 * @param ext [out] Pointer to file extension string (wide character, pointer to static string)
 * @return true if data format was recognized, false if unable to detect
 *
 * @note Returned mime and ext pointers point to static strings and should not be freed.
 */
bool gcmz_sniff(void const *const data, size_t const len, wchar_t const **const mime, wchar_t const **const ext);
