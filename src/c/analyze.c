#include "analyze.h"

#include <ovarray.h>
#include <ovprintf.h>
#include <ovsort.h>

#include <string.h>

#include "analyze_meta.h"
#include "gcmz_types.h"
#include "isotime.h"

enum {
  zoom_bar_count = 26,
  max_window_list_size = 8,
};

#if 0
/**
 * Gets the pixel color at the specified coordinates from a BGR format bitmap.
 * @param bitmap_data BGR format bitmap data (24-bit)
 * @param width Bitmap width
 * @param x X coordinate of the pixel
 * @param y Y coordinate of the pixel
 * @param r [out] Red component
 * @param g [out] Green component
 * @param b [out] Blue component
 */
static inline void get_pixel_color(uint8_t const *const bitmap_data,
                                   int const width,
                                   int const x,
                                   int const y,
                                   uint8_t *const r,
                                   uint8_t *const g,
                                   uint8_t *const b) {
  int const i = (y * ((width * 3 + 3) & ~3)) + (x * 3);
  *b = bitmap_data[i];
  *g = bitmap_data[i + 1];
  *r = bitmap_data[i + 2];
}
#endif

/**
 * Checks if the specified RGB color matches the target color within the tolerance range.
 * @param r Red component of the input color
 * @param g Green component of the input color
 * @param b Blue component of the input color
 * @param tr Red component of the target color
 * @param tg Green component of the target color
 * @param tb Blue component of the target color
 * @param tolerance Tolerance range (maximum difference for each component)
 * @return true if the colors match, false otherwise
 */
static inline bool is_color_match(uint8_t const r,
                                  uint8_t const g,
                                  uint8_t const b,
                                  uint8_t const tr,
                                  uint8_t const tg,
                                  uint8_t const tb,
                                  int const tolerance) {
  int const dr = (int)r - (int)tr;
  int const dg = (int)g - (int)tg;
  int const db = (int)b - (int)tb;
  return (dr <= tolerance) && (dr >= -tolerance) && (dg <= tolerance) && (dg >= -tolerance) && (db <= tolerance) &&
         (db >= -tolerance);
}

/**
 * Get zoom bar block height based on style configuration
 * @param style Style containing scroll bar size and zoom bar margin
 * @return Zoom bar block height in pixels
 */
static int get_zoom_bar_block_height(struct gcmz_analyze_style const *const style) {
  return style->scroll_bar_size - style->zoom_bar_margin * 2;
}

/**
 * Calculate expected active block count from zoom value
 * The zoom bar has 27 zoom levels (20 to 100000) with 26 blocks
 * At zoom=20, 0 blocks are active; at zoom=100000, all 26 blocks are active
 * @param zoom Zoom value (20, 30, 40, 50, 75, 100, ..., 100000)
 * @return Expected active block count (0-26), or 19(=10000) if zoom value is invalid
 */
static size_t calculate_expected_active_count(int const zoom) {
  // clang-format off
  _Static_assert(zoom_bar_count == 26, "zoom_bar_count expected to be 26");
  static int const zoom_levels[zoom_bar_count + 1] = {
                20,    30,    40,    50,    75,    100,
        150,   200,   300,   400,   500,   750,   1000,
       1500,  2000,  3000,  4000,  5000,  7500,  10000,
      15000, 20000, 30000, 40000, 50000, 75000, 100000,
  };
  // clang-format on
  size_t const len = sizeof(zoom_levels) / sizeof(zoom_levels[0]);
  for (size_t i = 0; i < len; ++i) {
    if (zoom <= zoom_levels[i]) {
      return i;
    }
  }
  return 19;
}

/**
 * Validate that a rectangle is filled with expected color above threshold ratio
 * @param bitmap_data BGR format bitmap data (24-bit)
 * @param stride Bitmap row stride (bytes per row, 4-byte aligned)
 * @param rect_x Rectangle X coordinate
 * @param rect_y Rectangle Y coordinate
 * @param rect_width Rectangle width in pixels
 * @param rect_height Rectangle height in pixels
 * @param color Expected color
 * @param tolerance Color matching tolerance
 * @param threshold Ratio threshold in fixed-point format (0x10000 = 100%)
 * @return true if rectangle matches expected color above threshold, false otherwise
 */
static bool validate_rect(uint8_t const *const bitmap_data,
                          int const stride,
                          int const rect_x,
                          int const rect_y,
                          int const rect_width,
                          int const rect_height,
                          struct gcmz_color const color,
                          int const tolerance,
                          int const threshold) {
  int matching_pixels = 0;
  int const col_offset = rect_x * 3;
  int const col_width = rect_width * 3;
  int const row_end = (rect_y + rect_height) * stride;
  for (int row_index = rect_y * stride; row_index < row_end; row_index += stride) {
    int col = row_index + col_offset;
    int const col_end = col + col_width;
    for (; col < col_end; col += 3) {
      if (is_color_match(
              bitmap_data[col + 2], bitmap_data[col + 1], bitmap_data[col], color.r, color.g, color.b, tolerance)) {
        ++matching_pixels;
      }
    }
  }
  int const pixel_threshold = (rect_width * rect_height * threshold) >> 16;
  return matching_pixels >= pixel_threshold;
}

/**
 * Detects the zoom bar of AviUtl ExEdit2 from screen captured image.
 *
 * The zoom bar consists of 26 small rectangles (2x variable height pixels) arranged with 1-pixel gaps,
 * having 4 types of colors based on combinations of active/inactive states and hover/normal states.
 * Active blocks are continuously placed on the left side, followed by inactive blocks.
 *
 * The zoom value is used to calculate the expected number of active blocks.
 * Only zoom bars matching the expected active count for that zoom level will be considered valid.
 * If multiple zoom bars are present in the image, the search continues until a matching one is found.
 *
 * @param bitmap_data BGR format bitmap data (24-bit)
 * @param width Bitmap width
 * @param height Bitmap height
 * @param style Style containing color information and zoom bar configuration
 * @param zoom Zoom value used to calculate expected active block count
 * @param zoom_bar_rect [out] Rectangle of the detected zoom bar
 * @return true if zoom bar is detected, false otherwise
 */
static bool find_zoom_bar(uint8_t const *const bitmap_data,
                          int const width,
                          int const height,
                          struct gcmz_analyze_style const *const style,
                          int const zoom,
                          struct gcmz_analyze_rect *const zoom_bar_rect) {
  if (!bitmap_data || width <= 0 || height <= 0 || !style || !zoom_bar_rect) {
    return false;
  }

  static int const tolerance = 15;
  static int const threshold = 0xC000; // 75% (fixed-point: 0x10000 = 100%)

  int const stride = (width * 3 + 3) & ~3;

  int const zoom_total_width =
      zoom_bar_count * (style->zoom_bar_block_width + style->zoom_bar_block_gap) - style->zoom_bar_block_gap;
  int const zoom_height = get_zoom_bar_block_height(style);

  struct gcmz_color const bg_col = style->background;

  // Select target colors based on expected_active_count
  // First block is active if expected_active_count > 0
  size_t const expected_active_count = calculate_expected_active_count(zoom);
  struct gcmz_color const first_normal = expected_active_count > 0 ? style->active_normal : style->inactive_normal;
  struct gcmz_color const first_hover = expected_active_count > 0 ? style->active_hover : style->inactive_hover;

  // Scan area with zoom_bar_margin on all sides to ensure zoom bar is surrounded by background
  for (int y = style->zoom_bar_margin; y + zoom_height - 1 < height - style->zoom_bar_margin; ++y) {
    int const row_offset = y * stride;
    for (int x = style->zoom_bar_margin; x + zoom_total_width - 1 < width - style->zoom_bar_margin; ++x) {
      struct gcmz_color active_col;
      struct gcmz_color inactive_col;

      // Early return check: verify first pixel matches expected zoom bar color
      int const idx = row_offset + (x * 3);
      uint8_t const b = bitmap_data[idx];
      uint8_t const g = bitmap_data[idx + 1];
      uint8_t const r = bitmap_data[idx + 2];
      if (is_color_match(r, g, b, first_normal.r, first_normal.g, first_normal.b, tolerance)) {
        active_col = style->active_normal;
        inactive_col = style->inactive_normal;
      } else if (is_color_match(r, g, b, first_hover.r, first_hover.g, first_hover.b, tolerance)) {
        active_col = style->active_hover;
        inactive_col = style->inactive_hover;
      } else {
        continue;
      }

      // Check if zoom bar area is surrounded by background color
      if (!validate_rect(bitmap_data,
                         stride,
                         x - style->zoom_bar_margin,
                         y - style->zoom_bar_margin,
                         zoom_total_width + style->zoom_bar_margin * 2,
                         style->zoom_bar_margin,
                         bg_col,
                         tolerance,
                         threshold)) {
        continue;
      }
      if (!validate_rect(bitmap_data,
                         stride,
                         x - style->zoom_bar_margin,
                         y + zoom_height,
                         zoom_total_width + style->zoom_bar_margin * 2,
                         style->zoom_bar_margin,
                         bg_col,
                         tolerance,
                         threshold)) {
        continue;
      }
      if (!validate_rect(bitmap_data,
                         stride,
                         x - style->zoom_bar_margin,
                         y,
                         style->zoom_bar_margin,
                         zoom_height,
                         bg_col,
                         tolerance,
                         threshold)) {
        continue;
      }
      if (!validate_rect(bitmap_data,
                         stride,
                         x + zoom_total_width,
                         y,
                         style->zoom_bar_margin,
                         zoom_height,
                         bg_col,
                         tolerance,
                         threshold)) {
        continue;
      }

      bool is_zoom_bar = true;
      for (int i = 0; i < zoom_bar_count; ++i) {
        int const rect_x = x + i * (style->zoom_bar_block_width + style->zoom_bar_block_gap);
        int const gap_x = rect_x + style->zoom_bar_block_width;
        struct gcmz_color const c = (size_t)i < expected_active_count ? active_col : inactive_col;
        if (!validate_rect(
                bitmap_data, stride, rect_x, y, style->zoom_bar_block_width, zoom_height, c, tolerance, threshold)) {
          is_zoom_bar = false;
          break;
        }
        if (i < zoom_bar_count - 1 &&
            !validate_rect(
                bitmap_data, stride, gap_x, y, style->zoom_bar_block_gap, zoom_height, bg_col, tolerance, threshold)) {
          is_zoom_bar = false;
          break;
        }
      }

      if (is_zoom_bar) {
        *zoom_bar_rect = (struct gcmz_analyze_rect){
            .x = x,
            .y = y,
            .width = zoom_total_width,
            .height = zoom_height,
        };
        return true;
      }
    }
  }

  return false;
}

/**
 * Detects the layer window boundaries based on the zoom bar position
 *
 * Uses the position above the top-left of the zoom bar (-1 row position) as the starting point,
 * then scans left and right to determine the window width from background color repetition.
 * After that, scans up and down from the right edge to determine the window height from background color repetition.
 *
 * @param bitmap_data BGR format bitmap data (24bit)
 * @param width Bitmap width
 * @param height Bitmap height
 * @param zoom_bar_rect Zoom bar rectangle
 * @param color Background color
 * @param layer_window_rect [out] Detected layer window boundaries
 * @return true if layer window is detected, false otherwise
 */
static bool detect_layer_window(uint8_t const *const bitmap_data,
                                int const width,
                                int const height,
                                struct gcmz_analyze_rect const *const zoom_bar_rect,
                                struct gcmz_color const color,
                                struct gcmz_analyze_rect *const layer_window_rect) {
  if (!bitmap_data || width <= 0 || height <= 0 || !zoom_bar_rect || !layer_window_rect) {
    return false;
  }
  int initial_x = zoom_bar_rect->x;
  int initial_y = zoom_bar_rect->y - 1;
  if (initial_x < 0 || initial_y < 0 || initial_y >= height) {
    return false;
  }

  static int const tolerance = 15;
  int const stride = (width * 3 + 3) & ~3;

  // 1. Scan left and right from initial point to find background color repetition (determine window width)

  int left_bound = initial_x;
  int right_bound = initial_x;

  int const row_offset = initial_y * stride;
  for (int x = initial_x - 1; x >= 0; --x) {
    int const idx = row_offset + (x * 3);
    uint8_t const bb = bitmap_data[idx];
    uint8_t const gg = bitmap_data[idx + 1];
    uint8_t const rr = bitmap_data[idx + 2];
    if (is_color_match(rr, gg, bb, color.r, color.g, color.b, tolerance)) {
      left_bound = x;
    } else {
      break;
    }
  }
  for (int x = initial_x + 1; x < width; ++x) {
    int const idx = row_offset + (x * 3);
    uint8_t const bb = bitmap_data[idx];
    uint8_t const gg = bitmap_data[idx + 1];
    uint8_t const rr = bitmap_data[idx + 2];
    if (is_color_match(rr, gg, bb, color.r, color.g, color.b, tolerance)) {
      right_bound = x;
    } else {
      break;
    }
  }

  // 2. Scan up and down from the right edge to find background color repetition (determine window height)

  int top_bound = initial_y;
  int bottom_bound = initial_y;

  int const col_offset = right_bound * 3;
  for (int y = initial_y - 1; y >= 0; --y) {
    int const idx = y * stride + col_offset;
    uint8_t const bb = bitmap_data[idx];
    uint8_t const gg = bitmap_data[idx + 1];
    uint8_t const rr = bitmap_data[idx + 2];
    if (is_color_match(rr, gg, bb, color.r, color.g, color.b, tolerance)) {
      top_bound = y;
    } else {
      break;
    }
  }
  for (int y = initial_y + 1; y < height; ++y) {
    int const idx = y * stride + col_offset;
    uint8_t const bb = bitmap_data[idx];
    uint8_t const gg = bitmap_data[idx + 1];
    uint8_t const rr = bitmap_data[idx + 2];
    if (is_color_match(rr, gg, bb, color.r, color.g, color.b, tolerance)) {
      bottom_bound = y;
    } else {
      break;
    }
  }

  layer_window_rect->x = left_bound;
  layer_window_rect->y = top_bound;
  layer_window_rect->width = right_bound + 1 - left_bound;
  layer_window_rect->height = bottom_bound + 1 - top_bound;
  return true;
}

static inline bool is_scrollbar_at_top(struct gcmz_analyze_rect const *const zoom_bar_rect,
                                       struct gcmz_analyze_rect const *const layer_window_rect) {
  if (!zoom_bar_rect || !layer_window_rect) {
    return false;
  }
  return (zoom_bar_rect->y - layer_window_rect->y) < (layer_window_rect->height / 2);
}

/**
 * Calculate effective area from layer window rectangle and layout configuration
 * @param layer_window_rect Layer window boundary rectangle
 * @param zoom_bar_rect Zoom bar rectangle (used to determine scrollbar position)
 * @param style Style containing layout configuration
 * @param effective_area [out] Calculated effective area rectangle
 * @return true on success, false if input validation failed
 */
static bool calculate_effective_area(struct gcmz_analyze_rect const *const layer_window_rect,
                                     struct gcmz_analyze_rect const *const zoom_bar_rect,
                                     struct gcmz_analyze_style const *const style,
                                     struct gcmz_analyze_rect *const effective_area) {
  if (!layer_window_rect || !zoom_bar_rect || !style || !effective_area) {
    return false;
  }

  bool const scrollbar_at_top = is_scrollbar_at_top(zoom_bar_rect, layer_window_rect);
  effective_area->x = layer_window_rect->x + style->layer_header_width;

  if (scrollbar_at_top) {
    effective_area->y = layer_window_rect->y + style->scroll_bar_size + style->time_gauge_height;
    int const h = layer_window_rect->height - style->scroll_bar_size - style->time_gauge_height;
    effective_area->height = (h > 0) ? h : 0;
  } else {
    effective_area->y = layer_window_rect->y + style->time_gauge_height;
    int const h = layer_window_rect->height - style->time_gauge_height - style->scroll_bar_size;
    effective_area->height = (h > 0) ? h : 0;
  }

  int const w = layer_window_rect->width - style->layer_header_width - style->scroll_bar_size;
  effective_area->width = (w > 0) ? w : 0;
  return true;
}

/**
 * Calculate cursor detection area excluding UI chrome to reduce false positives during cursor search
 * @param layer_window_rect Layer window boundary rectangle
 * @param zoom_bar_rect Zoom bar rectangle (used to determine scrollbar position)
 * @param effective_area Effective area rectangle previously calculated
 * @param style Style containing layout configuration
 * @param area [out] Calculated cursor detection rectangle
 * @return true on success, false if input validation failed
 */
static bool calculate_cursor_detection_area(struct gcmz_analyze_rect const *const layer_window_rect,
                                            struct gcmz_analyze_rect const *const zoom_bar_rect,
                                            struct gcmz_analyze_rect const *const effective_area,
                                            struct gcmz_analyze_style const *const style,
                                            struct gcmz_analyze_rect *const area) {
  if (!layer_window_rect || !zoom_bar_rect || !effective_area || !area) {
    return false;
  }
  area->x = effective_area->x;
  area->width = effective_area->width;
  area->y = layer_window_rect->y;
  area->height = effective_area->y - layer_window_rect->y;
  if (is_scrollbar_at_top(zoom_bar_rect, layer_window_rect)) {
    int const h = zoom_bar_rect->height + style->zoom_bar_margin * 2;
    area->y += h;
    area->height -= h;
  }
  return true;
}

/**
 * Detects cursor (vertical frame bar) inside a precomputed detection area.
 *
 * Searches only within the provided detection rectangle and uses
 * column-wise validation to find a vertical cursor color. Expands width
 * when appropriate (high zoom) to detect wide cursors.
 *
 * @param bitmap_data BGR format bitmap data (24-bit)
 * @param width Bitmap width
 * @param height Bitmap height
 * @param cursor_detection_area Precomputed area to search for the cursor
 * @param effective_area_rect Effective area used to set the cursor's vertical bounds
 * @param style Style configuration for cursor color selection and margins
 * @param zoom Zoom value (affects color selection and width measurement)
 * @param cursor_rect [out] Detected cursor rectangle information
 * @return true if cursor is detected, false otherwise
 */
static bool detect_cursor_position(uint8_t const *const bitmap_data,
                                   int const width,
                                   int const height,
                                   struct gcmz_analyze_rect const *const cursor_detection_area,
                                   struct gcmz_analyze_rect const *const effective_area_rect,
                                   struct gcmz_analyze_style const *const style,
                                   int const zoom,
                                   struct gcmz_analyze_rect *const cursor_rect) {
  if (!bitmap_data || width <= 0 || height <= 0 || !cursor_detection_area || !effective_area_rect || !style ||
      !cursor_rect) {
    return false;
  }

  bool const wide_cursor = zoom > 10000;
  struct gcmz_color const cursor_color = wide_cursor ? style->frame_cursor_wide : style->frame_cursor;

  static int const tolerance = 15;
  // Set to 50% to detect cursor even when partially obscured by overlapping text or other elements
  static int const threshold = 0x8000; // 50% (fixed-point: 0x10000 = 100%)
  int const stride = (width * 3 + 3) & ~3;

  int const x_start = cursor_detection_area->x;
  int const x_end = cursor_detection_area->x + cursor_detection_area->width;
  int const y_start = cursor_detection_area->y;
  int const h = cursor_detection_area->height;

  // Search for a 1px-wide vertical bar matching cursor_color
  int left = -1;
  for (int x = x_start; x < x_end; ++x) {
    if (x < 0 || x >= width) {
      continue;
    }
    if (validate_rect(bitmap_data, stride, x, y_start, 1, h, cursor_color, tolerance, threshold)) {
      left = x;
      break;
    }
  }
  if (left < 0) {
    return false;
  }

  // Measure contiguous columns width
  int right = left;
  if (wide_cursor) {
    while (right + 1 < x_end) {
      if (validate_rect(bitmap_data, stride, right + 1, y_start, 1, h, cursor_color, tolerance, threshold)) {
        ++right;
      } else {
        break;
      }
    }
  }

  cursor_rect->x = left;
  cursor_rect->width = right + 1 - left;
  cursor_rect->y = effective_area_rect->y;
  cursor_rect->height = effective_area_rect->height;
  return true;
}

/**
 * Analyze instance structure
 *
 * Multi-window Detection Mechanism:
 * =================================
 * Recent AviUtl2 updates resulted in multiple aviutl2Manager windows,
 * with the layer window potentially in any one of them.
 * This structure implements an automatic detection mechanism
 * to find and cache the correct window.
 *
 * Detection Flow:
 * 1. If target_window is set (previous successful capture):
 *    - Try to capture and analyze from cached target_window
 *    - If successful, use the result (fast path)
 *    - If failed, proceed to step 2
 *
 * 2. Window search when target_window is unset or analysis failed:
 *    - Call get_window_list() callback to retrieve all candidate windows
 *    - Iterate through each window in the list
 *    - For each window, perform capture and zoom bar detection
 *    - If zoom bar detected successfully, cache this window as target_window
 *    - Return the successful result
 *
 * 3. If no working window found:
 *    - Return failure
 *
 * Caching Strategy:
 * - target_window caches the last successfully analyzed window
 * - Always try cached window first to minimize expensive searches
 * - Only search when cached window fails or is unset
 */
struct gcmz_analyze {
  struct gcmz_analyze_style style;

  gcmz_analyze_capture_func capture;
  gcmz_analyze_get_window_list_func get_window_list;
  gcmz_analyze_get_style_func get_style;
  void *userdata;

  void *target_window;
};

/**
 * Create and initialize analyze instance with options
 * @param ap [out] Pointer to store the created analyze instance
 * @param options Options for analyze instance (NULL for defaults)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_analyze_create(struct gcmz_analyze **const ap,
                                   struct gcmz_analyze_options const *options,
                                   struct ov_error *const err) {
  if (!ap || !options || !options->get_window_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_analyze *a = NULL;
  bool result = false;
  if (!OV_REALLOC(&a, 1, sizeof(struct gcmz_analyze))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  *a = (struct gcmz_analyze){
      .capture = options->capture,
      .get_window_list = options->get_window_list,
      .get_style = options->get_style,
      .userdata = options->userdata,
  };
  *ap = a;
  result = true;

cleanup:
  if (!result && a) {
    gcmz_analyze_destroy(&a);
  }
  return result;
}

void gcmz_analyze_destroy(struct gcmz_analyze **const ap) {
  if (!ap || !*ap) {
    return;
  }

  struct gcmz_analyze *a = *ap;

  a->style = (struct gcmz_analyze_style){0};

  OV_FREE(ap);
}

/**
 * Private save context structure for internal callback handling
 */
struct gcmz_analyze_save_context {
  uint8_t *bitmap;
  int width;
  int height;
  struct gcmz_analyze_metadata metadata;
};

/**
 * Adapter function to convert gcmz_analyze_save_context to gcmz_analyze_save_png_with_metadata signature
 * @param ctx Save context containing bitmap and metadata
 * @param path File path to save to
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool save_context_to_file(struct gcmz_analyze_save_context *const ctx,
                                 NATIVE_CHAR const *const path,
                                 struct ov_error *const err) {
  if (!ctx || !path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!gcmz_analyze_save_png_with_metadata(path, ctx->bitmap, ctx->width, ctx->height, &ctx->metadata, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

/**
 * Call the analyze complete callback if configured
 * Invokes the callback to handle the completed analysis.
 * @param a Analyze instance
 * @param data Captured bitmap data
 * @param width Bitmap width
 * @param height Bitmap height
 * @param zoom Zoom value used in analysis
 * @param status Analysis status
 * @param on_analyze_complete Completion callback (can be NULL)
 * @param complete_userdata User data for completion callback
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool call_analyze_complete_callback(struct gcmz_analyze *const a,
                                           uint8_t *const data,
                                           int const width,
                                           int const height,
                                           int const zoom,
                                           enum gcmz_analyze_status const status,
                                           gcmz_analyze_complete_func on_analyze_complete,
                                           void *const complete_userdata,
                                           struct ov_error *const err) {
  if (!a) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  bool success = false;
  if (!on_analyze_complete) {
    success = true;
    goto cleanup;
  }
  if (!on_analyze_complete(
          &(struct gcmz_analyze_save_context){
              .bitmap = data,
              .width = width,
              .height = height,
              .metadata =
                  {
                      .zoom = zoom,
                      .timestamp_us = isotime_now(),
                      .status = status,
                      .style = a->style,
                  },
          },
          save_context_to_file,
          status,
          complete_userdata,
          err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  success = true;
cleanup:
  return success;
}

static int analyze_window_compare(void const *const a, void const *const b, void *userdata) {
  (void)userdata;
  struct gcmz_window_info const *const w0 = (struct gcmz_window_info const *)a;
  struct gcmz_window_info const *const w1 = (struct gcmz_window_info const *)b;
  size_t const pixels0 = (size_t)(w0->width * w0->height);
  size_t const pixels1 = (size_t)(w1->width * w1->height);
  if (pixels0 > pixels1) {
    return -1;
  }
  if (pixels0 < pixels1) {
    return 1;
  }
  if (w0->window > w1->window) {
    return -1;
  }
  if (w0->window < w1->window) {
    return 1;
  }
  return 0;
}

/**
 * Analyze captured image
 * @param data Bitmap data (BGR format, 24-bit)
 * @param width Image width
 * @param height Image height
 * @param zoom Expected zoom value for validation (< 0 to skip validation)
 * @param style Style configuration for detection
 * @param result [out] Detection results
 * @return true if all elements detected successfully, false otherwise
 */
static bool analyze(uint8_t const *const data,
                    int const width,
                    int const height,
                    int const zoom,
                    struct gcmz_analyze_style const *const style,
                    struct gcmz_analyze_result *const result) {
  result->layer_height = style->layer_height;
  if (!find_zoom_bar(data, width, height, style, zoom, &result->zoom_bar)) {
    if (result) {
      result->status = gcmz_analyze_status_zoom_bar_not_found;
    }
    return false;
  }
  if (!detect_layer_window(data, width, height, &result->zoom_bar, style->background, &result->layer_window)) {
    if (result) {
      result->status = gcmz_analyze_status_layer_window_not_found;
    }
    return false;
  }
  if (!calculate_effective_area(&result->layer_window, &result->zoom_bar, style, &result->effective_area)) {
    if (result) {
      result->status = gcmz_analyze_status_effective_area_calculation_failed;
    }
    return false;
  }
  if (!calculate_cursor_detection_area(
          &result->layer_window, &result->zoom_bar, &result->effective_area, style, &result->cursor_detection_area)) {
    if (result) {
      result->status = gcmz_analyze_status_cursor_detection_area_calculation_failed;
    }
    return false;
  }
  if (!detect_cursor_position(
          data, width, height, &result->cursor_detection_area, &result->effective_area, style, zoom, &result->cursor)) {
    result->cursor = (struct gcmz_analyze_rect){0}; // no error if cursor not found
  }
  result->status = gcmz_analyze_status_success;
  return true;
}

/**
 * Find timeline window from window list by trying capture on each
 * @param a Analyze instance
 * @param windows Array of window information (sorted by size descending)
 * @param num_windows Number of windows in array
 * @param zoom Expected zoom value for validation (< 0 to skip validation)
 * @param result [out] Analysis result from detection attempts (success or last failure)
 * @param data [in/out] Captured bitmap buffer (reused across attempts)
 * @param width [out] Captured bitmap width (last attempt)
 * @param height [out] Captured bitmap height (last attempt)
 * @param err [out] Error information on failure
 * @return true if working window found, false otherwise
 */
static bool find_timeline_window(struct gcmz_analyze *const a,
                                 struct gcmz_window_info const *const windows,
                                 size_t const num_windows,
                                 int const zoom,
                                 struct gcmz_analyze_result *const result,
                                 uint8_t **const data,
                                 int *const width,
                                 int *const height,
                                 struct ov_error *const err) {
  for (size_t i = 0; i < num_windows; ++i) {
    void *const window = windows[i].window;
    if (!a->capture(window, data, width, height, a->userdata, err)) {
      OV_ERROR_DESTROY(err);
      continue;
    }
    if (!analyze(*data, *width, *height, zoom, &a->style, result)) {
      continue;
    }
    a->target_window = window;
    return true;
  }
  OV_ERROR_SET_GENERIC(err, ov_error_generic_not_found);
  return false;
}

NODISCARD bool gcmz_analyze_run(struct gcmz_analyze *const a,
                                int const zoom,
                                struct gcmz_analyze_result *const result,
                                gcmz_analyze_complete_func on_analyze_complete,
                                void *const complete_userdata,
                                struct ov_error *const err) {
  if (!a || !result) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  uint8_t *data = NULL;
  int width = 0;
  int height = 0;
  bool success = false;
  struct gcmz_analyze_result analysis = {0};

  // Load style lazily
  if (a->style.scroll_bar_size == 0) {
    if (!a->get_style(&a->style, a->userdata, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  analysis.layer_height = a->style.layer_height;

  // Try cached target window first if available
  if (a->target_window != NULL) {
    if (a->capture(a->target_window, &data, &width, &height, a->userdata, err)) {
      if (analyze(data, width, height, zoom, &a->style, &analysis)) {
        // Success with cached window
        analysis.window = a->target_window;
        *result = analysis;
        success = true;
        goto cleanup;
      }
    }
    // Cached window failed, clear error and target to trigger search
    OV_ERROR_DESTROY(err);
    a->target_window = NULL;
  }

  // Get window list and search for working window
  {
    struct gcmz_window_info windows[max_window_list_size] = {0};
    size_t const count = a->get_window_list(windows, max_window_list_size, a->userdata, err);
    if (count == SIZE_MAX) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Sort by size (largest first) for efficient buffer reuse
    ov_qsort(windows, count, sizeof(struct gcmz_window_info), analyze_window_compare, NULL);

    if (!find_timeline_window(a, windows, count, zoom, &analysis, &data, &width, &height, err)) {
      OV_ERROR_ADD_TRACE(err);
      analysis.status = gcmz_analyze_status_invalid;
      goto cleanup;
    }
  }

  analysis.layer_height = a->style.layer_height;
  analysis.window = a->target_window;
  *result = analysis;

  success = true;

cleanup:
  if (analysis.status != gcmz_analyze_status_invalid) {
    if (!call_analyze_complete_callback(
            a, data, width, height, zoom, analysis.status, on_analyze_complete, complete_userdata, err)) {
      OV_ERROR_ADD_TRACE(err);
      success = false;
    }
  }
  if (data) {
    OV_ARRAY_DESTROY(&data);
  }
  if (!success) {
    *result = (struct gcmz_analyze_result){0};
  }
  return success;
}
