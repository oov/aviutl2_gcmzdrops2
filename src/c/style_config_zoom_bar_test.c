#include <ovtest.h>

#include "style_config.h"

#include <ovarray.h>
#include <ovprintf.h>

#ifndef SOURCE_DIR
#  define SOURCE_DIR .
#endif

#define LSTR(x) L##x
#define LSTR2(x) LSTR(#x)
#define STRINGIZE(x) LSTR2(x)
#define TEST_PATH(relative_path) STRINGIZE(SOURCE_DIR) L"/test_data/" relative_path

/**
 * Test zoom bar detection with default colors
 */
static void test_zoom_bar_detection_default_colors(void) {
  bool success = false;
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color const default_zoom_gauge = {96, 160, 255};
  struct gcmz_color const default_zoom_gauge_hover = {128, 192, 255};
  struct gcmz_color const default_zoom_gauge_off = {32, 64, 128};
  struct gcmz_color const default_zoom_gauge_off_hover = {48, 96, 160};
  struct gcmz_color const default_background = {32, 32, 32};
  struct gcmz_color colors[5];

  // Use default configuration (no custom config files)
  config = gcmz_style_config_create(NULL, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Get default colors
  colors[0] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", default_zoom_gauge);
  colors[1] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", default_zoom_gauge_hover);
  colors[2] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", default_zoom_gauge_off);
  colors[3] =
      gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", default_zoom_gauge_off_hover);
  colors[4] = gcmz_style_config_get_blended_color_fallback(config, NULL, "Background", default_background);

  // Verify default colors are loaded correctly
  TEST_CHECK(colors[0].r == 96 && colors[0].g == 160 && colors[0].b == 255);

  TEST_CHECK(colors[2].r == 32 && colors[2].g == 64 && colors[2].b == 128);

  TEST_CHECK(colors[4].r == 32 && colors[4].g == 32 && colors[4].b == 32);
  TEST_MSG("Background: expected (32,32,32), got (%d,%d,%d)", colors[4].r, colors[4].g, colors[4].b);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test zoom bar detection with custom colors
 */
static void test_zoom_bar_detection_custom_colors(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = TEST_PATH(L"style_config/custom_style.conf"),
      .override_config_path = NULL,
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color const black_default = {0, 0, 0};
  struct gcmz_color colors[5];

  // Use custom configuration
  config = gcmz_style_config_create(&options, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Get custom colors
  colors[0] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  colors[1] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", black_default);
  colors[2] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", black_default);
  colors[3] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", black_default);
  colors[4] = gcmz_style_config_get_blended_color_fallback(config, NULL, "Background", black_default);

  // Verify custom colors are loaded correctly (bright green theme)
  TEST_CHECK(colors[0].r == 0x00 && colors[0].g == 0xff && colors[0].b == 0x00);

  TEST_CHECK(colors[1].r == 0x40 && colors[1].g == 0xff && colors[1].b == 0x40);

  TEST_CHECK(colors[2].r == 0x00 && colors[2].g == 0x80 && colors[2].b == 0x00);

  TEST_CHECK(colors[4].r == 0x40 && colors[4].g == 0x40 && colors[4].b == 0x40);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test alpha blending functionality with RGBA colors
 */
static void test_alpha_blending_validation(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = TEST_PATH(L"style_config/rgba_style.conf"),
      .override_config_path = NULL,
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color color = {0};
  struct gcmz_color const black_default = {0, 0, 0};

  // Use RGBA configuration
  config = gcmz_style_config_create(&options, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Test 50% red (ff000080) with background (202020)
  // Expected: (0.5 * 255 + 0.5 * 32, 0.5 * 0 + 0.5 * 32, 0.5 * 0 + 0.5 * 32) ≈ (143, 16, 16)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);

  // Allow some tolerance for fixed-point arithmetic
  TEST_CHECK(color.r >= 140 && color.r <= 146);
  TEST_MSG("50%% red blend: expected r in [140,146], got r=%d", color.r);
  TEST_CHECK(color.g >= 14 && color.g <= 18);
  TEST_MSG("50%% red blend: expected g in [14,18], got g=%d", color.g);
  TEST_CHECK(color.b >= 14 && color.b <= 18);
  TEST_MSG("50%% red blend: expected b in [14,18], got b=%d", color.b);

  // Test 25% green (00ff0040) with background (202020)
  // Expected: (0.25 * 0 + 0.75 * 32, 0.25 * 255 + 0.75 * 32, 0.25 * 0 + 0.75 * 32) ≈ (24, 87, 24)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", black_default);

  TEST_CHECK(color.r >= 22 && color.r <= 26);
  TEST_MSG("25%% green blend: expected r in [22,26], got r=%d", color.r);
  TEST_CHECK(color.g >= 85 && color.g <= 89);
  TEST_MSG("25%% green blend: expected g in [85,89], got g=%d", color.g);
  TEST_CHECK(color.b >= 22 && color.b <= 26);
  TEST_MSG("25%% green blend: expected b in [22,26], got b=%d", color.b);

  // Test 75% yellow (ffff00c0) with background (202020)
  // Expected: (0.75 * 255 + 0.25 * 32, 0.75 * 255 + 0.25 * 32, 0.75 * 0 + 0.25 * 32) ≈ (199, 199, 8)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", black_default);

  TEST_CHECK(color.r >= 197 && color.r <= 201);
  TEST_MSG("75%% yellow blend: expected r in [197,201], got r=%d", color.r);
  TEST_CHECK(color.g >= 197 && color.g <= 201);
  TEST_MSG("75%% yellow blend: expected g in [197,201], got g=%d", color.g);
  TEST_CHECK(color.b >= 6 && color.b <= 10);
  TEST_MSG("75%% yellow blend: expected b in [6,10], got b=%d", color.b);

  // Test fully transparent color (should be background color)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TransparentColor", black_default);
  TEST_CHECK(color.r == 0x20 && color.g == 0x20 && color.b == 0x20);
  TEST_MSG("Transparent color: expected (32,32,32), got (%d,%d,%d)", color.r, color.g, color.b);

  // Test fully opaque color (should be foreground color)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "OpaqueColor", black_default);
  TEST_CHECK(color.r == 0xff && color.g == 0xff && color.b == 0xff);
  TEST_MSG("Opaque color: expected (255,255,255), got (%d,%d,%d)", color.r, color.g, color.b);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test configuration override behavior in practice
 */
static void test_configuration_override_validation(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = TEST_PATH(L"style_config/default_style.conf"),
      .override_config_path = TEST_PATH(L"style_config/custom_style.conf"),
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color color = {0};
  struct gcmz_color const black_default = {0, 0, 0};

  // Test with both base and override configurations
  config = gcmz_style_config_create(&options, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Override should take precedence - custom_style.conf has green colors
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r == 0x00 && color.g == 0xff && color.b == 0x00);
  TEST_MSG("Override ZoomGauge: expected green (0,255,0), got (%d,%d,%d)", color.r, color.g, color.b);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "Background", black_default);
  TEST_CHECK(color.r == 0x40 && color.g == 0x40 && color.b == 0x40);
  TEST_MSG("Override Background: expected (64,64,64), got (%d,%d,%d)", color.r, color.g, color.b);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test error conditions and edge cases
 */
static void test_error_conditions_validation(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = TEST_PATH(L"style_config/malformed_colors.conf"),
      .override_config_path = NULL,
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color color = {0};
  struct gcmz_color const test_default = {100, 150, 200};
  struct gcmz_color const black_default = {0, 0, 0};

  // Test with malformed configuration
  config = gcmz_style_config_create(&options, &err);
  // Should succeed even with malformed colors
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Invalid colors should fall back to defaults
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TooShort", test_default);
  TEST_CHECK(color.r == 100 && color.g == 150 && color.b == 200);
  TEST_MSG("Malformed color fallback: expected (100,150,200), got (%d,%d,%d)", color.r, color.g, color.b);

  // Valid colors should work
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ValidRGB", black_default);
  TEST_CHECK(color.r == 0x60 && color.g == 0xa0 && color.b == 0xff);
  TEST_MSG("Valid color: expected (96,160,255), got (%d,%d,%d)", color.r, color.g, color.b);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test missing configuration files fallback
 */
static void test_missing_config_validation(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = L"nonexistent_base.conf",
      .override_config_path = L"nonexistent_override.conf",
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color color = {0};
  struct gcmz_color const zoom_gauge_default = {96, 160, 255};
  struct gcmz_color const background_default = {32, 32, 32};

  // Test with non-existent configuration files
  config = gcmz_style_config_create(&options, &err);
  // Should succeed even with missing files
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // All colors should fall back to provided defaults
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", zoom_gauge_default);
  TEST_CHECK(color.r == 96 && color.g == 160 && color.b == 255);
  TEST_MSG("Missing config fallback: expected (96,160,255), got (%d,%d,%d)", color.r, color.g, color.b);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "Background", background_default);
  TEST_CHECK(color.r == 32 && color.g == 32 && color.b == 32);
  TEST_MSG("Missing config fallback: expected (32,32,32), got (%d,%d,%d)", color.r, color.g, color.b);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test color tolerance and matching behavior
 */
static void test_color_tolerance_validation(void) {
  bool success = false;
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color colors[5];
  struct gcmz_color const default_zoom_gauge = {96, 160, 255};
  struct gcmz_color const default_zoom_gauge_hover = {128, 192, 255};
  struct gcmz_color const default_zoom_gauge_off = {32, 64, 128};
  struct gcmz_color const default_zoom_gauge_off_hover = {48, 96, 160};
  struct gcmz_color const default_background = {32, 32, 32};
  int r_diff, g_diff, b_diff;

  // Test that colors are within expected tolerance ranges for detection
  config = gcmz_style_config_create(NULL, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Get all zoom bar colors
  colors[0] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", default_zoom_gauge);
  colors[1] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", default_zoom_gauge_hover);
  colors[2] = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", default_zoom_gauge_off);
  colors[3] =
      gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", default_zoom_gauge_off_hover);
  colors[4] = gcmz_style_config_get_blended_color_fallback(config, NULL, "Background", default_background);

  // Verify colors are distinct enough for detection (tolerance = 15)
  // Active normal vs inactive normal should be sufficiently different
  r_diff = abs((int)colors[0].r - (int)colors[2].r);
  g_diff = abs((int)colors[0].g - (int)colors[2].g);
  b_diff = abs((int)colors[0].b - (int)colors[2].b);

  TEST_CHECK(r_diff > 15 || g_diff > 15 || b_diff > 15);
  TEST_MSG(
      "Active/Inactive color difference: r_diff=%d, g_diff=%d, b_diff=%d (should be >15 in at least one component)",
      r_diff,
      g_diff,
      b_diff);

  // Active normal vs active hover should be distinguishable
  r_diff = abs((int)colors[0].r - (int)colors[1].r);
  g_diff = abs((int)colors[0].g - (int)colors[1].g);
  b_diff = abs((int)colors[0].b - (int)colors[1].b);

  TEST_CHECK(r_diff > 15 || g_diff > 15 || b_diff > 15);
  TEST_MSG(
      "Active normal/hover color difference: r_diff=%d, g_diff=%d, b_diff=%d (should be >15 in at least one component)",
      r_diff,
      g_diff,
      b_diff);

  // Background should be different from all zoom bar colors
  for (int i = 0; i < 4; i++) {
    r_diff = abs((int)colors[4].r - (int)colors[i].r);
    g_diff = abs((int)colors[4].g - (int)colors[i].g);
    b_diff = abs((int)colors[4].b - (int)colors[i].b);

    TEST_CHECK(r_diff > 15 || g_diff > 15 || b_diff > 15);
    TEST_MSG(
        "Background vs color[%d] difference: r_diff=%d, g_diff=%d, b_diff=%d (should be >15 in at least one component)",
        i,
        r_diff,
        g_diff,
        b_diff);
  }

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test integration with gcmz_capture initialization
 */
static void test_gcmz_integration_validation(void) {
  bool success = false;
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color color = {0};
  struct {
    char const *key;
    uint8_t default_r, default_g, default_b;
  } required_colors[] = {{"ZoomGauge", 96, 160, 255},
                         {"ZoomGaugeHover", 128, 192, 255},
                         {"ZoomGaugeOff", 32, 64, 128},
                         {"ZoomGaugeOffHover", 48, 96, 160},
                         {"Background", 32, 32, 32}};

  // This test verifies that the color configuration system integrates properly
  // with the main gcmz_capture module

  // The gcmz_capture module should initialize colors on first access
  // We can't directly test the find_zoom_bar function without creating a full bitmap,
  // but we can verify that the configuration system works as expected

  config = gcmz_style_config_create(NULL, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Verify that all required color keys can be retrieved with proper defaults

  for (size_t i = 0; i < sizeof(required_colors) / sizeof(required_colors[0]); i++) {
    struct gcmz_color const default_color = {
        required_colors[i].default_r, required_colors[i].default_g, required_colors[i].default_b};
    color = gcmz_style_config_get_blended_color_fallback(config, NULL, required_colors[i].key, default_color);
    TEST_MSG(
        "Successfully retrieved color for key: %s -> (%d,%d,%d)", required_colors[i].key, color.r, color.g, color.b);

    // Verify color values are reasonable (should match defaults when no config file exists)
    TEST_CHECK(color.r > 0 || color.g > 0 || color.b > 0);
    TEST_MSG("Color %s should have at least one non-zero component", required_colors[i].key);

    // Verify the color matches expected default values
    TEST_CHECK(color.r == required_colors[i].default_r);
    TEST_CHECK(color.g == required_colors[i].default_g);
    TEST_CHECK(color.b == required_colors[i].default_b);
    TEST_MSG("Color %s: expected (%d,%d,%d), got (%d,%d,%d)",
             required_colors[i].key,
             required_colors[i].default_r,
             required_colors[i].default_g,
             required_colors[i].default_b,
             color.r,
             color.g,
             color.b);
  }

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test real-world zoom bar detection scenarios with custom colors
 */
static void test_real_world_custom_color_detection(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = TEST_PATH(L"style_config/custom_style.conf"),
      .override_config_path = NULL,
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color const black_default = {0, 0, 0};
  struct gcmz_color active_normal, inactive_normal, background;
  int r_diff, g_diff, b_diff;

  // Test with custom green theme configuration
  config = gcmz_style_config_create(&options, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Get custom colors for validation
  active_normal = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  inactive_normal = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", black_default);
  background = gcmz_style_config_get_blended_color_fallback(config, NULL, "Background", black_default);

  // Verify custom colors are loaded (green theme)
  TEST_CHECK(active_normal.r == 0x00 && active_normal.g == 0xff && active_normal.b == 0x00);
  TEST_CHECK(inactive_normal.r == 0x00 && inactive_normal.g == 0x80 && inactive_normal.b == 0x00);
  TEST_CHECK(background.r == 0x40 && background.g == 0x40 && background.b == 0x40);

  // Test color distinction for detection algorithm
  // Colors should be sufficiently different for 15-pixel tolerance detection
  r_diff = abs((int)active_normal.r - (int)inactive_normal.r);
  g_diff = abs((int)active_normal.g - (int)inactive_normal.g);
  b_diff = abs((int)active_normal.b - (int)inactive_normal.b);

  TEST_CHECK(r_diff > 15 || g_diff > 15 || b_diff > 15);
  TEST_MSG("Custom colors active/inactive distinction: r_diff=%d, g_diff=%d, b_diff=%d", r_diff, g_diff, b_diff);

  // Test background distinction
  r_diff = abs((int)background.r - (int)active_normal.r);
  g_diff = abs((int)background.g - (int)active_normal.g);
  b_diff = abs((int)background.b - (int)active_normal.b);

  TEST_CHECK(r_diff > 15 || g_diff > 15 || b_diff > 15);
  TEST_MSG("Custom colors background/active distinction: r_diff=%d, g_diff=%d, b_diff=%d", r_diff, g_diff, b_diff);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

/**
 * Test edge cases in alpha blending with extreme values
 */
static void test_alpha_blending_edge_cases(void) {
  bool success = false;
  struct gcmz_style_config_options options = {
      .base_config_path = TEST_PATH(L"style_config/rgba_style.conf"),
      .override_config_path = NULL,
  };
  struct gcmz_style_config *config = NULL;
  struct ov_error err = {0};
  struct gcmz_color color = {0};
  struct gcmz_color const white_default = {255, 255, 255};
  struct gcmz_color const black_default = {0, 0, 0};
  int r_diff, g_diff, b_diff;

  config = gcmz_style_config_create(&options, &err);
  if (!TEST_CHECK(config != NULL)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  // Test fully transparent color (should become background)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TransparentColor", white_default);
  TEST_CHECK(color.r == 0x20 && color.g == 0x20 && color.b == 0x20);
  TEST_MSG("Fully transparent: expected background (32,32,32), got (%d,%d,%d)", color.r, color.g, color.b);

  // Test fully opaque color (should be unchanged)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "OpaqueColor", black_default);
  TEST_CHECK(color.r == 0xff && color.g == 0xff && color.b == 0xff);
  TEST_MSG("Fully opaque: expected (255,255,255), got (%d,%d,%d)", color.r, color.g, color.b);

  // Test that blended colors are within detection tolerance
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);

  // 50% red with background should be detectable as distinct from background
  r_diff = abs((int)color.r - 32);
  g_diff = abs((int)color.g - 32);
  b_diff = abs((int)color.b - 32);

  TEST_CHECK(r_diff > 15 || g_diff > 15 || b_diff > 15);
  TEST_MSG("Blended color distinction from background: r_diff=%d, g_diff=%d, b_diff=%d", r_diff, g_diff, b_diff);

  gcmz_style_config_destroy(&config);
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

TEST_LIST = {
    {"test_zoom_bar_detection_default_colors", test_zoom_bar_detection_default_colors},
    {"test_zoom_bar_detection_custom_colors", test_zoom_bar_detection_custom_colors},
    {"test_alpha_blending_validation", test_alpha_blending_validation},
    {"test_configuration_override_validation", test_configuration_override_validation},
    {"test_error_conditions_validation", test_error_conditions_validation},
    {"test_missing_config_validation", test_missing_config_validation},
    {"test_color_tolerance_validation", test_color_tolerance_validation},
    {"test_gcmz_integration_validation", test_gcmz_integration_validation},
    {"test_real_world_custom_color_detection", test_real_world_custom_color_detection},
    {"test_alpha_blending_edge_cases", test_alpha_blending_edge_cases},
    {NULL, NULL},
};
