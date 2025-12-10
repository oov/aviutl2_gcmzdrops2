#include <ovtest.h>

#include "style_config.h"

#include <ovarray.h>

#ifndef SOURCE_DIR
#  define SOURCE_DIR .
#endif

#define LSTR(x) L##x
#define LSTR2(x) LSTR(#x)
#define STRINGIZE(x) LSTR2(x)
#define TEST_PATH(relative_path) STRINGIZE(SOURCE_DIR) L"/test_data/" relative_path

static void test_config_create(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(NULL, &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const test_default = {100, 150, 200};
  struct gcmz_color color = gcmz_style_config_get_blended_color_fallback(config, NULL, "NonExistentKey", test_default);
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);
  gcmz_style_config_destroy(&config);

  gcmz_style_config_destroy(NULL);
}

static void test_color_parsing(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/default_style.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const black_default = {0, 0, 0};
  static struct gcmz_color const test_default2 = {100, 150, 200};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r == 0x60);
  TEST_MSG("Expected r=0x60, got r=0x%02x", color.r);
  TEST_CHECK(color.g == 0xa0);
  TEST_MSG("Expected g=0xa0, got g=0x%02x", color.g);
  TEST_CHECK(color.b == 0xff);
  TEST_MSG("Expected b=0xff, got b=0x%02x", color.b);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", black_default);
  TEST_CHECK(color.r == 0x80);
  TEST_CHECK(color.g == 0xc0);
  TEST_CHECK(color.b == 0xff);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", black_default);
  TEST_CHECK(color.r == 0x20);
  TEST_CHECK(color.g == 0x40);
  TEST_CHECK(color.b == 0x80);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", black_default);
  TEST_CHECK(color.r == 0x30);
  TEST_CHECK(color.g == 0x60);
  TEST_CHECK(color.b == 0xa0);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "NonExistentKey", test_default2);
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  gcmz_style_config_destroy(&config);
}

static void test_rgba_parsing(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/rgba_style.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const black_default = {0, 0, 0};
  struct gcmz_color color = {0};

  // Test 50% red (ff000080) with background (202020)
  // Expected: (0.5 * 255 + 0.5 * 32, 0.5 * 0 + 0.5 * 32, 0.5 * 0 + 0.5 * 32) = (143, 16, 16)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r > 140 && color.r < 146);
  TEST_MSG("Expected r in range 140-146, got r=0x%02x", color.r);
  TEST_CHECK(color.g > 14 && color.g < 18);
  TEST_MSG("Expected g in range 14-18, got g=0x%02x", color.g);
  TEST_CHECK(color.b > 14 && color.b < 18);
  TEST_MSG("Expected b in range 14-18, got b=0x%02x", color.b);

  // Test 25% green (00ff0040) with background (202020)
  // Expected: (0.25 * 0 + 0.75 * 32, 0.25 * 255 + 0.75 * 32, 0.25 * 0 + 0.75 * 32) = (24, 87, 24)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", black_default);
  TEST_CHECK(color.r > 22 && color.r < 26);
  TEST_MSG("Expected r in range 22-26, got r=0x%02x", color.r);
  TEST_CHECK(color.g > 85 && color.g < 89);
  TEST_MSG("Expected g in range 85-89, got g=0x%02x", color.g);
  TEST_CHECK(color.b > 22 && color.b < 26);
  TEST_MSG("Expected b in range 22-26, got b=0x%02x", color.b);

  // Test 75% yellow (ffff00c0) with background (202020)
  // Expected: (0.75 * 255 + 0.25 * 32, 0.75 * 255 + 0.25 * 32, 0.75 * 0 + 0.25 * 32) = (199, 199, 8)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", black_default);
  TEST_CHECK(color.r > 197 && color.r < 201);
  TEST_MSG("Expected r in range 197-201, got r=0x%02x", color.r);
  TEST_CHECK(color.g > 197 && color.g < 201);
  TEST_MSG("Expected g in range 197-201, got g=0x%02x", color.g);
  TEST_CHECK(color.b > 6 && color.b < 10);
  TEST_MSG("Expected b in range 6-10, got b=0x%02x", color.b);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "OpaqueColor", black_default);
  TEST_CHECK(color.r == 0xff);
  TEST_CHECK(color.g == 0xff);
  TEST_CHECK(color.b == 0xff);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TransparentColor", black_default);
  TEST_CHECK(color.r == 0x20); // Should be background color
  TEST_CHECK(color.g == 0x20);
  TEST_CHECK(color.b == 0x20);

  gcmz_style_config_destroy(&config);
}

static void test_invalid_colors(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/malformed_colors.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const test_default = {100, 150, 200};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TooShort", test_default);
  TEST_CHECK(color.r == 100); // Should use default
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TooLong", test_default);
  TEST_CHECK(color.r == 100); // Should use default
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "InvalidChars", test_default);
  TEST_CHECK(color.r == 100); // Should use default
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "EmptyValue", test_default);
  TEST_CHECK(color.r == 100); // Should use default
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "NonColor", test_default);
  TEST_CHECK(color.r == 100); // Should use default
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  gcmz_style_config_destroy(&config);
}

static void test_config_override(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/base_config.conf"),
          .override_config_path = TEST_PATH(L"style_config/override_config.conf"),
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const black_default = {0, 0, 0};
  static struct gcmz_color const test_default = {100, 100, 100};
  struct gcmz_color color = {0};

  // Test overridden color (should use override value)
  // Base: ZoomGauge=ff0000 (red), Override: ZoomGauge=00ff00 (green)
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r == 0x00); // Should be green from override
  TEST_CHECK(color.g == 0xff);
  TEST_CHECK(color.b == 0x00);

  // Test partially overridden color (should use override value)
  // Base: ZoomGaugeHover=ff4040, Override: ZoomGaugeHover=40ff40
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", black_default);
  TEST_CHECK(color.r == 0x40); // Should be from override
  TEST_CHECK(color.g == 0xff);
  TEST_CHECK(color.b == 0x40);

  // Test non-overridden color (should use base value)
  // Base: ZoomGaugeOff=800000, Override: not defined
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", black_default);
  TEST_CHECK(color.r == 0x80); // Should be from base
  TEST_CHECK(color.g == 0x00);
  TEST_CHECK(color.b == 0x00);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "BaseOnly", test_default);
  // This should fall back to default since BaseOnly is not a color key
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 100);
  TEST_CHECK(color.b == 100);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "OverrideOnly", test_default);
  // This should fall back to default since OverrideOnly is not a color key
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 100);
  TEST_CHECK(color.b == 100);

  gcmz_style_config_destroy(&config);
}

static void test_base_config_only(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/default_style.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const black_default = {0, 0, 0};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r == 0x60);
  TEST_CHECK(color.g == 0xa0);
  TEST_CHECK(color.b == 0xff);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", black_default);
  TEST_CHECK(color.r == 0x80);
  TEST_CHECK(color.g == 0xc0);
  TEST_CHECK(color.b == 0xff);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOff", black_default);
  TEST_CHECK(color.r == 0x20);
  TEST_CHECK(color.g == 0x40);
  TEST_CHECK(color.b == 0x80);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeOffHover", black_default);
  TEST_CHECK(color.r == 0x30);
  TEST_CHECK(color.g == 0x60);
  TEST_CHECK(color.b == 0xa0);

  gcmz_style_config_destroy(&config);
}

static void test_missing_config_fallback(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"nonexistent.conf"),
          .override_config_path = TEST_PATH(L"also_nonexistent.conf"),
      },
      &err);
  TEST_CHECK(config != NULL);

  static struct gcmz_color const test_default1 = {100, 150, 200};
  static struct gcmz_color const test_default2 = {50, 75, 100};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", test_default1);
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGaugeHover", test_default2);
  TEST_CHECK(color.r == 50);
  TEST_CHECK(color.g == 75);
  TEST_CHECK(color.b == 100);

  gcmz_style_config_destroy(&config);
}

static void test_override_only_config(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/nonexistent_base.conf"),
          .override_config_path = TEST_PATH(L"style_config/custom_style.conf"),
      },
      &err);
  TEST_CHECK(config != NULL);

  static struct gcmz_color const black_default = {0, 0, 0};
  static struct gcmz_color const test_default = {123, 234, 45};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r == 0x00); // Green from custom_style.conf
  TEST_CHECK(color.g == 0xff);
  TEST_CHECK(color.b == 0x00);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "NonExistentKey", test_default);
  TEST_CHECK(color.r == 123);
  TEST_CHECK(color.g == 234);
  TEST_CHECK(color.b == 45);

  gcmz_style_config_destroy(&config);
}

static void test_mixed_valid_invalid_config(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/default_style.conf"),
          .override_config_path = TEST_PATH(L"style_config/malformed_colors.conf"),
      },
      &err);
  TEST_CHECK(config != NULL);

  static struct gcmz_color const black_default = {0, 0, 0};
  static struct gcmz_color const test_default = {111, 222, 33};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ZoomGauge", black_default);
  TEST_CHECK(color.r == 0x60); // From default_style.conf
  TEST_CHECK(color.g == 0xa0);
  TEST_CHECK(color.b == 0xff);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "ValidRGB", black_default);
  TEST_CHECK(color.r == 0x60); // From malformed_colors.conf
  TEST_CHECK(color.g == 0xa0);
  TEST_CHECK(color.b == 0xff);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TooShort", test_default);
  TEST_CHECK(color.r == 111); // Should use default
  TEST_CHECK(color.g == 222);
  TEST_CHECK(color.b == 33);

  gcmz_style_config_destroy(&config);
}

static void test_null_parameter_handling(void) {
  struct gcmz_style_config *config = NULL;

  static struct gcmz_color const test_default = {100, 150, 200};
  struct gcmz_color color = {0};

  // Test NULL config parameter - should return default color
  color = gcmz_style_config_get_blended_color_fallback(NULL, NULL, "ZoomGauge", test_default);
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  // Test NULL key parameter - should return default color
  color = gcmz_style_config_get_blended_color_fallback(config, NULL, NULL, test_default);
  TEST_CHECK(color.r == 100);
  TEST_CHECK(color.g == 150);
  TEST_CHECK(color.b == 200);

  gcmz_style_config_destroy(&config);
}

static void test_alpha_blending_edge_cases(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/rgba_style.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  static struct gcmz_color const black_default = {0, 0, 0};
  struct gcmz_color color = {0};

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "TransparentColor", black_default);
  TEST_CHECK(color.r == 0x20); // Should be background (202020)
  TEST_CHECK(color.g == 0x20);
  TEST_CHECK(color.b == 0x20);

  color = gcmz_style_config_get_blended_color_fallback(config, NULL, "OpaqueColor", black_default);
  TEST_CHECK(color.r == 0xff); // Should be white (ffffff)
  TEST_CHECK(color.g == 0xff);
  TEST_CHECK(color.b == 0xff);

  gcmz_style_config_destroy(&config);
}

static void test_integer_config_loading(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/integer_base_config.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "LayerHeight", 0) == 20);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "MaxFrames", 0) == 1000);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "Offset", 0) == -10);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "InitialValue", 999) == 0);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "LargeValue", 0) == INT64_MAX);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "SmallValue", 0) == -9223372036854775807LL);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "MinInt64", 0) == INT64_MIN);

  gcmz_style_config_destroy(&config);
}

static void test_integer_override_precedence(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/integer_base_config.conf"),
          .override_config_path = TEST_PATH(L"style_config/integer_override_config.conf"),
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "LayerHeight", 0) == 25);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "MaxFrames", 0) == 2000);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "OverrideIntTest", 0) == 200);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "BufferSize", 0) == 512);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "BaseOnlyInt", 0) == 42);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "OverrideOnlyInt", 0) == 999);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "SpacedValue", 0) == 123);

  gcmz_style_config_destroy(&config);
}

static void test_integer_default_fallback(void) {
  struct ov_error err = {0};
  struct gcmz_style_config *config = gcmz_style_config_create(
      &(struct gcmz_style_config_options){
          .base_config_path = TEST_PATH(L"style_config/invalid_integers.conf"),
          .override_config_path = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(config != NULL, &err)) {
    return;
  }

  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "NotANumber", 123) == 123);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "EmptyValue", 456) == 456);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "JustText", 789) == 789);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "TooLarge", 999) == 999);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "InvalidChars", 111) == 111);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "NonExistentKey", 555) == 555);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "ValidInt", 0) == 42);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(config, NULL, "ValidNegative", 0) == -50);

  gcmz_style_config_destroy(&config);
}

static void test_integer_uninitialized_config(void) {
  TEST_CHECK(gcmz_style_config_get_integer_fallback(NULL, NULL, "LayerHeight", 100) == 100);
  TEST_CHECK(gcmz_style_config_get_integer_fallback(NULL, NULL, NULL, 200) == 200);
}

TEST_LIST = {
    {"test_config_create", test_config_create},
    {"test_color_parsing", test_color_parsing},
    {"test_rgba_parsing", test_rgba_parsing},
    {"test_invalid_colors", test_invalid_colors},
    {"test_config_override", test_config_override},
    {"test_base_config_only", test_base_config_only},
    {"test_missing_config_fallback", test_missing_config_fallback},
    {"test_override_only_config", test_override_only_config},
    {"test_mixed_valid_invalid_config", test_mixed_valid_invalid_config},
    {"test_null_parameter_handling", test_null_parameter_handling},
    {"test_alpha_blending_edge_cases", test_alpha_blending_edge_cases},
    {"test_integer_config_loading", test_integer_config_loading},
    {"test_integer_override_precedence", test_integer_override_precedence},
    {"test_integer_default_fallback", test_integer_default_fallback},
    {"test_integer_uninitialized_config", test_integer_uninitialized_config},
    {NULL, NULL},
};
