#include <ovtest.h>

#include <ovl/crypto.h>
#include <ovl/file.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <string.h>

#include "ini_reader.h"

#include "ini_sign.c"

#ifndef SOURCE_DIR
#  define SOURCE_DIR .
#endif

#define LSTR(x) L##x
#define LSTR2(x) LSTR(#x)
#define STRINGIZE(x) LSTR2(x)
#define TEST_PATH(relative_path) STRINGIZE(SOURCE_DIR) L"/test_data/" relative_path

static bool create_test_ini_with_signature(wchar_t const *filepath, char const *signature_hex, struct ov_error *err) {
  static char const format[] = "signature = %1$s\n"
                               "updated_at = 2025-09-21T12:00:00+09:00\n"
                               "\n"
                               "[test_version_1]\n"
                               "version_string = 0x12345678\n"
                               "version_string_hash = 0xabcdef0123456789\n"
                               "\n"
                               "[test_version_2]\n"
                               "version_string = 0x87654321\n"
                               "version_string_hash = 0x9876543210fedcba\n";

  struct ovl_file *f = NULL;
  char content[1024];
  int content_len = 0;
  size_t written = 0;
  bool result = false;

  if (!ovl_file_create(filepath, &f, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  content_len = ov_snprintf_char(content, sizeof(content), format, format, signature_hex);
  if (content_len < 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }

  if (!ovl_file_write(f, content, (size_t)content_len, &written, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (f) {
    ovl_file_close(f);
  }
  return result;
}

static void test_sign_and_verify_basic(void) {
  struct gcmz_ini_reader *reader = NULL;
  struct ov_error err = {0};
  uint8_t public_key[gcmz_sign_public_key_size];
  uint8_t secret_key[gcmz_sign_secret_key_size];
  uint8_t signature[gcmz_sign_signature_size];
  wchar_t const test_ini[] = TEST_PATH(L"ini_sign/test_basic.ini");
  wchar_t const temp_ini[] = L"test_temp_signed.ini";

  if (!TEST_CHECK(ovl_crypto_sign_generate_keypair(public_key, secret_key, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_sign(reader, secret_key, signature, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  char signature_hex[gcmz_sign_signature_size * 2 + 1];
  for (size_t i = 0; i < gcmz_sign_signature_size; ++i) {
    ov_snprintf_char(signature_hex + i * 2, 3, NULL, "%1$02x", signature[i]);
  }
  signature_hex[gcmz_sign_signature_size * 2] = '\0';

  if (!TEST_CHECK(create_test_ini_with_signature(temp_ini, signature_hex, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  gcmz_ini_reader_destroy(&reader);
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, temp_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_sign_verify(reader, public_key, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

cleanup:
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
  DeleteFileW(temp_ini);
}

static void test_verify_wrong_key(void) {
  struct gcmz_ini_reader *reader = NULL;
  struct ov_error err = {0};
  uint8_t public_key1[gcmz_sign_public_key_size];
  uint8_t secret_key1[gcmz_sign_secret_key_size];
  uint8_t public_key2[gcmz_sign_public_key_size];
  uint8_t secret_key2[gcmz_sign_secret_key_size];
  uint8_t signature[gcmz_sign_signature_size];
  wchar_t const *const test_ini = TEST_PATH(L"ini_sign/test_basic.ini");
  wchar_t const temp_ini[] = L"test_temp_wrong_key.ini";

  if (!TEST_CHECK(ovl_crypto_sign_generate_keypair(public_key1, secret_key1, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(ovl_crypto_sign_generate_keypair(public_key2, secret_key2, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_sign(reader, secret_key1, signature, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  char signature_hex[gcmz_sign_signature_size * 2 + 1];
  for (size_t i = 0; i < gcmz_sign_signature_size; ++i) {
    ov_snprintf_char(signature_hex + i * 2, 3, NULL, "%1$02x", signature[i]);
  }
  signature_hex[gcmz_sign_signature_size * 2] = '\0';

  if (!TEST_CHECK(create_test_ini_with_signature(temp_ini, signature_hex, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  gcmz_ini_reader_destroy(&reader);
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, temp_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (TEST_CHECK(!gcmz_sign_verify(reader, public_key2, &err))) {
    OV_ERROR_DESTROY(&err);
  }

cleanup:
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
  DeleteFileW(temp_ini); // Clean up temporary file
}

static void test_error_cases(void) {
  struct gcmz_ini_reader *reader = NULL;
  struct ov_error err = {0};
  uint8_t public_key[gcmz_sign_public_key_size];
  uint8_t secret_key[gcmz_sign_secret_key_size];
  uint8_t signature[gcmz_sign_signature_size];
  wchar_t const *const test_ini = TEST_PATH(L"ini_sign/test_no_updated_at.ini");

  if (!TEST_CHECK(ovl_crypto_sign_generate_keypair(public_key, secret_key, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (TEST_CHECK(!gcmz_sign(NULL, secret_key, signature, &err))) {
    OV_ERROR_DESTROY(&err);
  }
  if (TEST_CHECK(!gcmz_sign_verify(NULL, public_key, &err))) {
    OV_ERROR_DESTROY(&err);
  }
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (TEST_CHECK(!gcmz_sign(reader, secret_key, signature, &err))) {
    OV_ERROR_DESTROY(&err);
  }

cleanup:
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
}

static void test_quicksort_sections(void) {
  size_t const num_sections = 3;
  size_t const data_size = sizeof(uint64_t) + sizeof(struct section) * num_sections;
  struct canonical_data *data = NULL;
  size_t line_numbers[3] = {7, 3, 5};

  TEST_ASSERT(OV_REALLOC(&data, data_size, 1));

  strcpy(data->sections[0].name, "section_c");
  strcpy(data->sections[1].name, "section_a");
  strcpy(data->sections[2].name, "section_b");
  data->sections[0].version_string = 3;
  data->sections[1].version_string = 1;
  data->sections[2].version_string = 2;

  ov_sort(3,
          canonical_sections_compare,
          canonical_sections_swap,
          &(struct canonical_sort_context){
              .data = data,
              .line_numbers = line_numbers,
          });

  TEST_CHECK(line_numbers[0] == 3); // section_a first
  TEST_CHECK(line_numbers[1] == 5); // section_b second
  TEST_CHECK(line_numbers[2] == 7); // section_c third
  TEST_CHECK(strcmp(data->sections[0].name, "section_a") == 0);
  TEST_CHECK(strcmp(data->sections[1].name, "section_b") == 0);
  TEST_CHECK(strcmp(data->sections[2].name, "section_c") == 0);

  OV_FREE(&data);
}

static void test_build_canonical_data(void) {
  struct gcmz_ini_reader *reader = NULL;
  struct ov_error err = {0};
  void *canonical_data = NULL;
  size_t canonical_data_size = 0;
  size_t section_count = 0;
  wchar_t const *const test_ini = TEST_PATH(L"ini_sign/test_basic.ini");

  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  section_count = gcmz_ini_reader_get_section_count(reader);
  TEST_CHECK(section_count > 0);

  if (!TEST_CHECK(build_canonical_data(reader, &canonical_data, &canonical_data_size, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  TEST_CHECK(canonical_data != NULL);
  TEST_CHECK(canonical_data_size > 0);

cleanup:
  if (canonical_data) {
    OV_FREE(&canonical_data);
  }
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
}

static void test_parse_iso8601_with_isotime(void) {
  uint64_t timestamp = 0;
  int32_t offset = 0;
  if (!TEST_CHECK(isotime_parse("2025-09-21T12:00:00+09:00", 25, &timestamp, &offset))) {
    return;
  }
  TEST_CHECK(offset == 9 * 3600);

  char buf[26];
  TEST_CHECK(isotime_format(timestamp, buf, 0) == buf);
  TEST_CHECK(strcmp(buf, "2025-09-21T03:00:00Z") == 0);
}

static void test_parse_hex_uint64(void) {
  uint64_t result;
  TEST_CHECK(parse_hex_uint64("0x12345678", 10, &result));
  TEST_CHECK(result == 0x12345678);
}

static void test_ini_reader_basic(void) {
  struct gcmz_ini_reader *reader = NULL;
  struct ov_error err = {0};
  struct gcmz_ini_value updated_at = {0};
  wchar_t const *const test_ini = TEST_PATH(L"ini_sign/test_basic.ini");

  if (!TEST_CHECK(gcmz_ini_reader_create(&reader, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  updated_at = gcmz_ini_reader_get_value(reader, NULL, "updated_at");
  TEST_CHECK(updated_at.ptr != NULL);
  TEST_CHECK(updated_at.size > 0);

  // Should have at least global + test sections
  TEST_CHECK(gcmz_ini_reader_get_section_count(reader) >= 2);

cleanup:
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
}

static void test_canonical_data_consistency(void) {
  struct gcmz_ini_reader *reader1 = NULL;
  struct gcmz_ini_reader *reader2 = NULL;
  struct ov_error err = {0};
  uint8_t public_key[gcmz_sign_public_key_size];
  uint8_t secret_key[gcmz_sign_secret_key_size];
  uint8_t signature1[gcmz_sign_signature_size];
  uint8_t signature2[gcmz_sign_signature_size];
  wchar_t const *const test_ini = TEST_PATH(L"ini_sign/test_basic.ini");

  if (!TEST_CHECK(ovl_crypto_sign_generate_keypair(public_key, secret_key, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader1, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_create(&reader2, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader1, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_ini_reader_load_file(reader2, test_ini, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  // Sign both - should produce identical signatures
  if (!TEST_CHECK(gcmz_sign(reader1, secret_key, signature1, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }
  if (!TEST_CHECK(gcmz_sign(reader2, secret_key, signature2, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  TEST_CHECK(memcmp(signature1, signature2, gcmz_sign_signature_size) == 0);

cleanup:
  if (reader1) {
    gcmz_ini_reader_destroy(&reader1);
  }
  if (reader2) {
    gcmz_ini_reader_destroy(&reader2);
  }
}

TEST_LIST = {
    {"test_parse_iso8601_with_isotime", test_parse_iso8601_with_isotime},
    {"test_parse_hex_uint64", test_parse_hex_uint64},
    {"test_ini_reader_basic", test_ini_reader_basic},
    {"test_quicksort_sections", test_quicksort_sections},
    {"test_build_canonical_data", test_build_canonical_data},
    {"test_sign_and_verify_basic", test_sign_and_verify_basic},
    {"test_verify_wrong_key", test_verify_wrong_key},
    {"test_error_cases", test_error_cases},
    {"test_canonical_data_consistency", test_canonical_data_consistency},
    {NULL, NULL},
};
