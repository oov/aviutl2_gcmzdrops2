#pragma once

#include <ovbase.h>

/**
 * @brief Data URI encoding type
 */
enum gcmz_data_uri_encoding {
  data_uri_encoding_percent = 0, ///< Percent-encoding
  data_uri_encoding_base64 = 1,  ///< Base64 encoding
};

/**
 * @brief Parsed data URI information
 */
struct gcmz_data_uri {
  wchar_t mime[256];
  wchar_t charset[128];
  wchar_t ext_filename[128]; ///< Non-standard extension
  int encoding;
  wchar_t const *encoded; ///< Pointer into original string (not owned)
  size_t encoded_len;
  void *decoded; ///< Allocated by gcmz_data_uri_decode(), freed by gcmz_data_uri_destroy()
  size_t decoded_len;
};

/**
 * @brief Parse a data URI string
 *
 * Parses a data URI string into its components (MIME type, charset, encoding, data).
 * The decoded data is not automatically decompressed; call gcmz_data_uri_decode() to decompress.
 *
 * @param ws Data URI string (wide character, null-terminated)
 * @param wslen Length of ws in characters (not including null terminator)
 * @param d [out] Parsed data URI information
 * @param err [out] Error information
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_data_uri_parse(wchar_t const *const ws,
                                   size_t const wslen,
                                   struct gcmz_data_uri *const d,
                                   struct ov_error *const err);
/**
 * @brief Decode the encoded data in a parsed data URI
 *
 * Decodes the encoded data portion (base64 or percent-encoded).
 * Must be called after gcmz_data_uri_parse().
 *
 * @param d [in,out] Parsed data URI structure to decode
 * @param err [out] Error information
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_data_uri_decode(struct gcmz_data_uri *const d, struct ov_error *const err);

/**
 * @brief Destroy data URI structure and free allocated memory
 *
 * Cleans up and frees decoded data. Safe to call with NULL.
 *
 * @param d Data URI structure to destroy
 */
void gcmz_data_uri_destroy(struct gcmz_data_uri *const d);

/**
 * @brief Get suggested filename from data URI
 *
 * Extracts or constructs a filename for the data.
 *
 * @param d Parsed data URI structure
 * @param dest [out] Suggested filename (caller must OV_ARRAY_DESTROY)
 * @param err [out] Error information
 * @return true on success, false on failure
 */
NODISCARD bool
gcmz_data_uri_suggest_filename(struct gcmz_data_uri const *const d, wchar_t **const dest, struct ov_error *const err);

/**
 * @brief Get MIME type from data URI
 *
 * Extracts the MIME type as a dynamically allocated string.
 *
 * @param d Parsed data URI structure
 * @param dest [out] MIME type string (caller must OV_ARRAY_DESTROY)
 * @param err [out] Error information
 * @return true on success, false on failure
 */
NODISCARD bool
gcmz_data_uri_get_mime(struct gcmz_data_uri const *const d, wchar_t **const dest, struct ov_error *const err);
