#pragma once

#include <ovbase.h>

#include "gcmz_types.h"

struct gcmz_style_config;

/**
 * @brief Parsed color value with alpha support
 */
struct gcmz_style_config_color {
  uint8_t r, g, b, a;
};

/**
 * @brief Configuration initialization options
 */
struct gcmz_style_config_options {
  NATIVE_CHAR const *base_config_path;     ///< Base configuration file path (NULL for default)
  NATIVE_CHAR const *override_config_path; ///< Override configuration file path (NULL for default)
};

/**
 * @brief Initialize configuration system
 *
 * Loads configuration from both base and override locations
 *
 * @param options Configuration options (NULL for defaults)
 * @param err [out] Error information on failure
 * @return Created configuration instance on success, NULL on failure
 */
NODISCARD struct gcmz_style_config *gcmz_style_config_create(struct gcmz_style_config_options const *const options,
                                                             struct ov_error *const err);

/**
 * @brief Cleanup configuration and free all resources
 *
 * @param config Configuration instance to destroy
 */
void gcmz_style_config_destroy(struct gcmz_style_config **const config);

/**
 * @brief Get raw color value by section and key (without alpha blending)
 *
 * @param config Configuration instance
 * @param section Section name (NULL for global section)
 * @param key Color key to retrieve
 * @param value [out] Parsed color value result (may contain alpha)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_style_config_get_raw_color(struct gcmz_style_config const *const config,
                                               char const *const section,
                                               char const *const key,
                                               struct gcmz_style_config_color *const value,
                                               struct ov_error *const err);

/**
 * @brief Get raw color value by section and key with fallback to defaults (without alpha blending)
 *
 * @param config Configuration instance
 * @param section Section name (NULL for global section)
 * @param key Color key to retrieve
 * @param default_color Default color value to use if key not found or invalid
 * @return Parsed color value result (may contain alpha), or default_color on failure
 */
NODISCARD struct gcmz_style_config_color
gcmz_style_config_get_raw_color_fallback(struct gcmz_style_config const *const config,
                                         char const *const section,
                                         char const *const key,
                                         struct gcmz_style_config_color const default_color);

/**
 * @brief Get color value by section and key (with alpha blending applied)
 *
 * @param config Configuration instance
 * @param section Section name (NULL for global section)
 * @param key Color key to retrieve
 * @param value [out] Parsed RGB color value result (alpha blended with background)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_style_config_get_blended_color(struct gcmz_style_config const *const config,
                                                   char const *const section,
                                                   char const *const key,
                                                   struct gcmz_color *const value,
                                                   struct ov_error *const err);

/**
 * @brief Get color value by section and key with fallback to defaults (with alpha blending applied)
 *
 * @param config Configuration instance
 * @param section Section name (NULL for global section)
 * @param key Color key to retrieve
 * @param default_color Default RGB color value to use if key not found or invalid
 * @return Parsed RGB color value result (alpha blended with background), or default_color on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_blended_color_fallback(struct gcmz_style_config const *const config,
                                                                         char const *const section,
                                                                         char const *const key,
                                                                         struct gcmz_color const default_color);

/**
 * @brief Get integer value by section and key
 *
 * @param config Configuration instance
 * @param section Section name (NULL for global section)
 * @param key Integer key to retrieve
 * @param value [out] Parsed integer value result
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_style_config_get_integer(struct gcmz_style_config const *const config,
                                             char const *const section,
                                             char const *const key,
                                             int64_t *const value,
                                             struct ov_error *const err);

/**
 * @brief Get integer value by section and key with fallback to defaults
 *
 * @param config Configuration instance
 * @param section Section name (NULL for global section)
 * @param key Integer key to retrieve
 * @param default_value Default integer value to use if key not found or invalid
 * @return Parsed integer value result, or default_value on failure
 */
NODISCARD int64_t gcmz_style_config_get_integer_fallback(struct gcmz_style_config const *const config,
                                                         char const *const section,
                                                         char const *const key,
                                                         int64_t const default_value);

/**
 * @brief Get ScrollBarSize from Layout section
 *
 * @param config Configuration instance
 * @return ScrollBarSize value on success, default value on failure
 */
NODISCARD int gcmz_style_config_get_layout_scroll_bar_size(struct gcmz_style_config const *const config);

/**
 * @brief Get TimeGaugeHeight from Layout section
 *
 * @param config Configuration instance
 * @return TimeGaugeHeight value on success, default value on failure
 */
NODISCARD int gcmz_style_config_get_layout_time_gauge_height(struct gcmz_style_config const *const config);

/**
 * @brief Get LayerHeaderWidth from Layout section
 *
 * @param config Configuration instance
 * @return LayerHeaderWidth value on success, default value on failure
 */
NODISCARD int gcmz_style_config_get_layout_layer_header_width(struct gcmz_style_config const *const config);

/**
 * @brief Get LayerHeight from Layout section
 *
 * @param config Configuration instance
 * @return LayerHeight value on success, default value on failure
 */
NODISCARD int gcmz_style_config_get_layout_layer_height(struct gcmz_style_config const *const config);

/**
 * @brief Get ZoomGauge color from Color section
 *
 * @param config Configuration instance
 * @return ZoomGauge color value on success, default value on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_color_zoom_gauge(struct gcmz_style_config const *const config);

/**
 * @brief Get ZoomGaugeHover color from Color section
 *
 * @param config Configuration instance
 * @return ZoomGaugeHover color value on success, default value on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_color_zoom_gauge_hover(struct gcmz_style_config const *const config);

/**
 * @brief Get ZoomGaugeOff color from Color section
 *
 * @param config Configuration instance
 * @return ZoomGaugeOff color value on success, default value on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_color_zoom_gauge_off(struct gcmz_style_config const *const config);

/**
 * @brief Get ZoomGaugeOffHover color from Color section
 *
 * @param config Configuration instance
 * @return ZoomGaugeOffHover color value on success, default value on failure
 */
NODISCARD struct gcmz_color
gcmz_style_config_get_color_zoom_gauge_off_hover(struct gcmz_style_config const *const config);

/**
 * @brief Get Background color from Color section
 *
 * @param config Configuration instance
 * @return Background color value on success, default value on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_color_background(struct gcmz_style_config const *const config);

/**
 * @brief Get FrameCursor color from Color section
 *
 * @param config Configuration instance
 * @return FrameCursor color value on success, default value on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_color_frame_cursor(struct gcmz_style_config const *const config);

/**
 * @brief Get FrameCursorWide color from Color section
 *
 * @param config Configuration instance
 * @return FrameCursorWide color value on success, default value on failure
 */
NODISCARD struct gcmz_color gcmz_style_config_get_color_frame_cursor_wide(struct gcmz_style_config const *const config);
