#include "logf.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#include <ovarray.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/os.h>
#include <ovl/path.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <aviutl2_logger2.h>

enum log_level {
  log_level_verbose,
  log_level_info,
  log_level_warn,
  log_level_error,
};

static struct aviutl2_log_handle *g_logger = NULL;

struct utf8_to_wchar_context {
  wchar_t *buffer;
  size_t capacity;
  size_t length;
  char utf8_buffer[8];
  size_t utf8_pos;
  size_t expected_len;
};

/**
 * Get first byte length for UTF-8 sequence
 */
static size_t first_byte_to_len(unsigned char const c) {
  if ((c & 0x80) == 0) {
    return 1;
  }
  if ((c & 0xe0) == 0xc0) {
    return 2;
  }
  if ((c & 0xf0) == 0xe0) {
    return 3;
  }
  if ((c & 0xf8) == 0xf0) {
    return 4;
  }
  return 0;
}

/**
 * UTF-8 to wchar_t converter callback for ov_vpprintf_char
 */
static void utf8_to_wchar_putchar(int const c, void *const userdata) {
  struct utf8_to_wchar_context *const ctx = (struct utf8_to_wchar_context *)userdata;
  if (ctx->expected_len == 0) {
    ctx->expected_len = first_byte_to_len((unsigned char)c);
    if (ctx->expected_len == 0) {
      return;
    }
  } else if (ctx->utf8_pos > 0) {
    if ((c & 0xc0) != 0x80) {
      // Invalid continuation byte, reset and try this byte as start
      ctx->utf8_pos = 0;
      ctx->expected_len = first_byte_to_len((unsigned char)c);
      if (ctx->expected_len == 0) {
        return;
      }
    }
  }
  if (ctx->utf8_pos < sizeof(ctx->utf8_buffer) - 1) {
    ctx->utf8_buffer[ctx->utf8_pos++] = (char)c;
  }
  if (ctx->utf8_pos == ctx->expected_len) {
    ctx->utf8_buffer[ctx->utf8_pos] = '\0';
    enum {
      buffer_size = 4, // surrogate pair + L'\0', just enough
    };
    wchar_t wc[buffer_size];
    size_t read = 0;
    size_t result = ov_utf8_to_wchar(ctx->utf8_buffer, ctx->utf8_pos, wc, buffer_size, &read);
    if (result > 0 && read == ctx->utf8_pos) {
      for (size_t i = 0; i < result && ctx->length < ctx->capacity - 1; ++i) {
        ctx->buffer[ctx->length++] = wc[i];
      }
    }
    ctx->utf8_pos = 0;
    ctx->expected_len = 0;
  }
}

/**
 * Log via official API (priority 1)
 * @param level Log level
 * @param message Message in UTF-16
 * @return true if logged successfully, false otherwise
 */
static bool call_api(enum log_level const level, wchar_t const *const message) {
  switch (level) {
  case log_level_verbose:
    if (g_logger->verbose) {
      g_logger->verbose(g_logger, message);
      return true;
    }
    break;
  case log_level_info:
    if (g_logger->info) {
      g_logger->info(g_logger, message);
      return true;
    }
    break;
  case log_level_warn:
    if (g_logger->warn) {
      g_logger->warn(g_logger, message);
      return true;
    }
    break;
  case log_level_error:
    if (g_logger->error) {
      g_logger->error(g_logger, message);
      return true;
    }
    break;
  }
  return false;
}

/**
 * Internal logging function with fallback mechanism
 * @param level Log level
 * @param err Error information to include (can be NULL)
 * @param reference Reference format string (can be NULL)
 * @param format Printf-style format string (can be NULL if err is provided)
 * @param args Variable arguments
 */
static void log_core(enum log_level const level,
                     struct ov_error const *const err,
                     char const *const reference,
                     char const *const format,
                     va_list args) {
  if (!format && !err) {
    return;
  }

  char *err_str = NULL;
  wchar_t msg[1024];
  struct utf8_to_wchar_context ctx = {
      .buffer = msg,
      .capacity = sizeof(msg) / sizeof(msg[0]),
      .length = 0,
      .utf8_buffer = {0},
      .utf8_pos = 0,
      .expected_len = 0,
  };
  struct ov_error err2 = {0};
  bool has_error_string = false;

  if (err) {
    if (!ov_error_to_string(err, &err_str, true, &err2)) {
      assert(err2.stack[0].info.type != ov_error_type_invalid);
      OV_ERROR_REPORT(&err2, NULL);
    } else {
      has_error_string = true;
    }
  }

  if (format) {
    ov_vpprintf_char(utf8_to_wchar_putchar, &ctx, reference, format, args);
  }

  if (has_error_string) {
    if (format) {
      // Add separator between message and error details
      char const *const separator = "\n\n";
      for (char const *p = separator; *p; ++p) {
        utf8_to_wchar_putchar(*p, &ctx);
      }
    }
    // Append error string
    for (char const *p = err_str; *p; ++p) {
      utf8_to_wchar_putchar(*p, &ctx);
    }
  }

  ctx.buffer[ctx.length] = L'\0';
  if (g_logger && call_api(level, msg)) {
    goto cleanup;
  }
  OutputDebugStringW(msg);

cleanup:
  if (err_str) {
    OV_ARRAY_DESTROY(&err_str);
  }
}

void gcmz_logf_set_handle(struct aviutl2_log_handle *handle) { g_logger = handle; }

#define DEFINE_LOG_FUNCTION(level)                                                                                     \
  void gcmz_logf_##level(                                                                                              \
      struct ov_error const *const err, char const *const reference, char const *const format, ...) {                  \
    va_list args;                                                                                                      \
    va_start(args, format);                                                                                            \
    log_core(log_level_##level, err, reference, format, args);                                                         \
    va_end(args);                                                                                                      \
  }

DEFINE_LOG_FUNCTION(verbose)
DEFINE_LOG_FUNCTION(info)
DEFINE_LOG_FUNCTION(warn)
DEFINE_LOG_FUNCTION(error)
