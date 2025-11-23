#pragma once

// This file based on input2.h from AviUtl ExEdit2 Plugin SDK
// LICENSE: The MIT License / Copyright (c) 2025 Kenkun

// Input plugins must export the following functions for the host to call:
//
// Required entry point returning the input plugin definition
//   struct aviutl2_input_plugin_table *GetInputPluginTable(void)
//
// Optional plugin DLL initialization function (version is the host application version)
//   bool InitializePlugin(uint32_t version)
//
// Optional plugin DLL uninitialization function
//   void UninitializePlugin(void)
//
// Optional logger initialization function (see aviutl2_logger2.h). Called before InitializePlugin()
//   void InitializeLogger(struct aviutl2_log_handle *logger)

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

/**
 * Input file information
 * Supports image formats: RGB24bit, RGBA32bit, PA64, HF64, YUY2, YC48
 * Supports audio formats: PCM16bit, PCM(float)32bit
 * PA64 is DXGI_FORMAT_R16G16B16A16_UNORM (premultiplied alpha)
 * HF64 is DXGI_FORMAT_R16G16B16A16_FLOAT (premultiplied alpha, internal format)
 * YC48 is a compatibility support format
 */
struct aviutl2_input_info {
  /**
   * Flags
   */
  enum {
    /**
     * Input video data
     */
    aviutl2_input_info_flag_video = 1,

    /**
     * Input audio data
     */
    aviutl2_input_info_flag_audio = 2,

    /**
     * Convert frame number from time (func_time_to_frame() will be called)
     */
    aviutl2_input_info_flag_time_to_frame = 16,
  };

  /**
   * Flags indicating which data types are available
   */
  int flag;

  /**
   * Frame rate, scale
   */
  int rate, scale;

  /**
   * Frame length
   */
  int n;

  /**
   * Pointer to image format (must remain valid until the next callback)
   */
  BITMAPINFOHEADER *format;

  /**
   * Image format size
   */
  int format_size;

  /**
   * Audio sampling count
   */
  int audio_n;

  /**
   * Pointer to audio format (must remain valid until the next callback)
   */
  WAVEFORMATEX *audio_format;

  /**
   * Audio format size
   */
  int audio_format_size;
};

/**
 * Input file handle
 */
typedef void *aviutl2_input_handle;

/**
 * Input plugin table
 */
struct aviutl2_input_plugin_table {
  /**
   * Flags
   */
  enum {
    /**
     * Support video input
     */
    aviutl2_input_plugin_table_flag_video = 1,

    /**
     * Support audio input
     */
    aviutl2_input_plugin_table_flag_audio = 2,

    /**
     * Support concurrent data retrieval
     * Image and audio retrieval functions are called concurrently from different handles
     * Each function is called concurrently from different handles
     */
    aviutl2_input_plugin_table_flag_concurrent = 16,

    /**
     * Support multi-track (func_set_track() will be called)
     */
    aviutl2_input_plugin_table_flag_multi_track = 32,
  };

  /**
   * Flags indicating which input types are supported
   */
  int flag;

  /**
   * Plugin name
   */
  wchar_t const *name;

  /**
   * Input file filter
   */
  wchar_t const *filefilter;

  /**
   * Plugin information
   */
  wchar_t const *information;

  /**
   * Open input file function
   * @param file File name
   * @return Input file handle (NULL on failure)
   */
  aviutl2_input_handle (*func_open)(wchar_t const *file);

  /**
   * Close input file function
   * @param ih Input file handle
   * @return true if close succeeded, false otherwise
   */
  bool (*func_close)(aviutl2_input_handle ih);

  /**
   * Get input file information function
   * @param ih Input file handle
   * @param iip Pointer to input file information structure
   * @return true if succeeded, false otherwise
   */
  bool (*func_info_get)(aviutl2_input_handle ih, struct aviutl2_input_info *iip);

  /**
   * Read video data function
   * @param ih Input file handle
   * @param frame Frame number to read
   * @param buf Pointer to buffer where data will be stored
   * @return Size of read data
   */
  int (*func_read_video)(aviutl2_input_handle ih, int frame, void *buf);

  /**
   * Read audio data function
   * @param ih Input file handle
   * @param start Start sample number to read
   * @param length Number of samples to read
   * @param buf Pointer to buffer where data will be stored
   * @return Number of samples actually read
   */
  int (*func_read_audio)(aviutl2_input_handle ih, int start, int length, void *buf);

  /**
   * Config function pointer called when input config dialog is requested (NULL if not supported)
   * @param hwnd Window handle
   * @param dll_hinst Instance handle
   * @return true if config succeeded, false otherwise
   */
  bool (*func_config)(HWND hwnd, HINSTANCE dll_hinst);

  /**
   * Track type for func_set_track
   */
  enum aviutl2_input_track_type {
    /**
     * Video track
     */
    aviutl2_input_track_type_video = 0,

    /**
     * Audio track
     */
    aviutl2_input_track_type_audio = 1,
  };

  /**
   * Set track function for reading target track of input file (called only if FLAG_MULTI_TRACK is set)
   * Called immediately after func_open(); track count lookup and track selection happen at that timing.
   * The function is not invoked again after the initial open-time configuration.
   * @param ih Input file handle
   * @param type Media type (aviutl2_input_track_type_video = 0, aviutl2_input_track_type_audio = 1)
   * @param index Track number (-1 if specified to get number of available tracks)
   * @return Set track number (return -1 on failure). When getting number of available tracks, return number of tracks
   * that can be set (return 0 if no media)
   */
  int (*func_set_track)(aviutl2_input_handle ih, int type, int index);

  /**
   * Convert media time to frame number function (called only if FLAG_TIME_TO_FRAME is set)
   * Called before reading image data. Read the corresponding frame number from the result frame number.
   * When using FLAG_TIME_TO_FRAME, set rate and scale in INPUT_INFO to appropriate frame rate values.
   * @param ih Input file handle
   * @param time Media time (seconds)
   * @return Frame number corresponding to media time
   */
  int (*func_time_to_frame)(aviutl2_input_handle ih, double time);
};
