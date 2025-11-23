#include "ini_sign.h"

#include <assert.h>
#include <string.h>

#include <ovarray.h>
#include <ovbase.h>
#include <ovmo.h>
#include <ovsort.h>

#include <ovl/crypto.h>

#include "ini_reader.h"
#include "isotime.h"

// Verify that size constants match ovl/crypto.h
static_assert((int)gcmz_sign_public_key_size == (int)ovl_crypto_sign_publickey_size, "Public key size mismatch");
static_assert((int)gcmz_sign_secret_key_size == (int)ovl_crypto_sign_secretkey_size, "Secret key size mismatch");
static_assert((int)gcmz_sign_signature_size == (int)ovl_crypto_sign_signature_size, "Signature size mismatch");

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wunused-function")
#    pragma GCC diagnostic ignored "-Wunused-function"
#  endif
#endif // __GNUC__

static inline bool is_little_endian(void) {
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ||                                          \
    (defined(_M_ENDIAN) && _M_ENDIAN == 0) || defined(__LITTLE_ENDIAN__)
#  ifndef __LITTLE_ENDIAN__
#    define __LITTLE_ENDIAN__
#  endif
  return true;
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || (defined(_M_ENDIAN) && _M_ENDIAN == 1) || \
    defined(__BIG_ENDIAN__)
#  ifndef __BIG_ENDIAN__
#    define __BIG_ENDIAN__
#  endif
  return false;
#else
  return (union {
           uint32_t u;
           uint8_t c[sizeof(uint32_t)];
         }){.u = 1}
             .c[0] == 1;
#endif
}

static inline uint64_t swap64(uint64_t v) {
#if defined(__clang__) || defined(__GNUC__)
  return __builtin_bswap64(v);
#elif defined(_MSC_VER)
  return _byteswap_uint64(v);
#else
  return (((v >> 56) & UINT64_C(0x00000000000000ff)) | ((v >> 40) & UINT64_C(0x000000000000ff00)) |
          ((v >> 24) & UINT64_C(0x0000000000ff0000)) | ((v >> 8) & UINT64_C(0x00000000ff000000)) |
          ((v << 8) & UINT64_C(0x000000ff00000000)) | ((v << 24) & UINT64_C(0x0000ff0000000000)) |
          ((v << 40) & UINT64_C(0x00ff000000000000)) | ((v << 56) & UINT64_C(0xff00000000000000)));
#endif
}

static inline uint64_t to_little_endian64(uint64_t v) {
#ifdef __LITTLE_ENDIAN__
  return v;
#elif defined(__BIG_ENDIAN__)
  return swap64(v);
#else
  return is_little_endian() ? v : swap64(v);
#endif
}

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

static int hex_char_to_value(int const c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
    return (c | 0x20) - 'a' + 10;
  }
  return -1;
}

static bool parse_hex_uint64(char const *str, size_t len, uint64_t *value) {
  if (!str || !value) {
    return false;
  }

  if (len >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    str += 2;
    len -= 2;
  }

  if (len == 0 || len > 16) {
    return false;
  }

  *value = 0;
  for (size_t i = 0; i < len; ++i) {
    int const digit = hex_char_to_value(str[i]);
    if (digit < 0) {
      return false;
    }
    *value = (*value << 4) | (uint64_t)digit;
  }
  return true;
}

static bool parse_signature(char const *const str,
                            size_t const len,
                            uint8_t signature[gcmz_sign_signature_size],
                            struct ov_error *const err) {
  if (!str || len != gcmz_sign_signature_size * 2 || !signature) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  for (size_t i = 0; i < gcmz_sign_signature_size; ++i) {
    int const val1 = hex_char_to_value(str[i * 2]);
    int const val2 = hex_char_to_value(str[i * 2 + 1]);
    if (val1 < 0 || val2 < 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("invalid hex character in signature"));
      return false;
    }
    signature[i] = (uint8_t)((val1 << 4) | val2);
  }
  return true;
}

struct section {
  char name[32];
  uint64_t version_string;
  uint64_t version_hash;
} __attribute__((packed));

struct canonical_data {
  uint64_t updated_at;
  struct section sections[1];
} __attribute__((packed));

struct canonical_sort_context {
  struct canonical_data *data;
  size_t *line_numbers;
};

static int canonical_sections_compare(size_t idx0, size_t idx1, void *userdata) {
  struct canonical_sort_context *const ctx = (struct canonical_sort_context *)userdata;
  size_t const line0 = ctx->line_numbers[idx0];
  size_t const line1 = ctx->line_numbers[idx1];
  if (line0 < line1) {
    return -1;
  }
  if (line0 > line1) {
    return 1;
  }
  return 0;
}

static void canonical_sections_swap(size_t idx0, size_t idx1, void *userdata) {
  struct canonical_sort_context *const ctx = (struct canonical_sort_context *)userdata;
  struct section const tmp_section = ctx->data->sections[idx0];
  ctx->data->sections[idx0] = ctx->data->sections[idx1];
  ctx->data->sections[idx1] = tmp_section;

  size_t const tmp_line = ctx->line_numbers[idx0];
  ctx->line_numbers[idx0] = ctx->line_numbers[idx1];
  ctx->line_numbers[idx1] = tmp_line;
}

/**
 * Build canonical data from INI reader.
 * Format: [updated_at:8][section_name:32,version:8,hash:8]*n
 * @param reader INI reader instance
 * @param canonical_data Output pointer for allocated canonical data
 * @param canonical_data_size Output pointer for canonical data size
 * @param err Error information on failure
 * @return true on success, false on failure
 */
static bool build_canonical_data(struct gcmz_ini_reader const *const reader,
                                 void **const canonical_data,
                                 size_t *const canonical_data_size,
                                 struct ov_error *const err) {
  if (!reader || !canonical_data || !canonical_data_size) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct canonical_data *data = NULL;
  size_t *line_numbers = NULL;
  size_t section_count = 0;
  bool result = false;

  {
    struct gcmz_ini_value const updated_at = gcmz_ini_reader_get_value(reader, NULL, "updated_at");
    if (!updated_at.ptr) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("no updated_at found in global section"));
      goto cleanup;
    }
    uint64_t updated_at_timestamp;
    if (!isotime_parse(updated_at.ptr, updated_at.size, &updated_at_timestamp, NULL)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("invalid updated_at timestamp format"));
      goto cleanup;
    }

    // Allocate canonical data structure with maximum possible size + line number buffer
    size_t const total_sections = gcmz_ini_reader_get_section_count(reader);
    size_t const data_size = sizeof(data->updated_at) + sizeof(data->sections[0]) * total_sections +
                             sizeof(line_numbers[0]) * total_sections;
    if (!OV_REALLOC(&data, data_size, 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    line_numbers = (size_t *)(void *)&data->sections[total_sections];

    // First pass: populate data with conversion and collect line numbers

    data->updated_at = to_little_endian64(updated_at_timestamp);
    struct gcmz_ini_iter iter = {0};
    while (gcmz_ini_reader_iter_sections(reader, &iter)) {
      if (iter.name_len == 0) {
        continue;
      }

      char section_name[32] = {0};
      size_t const name_len = iter.name_len >= sizeof(section_name) ? sizeof(section_name) - 1 : iter.name_len;
      memcpy(section_name, iter.name, name_len);

      struct gcmz_ini_value const version_string_val =
          gcmz_ini_reader_get_value(reader, section_name, "version_string");
      if (!version_string_val.ptr) {
        continue;
      }
      uint64_t version_string;
      if (!parse_hex_uint64(version_string_val.ptr, version_string_val.size, &version_string)) {
        continue;
      }

      struct gcmz_ini_value const version_hash_val =
          gcmz_ini_reader_get_value(reader, section_name, "version_string_hash");
      if (!version_hash_val.ptr) {
        continue;
      }
      uint64_t version_hash;
      if (!parse_hex_uint64(version_hash_val.ptr, version_hash_val.size, &version_hash)) {
        continue;
      }

      memcpy(data->sections[section_count].name, section_name, sizeof(section_name));
      data->sections[section_count].version_string = to_little_endian64(version_string);
      data->sections[section_count].version_hash = to_little_endian64(version_hash);
      line_numbers[section_count] = iter.line_number;
      ++section_count;
    }
    if (section_count == 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    ov_sort(section_count,
            canonical_sections_compare,
            canonical_sections_swap,
            &(struct canonical_sort_context){
                .data = data,
                .line_numbers = line_numbers,
            });
    *canonical_data_size = sizeof(data->updated_at) + sizeof(data->sections[0]) * section_count;
  }

  *canonical_data = data;
  data = NULL;
  result = true;

cleanup:
  if (data) {
    OV_FREE(&data);
  }
  return result;
}

bool gcmz_sign(struct gcmz_ini_reader const *const reader,
               uint8_t const secret_key[gcmz_sign_secret_key_size],
               uint8_t signature[gcmz_sign_signature_size],
               struct ov_error *const err) {
  if (!reader || !secret_key || !signature) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  void *canonical_data = NULL;
  size_t canonical_data_size = 0;
  bool result = false;

  if (!build_canonical_data(reader, &canonical_data, &canonical_data_size, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  if (!ovl_crypto_sign(signature, canonical_data, canonical_data_size, secret_key, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (canonical_data) {
    OV_FREE(&canonical_data);
  }
  return result;
}

bool gcmz_sign_verify(struct gcmz_ini_reader const *const reader,
                      uint8_t const public_key[gcmz_sign_public_key_size],
                      struct ov_error *const err) {
  if (!reader || !public_key) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  uint8_t signature[gcmz_sign_signature_size];
  void *canonical_data = NULL;
  size_t canonical_data_size = 0;
  bool result = false;

  {
    struct gcmz_ini_value const v = gcmz_ini_reader_get_value(reader, NULL, "signature");
    if (!v.ptr || v.size == 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("no signature found in INI file"));
      goto cleanup;
    }
    if (!parse_signature(v.ptr, v.size, signature, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }
  if (!build_canonical_data(reader, &canonical_data, &canonical_data_size, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  if (!ovl_crypto_sign_verify(signature, canonical_data, canonical_data_size, public_key, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  result = true;

cleanup:
  if (canonical_data) {
    OV_FREE(&canonical_data);
  }
  return result;
}
