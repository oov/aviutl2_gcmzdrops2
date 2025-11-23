#include <ovarray.h>
#include <ovbase.h>
#include <ovl/crypto.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini_reader.h"
#include "ini_sign.h"

static void putc_stdout(int c, void *ctx) {
  (void)ctx;
  fputc(c, stdout);
}

static int print_stdout(char const *const format, ...) {
  va_list args;
  va_start(args, format);
  int ret = ov_vpprintf_char(putc_stdout, NULL, NULL, format, args);
  va_end(args);
  return ret;
}

static void putc_stderr(int c, void *ctx) {
  (void)ctx;
  fputc(c, stderr);
}

static int print_stderr(char const *const format, ...) {
  va_list args;
  va_start(args, format);
  int ret = ov_vpprintf_char(putc_stderr, NULL, NULL, format, args);
  va_end(args);
  return ret;
}

static void print_usage(char const *const program_name) {
  print_stderr("Usage:\n");
  print_stderr("  %s keygen                         - Generate new keypair\n", program_name);
  print_stderr("  %s sign <ini_file>                - Sign using key from environment\n", program_name);
  print_stderr("\n");
  print_stderr("Environment Variables:\n");
  print_stderr("  GCMZ_SECRET_KEY                   - Secret key (64-character hex string)\n");
  print_stderr("\n");
  print_stderr("Examples:\n");
  print_stderr("  # Generate new keys\n");
  print_stderr("  %s keygen > .env\n", program_name);
  print_stderr("\n");
  print_stderr("  # Use keys from .env file\n");
  print_stderr("  source .env\n");
  print_stderr("  %s sign aviutl2_addr.ini\n", program_name);
  print_stderr("\n");
  print_stderr("  # Or export directly\n");
  print_stderr("  export GCMZ_SECRET_KEY=d86039de6302f08d03242191d2a2caa6c834fe56c63b18c2cd2b63f9d4386e7a\n");
  print_stderr("  %s sign aviutl2_addr.ini\n", program_name);
  print_stderr("\n");
  print_stderr("Note: Secret key is only accepted via environment variable to prevent\n");
  print_stderr("      exposure in command history and process lists.\n");
}

static bool
parse_hex_key(char const *const hex_str, uint8_t key[gcmz_sign_secret_key_size], struct ov_error *const err) {
  if (!hex_str || !key) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  size_t const hex_len = strlen(hex_str);
  if (hex_len != gcmz_sign_secret_key_size * 2) {
    OV_ERROR_SETF(err,
                  ov_error_type_generic,
                  ov_error_generic_invalid_argument,
                  "%1$d%2$zu",
                  gettext("invalid key length: expected %1$d hex characters, got %2$zu"),
                  gcmz_sign_secret_key_size * 2,
                  hex_len);
    return false;
  }

  for (size_t i = 0; i < gcmz_sign_secret_key_size; ++i) {
    char hex_byte[3] = {hex_str[i * 2], hex_str[i * 2 + 1], '\0'};
    char *endptr;
    unsigned long byte_val = strtoul(hex_byte, &endptr, 16);

    if (*endptr != '\0' || byte_val > 255) {
      OV_ERROR_SETF(err,
                    ov_error_type_generic,
                    ov_error_generic_invalid_argument,
                    "%1$zu",
                    gettext("invalid hex character at position %1$zu"),
                    i * 2);
      return false;
    }

    key[i] = (uint8_t)byte_val;
  }

  return true;
}

static bool get_secret_key_from_env(uint8_t key[gcmz_sign_secret_key_size], struct ov_error *const err) {
  char const *const env_key = getenv("GCMZ_SECRET_KEY");
  if (!env_key) {
    OV_ERROR_SET(
        err, ov_error_type_generic, ov_error_generic_not_found, "GCMZ_SECRET_KEY environment variable not set");
    return false;
  }

  return parse_hex_key(env_key, key, err);
}

static bool generate_keypair(struct ov_error *const err) {
  uint8_t public_key[gcmz_sign_public_key_size];
  uint8_t secret_key[gcmz_sign_secret_key_size];

  if (!ovl_crypto_sign_generate_keypair(public_key, secret_key, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }

  print_stdout("GCMZ_SECRET_KEY=");
  for (size_t i = 0; i < gcmz_sign_secret_key_size; ++i) {
    print_stdout("%02x", secret_key[i]);
  }
  print_stdout("\n");

  print_stdout("GCMZ_PUBLIC_KEY=");
  for (size_t i = 0; i < gcmz_sign_public_key_size; ++i) {
    print_stdout("%02x", public_key[i]);
  }
  print_stdout("\n");

  return true;
}

static bool sign_ini_file(char const *const ini_file,
                          uint8_t const secret_key[gcmz_sign_secret_key_size],
                          struct ov_error *const err) {
  struct gcmz_ini_reader *reader = NULL;
  wchar_t *wide_filename = NULL;
  uint8_t signature[gcmz_sign_signature_size];
  bool result = false;

  {
    if (!gcmz_ini_reader_create(&reader, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    size_t const required_len = ov_utf8_to_wchar_len(ini_file, strlen(ini_file));
    if (required_len == 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&wide_filename, required_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    size_t read = 0;
    if (!ov_utf8_to_wchar(ini_file, strlen(ini_file), wide_filename, required_len + 1, &read)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (!gcmz_ini_reader_load_file(reader, wide_filename, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!gcmz_sign(reader, secret_key, signature, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    for (size_t i = 0; i < gcmz_sign_signature_size; ++i) {
      print_stdout("%02x", signature[i]);
    }
    print_stdout("\n");
  }

  result = true;

cleanup:
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
  if (wide_filename) {
    OV_ARRAY_DESTROY(&wide_filename);
  }
  return result;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  ov_init();

  struct ov_error err = {0};
  int exit_code = 1;

  // Handle keygen command
  if (argc == 2 && strcmp(argv[1], "keygen") == 0) {
    if (!generate_keypair(&err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    exit_code = 0;
    goto cleanup;
  }

  // Handle sign command
  if (argc == 3 && strcmp(argv[1], "sign") == 0) {
    char const *const ini_file = argv[2];
    uint8_t secret_key[gcmz_sign_secret_key_size];

    if (!get_secret_key_from_env(secret_key, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!sign_ini_file(ini_file, secret_key, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    exit_code = 0;
    goto cleanup;
  }

  // Invalid usage
  print_usage(argv[0]);

cleanup:
  if (exit_code != 0) {
    OV_ERROR_REPORT(&err, NULL);
  }
  ov_exit();
  return exit_code;
}
