#include "analyze_meta.h"

#include <string.h>

#include <ovarray.h>
#include <ovl/file.h>
#include <ovprintf.h>

#include "isotime.h"
#include "json.h"
#include "lodepng.h"

static char const g_meta_key[] = "X-GCMZ-Metadata";

/**
 * Get result reason as string for PNG metadata
 * @param status Result reason enum
 * @return String representation of result reason
 */
static char const *status_to_string(enum gcmz_analyze_status const status) {
  switch (status) {
  case gcmz_analyze_status_success:
    return "success";
  case gcmz_analyze_status_zoom_bar_not_found:
    return "zoom_bar_not_found";
  case gcmz_analyze_status_layer_window_not_found:
    return "layer_window_not_found";
  case gcmz_analyze_status_effective_area_calculation_failed:
    return "effective_area_calculation_failed";
  case gcmz_analyze_status_cursor_detection_area_calculation_failed:
    return "cursor_detection_area_calculation_failed";
  case gcmz_analyze_status_invalid:
    return "invalid";
  }
  return "unknown";
}

static enum gcmz_analyze_status status_from_string(char const *const str) {
  if (!str) {
    return gcmz_analyze_status_invalid;
  }
  if (strcmp(str, "success") == 0) {
    return gcmz_analyze_status_success;
  }
  if (strcmp(str, "zoom_bar_not_found") == 0) {
    return gcmz_analyze_status_zoom_bar_not_found;
  }
  if (strcmp(str, "layer_window_not_found") == 0) {
    return gcmz_analyze_status_layer_window_not_found;
  }
  if (strcmp(str, "effective_area_calculation_failed") == 0) {
    return gcmz_analyze_status_effective_area_calculation_failed;
  }
  if (strcmp(str, "cursor_detection_area_calculation_failed") == 0) {
    return gcmz_analyze_status_cursor_detection_area_calculation_failed;
  }
  if (strcmp(str, "invalid") == 0) {
    return gcmz_analyze_status_invalid;
  }
  return gcmz_analyze_status_invalid;
}

/**
 * Convert gcmz_color to hexadecimal color string (#rrggbb format)
 * @param color Color to convert
 * @param buf Output buffer (must be at least 8 bytes: "#rrggbb\0")
 * @return buf pointer
 */
static inline char *color_to_hex(struct gcmz_color const color, char buf[8]) {
  ov_snprintf_char(buf, 8, NULL, "#%02x%02x%02x", color.r, color.g, color.b);
  return buf;
}

/**
 * Swap R and B channels in a bitmap in-place (BGR <-> RGB conversion)
 * @param bitmap Bitmap data in BGR format (24-bit)
 * @param width Bitmap width
 * @param height Bitmap height
 */
static inline void swap_rb_channels(uint8_t *const bitmap, int const width, int const height) {
  int const row_size = width * 3;
  int const stride = (row_size + 3) & ~3;
  uint8_t *const bitmap_end = bitmap + height * stride;
  for (uint8_t *row = bitmap; row < bitmap_end; row += stride) {
    uint8_t *const row_end = row + row_size;
    for (uint8_t *px = row; px < row_end; px += 3) {
      uint8_t const tmp = px[0];
      px[0] = px[2];
      px[2] = tmp;
    }
  }
}

/**
 * Serialize metadata to JSON string
 * @param zoom Zoom value
 * @param timestamp_us Timestamp in microseconds since Unix epoch
 * @param status Analysis status
 * @param style Style information
 * @param buf Output buffer for JSON string
 * @param buf_size Maximum size of output buffer
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool serialize_metadata_to_json(int const zoom,
                                       uint64_t const timestamp_us,
                                       enum gcmz_analyze_status const status,
                                       struct gcmz_analyze_style const *const style,
                                       char *const buf,
                                       size_t const buf_size,
                                       struct ov_error *const err) {
  if (!style || !buf) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  size_t len = 0;
  char iso8601_buf[26];
  bool result = false;

  yyjson_mut_doc *doc = yyjson_mut_doc_new(gcmz_json_get_alc());
  if (!doc) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }

  {
    yyjson_mut_val *const root = yyjson_mut_obj(doc);
    if (!root) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "status", status_to_string(status));
    yyjson_mut_obj_add_int(doc, root, "zoom", zoom);
    yyjson_mut_obj_add_strcpy(doc, root, "creation_time", isotime_format(timestamp_us, iso8601_buf, 0));

    {
      yyjson_mut_val *style_obj = yyjson_mut_obj(doc);
      if (!style_obj) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }

      char color_buf[8];
#define ADD_COLOR_FIELD(field_name)                                                                                    \
  yyjson_mut_obj_add_strcpy(doc, style_obj, #field_name, color_to_hex(style->field_name, color_buf))
      ADD_COLOR_FIELD(active_normal);
      ADD_COLOR_FIELD(active_hover);
      ADD_COLOR_FIELD(inactive_normal);
      ADD_COLOR_FIELD(inactive_hover);
      ADD_COLOR_FIELD(background);
      ADD_COLOR_FIELD(frame_cursor);
      ADD_COLOR_FIELD(frame_cursor_wide);
#undef ADD_COLOR_FIELD

#define ADD_INT_FIELD(field_name) yyjson_mut_obj_add_int(doc, style_obj, #field_name, style->field_name)
      ADD_INT_FIELD(time_gauge_height);
      ADD_INT_FIELD(layer_header_width);
      ADD_INT_FIELD(scroll_bar_size);
      ADD_INT_FIELD(layer_height);
      ADD_INT_FIELD(zoom_bar_margin);
      ADD_INT_FIELD(zoom_bar_block_width);
      ADD_INT_FIELD(zoom_bar_block_gap);
#undef ADD_INT_FIELD

      yyjson_mut_obj_add_val(doc, root, "style", style_obj);
    }

    char const *json_cstr = yyjson_mut_write(doc, 0, &len);
    if (!json_cstr) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (len >= buf_size) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    memcpy(buf, json_cstr, len + 1);
  }

  result = true;

cleanup:
  if (doc) {
    yyjson_mut_doc_free(doc);
  }
  return result;
}

bool gcmz_analyze_save_png_with_metadata(NATIVE_CHAR const *const filepath,
                                         uint8_t *const bitmap,
                                         int const width,
                                         int const height,
                                         struct gcmz_analyze_metadata const *const metadata,
                                         struct ov_error *const err) {
  if (!bitmap || !metadata || !filepath) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_file *file = NULL;
  uint8_t *png_data = NULL;
  size_t png_bytes = 0;
  LodePNGState state;
  bool state_initialized = false;
  bool result = false;

  {
    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_RGB;
    state.info_raw.bitdepth = 8;
    state_initialized = true;

    if (lodepng_add_text(&state.info_png, "Software", "GCMZDrops") != 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    {
      char metadata_json[1024];
      if (!serialize_metadata_to_json(metadata->zoom,
                                      metadata->timestamp_us,
                                      metadata->status,
                                      &metadata->style,
                                      metadata_json,
                                      sizeof(metadata_json),
                                      err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      unsigned const add_result = lodepng_add_text(&state.info_png, g_meta_key, metadata_json);
      if (add_result != 0) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }
    }
    {
      size_t const compact_row_size = (size_t)width * 3;
      size_t const padded_row_size = (size_t)((width * 3 + 3) & ~3);

      // convert BGR to RGB and remove padding
      swap_rb_channels(bitmap, width, height);
      for (size_t y = 1; y < (size_t)height; ++y) {
        memmove(bitmap + y * compact_row_size, bitmap + y * padded_row_size, compact_row_size);
      }

      unsigned int const r =
          lodepng_encode(&png_data, &png_bytes, bitmap, (unsigned int)width, (unsigned int)height, &state);

      // Restore
      for (size_t y = (size_t)(height - 1); y > 0; --y) {
        memmove(bitmap + y * padded_row_size, bitmap + y * compact_row_size, compact_row_size);
      }
      swap_rb_channels(bitmap, width, height);

      if (r != 0) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }
    }

    if (!ovl_file_create(filepath, &file, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    size_t written = 0;
    if (!ovl_file_write(file, png_data, png_bytes, &written, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (file) {
    ovl_file_close(file);
  }
  if (png_data) {
    OV_FREE(&png_data);
  }
  if (state_initialized) {
    lodepng_state_cleanup(&state);
  }
  return result;
}

/**
 * Convert a single hexadecimal character to its integer value
 * @param c Hexadecimal character ('0'-'9', 'a'-'f', 'A'-'F')
 * @return Integer value (0-15), or 256 if invalid
 */
static inline unsigned int hex_char_to_uint(int const c) {
  if (c >= '0' && c <= '9') {
    return (c - '0') & 0xff;
  }
  if (c >= 'a' && c <= 'f') {
    return (c - 'a' + 10) & 0xff;
  }
  if (c >= 'A' && c <= 'F') {
    return (c - 'A' + 10) & 0xff;
  }
  return 256;
}

/**
 * Parse two hexadecimal characters to a single byte
 * @param chrs Pointer to two hexadecimal characters (must point to 2 adjacent chars)
 * @return Parsed byte value (0-255), or 0 if invalid
 */
static uint8_t hex_to_byte(char const *const chrs) {
  unsigned int const val = (unsigned int)(hex_char_to_uint(chrs[0]) << 4) | hex_char_to_uint(chrs[1]);
  if (val > 0xff) {
    return 0;
  } else {
    return (uint8_t)val;
  }
}

/**
 * Parse hexadecimal color string (#rrggbb) to gcmz_color
 * @param hex_str Hexadecimal color string (e.g., "#60a0ff")
 * @return gcmz_color structure with parsed values, or zero-initialized on failure
 */
static struct gcmz_color parse_hex_color(char const *const hex_str) {
  if (!hex_str || strlen(hex_str) != 7 || hex_str[0] != '#') {
    return (struct gcmz_color){0};
  }
  return (struct gcmz_color){
      .r = hex_to_byte(hex_str + 1),
      .g = hex_to_byte(hex_str + 3),
      .b = hex_to_byte(hex_str + 5),
  };
}

/**
 * Parse metadata JSON and populate metadata structure
 * @param json_str JSON string containing metadata
 * @param meta [out] Metadata structure to fill
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool parse_metadata_from_json(char const *const json_str,
                                     struct gcmz_analyze_metadata *const meta,
                                     struct ov_error *const err) {
  if (!json_str || !meta) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  *meta = (struct gcmz_analyze_metadata){0};

  yyjson_doc *doc = NULL;
  bool result = false;

  {
    doc = yyjson_read(json_str, strlen(json_str), 0);
    if (!doc) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    yyjson_val *status_val = yyjson_obj_get(root, "status");
    if (status_val && yyjson_is_str(status_val)) {
      char const *const status_str = yyjson_get_str(status_val);
      if (status_str) {
        meta->status = status_from_string(status_str);
      }
    }

    yyjson_val *zoom_val = yyjson_obj_get(root, "zoom");
    if (zoom_val && yyjson_is_int(zoom_val)) {
      meta->zoom = (int)yyjson_get_int(zoom_val);
    }

    yyjson_val *time_val = yyjson_obj_get(root, "creation_time");
    if (time_val && yyjson_is_str(time_val)) {
      char const *const time_str = yyjson_get_str(time_val);
      if (time_str) {
        isotime_parse(time_str, strlen(time_str), &meta->timestamp_us, NULL);
      }
    }

    yyjson_val *style_obj = yyjson_obj_get(root, "style");
    if (style_obj && yyjson_is_obj(style_obj)) {
      meta->style = (struct gcmz_analyze_style){0};

#define PARSE_COLOR_FIELD(field_name)                                                                                  \
  do {                                                                                                                 \
    yyjson_val *const color_val = yyjson_obj_get(style_obj, #field_name);                                              \
    if (color_val && yyjson_is_str(color_val)) {                                                                       \
      char const *const color_str = yyjson_get_str(color_val);                                                         \
      if (color_str) {                                                                                                 \
        meta->style.field_name = parse_hex_color(color_str);                                                           \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)

      PARSE_COLOR_FIELD(active_normal);
      PARSE_COLOR_FIELD(active_hover);
      PARSE_COLOR_FIELD(inactive_normal);
      PARSE_COLOR_FIELD(inactive_hover);
      PARSE_COLOR_FIELD(background);
      PARSE_COLOR_FIELD(frame_cursor);
      PARSE_COLOR_FIELD(frame_cursor_wide);

#undef PARSE_COLOR_FIELD

#define PARSE_INT_FIELD(field_name)                                                                                    \
  do {                                                                                                                 \
    yyjson_val *int_val = yyjson_obj_get(style_obj, #field_name);                                                      \
    if (int_val && yyjson_is_int(int_val)) {                                                                           \
      meta->style.field_name = (int)yyjson_get_int(int_val);                                                           \
    }                                                                                                                  \
  } while (0)

      PARSE_INT_FIELD(time_gauge_height);
      PARSE_INT_FIELD(layer_header_width);
      PARSE_INT_FIELD(scroll_bar_size);
      PARSE_INT_FIELD(layer_height);
      PARSE_INT_FIELD(zoom_bar_margin);
      PARSE_INT_FIELD(zoom_bar_block_width);
      PARSE_INT_FIELD(zoom_bar_block_gap);

#undef PARSE_INT_FIELD
    }
  }

  result = true;

cleanup:
  if (doc) {
    yyjson_doc_free(doc);
  }
  return result;
}

bool gcmz_analyze_load_png_with_metadata(wchar_t const *const filepath,
                                         uint8_t **const bitmap,
                                         int *const width,
                                         int *const height,
                                         struct gcmz_analyze_metadata *const metadata,
                                         struct ov_error *const err) {
  if (!filepath || !bitmap || !width || !height || !metadata) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  uint8_t *decoded = NULL;
  uint8_t *buffer = NULL;
  size_t buffer_size = 0;
  LodePNGState state;
  bool state_initialized = false;
  bool result = false;

  {
    struct ovl_file *file = NULL;
    if (!ovl_file_open(filepath, &file, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t file_size_u64 = 0;
    if (!ovl_file_size(file, &file_size_u64, err)) {
      OV_ERROR_ADD_TRACE(err);
      ovl_file_close(file);
      goto cleanup;
    }
    size_t const file_size = (size_t)file_size_u64;

    if (!OV_ARRAY_GROW(&buffer, file_size)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      ovl_file_close(file);
      goto cleanup;
    }

    size_t read_size = 0;
    if (!ovl_file_read(file, buffer, file_size, &read_size, err)) {
      OV_ERROR_ADD_TRACE(err);
      ovl_file_close(file);
      goto cleanup;
    }
    ovl_file_close(file);

    buffer_size = read_size;

    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_RGB;
    state.info_raw.bitdepth = 8;
    state_initialized = true;

    unsigned int w, h;
    unsigned error = lodepng_decode(&decoded, &w, &h, &state, buffer, buffer_size);
    if (error != 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    *width = (int)w;
    *height = (int)h;

    size_t const compact_row_size = (size_t)w * 3U;
    size_t const padded_row_size = (compact_row_size + 3U) & ~3U;

    if (!OV_ARRAY_GROW(bitmap, padded_row_size * h)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    for (size_t y = 0; y < h; ++y) {
      uint8_t const *src_row = decoded + (y * compact_row_size);
      uint8_t *dst_row = *bitmap + (y * padded_row_size);
      for (size_t x = 0; x < w; ++x) {
        dst_row[x * 3 + 0] = src_row[x * 3 + 2];
        dst_row[x * 3 + 1] = src_row[x * 3 + 1];
        dst_row[x * 3 + 2] = src_row[x * 3 + 0];
      }
    }

    *metadata = (struct gcmz_analyze_metadata){0};
    for (size_t i = 0; i < state.info_png.text_num; ++i) {
      if (strcmp(state.info_png.text_keys[i], g_meta_key) == 0 && state.info_png.text_strings[i]) {
        if (!parse_metadata_from_json(state.info_png.text_strings[i], metadata, err)) {
          OV_ERROR_ADD_TRACE(err);
          goto cleanup;
        }
        break;
      }
    }
  }

  result = true;

cleanup:
  if (buffer) {
    OV_ARRAY_DESTROY(&buffer);
  }
  if (decoded) {
    OV_FREE(&decoded);
  }
  if (state_initialized) {
    lodepng_state_cleanup(&state);
  }
  return result;
}
