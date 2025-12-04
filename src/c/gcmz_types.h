#pragma once

#include <ovbase.h>

#include <string.h>

enum {
  gcmz_error_type = 1000,
  gcmz_error_unknown_aviutl2_version = 1,
};

/**
 * @brief Additional modifier key flags
 *
 * These flags are used to track modifier keys not available in standard
 * Windows drag-and-drop key state (MK_* flags).
 */
enum gcmz_modifier_key_flags {
  gcmz_modifier_alt = 0x1, ///< Alt key is pressed
  gcmz_modifier_win = 0x2, ///< Windows key (either left or right) is pressed
};

/**
 * @brief File processing mode for file management
 */
enum gcmz_processing_mode {
  gcmz_processing_mode_auto = 0,   ///< Automatic determination
  gcmz_processing_mode_direct = 1, ///< Prefer direct read
  gcmz_processing_mode_copy = 2,   ///< Prefer copy
};

/**
 * @brief Convert integer to gcmz_processing_mode
 *
 * @param value Integer value to convert
 * @return Converted mode, or gcmz_processing_mode_auto if value is invalid
 */
static inline enum gcmz_processing_mode gcmz_processing_mode_from_int(int const value) {
  if (value < gcmz_processing_mode_auto || value > gcmz_processing_mode_copy) {
    return gcmz_processing_mode_auto;
  }
  return (enum gcmz_processing_mode)value;
}

/**
 * @brief Convert gcmz_processing_mode to integer
 *
 * @param mode Mode to convert
 * @return Integer representation of the mode, or gcmz_processing_mode_auto if mode is invalid
 */
static inline int gcmz_processing_mode_to_int(enum gcmz_processing_mode const mode) {
  int const value = (int)mode;
  if (value < gcmz_processing_mode_auto || value > gcmz_processing_mode_copy) {
    return (int)gcmz_processing_mode_auto;
  }
  return value;
}

/**
 * @brief Convert string to gcmz_processing_mode
 *
 * @param value String value to convert
 * @return Converted mode, or gcmz_processing_mode_auto if value is invalid or NULL
 */
static inline enum gcmz_processing_mode gcmz_processing_mode_from_string(char const *const value) {
  if (!value) {
    return gcmz_processing_mode_auto;
  }
  if (strcmp(value, "direct") == 0) {
    return gcmz_processing_mode_direct;
  }
  if (strcmp(value, "copy") == 0) {
    return gcmz_processing_mode_copy;
  }
  return gcmz_processing_mode_auto;
}

/**
 * @brief Convert gcmz_processing_mode to string
 *
 * @param mode Mode to convert
 * @return String representation of the mode, or default string if mode is invalid
 */
static inline char const *gcmz_processing_mode_to_string(enum gcmz_processing_mode const mode) {
  switch (mode) {
  case gcmz_processing_mode_auto:
    return "auto";
  case gcmz_processing_mode_direct:
    return "direct";
  case gcmz_processing_mode_copy:
    return "copy";
  }
  return "auto";
}

/**
 * @brief Project data structure representing current AviUtl ExEdit2 project state
 */
struct gcmz_project_data {
  int width;             ///< Video width in pixels
  int height;            ///< Video height in pixels
  int video_rate;        ///< Video frame rate numerator (fps = video_rate / video_scale)
  int video_scale;       ///< Video frame rate denominator
  int sample_rate;       ///< Audio sample rate in Hz
  int audio_ch;          ///< Number of audio channels
  int cursor_frame;      ///< Current cursor frame position
  int selected_layer;    ///< Currently selected layer
  int display_frame;     ///< Currently displayed frame position
  int display_layer;     ///< Currently displayed layer
  int display_zoom;      ///< Current display zoom level
  uint32_t flags;        ///< Flags
  wchar_t *project_path; ///< Project file path (do not free)
};

/**
 * @brief Color definition
 */
struct gcmz_color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

/**
 * @brief Window information structure
 */
struct gcmz_window_info {
  void *window;
  int width;
  int height;
};
