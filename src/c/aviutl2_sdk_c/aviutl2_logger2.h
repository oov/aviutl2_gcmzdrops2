#pragma once

// This file based on logger2.h from AviUtl ExEdit2 Plugin SDK
// LICENSE: The MIT License / Copyright (c) 2025 Kenkun

// Any plugin can export the following function to enable logging support:
//
// Logger initialization function (called before InitializePlugin())
//   void InitializeLogger(struct aviutl2_log_handle *logger)

#include <wchar.h>

/**
 * Logger handle interface for AviUtl2 plugins
 * Functions mirror the SDK logger callbacks and accept UTF-16 strings
 */
struct aviutl2_log_handle {
  /**
   * Output plugin log message at default level
   * @param logger Logger handle provided by the host
   * @param message Log message (UTF-16)
   */
  void (*log)(struct aviutl2_log_handle *logger, wchar_t const *message);

  /**
   * Output info level log message
   * @param logger Logger handle provided by the host
   * @param message Log message (UTF-16)
   */
  void (*info)(struct aviutl2_log_handle *logger, wchar_t const *message);

  /**
   * Output warning level log message
   * @param logger Logger handle provided by the host
   * @param message Log message (UTF-16)
   */
  void (*warn)(struct aviutl2_log_handle *logger, wchar_t const *message);

  /**
   * Output error level log message
   * @param logger Logger handle provided by the host
   * @param message Log message (UTF-16)
   */
  void (*error)(struct aviutl2_log_handle *logger, wchar_t const *message);

  /**
   * Output verbose level log message
   * @param logger Logger handle provided by the host
   * @param message Log message (UTF-16)
   */
  void (*verbose)(struct aviutl2_log_handle *logger, wchar_t const *message);
};
