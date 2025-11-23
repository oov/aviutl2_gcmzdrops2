#pragma once

#include <ovbase.h>

#include "analyze.h"

/**
 * @brief Private metadata structure for PNG encoding/decoding
 */
struct gcmz_analyze_metadata {
  int zoom;
  enum gcmz_analyze_status status;
  uint64_t timestamp_us; ///< Microseconds since Unix epoch
  struct gcmz_analyze_style style;
};

/**
 * @brief Save bitmap data to PNG file with debug metadata
 *
 * Performs in-place color space conversion (BGR ↔ RGB) around the PNG encoding.
 * Embeds debug information as text chunks in the PNG file.
 *
 * @param filepath Output file path
 * @param bitmap Mutable bitmap data (BGR format, 24-bit, 4-byte aligned rows)
 * @param width Bitmap width
 * @param height Bitmap height
 * @param metadata Metadata to embed in PNG file
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_analyze_save_png_with_metadata(NATIVE_CHAR const *const filepath,
                                                   uint8_t *const bitmap,
                                                   int const width,
                                                   int const height,
                                                   struct gcmz_analyze_metadata const *const metadata,
                                                   struct ov_error *const err);

/**
 * @brief Load PNG file and extract metadata
 *
 * @param filepath Input PNG file path
 * @param bitmap [out] Decoded bitmap data (BGR format, 24-bit, 4-byte aligned rows)
 * @param width [out] Bitmap width
 * @param height [out] Bitmap height
 * @param metadata [out] Extracted metadata from PNG file
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_analyze_load_png_with_metadata(wchar_t const *const filepath,
                                                   uint8_t **const bitmap,
                                                   int *const width,
                                                   int *const height,
                                                   struct gcmz_analyze_metadata *const metadata,
                                                   struct ov_error *const err);
