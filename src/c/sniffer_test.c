#include <ovtest.h>

#include "sniffer.h"

#include <string.h>

// Test helper to verify MIME type and extension
static void
check_sniff_result(void const *data, size_t len, wchar_t const *expected_mime, wchar_t const *expected_ext) {
  wchar_t const *mime = NULL;
  wchar_t const *ext = NULL;

  TEST_CHECK(gcmz_sniff((void const *)data, len, &mime, &ext));

  if (expected_mime) {
    TEST_ASSERT(mime != NULL);
    TEST_CHECK(wcscmp(mime, expected_mime) == 0);
    TEST_MSG("Expected MIME: '%ls', Got: '%ls'", expected_mime, mime ? mime : L"NULL");
  } else {
    TEST_ASSERT(mime != NULL); // Should never be NULL in our implementation
  }

  if (expected_ext) {
    TEST_ASSERT(ext != NULL);
    TEST_CHECK(wcscmp(ext, expected_ext) == 0);
    TEST_MSG("Expected ext: '%ls', Got: '%ls'", expected_ext, ext ? ext : L"NULL");
  } else {
    TEST_ASSERT(ext != NULL); // Should never be NULL in our implementation
  }
}

// Test invalid arguments
static void test_invalid_arguments(void) {
  wchar_t const *mime = NULL;
  wchar_t const *ext = NULL;
  uint8_t data[] = {
      0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // GIF89a with
                                                                                                       // padding

  // Test NULL data
  TEST_CHECK(!gcmz_sniff(NULL, 6, &mime, &ext));

  // Test valid cases
  TEST_CHECK(gcmz_sniff(data, 6, NULL, NULL));
  TEST_CHECK(gcmz_sniff(data, 6, &mime, NULL));
  TEST_CHECK(gcmz_sniff(data, 6, NULL, &ext));
  TEST_CHECK(gcmz_sniff(data, 6, &mime, &ext));
}

// Test image format detection - WHATWG MIME Sniffing Standard Section 6.1
static void test_image_formats(void) {
  // GIF87a signature
  uint8_t gif87a[] = {0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(gif87a, sizeof(gif87a), L"image/gif", L".gif");

  // GIF89a signature
  uint8_t gif89a[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(gif89a, sizeof(gif89a), L"image/gif", L".gif");

  // JPEG signature
  uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x48};
  check_sniff_result(jpeg, sizeof(jpeg), L"image/jpeg", L".jpg");

  // PNG signature
  uint8_t png[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52};
  check_sniff_result(png, sizeof(png), L"image/png", L".png");

  // WebP signature
  uint8_t webp[] = {0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x20};
  check_sniff_result(webp, sizeof(webp), L"image/webp", L".webp");

  // Windows Icon signature (ICO)
  uint8_t ico[] = {0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x10, 0x00, 0x01, 0x00, 0x04, 0x00, 0x28, 0x01};
  check_sniff_result(ico, sizeof(ico), L"image/x-icon", L".ico");

  // Windows Cursor signature (CUR)
  uint8_t cur[] = {0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x10, 0x10, 0x10, 0x00, 0x01, 0x00, 0x04, 0x00, 0x28, 0x01};
  check_sniff_result(cur, sizeof(cur), L"image/x-icon", L".cur");

  // BMP signature
  uint8_t bmp[] = {0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00};
  check_sniff_result(bmp, sizeof(bmp), L"image/bmp", L".bmp");
}

// Test audio and video format detection - WHATWG MIME Sniffing Standard Section 6.2
static void test_audio_video_formats(void) {
  // AIFF signature
  uint8_t aiff[] = {0x46, 0x4F, 0x52, 0x4D, 0x00, 0x00, 0x00, 0x00, 0x41, 0x49, 0x46, 0x46, 0x43, 0x4F, 0x4D, 0x4D};
  check_sniff_result(aiff, sizeof(aiff), L"audio/aiff", L".aiff");

  // MP3 with ID3 tag
  uint8_t mp3_id3[] = {0x49, 0x44, 0x33, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFB, 0x90, 0x00, 0x00, 0x00};
  check_sniff_result(mp3_id3, sizeof(mp3_id3), L"audio/mpeg", L".mp3");

  // OGG signature
  uint8_t ogg[] = {0x4F, 0x67, 0x67, 0x53, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(ogg, sizeof(ogg), L"application/ogg", L".ogg");

  // MIDI signature
  uint8_t midi[] = {0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x02, 0x00, 0x60, 0x00, 0x00};
  check_sniff_result(midi, sizeof(midi), L"audio/midi", L".mid");

  // AVI signature
  uint8_t avi[] = {0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 0x20, 0x4C, 0x49, 0x53, 0x54};
  check_sniff_result(avi, sizeof(avi), L"video/avi", L".avi");

  // WAV signature
  uint8_t wav[] = {0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20};
  check_sniff_result(wav, sizeof(wav), L"audio/wave", L".wav");
}

// Test MP4 signature algorithm - WHATWG MIME Sniffing Standard Section 6.2.1
static void test_mp4_signature(void) {
  // MP4 with "mp4" major brand at bytes 8-10 - box size must match data size
  uint8_t mp4_major[] = {0x00, 0x00, 0x00, 0x20, // box size = 32
                         0x66, 0x74, 0x79, 0x70, // "ftyp"
                         0x6D, 0x70, 0x34, 0x20, // "mp4 " (major brand)
                         0x00, 0x00, 0x00, 0x01, // minor version
                         0x69, 0x73, 0x6F, 0x6D, // compatible brands start
                         0x00, 0x00, 0x00, 0x00, // padding to reach 32 bytes
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(mp4_major, sizeof(mp4_major), L"video/mp4", L".mp4");

  // MP4 with "mp4" in compatible brands - box size must match data size
  uint8_t mp4_compat[] = {
      0x00, 0x00, 0x00, 0x1C, // box size = 28
      0x66, 0x74, 0x79, 0x70, // "ftyp"
      0x69, 0x73, 0x6F, 0x6D, // "isom" (major brand)
      0x00, 0x00, 0x00, 0x01, // minor version
      0x6D, 0x70, 0x34, 0x31, // "mp41" (compatible brand - contains "mp4")
      0x6D, 0x70, 0x34, 0x32, // "mp42" (compatible brand - contains "mp4")
      0x00, 0x00, 0x00, 0x00  // padding to reach 28 bytes
  };
  check_sniff_result(mp4_compat, sizeof(mp4_compat), L"video/mp4", L".mp4");
}

// Test WebM signature algorithm - WHATWG MIME Sniffing Standard Section 6.2.2
static void test_webm_signature(void) {
  // WebM EBML header with DocType containing "webm"
  uint8_t webm[] = {
      0x1A, 0x45, 0xDF, 0xA3, // EBML header
      0x9F,                   // Element size (variable)
      0x42, 0x82,             // DocType element ID
      0x84,                   // DocType size = 4
      0x77, 0x65, 0x62, 0x6D, // "webm"
      0x42, 0x87,             // DocTypeVersion
      0x81, 0x02,             // Version 2
      0x42, 0x85,             // DocTypeReadVersion
      0x81, 0x02              // ReadVersion 2
  };
  check_sniff_result(webm, sizeof(webm), L"video/webm", L".webm");
}

// Test MP3 without ID3 signature - WHATWG MIME Sniffing Standard Section 6.2.3
static void test_mp3_no_id3_signature(void) {
  // Per the WHATWG "Signature for MP3 without ID3" specification, a valid MP3 file
  // without an ID3 tag must contain at least two valid MP3 frames. This test case
  // constructs a byte sequence that satisfies this requirement based on the exact
  // calculations performed by the functions in sniffer.c.
  // 1. The first header {0xFF, 0xFD, 0x90, 0x00} is specifically crafted to pass all
  //    checks in `match_mp3_header`, including the `final-layer` check.
  // 2. The frame size calculated by `compute_mp3_frame_size` for this header is 522 bytes.
  // 3. A second valid header is placed at offset 522, fulfilling the two-header requirement.
  uint8_t data[526] = {0};
  data[0] = 0xFF; // First header
  data[1] = 0xFD;
  data[2] = 0x90;
  data[3] = 0x00;
  data[522] = 0xFF; // Second header at offset 522
  data[523] = 0xFD;
  data[524] = 0x90;
  data[525] = 0x00;

  check_sniff_result(data, sizeof(data), L"audio/mpeg", L".mp3");
}

// Test font format detection - WHATWG MIME Sniffing Standard Section 6.3
static void test_font_formats(void) {
  // Embedded OpenType (EOT) signature
  uint8_t eot[36] = {0};
  eot[34] = 0x4C; // 'L'
  eot[35] = 0x50; // 'P'
  check_sniff_result(eot, sizeof(eot), L"application/vnd.ms-fontobject", L".eot");

  // TrueType Font signature
  uint8_t ttf[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x80, 0x00, 0x03, 0x00, 0x70, 0x47, 0x44, 0x45, 0x46};
  check_sniff_result(ttf, sizeof(ttf), L"font/ttf", L".ttf");

  // OpenType Font signature
  uint8_t otf[] = {0x4F, 0x54, 0x54, 0x4F, 0x00, 0x0C, 0x00, 0x80, 0x00, 0x03, 0x00, 0x70, 0x43, 0x46, 0x46, 0x20};
  check_sniff_result(otf, sizeof(otf), L"font/otf", L".otf");

  // TrueType Collection signature
  uint8_t ttc[] = {0x74, 0x74, 0x63, 0x66, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x90};
  check_sniff_result(ttc, sizeof(ttc), L"font/collection", L".ttc");

  // WOFF signature
  uint8_t woff[] = {0x77, 0x4F, 0x46, 0x46, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(woff, sizeof(woff), L"font/woff", L".woff");

  // WOFF2 signature
  uint8_t woff2[] = {0x77, 0x4F, 0x46, 0x32, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(woff2, sizeof(woff2), L"font/woff2", L".woff2");
}

// Test HTML detection - WHATWG MIME Sniffing Standard Section 8.2
static void test_html_detection(void) {
  // DOCTYPE HTML (case insensitive)
  uint8_t doctype[] = "<!DOCTYPE html>";
  check_sniff_result(doctype, strlen((char *)doctype) + 1, L"text/html", L".html");

  // DOCTYPE with whitespace prefix
  uint8_t doctype_ws[] = "  \t\n<!DOCTYPE HTML>";
  check_sniff_result(doctype_ws, strlen((char *)doctype_ws) + 1, L"text/html", L".html");

  // HTML tag
  uint8_t html_tag[] = "<HTML>";
  check_sniff_result(html_tag, strlen((char *)html_tag) + 1, L"text/html", L".html");

  // HEAD tag (case insensitive)
  uint8_t head_tag[] = "<head>";
  check_sniff_result(head_tag, strlen((char *)head_tag) + 1, L"text/html", L".html");

  // SCRIPT tag
  uint8_t script_tag[] = "<SCRIPT>";
  check_sniff_result(script_tag, strlen((char *)script_tag) + 1, L"text/html", L".html");

  // Various HTML tags
  uint8_t iframe_tag[] = "<IFRAME>";
  check_sniff_result(iframe_tag, strlen((char *)iframe_tag) + 1, L"text/html", L".html");

  uint8_t h1_tag[] = "<H1>";
  check_sniff_result(h1_tag, strlen((char *)h1_tag) + 1, L"text/html", L".html");

  uint8_t div_tag[] = "<DIV>";
  check_sniff_result(div_tag, strlen((char *)div_tag) + 1, L"text/html", L".html");

  uint8_t font_tag[] = "<FONT>";
  check_sniff_result(font_tag, strlen((char *)font_tag) + 1, L"text/html", L".html");

  uint8_t table_tag[] = "<TABLE>";
  check_sniff_result(table_tag, strlen((char *)table_tag) + 1, L"text/html", L".html");

  uint8_t a_tag[] = "<A>";
  check_sniff_result(a_tag, strlen((char *)a_tag) + 1, L"text/html", L".html");

  uint8_t style_tag[] = "<STYLE>";
  check_sniff_result(style_tag, strlen((char *)style_tag) + 1, L"text/html", L".html");

  uint8_t title_tag[] = "<TITLE>";
  check_sniff_result(title_tag, strlen((char *)title_tag) + 1, L"text/html", L".html");

  uint8_t body_tag[] = "<BODY>";
  check_sniff_result(body_tag, strlen((char *)body_tag) + 1, L"text/html", L".html");

  uint8_t br_tag[] = "<BR>";
  check_sniff_result(br_tag, strlen((char *)br_tag) + 1, L"text/html", L".html");

  uint8_t p_tag[] = "<P>";
  check_sniff_result(p_tag, strlen((char *)p_tag) + 1, L"text/html", L".html");

  // HTML comment
  uint8_t comment[] = "<!-- ";
  check_sniff_result(comment, strlen((char *)comment) + 1, L"text/html", L".html");
}

// Test XML and other text formats
static void test_xml_and_text_formats(void) {
  // XML declaration
  uint8_t xml[] = "<?xml version=\"1.0\"?>";
  check_sniff_result(xml, strlen((char *)xml) + 1, L"text/xml", L".xml");

  // PostScript signature
  uint8_t postscript[] = "%!PS-Adobe-3.0";
  check_sniff_result(postscript, strlen((char *)postscript) + 1, L"application/postscript", L".ps");

  // UTF-16BE BOM
  uint8_t utf16be[] = {0xFE, 0xFF, 0x00, 0x48, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x6F, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(utf16be, sizeof(utf16be), L"text/plain", L".txt");

  // UTF-16LE BOM
  uint8_t utf16le[] = {0xFF, 0xFE, 0x48, 0x00, 0x65, 0x00, 0x6C, 0x00, 0x6C, 0x00, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(utf16le, sizeof(utf16le), L"text/plain", L".txt");

  // UTF-8 BOM
  uint8_t utf8bom[] = {0xEF, 0xBB, 0xBF, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x00, 0x00};
  check_sniff_result(utf8bom, sizeof(utf8bom), L"text/plain", L".txt");
}

// Test archive formats - WHATWG MIME Sniffing Standard Section 6.4
static void test_archive_formats(void) {
  // GZIP signature
  uint8_t gzip[] = {0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x07};
  check_sniff_result(gzip, sizeof(gzip), L"application/x-gzip", L".gz");

  // ZIP signature
  uint8_t zip[] = {0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  check_sniff_result(zip, sizeof(zip), L"application/zip", L".zip");

  // RAR signature
  uint8_t rar[] = {0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00, 0xCF, 0x90, 0x73, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00};
  check_sniff_result(rar, sizeof(rar), L"application/x-rar-compressed", L".rar");
}

// Test PDF format
static void test_pdf_format(void) {
  uint8_t pdf[] = "%PDF-1.4";
  check_sniff_result(pdf, strlen((char *)pdf) + 1, L"application/pdf", L".pdf");
}

// Test fallback to binary data
static void test_unknown_format(void) {
  uint8_t unknown[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  check_sniff_result(unknown, sizeof(unknown), L"application/octet-stream", L".bin");
}

// Test edge cases and boundary conditions
static void test_edge_cases(void) {
  // Minimum required length
  uint8_t min_data[16] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  wchar_t const *mime = NULL;
  wchar_t const *ext = NULL;
  TEST_CHECK(gcmz_sniff(min_data, 16, &mime, &ext));
  TEST_ASSERT(mime != NULL);
  TEST_ASSERT(ext != NULL);

  // Large buffer with pattern at the beginning
  uint8_t large_gif[1000];
  memset(large_gif, 0, sizeof(large_gif));
  memcpy(large_gif, "GIF89a", 6);
  check_sniff_result(large_gif, sizeof(large_gif), L"image/gif", L".gif");

  // Test patterns that require specific lengths (EOT needs 36 bytes)
  uint8_t short_eot[35] = {0};
  short_eot[34] = 0x4C;
  // Should not be detected as EOT because it's too short
  check_sniff_result(short_eot, sizeof(short_eot), L"application/octet-stream", L".bin");
}

TEST_LIST = {
    {"invalid_arguments", test_invalid_arguments},
    {"image_formats", test_image_formats},
    {"audio_video_formats", test_audio_video_formats},
    {"mp4_signature", test_mp4_signature},
    {"webm_signature", test_webm_signature},
    {"mp3_no_id3_signature", test_mp3_no_id3_signature},
    {"font_formats", test_font_formats},
    {"html_detection", test_html_detection},
    {"xml_and_text_formats", test_xml_and_text_formats},
    {"archive_formats", test_archive_formats},
    {"pdf_format", test_pdf_format},
    {"unknown_format", test_unknown_format},
    {"edge_cases", test_edge_cases},
    {NULL, NULL},
};
