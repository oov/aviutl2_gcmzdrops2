#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <objidl.h>

#include <gdiplus.h>

#define SAVE_ANNOTATED_IMAGES 1

static ULONG_PTR g_gdiplusToken = 0;

static inline void my_test_init(void) {
  GdiplusStartupInput gdiplusStartupInput = {0};
  gdiplusStartupInput.GdiplusVersion = 1;
  GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
}

static inline void my_test_fini(void) {
  if (g_gdiplusToken != 0) {
    GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
  }
}

#define TEST_MY_INIT my_test_init()
#define TEST_MY_FINI my_test_fini()

#include <ovtest.h>

#include "analyze.c"

#include <ovarray.h>

#include <string.h>

#ifndef SOURCE_DIR
#  define SOURCE_DIR .
#endif

#define LSTR(x) L##x
#define LSTR2(x) LSTR(#x)
#define STRINGIZE(x) LSTR2(x)
#define TEST_PATH(relative_path) STRINGIZE(SOURCE_DIR) L"/test_data/" relative_path

static struct color_match_test_case {
  uint8_t r, g, b, tr, tg, tb;
  int tol;
  bool expected;
} const color_match_test_cases[] = {
    // Exact match
    {100, 150, 200, 100, 150, 200, 0, true},
    {0, 0, 0, 0, 0, 0, 0, true},
    {255, 255, 255, 255, 255, 255, 0, true},
    // Within tolerance
    {100, 150, 200, 110, 160, 210, 10, true},
    {100, 150, 200, 90, 140, 190, 10, true},
    // Outside tolerance
    {100, 150, 200, 111, 150, 200, 10, false},
    // Boundary conditions
    {100, 150, 200, 115, 165, 215, 15, true},
    {100, 150, 200, 116, 150, 200, 15, false},
    // Edge values
    {0, 0, 0, 10, 10, 10, 10, true},
    {255, 255, 255, 245, 245, 245, 10, true},
    {0, 0, 0, 255, 255, 255, 255, true},
    {255, 255, 255, 0, 0, 0, 255, true},
};

static void test_is_color_match_basic(void) {
  for (size_t i = 0; i < sizeof(color_match_test_cases) / sizeof(color_match_test_cases[0]); i++) {
    struct color_match_test_case const *const tc = &color_match_test_cases[i];
    TEST_CASE_(
        "case %zu: RGB(%u,%u,%u) vs target(%u,%u,%u) tol=%d", i, tc->r, tc->g, tc->b, tc->tr, tc->tg, tc->tb, tc->tol);
    bool const result = is_color_match(tc->r, tc->g, tc->b, tc->tr, tc->tg, tc->tb, tc->tol);
    TEST_CHECK(result == tc->expected);
  }
}

/**
 * Naive is_color_match implementation for comparison
 */
static bool naive_is_color_match(uint8_t r, uint8_t g, uint8_t b, uint8_t tr, uint8_t tg, uint8_t tb, int tol) {
  return (abs((int)r - (int)tr) <= tol) && (abs((int)g - (int)tg) <= tol) && (abs((int)b - (int)tb) <= tol);
}

static void test_is_color_match_consistency(void) {
  for (size_t i = 0; i < sizeof(color_match_test_cases) / sizeof(color_match_test_cases[0]); i++) {
    struct color_match_test_case const *const tc = &color_match_test_cases[i];
    TEST_CASE_(
        "case %zu: RGB(%u,%u,%u) vs target(%u,%u,%u) tol=%d", i, tc->r, tc->g, tc->b, tc->tr, tc->tg, tc->tb, tc->tol);
    bool const optimized = is_color_match(tc->r, tc->g, tc->b, tc->tr, tc->tg, tc->tb, tc->tol);
    bool const naive = naive_is_color_match(tc->r, tc->g, tc->b, tc->tr, tc->tg, tc->tb, tc->tol);
    TEST_CHECK(optimized == naive);
  }
}

#if SAVE_ANNOTATED_IMAGES
/**
 * Save bitmap with annotated rectangles for visual verification
 * Draws semi-transparent colored fills over detected regions for pixel-accurate verification
 * @param bitmap Source bitmap data (24-bit BGR)
 * @param width Image width
 * @param height Image height
 * @param result Analysis result containing rectangles to annotate
 * @param output_path Output file path
 */
static bool save_annotated_image(uint8_t const *const bitmap,
                                 int const width,
                                 int const height,
                                 struct gcmz_analyze_result const *const result,
                                 wchar_t const *const output_path) {
  if (!bitmap || !output_path) {
    return false;
  }

  GpBitmap *gdi_bitmap = NULL;
  GpGraphics *graphics = NULL;
  GpSolidFill *brush = NULL;
  bool success = false;

  {
    size_t const row_size = (size_t)((((unsigned int)width * 3U + 3U) & ~3U));

    GpStatus status = GdipCreateBitmapFromScan0(width, height, (INT)row_size, PixelFormat24bppRGB, NULL, &gdi_bitmap);
    if (status != Ok || !gdi_bitmap) {
      goto cleanup;
    }

    // Copy source bitmap data
    BitmapData bmpData = {0};
    Rect rect = {0, 0, width, height};
    status = GdipBitmapLockBits(gdi_bitmap, &rect, ImageLockModeWrite, PixelFormat24bppRGB, &bmpData);
    if (status != Ok) {
      goto cleanup;
    }

    for (int y = 0; y < height; y++) {
      uint8_t const *src_row = bitmap + (y * (int)row_size);
      uint8_t *dst_row = (uint8_t *)bmpData.Scan0 + (y * bmpData.Stride);
      memcpy(dst_row, src_row, (size_t)width * 3U);
    }

    GdipBitmapUnlockBits(gdi_bitmap, &bmpData);

    status = GdipGetImageGraphicsContext((GpImage *)gdi_bitmap, &graphics);
    if (status != Ok || !graphics) {
      goto cleanup;
    }

    if (result) {
      // Draw layer window in green
      if (result->layer_window.width > 0 && result->layer_window.height > 0) {
        GdipCreateSolidFill(0x1f00ff00, &brush);
        GdipFillRectangleI(graphics,
                           brush,
                           result->layer_window.x,
                           result->layer_window.y,
                           result->layer_window.width,
                           result->layer_window.height);
        GdipDeleteBrush((GpBrush *)brush);
        brush = NULL;
      }

      // Draw effective area in yellow
      if (result->effective_area.width > 0 && result->effective_area.height > 0) {
        GdipCreateSolidFill(0x3fffff00, &brush);
        GdipFillRectangleI(graphics,
                           brush,
                           result->effective_area.x,
                           result->effective_area.y,
                           result->effective_area.width,
                           result->effective_area.height);
        GdipDeleteBrush((GpBrush *)brush);
        brush = NULL;
      }

      // Draw cursor detection area in cyan
      if (result->cursor_detection_area.width > 0 && result->cursor_detection_area.height > 0) {
        GdipCreateSolidFill(0x3f00ffff, &brush);
        GdipFillRectangleI(graphics,
                           brush,
                           result->cursor_detection_area.x,
                           result->cursor_detection_area.y,
                           result->cursor_detection_area.width,
                           result->cursor_detection_area.height);
        GdipDeleteBrush((GpBrush *)brush);
        brush = NULL;
      }

      // Draw zoom bar in red
      if (result->zoom_bar.width > 0 && result->zoom_bar.height > 0) {
        GdipCreateSolidFill(0x5fff0000, &brush);
        GdipFillRectangleI(
            graphics, brush, result->zoom_bar.x, result->zoom_bar.y, result->zoom_bar.width, result->zoom_bar.height);
        GdipDeleteBrush((GpBrush *)brush);
        brush = NULL;
      }

      // Draw cursor in blue
      if (result->cursor.width > 0 && result->cursor.height > 0) {
        GdipCreateSolidFill(0x7f0000ff, &brush);
        GdipFillRectangleI(
            graphics, brush, result->cursor.x, result->cursor.y, result->cursor.width, result->cursor.height);
        GdipDeleteBrush((GpBrush *)brush);
        brush = NULL;
      }
    }

    // Get PNG encoder CLSID
    CLSID pngClsid;
    {
      UINT num = 0;
      UINT size = 0;
      GdipGetImageEncodersSize(&num, &size);
      if (size == 0) {
        goto cleanup;
      }

      ImageCodecInfo *pImageCodecInfo = (ImageCodecInfo *)malloc(size);
      if (!pImageCodecInfo) {
        goto cleanup;
      }

      GdipGetImageEncoders(num, size, pImageCodecInfo);
      bool found = false;
      for (UINT j = 0; j < num; j++) {
        if (wcscmp(pImageCodecInfo[j].MimeType, L"image/png") == 0) {
          pngClsid = pImageCodecInfo[j].Clsid;
          found = true;
          break;
        }
      }
      free(pImageCodecInfo);
      if (!found) {
        goto cleanup;
      }
    }

    status = GdipSaveImageToFile((GpImage *)gdi_bitmap, output_path, &pngClsid, NULL);
    if (status != Ok) {
      goto cleanup;
    }

    success = true;
  }

cleanup:
  if (brush) {
    GdipDeleteBrush((GpBrush *)brush);
  }
  if (graphics) {
    GdipDeleteGraphics(graphics);
  }
  if (gdi_bitmap) {
    GdipDisposeImage((GpImage *)gdi_bitmap);
  }
  return success;
}
#endif // SAVE_ANNOTATED_IMAGES

static bool rects_equal(struct gcmz_analyze_rect const *a, struct gcmz_analyze_rect const *b) {
  if (!a || !b) {
    return false;
  }
  return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

static void report_rect_compare(char const *const name,
                                struct gcmz_analyze_rect const *const want,
                                struct gcmz_analyze_rect const *const got) {
  TEST_MSG("%s want=%d,%d,%d,%d got=%d,%d,%d,%d",
           name,
           want->x,
           want->y,
           want->width,
           want->height,
           got->x,
           got->y,
           got->width,
           got->height);
}

static void test_integration_real_images(void) {
  static struct integration_test_case {
    char const *caption;
    wchar_t const *image_path;
    struct gcmz_analyze_rect expected_zoom_bar;
    struct gcmz_analyze_rect expected_layer_window;
    struct gcmz_analyze_rect expected_effective_area;
    struct gcmz_analyze_rect expected_cursor_detection_area;
    struct gcmz_analyze_rect expected_cursor;
  } const test_cases[] = {
      {
          .caption = "Standard, no cursor, bottom scrollbar",
          .image_path = TEST_PATH(L"analyze/0a.png"),
          .expected_zoom_bar = {126, 533, 77, 8},
          .expected_layer_window = {117, 397, 649, 146},
          .expected_effective_area = {213, 419, 541, 112},
          .expected_cursor_detection_area = {213, 397, 541, 22},
          .expected_cursor = {0, 0, 0, 0},
      },
      {
          .caption = "Standard, has cursor, bottom scrollbar",
          .image_path = TEST_PATH(L"analyze/0b.png"),
          .expected_zoom_bar = {126, 533, 77, 8},
          .expected_layer_window = {117, 397, 649, 146},
          .expected_effective_area = {213, 419, 541, 112},
          .expected_cursor_detection_area = {213, 397, 541, 22},
          .expected_cursor = {293, 419, 1, 112},
      },
      {
          .caption = "Standard, no cursor, top scrollbar",
          .image_path = TEST_PATH(L"analyze/1a.png"),
          .expected_zoom_bar = {126, 399, 77, 8},
          .expected_layer_window = {117, 397, 649, 146},
          .expected_effective_area = {213, 431, 541, 112},
          .expected_cursor_detection_area = {213, 409, 541, 22},
          .expected_cursor = {0, 0, 0, 0},
      },
      {
          .caption = "Standard, has cursor, top scrollbar",
          .image_path = TEST_PATH(L"analyze/1b.png"),
          .expected_zoom_bar = {126, 399, 77, 8},
          .expected_layer_window = {117, 397, 649, 146},
          .expected_effective_area = {213, 431, 541, 112},
          .expected_cursor_detection_area = {213, 409, 541, 22},
          .expected_cursor = {293, 431, 1, 112},
      },
      {
          .caption = "Detached, no cursor, bottom scrollbar",
          .image_path = TEST_PATH(L"analyze/2a.png"),
          .expected_zoom_bar = {9, 207, 77, 8},
          .expected_layer_window = {0, 0, 307, 217},
          .expected_effective_area = {96, 22, 199, 183},
          .expected_cursor_detection_area = {96, 0, 199, 22},
          .expected_cursor = {0, 0, 0, 0},
      },
      {
          .caption = "Detached, has cursor, bottom scrollbar",
          .image_path = TEST_PATH(L"analyze/2b.png"),
          .expected_zoom_bar = {9, 207, 77, 8},
          .expected_layer_window = {0, 0, 307, 217},
          .expected_effective_area = {96, 22, 199, 183},
          .expected_cursor_detection_area = {96, 0, 199, 22},
          .expected_cursor = {104, 22, 1, 183},
      },
      {
          .caption = "Detached, no cursor, top scrollbar",
          .image_path = TEST_PATH(L"analyze/3a.png"),
          .expected_zoom_bar = {9, 2, 77, 8},
          .expected_layer_window = {0, 0, 307, 217},
          .expected_effective_area = {96, 34, 199, 183},
          .expected_cursor_detection_area = {96, 12, 199, 22},
          .expected_cursor = {0, 0, 0, 0},
      },
      {
          .caption = "Detached, has cursor, top scrollbar",
          .image_path = TEST_PATH(L"analyze/3b.png"),
          .expected_zoom_bar = {9, 2, 77, 8},
          .expected_layer_window = {0, 0, 307, 217},
          .expected_effective_area = {96, 34, 199, 183},
          .expected_cursor_detection_area = {96, 12, 199, 22},
          .expected_cursor = {104, 34, 1, 183},
      },
      {
          .caption = "Cursor with overlapping text",
          .image_path = TEST_PATH(L"analyze/cursor_text_overlapped.png"),
          .expected_zoom_bar = {9, 413, 77, 8},
          .expected_layer_window = {0, 0, 668, 423},
          .expected_effective_area = {96, 22, 560, 389},
          .expected_cursor_detection_area = {96, 0, 560, 22},
          .expected_cursor = {377, 22, 1, 389},
      },
  };

  uint8_t *bitmap = NULL;
  struct ov_error err = {0};

  {
    // Test each image
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
      struct integration_test_case const *const tc = &test_cases[i];
      TEST_CASE_("image #%zu: %s", i, tc->caption);

      struct gcmz_analyze_metadata metadata = {0};
      int width, height;
      bitmap = NULL;
      if (!TEST_CHECK(gcmz_analyze_load_png_with_metadata(tc->image_path, &bitmap, &width, &height, &metadata, &err))) {
        OV_ERROR_DESTROY(&err);
        continue;
      }

      struct gcmz_analyze_result result = {0};
      TEST_CHECK(analyze(bitmap, width, height, metadata.zoom, &metadata.style, &result));
      TEST_MSG("status want=%u got=%u", (unsigned int)gcmz_analyze_status_success, (unsigned int)result.status);
      TEST_CHECK(rects_equal(&result.zoom_bar, &tc->expected_zoom_bar));
      report_rect_compare("zoom_bar", &tc->expected_zoom_bar, &result.zoom_bar);
      TEST_CHECK(rects_equal(&result.layer_window, &tc->expected_layer_window));
      report_rect_compare("layer_window", &tc->expected_layer_window, &result.layer_window);
      TEST_CHECK(rects_equal(&result.effective_area, &tc->expected_effective_area));
      report_rect_compare("effective_area", &tc->expected_effective_area, &result.effective_area);
      TEST_CHECK(rects_equal(&result.cursor_detection_area, &tc->expected_cursor_detection_area));
      report_rect_compare("cursor_detection_area", &tc->expected_cursor_detection_area, &result.cursor_detection_area);
      TEST_CHECK(rects_equal(&result.cursor, &tc->expected_cursor));
      report_rect_compare("cursor", &tc->expected_cursor, &result.cursor);

#if SAVE_ANNOTATED_IMAGES
      wchar_t output_path[512];
      ov_snprintf_wchar(output_path, sizeof(output_path) / sizeof(output_path[0]), NULL, L"test_output_%zu.png", i);
      save_annotated_image(bitmap, width, height, &result, output_path);
#endif

      OV_ARRAY_DESTROY(&bitmap);
      bitmap = NULL;
    }
  }

  if (bitmap) {
    OV_ARRAY_DESTROY(&bitmap);
  }
}

TEST_LIST = {
    {"test_is_color_match_basic", test_is_color_match_basic},
    {"test_is_color_match_consistency", test_is_color_match_consistency},
    {"test_integration_real_images", test_integration_real_images},
    {NULL, NULL},
};
