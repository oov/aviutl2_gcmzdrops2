#pragma once

#include <stdbool.h>
#include <wchar.h>

static inline wchar_t to_lower(wchar_t c) {
  if (c >= L'A' && c <= L'Z') {
    return c | 0x20;
  }
  return c;
}

/**
 * @brief Case-insensitive ASCII comparison for extension strings
 *
 * Compares two wide-character strings using case-insensitive ASCII comparison.
 * Only ASCII characters (A-Z) are case-folded; other characters must match exactly.
 *
 * @param ext1 First extension string (e.g., L".txt" or pointer to extension in path)
 * @param ext2 Second extension string to compare against
 * @return true if strings match (case-insensitive), false otherwise
 *
 * @note Returns false if either ext1 or ext2 is NULL
 * @note Only ASCII uppercase letters (A-Z) are converted to lowercase
 * @note This function compares the entire strings, not just suffixes
 *
 * @example
 *   gcmz_extension_equals(L".TXT", L".txt")  // returns true
 *   gcmz_extension_equals(L".txt", L".TXT")  // returns true
 *   gcmz_extension_equals(L".doc", L".txt")  // returns false
 *   gcmz_extension_equals(NULL, L".txt")     // returns false
 */
static inline bool gcmz_extension_equals(wchar_t const *ext1, wchar_t const *ext2) {
  if (!ext1 || !ext2) {
    return false;
  }
  for (; *ext1 != L'\0' && *ext2 != L'\0'; ++ext1, ++ext2) {
    if (to_lower(*ext1) != to_lower(*ext2)) {
      return false;
    }
  }
  // Both strings must end at the same time
  return *ext1 == L'\0' && *ext2 == L'\0';
}
