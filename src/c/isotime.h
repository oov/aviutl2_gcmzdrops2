#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Parse ISO 8601 timestamp string to microseconds since Unix epoch
 *
 * Supports formats: YYYY-MM-DD[THH[:MM[:SS[.ssssss]]]][+HH[:MM]|-HH[:MM]|Z]
 *
 * @param str ISO 8601 string
 * @param len Length of string
 * @param timestamp_us [out] Timestamp in microseconds since Unix epoch
 * @param tz_offset_sec [out] Timezone offset in seconds (NULL to ignore)
 * @return true on success, false on parse failure
 */
bool isotime_parse(char const *const str, size_t const len, uint64_t *const timestamp_us, int32_t *const tz_offset_sec);

/**
 * @brief Convert microseconds since Unix epoch to ISO 8601 string
 *
 * Supports formats: YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]
 *
 * @param timestamp_us Timestamp in microseconds since Unix epoch
 * @param buf Output buffer for ISO 8601 string (must be at least 26 bytes)
 * @param tz_offset_sec Timezone offset in seconds (0 produces Z)
 * @return buf on success, NULL on failure (e.g., when buf is NULL)
 */
char *isotime_format(uint64_t const timestamp_us, char buf[26], int32_t const tz_offset_sec);

/**
 * @brief Get current time as microseconds since Unix epoch (UTC)
 *
 * @return Current time in microseconds since Unix epoch
 */
uint64_t isotime_now(void);
