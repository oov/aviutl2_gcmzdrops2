#include <ovtest.h>

#include "datauri.h"

#include <ovarray.h>
#include <string.h>

static void check_data_uri_result(wchar_t const *data_uri,
                                  wchar_t const *expected_mime,
                                  wchar_t const *expected_charset,
                                  wchar_t const *expected_ext_filename,
                                  int expected_encoding,
                                  void const *expected_decoded,
                                  size_t expected_decoded_len) {
  struct gcmz_data_uri d = {0};
  struct ov_error err = {0};
  bool result = gcmz_data_uri_parse(data_uri, wcslen(data_uri), &d, &err);
  TEST_CHECK(result);
  if (!result) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (expected_mime) {
    TEST_CHECK(wcscmp(d.mime, expected_mime) == 0);
    TEST_MSG("want %ls, got %ls", expected_mime, d.mime);
  }
  if (expected_charset) {
    TEST_CHECK(wcscmp(d.charset, expected_charset) == 0);
    TEST_MSG("want %ls, got %ls", expected_charset, d.charset);
  } else {
    TEST_CHECK(d.charset[0] == L'\0');
  }
  if (expected_ext_filename) {
    TEST_CHECK(wcscmp(d.ext_filename, expected_ext_filename) == 0);
    TEST_MSG("want %ls, got %ls", expected_ext_filename, d.ext_filename);
  } else {
    TEST_CHECK(d.ext_filename[0] == L'\0');
  }
  TEST_CHECK(d.encoding == expected_encoding);
  result = gcmz_data_uri_decode(&d, &err);
  TEST_CHECK(result);
  if (!result) {
    OV_ERROR_DESTROY(&err);
    gcmz_data_uri_destroy(&d);
    return;
  }
  if (expected_decoded && expected_decoded_len > 0) {
    TEST_CHECK(d.decoded_len == expected_decoded_len);
    if (d.decoded_len == expected_decoded_len) {
      TEST_CHECK(memcmp(d.decoded, expected_decoded, expected_decoded_len) == 0);
    }
  } else {
    TEST_CHECK(d.decoded_len == 0);
    TEST_CHECK(d.decoded == NULL);
  }

  gcmz_data_uri_destroy(&d);
}

static void test_basic_text(void) {
  static wchar_t const uri[] = L"data:,Hello%2C%20World%21";
  static char const expected[] = "Hello, World!";
  check_data_uri_result(uri, L"text/plain", L"US-ASCII", NULL, data_uri_encoding_percent, expected, strlen(expected));
}

static void test_text_with_charset(void) {
  static wchar_t const uri[] = L"data:text/plain;charset=utf-8,Hello%20World";
  static char const expected[] = "Hello World";
  check_data_uri_result(uri, L"text/plain", L"utf-8", NULL, data_uri_encoding_percent, expected, strlen(expected));
}

static void test_base64_text(void) {
  static wchar_t const uri[] = L"data:text/plain;base64,SGVsbG8sIFdvcmxkIQ==";
  static char const expected[] = "Hello, World!";
  check_data_uri_result(uri, L"text/plain", L"US-ASCII", NULL, data_uri_encoding_base64, expected, strlen(expected));
}

static void test_base64_image(void) {
  static wchar_t const uri[] =
      L"data:image/"
      L"png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChAI9hyhUKgAAAABJRU5ErkJggg==";
  static uint8_t const expected[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0x60, 0xf8, 0x5f, 0x0f, 0x00, 0x02, 0x84, 0x02, 0x3d,
      0x87, 0x28, 0x54, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  check_data_uri_result(uri, L"image/png", NULL, NULL, data_uri_encoding_base64, expected, sizeof(expected));
}

static void test_filename_extension(void) {
  static wchar_t const uri[] = L"data:text/plain;filename=test.txt,Hello%20World";
  static char const expected[] = "Hello World";
  check_data_uri_result(
      uri, L"text/plain", L"US-ASCII", L"test.txt", data_uri_encoding_percent, expected, strlen(expected));
}

static void test_filename_percent_encoded(void) {
  static wchar_t const uri[] = L"data:text/plain;filename=my%20file.txt,Hello";
  static char const expected[] = "Hello";
  check_data_uri_result(
      uri, L"text/plain", L"US-ASCII", L"my file.txt", data_uri_encoding_percent, expected, strlen(expected));
}

static void test_empty_data(void) {
  static wchar_t const uri[] = L"data:,";
  check_data_uri_result(uri, L"text/plain", L"US-ASCII", NULL, data_uri_encoding_percent, "", 0);
}

static void test_invalid_data_uris(void) {
  struct gcmz_data_uri d = {0};
  struct ov_error err = {0};

  TEST_CASE("Missing data: prefix");
  if (!TEST_CHECK(!gcmz_data_uri_parse(L"hello,world", 11, &d, &err))) {
    goto cleanup;
  }
  OV_ERROR_DESTROY(&err);

  TEST_CASE("Missing comma separator");
  if (!TEST_CHECK(!gcmz_data_uri_parse(L"data:text/plain", 15, &d, &err))) {
    goto cleanup;
  }
  OV_ERROR_DESTROY(&err);

  TEST_CASE("NULL pointer");
  if (!TEST_CHECK(!gcmz_data_uri_parse(NULL, 0, &d, &err))) {
    goto cleanup;
  }
  OV_ERROR_DESTROY(&err);

  TEST_CASE("NULL destination");
  if (!TEST_CHECK(!gcmz_data_uri_parse(L"data:,hello", 11, NULL, &err))) {
    goto cleanup;
  }
  OV_ERROR_DESTROY(&err);

cleanup:
  gcmz_data_uri_destroy(&d);
}

static void test_invalid_base64(void) {
  struct gcmz_data_uri d = {0};
  struct ov_error err = {0};

  if (!TEST_CHECK(gcmz_data_uri_parse(L"data:text/plain;base64,Invalid@Base64!", 39, &d, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (TEST_CHECK(!gcmz_data_uri_decode(&d, &err))) {
    OV_ERROR_DESTROY(&err);
  }

cleanup:
  gcmz_data_uri_destroy(&d);
}

static void test_suggest_filename(void) {
  struct gcmz_data_uri d = {0};
  wchar_t *filename1 = NULL;
  wchar_t *filename2 = NULL;
  struct ov_error err = {0};

  TEST_CASE("with explicit filename");
  if (!TEST_CHECK(gcmz_data_uri_parse(L"data:text/plain;filename=test.txt,Hello", 40, &d, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_data_uri_suggest_filename(&d, &filename1, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(wcscmp(filename1, L"test.txt") == 0)) {
    TEST_MSG("want test.txt, got %ls", filename1);
  }
  gcmz_data_uri_destroy(&d);

  TEST_CASE("with mime type extension");
  if (!TEST_CHECK(gcmz_data_uri_parse(L"data:image/png;base64,iVBORw0KGgo=", 32, &d, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_data_uri_decode(&d, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_data_uri_suggest_filename(&d, &filename2, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  {
    size_t const len = wcslen(filename2);
    if (!TEST_CHECK(len >= 4 && wcscmp(filename2 + len - 4, L".png") == 0)) {
      TEST_MSG("want .png extension, got %ls", filename2);
    }
  }

cleanup:
  if (filename1) {
    OV_ARRAY_DESTROY(&filename1);
  }
  if (filename2) {
    OV_ARRAY_DESTROY(&filename2);
  }
  gcmz_data_uri_destroy(&d);
}

static void test_get_mime(void) {
  struct gcmz_data_uri d = {0};
  wchar_t *mime1 = NULL;
  wchar_t *mime2 = NULL;
  struct ov_error err = {0};

  TEST_CASE("text/html with charset");
  if (!TEST_CHECK(gcmz_data_uri_parse(L"data:text/html;charset=utf-8,<html></html>", 42, &d, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_data_uri_get_mime(&d, &mime1, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(wcscmp(mime1, L"text/html; charset=utf-8") == 0)) {
    TEST_MSG("want 'text/html; charset=utf-8', got '%ls'", mime1);
  }
  gcmz_data_uri_destroy(&d);

  TEST_CASE("image/png");
  if (!TEST_CHECK(gcmz_data_uri_parse(L"data:image/png;base64,iVBORw0KGgo=", 32, &d, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_data_uri_get_mime(&d, &mime2, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(wcscmp(mime2, L"image/png") == 0)) {
    TEST_MSG("want 'image/png', got '%ls'", mime2);
  }

cleanup:
  if (mime1) {
    OV_ARRAY_DESTROY(&mime1);
  }
  if (mime2) {
    OV_ARRAY_DESTROY(&mime2);
  }
  gcmz_data_uri_destroy(&d);
}

static void test_complex_parameters(void) {
  static wchar_t const uri[] =
      L"data:application/json;charset=utf-8;filename=data.json;base64,eyJ0ZXN0IjoidmFsdWUifQ==";
  static char const expected[] = "{\"test\":\"value\"}";
  check_data_uri_result(
      uri, L"application/json", L"utf-8", L"data.json", data_uri_encoding_base64, expected, strlen(expected));
}

static void test_percent_encoding_special_chars(void) {
  static wchar_t const uri[] = L"data:text/plain,Line1%0ALine2%0D%0ATab%09Space%20";
  static char const expected[] = "Line1\nLine2\r\nTab\tSpace ";
  check_data_uri_result(uri, L"text/plain", L"US-ASCII", NULL, data_uri_encoding_percent, expected, strlen(expected));
}

TEST_LIST = {
    {"basic_text", test_basic_text},
    {"text_with_charset", test_text_with_charset},
    {"base64_text", test_base64_text},
    {"base64_image", test_base64_image},
    {"filename_extension", test_filename_extension},
    {"filename_percent_encoded", test_filename_percent_encoded},
    {"empty_data", test_empty_data},
    {"invalid_data_uris", test_invalid_data_uris},
    {"invalid_base64", test_invalid_base64},
    {"suggest_filename", test_suggest_filename},
    {"get_mime", test_get_mime},
    {"complex_parameters", test_complex_parameters},
    {"percent_encoding_special_chars", test_percent_encoding_special_chars},
    {NULL, NULL},
};
