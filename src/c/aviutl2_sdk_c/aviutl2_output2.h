#pragma once

// This file based on output2.h from AviUtl ExEdit2 Plugin SDK
// LICENSE: The MIT License / Copyright (c) 2025 Kenkun

// Output plugins must export the following functions for the host to call:
//
// Required entry point returning the output plugin definition
//   struct aviutl2_output_plugin_table *GetOutputPluginTable(void)
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
 * Output information
 */
struct aviutl2_output_info {
  /**
   * Flags
   */
  enum {
    /**
     * Output video data
     */
    aviutl2_output_info_flag_video = 1,

    /**
     * Output audio data
     */
    aviutl2_output_info_flag_audio = 2,
  };

  /**
   * Flags indicating which data types to output
   */
  int flag;

  /**
   * Image width, height
   */
  int w, h;

  /**
   * Frame rate, scale
   */
  int rate, scale;

  /**
   * Frame length
   */
  int n;

  /**
   * Audio sampling rate
   */
  int audio_rate;

  /**
   * Number of audio channels
   */
  int audio_ch;

  /**
   * Audio sampling count
   */
  int audio_n;

  /**
   * Pointer to save file name
   */
  wchar_t const *savefile;

  /**
   * Get image data in DIB format
   * @param frame Frame number
   * @param format Image format (0=BI_RGB, 'P''A''6''4'=PA64, 'H''F''6''4'=HF64, 'Y''U''Y''2'=YUY2, 'Y''C''4''8'=YC48)
   * @note PA64 is DXGI_FORMAT_R16G16B16A16_UNORM (premultiplied alpha)
   * @note HF64 is DXGI_FORMAT_R16G16B16A16_FLOAT (premultiplied alpha, internal format)
   * @note YC48 is a compatibility support format
   * @return Pointer to data. The data pointer content is valid while an external function using this function is called
   */
  void *(*func_get_video)(int frame, uint32_t format);

  /**
   * Get audio data in PCM format
   * @param start Start sample number
   * @param length Number of samples to read
   * @param readed Pointer to store number of samples actually read
   * @param format Audio format (1=WAVE_FORMAT_PCM/PCM16bit, 3=WAVE_FORMAT_IEEE_FLOAT/PCM(float)32bit)
   * @return Pointer to data. The data pointer content is valid while an external function using this function is called
   */
  void *(*func_get_audio)(int start, int length, int *readed, uint32_t format);

  /**
   * Check if abort has been requested
   * @return true if abort was requested
   */
  bool (*func_is_abort)(void);

  /**
   * Display remaining time
   * @param now Current processing frame number
   * @param total Total number of frames to process
   */
  void (*func_rest_time_disp)(int now, int total);

  /**
   * Set number of buffers used to prefetch data (measured in frames)
   * Standard buffer count is 4; half of the configured buffers are preloaded in advance
   * @param video_size Image data buffer count
   * @param audio_size Audio data buffer count
   */
  void (*func_set_buffer_size)(int video_size, int audio_size);
};

/**
 * Output plugin table
 */
struct aviutl2_output_plugin_table {
  /**
   * Flags (reserved for future use)
   */
  enum {
    /**
     * Support video output
     */
    aviutl2_output_plugin_table_flag_video = 1,

    /**
     * Support audio output
     */
    aviutl2_output_plugin_table_flag_audio = 2,
  };

  /**
   * Flags indicating which output types are supported
   */
  int flag;

  /**
   * Plugin name
   */
  wchar_t const *name;

  /**
   * File filter for save dialog
   */
  wchar_t const *filefilter;

  /**
   * Plugin information
   */
  wchar_t const *information;

  /**
   * Output function pointer called at output time
   * @param oip Pointer to output information
   * @return true if output succeeded, false otherwise
   */
  bool (*func_output)(struct aviutl2_output_info *oip);

  /**
   * Config function pointer called when output config dialog is requested (NULL if not supported)
   * @param hwnd Window handle
   * @param dll_hinst Instance handle
   * @return true if config succeeded, false otherwise
   */
  bool (*func_config)(HWND hwnd, HINSTANCE dll_hinst);

  /**
   * Function pointer called when output config text is requested (NULL if not supported)
   * @return Output config text (the function caller manages the returned text lifetime)
   */
  wchar_t const *(*func_get_config_text)(void);
};
