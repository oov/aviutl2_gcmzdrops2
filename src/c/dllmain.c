#define TARGET_LUA_MODULE 0
#define TARGET_AVIUTL2_FILTER_PLUGIN 0
#define TARGET_AVIUTL2_SCRIPT_MODULE 0
#define TARGET_AVIUTL2_PLUGIN 1

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#include <ole2.h>
#include <shlobj.h>

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wreserved-macro-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#  endif
#endif // __GNUC__
#include <lauxlib.h>
#include <lualib.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#include <ovarray.h>
#include <ovbase.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovthreads.h>
#include <ovutf.h>

#include <ovl/dialog.h>
#include <ovl/file.h>
#include <ovl/os.h>
#include <ovl/path.h>

#if TARGET_AVIUTL2_FILTER_PLUGIN
#  include <aviutl2_filter2.h>
#endif
#if TARGET_AVIUTL2_SCRIPT_MODULE
#  include <aviutl2_module2.h>
#endif
#if TARGET_AVIUTL2_PLUGIN
#  include <aviutl2_plugin2.h>
#endif

#include <aviutl2_logger2.h>

#include "analyze.h"
#include "api.h"
#include "aviutl2.h"
#include "config.h"
#include "config_dialog.h"
#include "copy.h"
#include "dataobj.h"
#include "delayed_cleanup.h"
#include "do.h"
#include "drop.h"
#include "error.h"
#include "file.h"
#include "gcmz_types.h"
#include "logf.h"
#include "lua.h"
#include "lua_api.h"
#include "luautil.h"
#include "project_info.h"
#include "style_config.h"
#include "temp.h"
#include "tray.h"
#include "version.h"
#include "window_list.h"

#ifndef GCMZ_SCRIPT_SUBDIR
#  define GCMZ_SCRIPT_SUBDIR "GCMZScript"
#endif

static struct mo *g_mo = NULL;
static struct gcmz_config *g_config = NULL;
static struct gcmz_api *g_api = NULL;
static struct gcmz_drop *g_drop = NULL;
static struct gcmz_lua_context *g_lua_ctx = NULL;
static struct gcmz_tray *g_tray = NULL;
static struct gcmz_analyze *g_capture = NULL;
static struct gcmz_window_list *g_window_list = NULL;

static struct aviutl2_log_handle *g_logger = NULL;
static struct aviutl2_edit_handle *g_edit = NULL;
static bool g_unknown_binary = false;
static uint32_t g_aviutl2_version = 0;

// Synchronization primitives for delayed initialization thread
// g_plugin_registered states:
//   ov_indeterminate: sync primitives not initialized
//   ov_false: initialized, waiting for RegisterPlugin
//   ov_true: RegisterPlugin completed
static mtx_t g_init_mtx;
static cnd_t g_init_cond;
static ov_tribool g_plugin_registered = ov_indeterminate;

static bool analyze_get_style_callback(struct gcmz_analyze_style *style, void *userdata, struct ov_error *const err) {
  (void)userdata;

  if (!style) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  struct gcmz_style_config *cfg = gcmz_style_config_create(NULL, err);
  if (!cfg) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  style->active_normal = gcmz_style_config_get_color_zoom_gauge(cfg);
  style->active_hover = gcmz_style_config_get_color_zoom_gauge_hover(cfg);
  style->inactive_normal = gcmz_style_config_get_color_zoom_gauge_off(cfg);
  style->inactive_hover = gcmz_style_config_get_color_zoom_gauge_off_hover(cfg);
  style->background = gcmz_style_config_get_color_background(cfg);
  style->frame_cursor = gcmz_style_config_get_color_frame_cursor(cfg);
  style->frame_cursor_wide = gcmz_style_config_get_color_frame_cursor_wide(cfg);

  style->time_gauge_height = gcmz_style_config_get_layout_time_gauge_height(cfg);
  style->layer_header_width = gcmz_style_config_get_layout_layer_header_width(cfg);
  style->scroll_bar_size = gcmz_style_config_get_layout_scroll_bar_size(cfg);
  style->layer_height = gcmz_style_config_get_layout_layer_height(cfg);
  style->zoom_bar_margin = 2;
  style->zoom_bar_block_width = 2;
  style->zoom_bar_block_gap = 1;

  result = true;

cleanup:
  if (cfg) {
    gcmz_style_config_destroy(&cfg);
  }
  return result;
}

struct cursor_position_params {
  int x;
  int y;
  void *window;
  struct gcmz_project_data original_data;
};

// Helper function to determine cursor position for drop operation
static bool determine_cursor_position(int const target_layer,
                                      struct cursor_position_params *const params,
                                      struct ov_error *const err) {
  if (!params) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  struct gcmz_project_data original_data = {0};
  struct gcmz_analyze_result capture_result = {0};
  struct gcmz_project_data data = {0};
  int drop_layer_offset = 0;

  {
    if (!gcmz_project_info_get(&original_data, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (original_data.display_zoom < 10000) {
      gcmz_logf_verbose(NULL,
                        "%1$d",
                        "Current display zoom is %1$d < 10000. Setting display zoom to 10000 for drop analysis.",
                        original_data.display_zoom);
      gcmz_aviutl2_set_display_zoom(10000);
      if (!gcmz_project_info_get(&original_data, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      if (original_data.display_zoom != 10000) {
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("failed to set display zoom"));
        goto cleanup;
      }
    }

    if (!gcmz_analyze_run(g_capture, original_data.display_zoom, &capture_result, NULL, NULL, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    int layer;
    if (target_layer < 0) {
      if (target_layer == INT_MIN) {
        layer = original_data.selected_layer;
      } else {
        layer = -target_layer + original_data.display_layer;
      }
    } else {
      layer = target_layer - 1;
    }

    int const n_layer = capture_result.effective_area.height / capture_result.layer_height;
    if (layer < original_data.display_layer || original_data.display_layer + n_layer <= layer) {
      // Scroll to bring the target layer on-screen with minimum scroll distance
      int to;
      if (layer < original_data.display_layer) {
        to = layer;
      } else {
        to = layer < n_layer - 1 ? 0 : layer - (n_layer - 1);
      }
      gcmz_aviutl2_set_display_layer(to);
      if (!gcmz_project_info_get(&data, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      if (data.display_layer != to) {
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to scroll");
        goto cleanup;
      }
    }
    drop_layer_offset = layer - data.display_layer;

    if (capture_result.cursor.width == 0 || capture_result.cursor.height == 0) {
      // Move the cursor to bring it on-screen as it appears to be off-screen
      int const pos = original_data.cursor_frame;
      gcmz_aviutl2_set_cursor_frame(pos ? pos - 1 : pos + 1);
      gcmz_aviutl2_set_cursor_frame(pos);
    }

    if (!gcmz_analyze_run(g_capture, original_data.display_zoom, &capture_result, NULL, NULL, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (capture_result.cursor.width == 0 || capture_result.cursor.height == 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "cursor is not visible");
      goto cleanup;
    }

    params->x = capture_result.cursor.x + (capture_result.cursor.width / 2);
    params->y = capture_result.cursor.y + 4 + (drop_layer_offset * capture_result.layer_height);
    params->window = capture_result.window;
    params->original_data = original_data;
  }

  result = true;
cleanup:
  return result;
}

static void api_request_callback(struct gcmz_api_request_params *const params,
                                 gcmz_api_request_complete_func const complete) {
  if (!params || !complete) {
    return;
  }

  struct ov_error err = {0};
  struct gcmz_project_data data = {0};
  bool success = false;

  {
    if (!gcmz_file_list_count(params->files)) {
      success = true;
      goto cleanup;
    }
    struct cursor_position_params pos_params = {0};
    if (!determine_cursor_position(params->layer, &pos_params, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_drop_simulate_drop(
            g_drop, pos_params.window, params->files, pos_params.x, pos_params.y, params->use_exo_converter, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (params->frame_advance != 0) {
      gcmz_aviutl2_set_cursor_frame(pos_params.original_data.cursor_frame + params->frame_advance);
      if (!gcmz_project_info_get(&data, &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
      if (pos_params.original_data.cursor_frame + params->frame_advance != data.cursor_frame) {
        OV_ERROR_SETF(&err,
                      ov_error_type_generic,
                      ov_error_generic_fail,
                      "%1$s",
                      "%1$s",
                      gettext("failed to move cursor to target position"));
        goto cleanup;
      }
    }
  }

  success = true;

cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to drop from external API request"));
    OV_ERROR_REPORT(&err, NULL);
  }
  if (complete) {
    complete(params);
  }
}

static void update_api_project_data_edit_section(struct aviutl2_edit_section *edit) {
  if (!g_api || !edit) {
    return;
  }
  struct ov_error err = {0};
  bool success = false;
  if (!gcmz_api_set_project_data(g_api,
                                 &(struct gcmz_project_data){
                                     .width = edit->info->width,
                                     .height = edit->info->height,
                                     .video_rate = edit->info->rate,
                                     .video_scale = edit->info->scale,
                                     .sample_rate = edit->info->sample_rate,
                                     .audio_ch = 2,
                                     .project_path = (wchar_t *)ov_deconster_(gcmz_aviutl2_get_project_path()),
                                 },
                                 &err)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  success = true;
cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to update external API project data"));
    OV_ERROR_DESTROY(&err);
  }
}

static void update_api_project_data(void *userdata) {
  (void)userdata;
  if (!g_api) {
    return;
  }
  // FIXME:
  // When trying to obtain the edit_handle using the official API to get project information,
  // such as when loading a project file, the application crashes.
  gcmz_aviutl2_create_simulated_edit_handle()->call_edit_section(update_api_project_data_edit_section);
}

#if !TARGET_AVIUTL2_PLUGIN
static void api_update_callback(struct gcmz_api *const api, void *const userdata) {
  (void)api;
  (void)userdata;
  if (!g_api) {
    return;
  }
  gcmz_do(update_api_project_data, NULL);
}
#endif

static bool create_external_api_once(struct ov_error *const err) {
  bool result = false;

  g_api = gcmz_api_create(
      &(struct gcmz_api_options){
          .request_callback = api_request_callback,
#if TARGET_AVIUTL2_PLUGIN
          // No periodic update needed: project_load_handler updates via register_project_load_handler
          .update_callback = NULL,
#else
          // Periodic update required: register_project_load_handler is unavailable in non-plugin builds
          .update_callback = api_update_callback,
#endif
          .userdata = NULL,
          .aviutl2_ver = g_aviutl2_version,
          .gcmz_ver = GCMZ_VERSION_UINT32,
      },
      err);
  if (!g_api) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  gcmz_logf_verbose(NULL, "%1$hs", "%1$hs", pgettext("external_api", "external API initialized successfully"));
  gcmz_do(update_api_project_data, NULL);
  result = true;

cleanup:
  return result;
}

static bool create_external_api(bool const use_retry, struct ov_error *const err) {
  if (g_api) {
    OV_ERROR_SET(
        err, ov_error_type_generic, ov_error_generic_fail, pgettext("external_api", "external API already exists"));
    return false;
  }

  if (g_unknown_binary) {
    gcmz_logf_warn(NULL,
                   "%s",
                   "%s",
                   pgettext("external_api", "external API is disabled because the AviUtl ExEdit2 version is unknown"));
    return true;
  }

  if (!use_retry) {
    return create_external_api_once(err);
  }

  wchar_t title[256];
  wchar_t main_instruction[256];
  wchar_t content[1024];

  while (true) {
    if (create_external_api_once(err)) {
      return true;
    }

    bool const is_already_exists = ov_error_is(err, ov_error_type_hresult, HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    if (is_already_exists) {
      ov_snprintf_wchar(title,
                        sizeof(title) / sizeof(title[0]),
                        L"%1$s%2$s",
                        L"%1$s - %2$s",
                        pgettext("external_api", "Error"),
                        gettext("GCMZDrops"));
      ov_snprintf_wchar(main_instruction,
                        sizeof(main_instruction) / sizeof(main_instruction[0]),
                        L"%1$s\n%2$s",
                        L"%1$s\n%2$s",
                        pgettext("external_api", "Failed to initialize the external API."),
                        pgettext("external_api", "Retry?"));
      ov_snprintf_wchar(content,
                        sizeof(content) / sizeof(content[0]),
                        L"%s",
                        L"%s",
                        pgettext("external_api",
                                 "This may occur when multiple instances of AviUtl ExEdit2 are running.\n"
                                 "Please close other instances and click Retry.\n\n"
                                 "If you cancel, the plugin will continue without the external API."));

      int const button_id = gcmz_error_dialog(
          NULL, err, title, main_instruction, content, TD_WARNING_ICON, TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON);
      OV_ERROR_DESTROY(err);

      if (button_id != IDRETRY) {
        return true;
      }
      continue;
    }

    ov_snprintf_wchar(
        title, sizeof(title) / sizeof(title[0]), L"%1$s%2$s", L"%1$s - %2$s", gettext("Error"), gettext("GCMZDrops"));
    ov_snprintf_wchar(main_instruction,
                      sizeof(main_instruction) / sizeof(main_instruction[0]),
                      L"%s",
                      L"%s",
                      pgettext("external_api", "Failed to initialize external API."));
    ov_snprintf_wchar(content,
                      sizeof(content) / sizeof(content[0]),
                      L"%s",
                      L"%s",
                      pgettext("external_api", "The external API has been temporarily disabled due to an error."));
    gcmz_error_dialog(NULL, err, title, main_instruction, content, TD_ERROR_ICON, TDCBF_OK_BUTTON);
    return true;
  }
}

static bool analyze_complete_callback(struct gcmz_analyze_save_context *const ctx,
                                      gcmz_analyze_save_to_file_func save_to_file,
                                      enum gcmz_analyze_status const status,
                                      void *userdata,
                                      struct ov_error *const err) {
  (void)userdata;
  (void)status;

  NATIVE_CHAR *selected_path = NULL;
  NATIVE_CHAR *default_full_path = NULL;
  PWSTR desktop_path = NULL;
  bool result = false;

  {
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Desktop, 0, NULL, &desktop_path);
    if (FAILED(hr) || !desktop_path) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }
    enum { filename_max_len = 32 };
    size_t const desktop_len = wcslen(desktop_path);
    if (!OV_ARRAY_GROW(&default_full_path, desktop_len + filename_max_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    SYSTEMTIME st = {0};
    GetSystemTime(&st);
    int const written = ov_snprintf_wchar(default_full_path,
                                          desktop_len + filename_max_len,
                                          NULL,
                                          L"%ls\\timeline_%04u%02u%02u_%02u%02u%02u.png",
                                          desktop_path,
                                          st.wYear,
                                          st.wMonth,
                                          st.wDay,
                                          st.wHour,
                                          st.wMinute,
                                          st.wSecond);
    if (written <= 0 || written >= (int)(desktop_len + filename_max_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    static GUID const client_guid = {0x1c0b30f8, 0x99c2, 0x4f7f, {0xb5, 0x98, 0xac, 0x59, 0xe2, 0xea, 0x18, 0x48}};

    wchar_t title_buf[128];
    ov_snprintf_wchar(
        title_buf, sizeof(title_buf) / sizeof(title_buf[0]), NULL, L"%s", gettext("Choose Screenshot Save Location"));
    wchar_t filter_buf[128];
    ov_snprintf_wchar(filter_buf,
                      sizeof(filter_buf) / sizeof(filter_buf[0]),
                      NULL,
                      L"%s(*.png)\n*.png\n%s\n*.*",
                      gettext("PNG Image"),
                      gettext("All Files"));
    for (wchar_t *p = filter_buf; *p; ++p) {
      if (*p == L'\n') {
        *p = L'\0';
      }
    }

    if (!ovl_dialog_save_file(gcmz_aviutl2_get_main_window(),
                              title_buf,
                              filter_buf,
                              &client_guid,
                              default_full_path,
                              &selected_path,
                              err)) {
      if (ov_error_is(err, ov_error_type_hresult, HRESULT_FROM_WIN32(ERROR_CANCELLED))) {
        OV_ERROR_DESTROY(err);
        result = true;
        goto cleanup;
      }
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!save_to_file(ctx, selected_path, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    gcmz_logf_info(NULL, "%1$ls", gettext("saved timeline screenshot to \"%ls\""), selected_path);
  }

  result = true;

cleanup:
  if (selected_path) {
    OV_ARRAY_DESTROY(&selected_path);
  }
  if (default_full_path) {
    OV_ARRAY_DESTROY(&default_full_path);
  }
  if (desktop_path) {
    CoTaskMemFree(desktop_path);
    desktop_path = NULL;
  }
  return result;
}

static void log_analyze(struct gcmz_analyze_result const *const result) {
  gcmz_logf_verbose(NULL,
                    NULL,
                    "Zoom bar: (%d, %d) %dx%d",
                    result->zoom_bar.x,
                    result->zoom_bar.y,
                    result->zoom_bar.width,
                    result->zoom_bar.height);
  gcmz_logf_verbose(NULL,
                    NULL,
                    "Layer window: (%d, %d) %dx%d",
                    result->layer_window.x,
                    result->layer_window.y,
                    result->layer_window.width,
                    result->layer_window.height);
  gcmz_logf_verbose(NULL,
                    NULL,
                    "Effective area: (%d, %d) %dx%d",
                    result->effective_area.x,
                    result->effective_area.y,
                    result->effective_area.width,
                    result->effective_area.height);
  gcmz_logf_verbose(NULL,
                    NULL,
                    "Cursor detection area: (%d, %d) %dx%d",
                    result->cursor_detection_area.x,
                    result->cursor_detection_area.y,
                    result->cursor_detection_area.width,
                    result->cursor_detection_area.height);
  gcmz_logf_verbose(NULL,
                    NULL,
                    "Cursor: (%d, %d) %dx%d",
                    result->cursor.x,
                    result->cursor.y,
                    result->cursor.width,
                    result->cursor.height);
}

static void tray_menu_debug_capture_callback(void *userdata, struct gcmz_tray_callback_event *const event) {
  (void)userdata;
  static wchar_t label[64];
  switch (event->type) {
  case gcmz_tray_callback_query_info: {
    bool show_debug_menu = false;
    struct ov_error err = {0};
    if (gcmz_config_get_show_debug_menu(g_config, &show_debug_menu, &err)) {
      if (show_debug_menu) {
        if (label[0] == L'\0') {
          ov_snprintf_wchar(
              label, sizeof(label) / sizeof(label[0]), NULL, L"%s", gettext("Save Timeline Screenshot (Debug)"));
        }
        event->result.query_info.label = label;
        event->result.query_info.enabled = true;
      } else {
        event->result.query_info.label = NULL;
      }
    } else {
      event->result.query_info.label = NULL;
      OV_ERROR_DESTROY(&err);
    }
    break;
  }

  case gcmz_tray_callback_clicked: {
    struct ov_error err = {0};
    struct gcmz_analyze_result result = {0};
    struct gcmz_project_data project_data = {0};
    int zoom = -1;
    bool success = false;

    if (gcmz_project_info_get(&project_data, &err)) {
      zoom = project_data.display_zoom;
      OV_ERROR_DESTROY(&err);
    }
    if (!gcmz_analyze_run(g_capture, zoom, &result, analyze_complete_callback, NULL, &err)) {
      gcmz_logf_error(&err, "%s", "%s", "failed to capture for debug");
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    log_analyze(&result);
    success = true;

  cleanup:
    if (!success) {
      gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to perform debug capture"));
      OV_ERROR_REPORT(&err, NULL);
    }
    break;
  }
  }
}

static void tray_menu_config_dialog_callback(void *userdata, struct gcmz_tray_callback_event *const event) {
  (void)userdata;
  static bool dialog_open;
  static wchar_t label[64];
  switch (event->type) {
  case gcmz_tray_callback_query_info:
    if (label[0] == L'\0') {
      ov_snprintf_wchar(label, sizeof(label) / sizeof(label[0]), L"%s", L"%s", gettext("GCMZDrops Settings..."));
    }
    event->result.query_info.label = label;
    event->result.query_info.enabled = !dialog_open;
    break;

  case gcmz_tray_callback_clicked: {
    struct ov_error err = {0};
    bool const running = g_api != NULL;
    bool external_api_enabled = false;
    bool success = false;

    dialog_open = true;
    if (!gcmz_config_dialog_show(g_config, gcmz_aviutl2_get_main_window(), running, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    if (!gcmz_config_get_external_api(g_config, &external_api_enabled, &err)) {
      gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to get external API setting"));
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (external_api_enabled == running) {
      success = true;
      goto cleanup;
    }
    if (external_api_enabled) {
      if (!create_external_api(true, &err)) {
        gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to initialize external API"));
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
    } else {
      gcmz_api_destroy(&g_api);
    }
    success = true;

  cleanup:
    if (!success) {
      gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to update external API state"));
      OV_ERROR_REPORT(&err, NULL);
    }
    dialog_open = false;
    break;
  }
  }
}

#ifndef NDEBUG

static void debug_edit_callback(struct aviutl2_edit_section *edit) {
  if (!edit || !edit->info) {
    return;
  }
  struct aviutl2_edit_info const *info = edit->info;
  gcmz_logf_info(NULL,
                 NULL,
                 "[edit_section] width: %d / height: %d / rate: %d / scale: %d / sample_rate: %d",
                 info->width,
                 info->height,
                 info->rate,
                 info->scale,
                 info->sample_rate);
  gcmz_logf_info(NULL,
                 NULL,
                 "[edit_section] frame: %d / layer: %d / frame_max: %d / layer_max: %d",
                 info->frame,
                 info->layer,
                 info->frame_max,
                 info->layer_max);
}

static void debug_output_project_info(void) {
  gcmz_logf_verbose(NULL, "%1$s", "† verbose output †");
  gcmz_logf_info(NULL, "%1$s", "† info output †");
  gcmz_logf_warn(NULL, "%1$s", "† warn output †");
  gcmz_logf_error(NULL, "%1$s", "† error output †");

  struct ov_error err = {0};

  gcmz_logf_info(NULL, NULL, "--- g_edit (0x%p) ---", (void *)g_edit);
  if (g_edit && g_edit->call_edit_section) {
    if (!g_edit->call_edit_section(debug_edit_callback)) {
      gcmz_logf_warn(NULL, NULL, "g_edit->call_edit_section failed");
    }
  } else {
    gcmz_logf_warn(NULL, NULL, "g_edit is not available");
  }

  struct aviutl2_edit_handle *simulated = gcmz_aviutl2_create_simulated_edit_handle();
  if (simulated && simulated != g_edit) {
    gcmz_logf_info(NULL, NULL, "--- simulated_edit_handle (0x%p) ---", (void *)simulated);
    if (simulated->call_edit_section) {
      if (!simulated->call_edit_section(debug_edit_callback)) {
        gcmz_logf_warn(NULL, NULL, "simulated->call_edit_section failed");
      }
    }
  } else if (simulated == g_edit) {
    gcmz_logf_info(NULL, NULL, "simulated_edit_handle == g_edit (same handle)");
  } else {
    gcmz_logf_info(NULL, NULL, "simulated_edit_handle is not available");
  }

  gcmz_logf_info(NULL, NULL, "--- extended_project_info ---");
  int display_frame = 0;
  int display_layer = 0;
  int display_zoom = 0;
  wchar_t const *project_path = NULL;
  if (gcmz_aviutl2_get_extended_project_info(&display_frame, &display_layer, &display_zoom, &project_path, &err)) {
    gcmz_logf_info(NULL,
                   NULL,
                   "[extended] display_frame: %d / display_layer: %d / display_zoom: %d",
                   display_frame,
                   display_layer,
                   display_zoom);
    gcmz_logf_info(NULL, NULL, "[extended] project_path: %ls", project_path ? project_path : L"(null)");
  } else {
    gcmz_logf_warn(&err, NULL, "gcmz_aviutl2_get_extended_project_info failed");
    OV_ERROR_DESTROY(&err);
  }
}

static void tray_menu_debug_output_callback(void *userdata, struct gcmz_tray_callback_event *const event) {
  (void)userdata;
  static wchar_t label[64];
  switch (event->type) {
  case gcmz_tray_callback_query_info:
    if (label[0] == L'\0') {
      ov_snprintf_wchar(label, sizeof(label) / sizeof(label[0]), L"%s", L"%s", "Test Output");
    }
    event->result.query_info.label = label;
    event->result.query_info.enabled = true;
    break;

  case gcmz_tray_callback_clicked: {
    struct ov_error err = {0};
    struct gcmz_analyze_result capture = {0};
    struct gcmz_project_data project_data = {0};
    bool success = false;

    if (!gcmz_project_info_get(&project_data, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_analyze_run(g_capture, project_data.display_zoom, &capture, NULL, NULL, &err)) {
      gcmz_logf_error(&err, "%s", "%s", "failed to capture for debug output");
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    log_analyze(&capture);
    debug_output_project_info();
    success = true;

  cleanup:
    if (!success) {
      OV_ERROR_REPORT(&err, NULL);
    }
    break;
  }
  }
}

static void tray_menu_test_external_api_complete_callback(struct gcmz_api_request_params *const params) {
  (void)params;
  gcmz_logf_info(NULL, "%s", "%s", "API request test completed");
}

static void tray_menu_test_external_api(void *userdata, struct gcmz_tray_callback_event *const event) {
  (void)userdata;
  static wchar_t label[64];
  switch (event->type) {
  case gcmz_tray_callback_query_info:
    if (label[0] == L'\0') {
      ov_snprintf_wchar(label, sizeof(label) / sizeof(label[0]), L"%s", L"%s", "Test API Request");
    }
    event->result.query_info.label = label;
    event->result.query_info.enabled = true;
    break;

  case gcmz_tray_callback_clicked: {
    struct ov_error err = {0};
    struct ovl_file *file = NULL;
    wchar_t *temp_path = NULL;
    struct gcmz_file_list *files = NULL;
    bool result = false;

    {
      char const *utf8_text = "Hello, World †";
      size_t const utf8_len = strlen(utf8_text);
      size_t written = 0;

      if (!ovl_file_create_temp(L"test.txt", &file, &temp_path, &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }

      if (!ovl_file_write(file, utf8_text, utf8_len, &written, &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }

      ovl_file_close(file);
      file = NULL;

      files = gcmz_file_list_create(&err);
      if (!files) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }

      if (!gcmz_file_list_add_temporary(files, temp_path, L"text/plain", &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }

      struct gcmz_api_request_params params = {
          .files = files,
          .layer = 5,
          .frame_advance = 3,
          .use_exo_converter = false,
          .err = &err,
          .userdata = NULL,
      };

      api_request_callback(&params, tray_menu_test_external_api_complete_callback);
      files = NULL;
      temp_path = NULL;
    }

    result = true;

  cleanup:
    if (file) {
      ovl_file_close(file);
    }
    if (temp_path) {
      OV_ARRAY_DESTROY(&temp_path);
    }
    if (files) {
      gcmz_file_list_destroy(&files);
    }
    if (!result) {
      gcmz_logf_error(&err, "%s", "%s", "failed to test API request");
      OV_ERROR_REPORT(&err, NULL);
    }
    break;
  }
  }
}
#endif // NDEBUG

static bool
drop_extract_callback(void *dataobj, struct gcmz_file_list *dest, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!gcmz_dataobj_extract_from_dataobj(dataobj, dest, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static bool schedule_cleanup_callback(wchar_t const *const path, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!gcmz_delayed_cleanup_schedule_file(path, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static bool
get_project_data_callback(struct gcmz_project_data *project_data, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!gcmz_project_info_get(project_data, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static wchar_t *drop_get_save_path_callback(wchar_t const *filename, void *userdata, struct ov_error *const err) {
  (void)userdata;
  wchar_t *const r = gcmz_config_get_save_path(g_config, filename, err);
  if (!r) {
    OV_ERROR_ADD_TRACE(err);
    return NULL;
  }
  return r;
}

static bool drop_file_manage_callback(wchar_t const *source_file,
                                      wchar_t **final_file,
                                      void *userdata,
                                      struct ov_error *const err) {
  (void)userdata;
  enum gcmz_processing_mode mode;
  if (!gcmz_config_get_processing_mode(g_config, &mode, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  if (!gcmz_copy(source_file, mode, drop_get_save_path_callback, NULL, final_file, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static char *lua_api_temp_file_callback(void *userdata, char const *filename, struct ov_error *err) {
  (void)userdata;
  wchar_t *filename_w = NULL;
  wchar_t *dest_path_w = NULL;
  char *dest_path = NULL;

  {
    // Convert filename from UTF-8 to wchar_t
    int const filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    if (filename_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&filename_w, (size_t)filename_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len) <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    // Create temp file
    if (!gcmz_temp_create_unique_file(filename_w, &dest_path_w, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Convert result path to UTF-8
    int const dest_len = WideCharToMultiByte(CP_UTF8, 0, dest_path_w, -1, NULL, 0, NULL, NULL);
    if (dest_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&dest_path, (size_t)dest_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, dest_path_w, -1, dest_path, dest_len, NULL, NULL) <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }

  OV_ARRAY_DESTROY(&filename_w);
  OV_ARRAY_DESTROY(&dest_path_w);
  return dest_path;

cleanup:
  if (filename_w) {
    OV_ARRAY_DESTROY(&filename_w);
  }
  if (dest_path_w) {
    OV_ARRAY_DESTROY(&dest_path_w);
  }
  if (dest_path) {
    OV_ARRAY_DESTROY(&dest_path);
  }
  return NULL;
}

static char *lua_api_save_path_callback(void *userdata, char const *filename, struct ov_error *err) {
  (void)userdata;
  wchar_t *filename_w = NULL;
  wchar_t *dest_path_w = NULL;
  char *dest_path = NULL;

  {
    // Convert filename from UTF-8 to wchar_t
    int const filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    if (filename_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&filename_w, (size_t)filename_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len) <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    // Get save path
    dest_path_w = gcmz_config_get_save_path(g_config, filename_w, err);
    if (!dest_path_w) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Convert result path to UTF-8
    int const dest_len = WideCharToMultiByte(CP_UTF8, 0, dest_path_w, -1, NULL, 0, NULL, NULL);
    if (dest_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&dest_path, (size_t)dest_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, dest_path_w, -1, dest_path, dest_len, NULL, NULL) <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }

  OV_ARRAY_DESTROY(&filename_w);
  OV_ARRAY_DESTROY(&dest_path_w);
  return dest_path;

cleanup:
  if (filename_w) {
    OV_ARRAY_DESTROY(&filename_w);
  }
  if (dest_path_w) {
    OV_ARRAY_DESTROY(&dest_path_w);
  }
  if (dest_path) {
    OV_ARRAY_DESTROY(&dest_path);
  }
  return NULL;
}

static size_t get_window_list_callback(struct gcmz_window_info *windows,
                                       size_t window_len,
                                       void *userdata,
                                       struct ov_error *const err) {
  (void)userdata;
  if (!windows || window_len == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return SIZE_MAX;
  }
  if (!g_window_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
    return SIZE_MAX;
  }
  size_t num_items = 0;
  struct gcmz_window_info const *const items = gcmz_window_list_get(g_window_list, &num_items);
  if (!items || num_items == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return SIZE_MAX;
  }
  size_t const n = num_items < window_len ? num_items : window_len;
  memcpy(windows, items, n * sizeof(struct gcmz_window_info));
  return n;
}

/**
 * Check if running under Wine environment
 * @return true if running under Wine, false otherwise
 */
static bool is_wine_environment(void) {
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  if (!ntdll) {
    return false;
  }
  return GetProcAddress(ntdll, "wine_get_version") != NULL;
}

/**
 * Capture window and get bitmap data
 * @param window Window handle to capture
 * @param data [in/out] Captured image data (caller must free with OV_ARRAY_DESTROY)
 * @param width [out] Captured image width
 * @param height [out] Captured image height
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool
capture_window(HWND window, uint8_t **const data, int *const width, int *const height, struct ov_error *const err) {
  if (!window || !width || !height || !data) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!IsWindowVisible(window) || !IsWindowEnabled(window)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }

  HDC screen_dc = NULL;
  HDC window_dc = NULL;
  HDC mem_dc = NULL;
  HBITMAP bitmap = NULL;
  bool const data_was_null = (*data == NULL);
  bool result = false;

  {
    screen_dc = GetDC(NULL);
    if (screen_dc == NULL) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    RECT rect;
    if (!GetClientRect(window, &rect)) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
    if (*width <= 0 || *height <= 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    // Choose capture method based on Wine detection
    bool const use_bitblt = is_wine_environment();
    HDC source_dc;

    if (use_bitblt) {
      window_dc = GetDC(window);
      if (window_dc == NULL) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }
      source_dc = window_dc;
    } else {
      source_dc = screen_dc;
    }

    mem_dc = CreateCompatibleDC(source_dc);
    if (mem_dc == NULL) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    bitmap = CreateCompatibleBitmap(source_dc, *width, *height);
    if (bitmap == NULL) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    HBITMAP const old_bitmap = (HBITMAP)SelectObject(mem_dc, bitmap);

    WINBOOL capture_result;
    if (use_bitblt) {
      capture_result = BitBlt(mem_dc, 0, 0, *width, *height, window_dc, 0, 0, SRCCOPY);
    } else {
      capture_result = PrintWindow(window, mem_dc, PW_CLIENTONLY);
    }

    SelectObject(mem_dc, old_bitmap);

    if (!capture_result) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    size_t const data_size = (size_t)(((*width * 3 + 3) & ~3) * *height);
    if (!OV_ARRAY_GROW(data, data_size)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (!GetDIBits(screen_dc,
                   bitmap,
                   0,
                   (UINT)*height,
                   *data,
                   &(BITMAPINFO){
                       .bmiHeader =
                           {
                               .biSize = sizeof(BITMAPINFOHEADER),
                               .biWidth = *width,
                               .biHeight = -*height,
                               .biPlanes = 1,
                               .biBitCount = 24,
                               .biCompression = BI_RGB,
                           },
                   },
                   DIB_RGB_COLORS)) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (!result && data_was_null && *data) {
    OV_ARRAY_DESTROY(data);
  }
  if (bitmap) {
    DeleteObject(bitmap);
    bitmap = NULL;
  }
  if (mem_dc) {
    DeleteDC(mem_dc);
    mem_dc = NULL;
  }
  if (window_dc) {
    ReleaseDC(window, window_dc);
    window_dc = NULL;
  }
  if (screen_dc) {
    ReleaseDC(NULL, screen_dc);
    screen_dc = NULL;
  }
  return result;
}

/**
 * Callback function for capturing window bitmap data
 */
static bool capture_window_callback(
    void *window, uint8_t **data, int *width, int *height, void *userdata, struct ov_error *const err) {
  (void)userdata;
  return capture_window((HWND)window, data, width, height, err);
}

/**
 * Callback function invoked when the active window state changes.
 *
 * This callback is called frequently by gcmz_do_init, so performance is important.
 * Heavy processing should be avoided to prevent UI lag.
 *
 * @param userdata User-defined data pointer
 */
static void on_change_activate(void *const userdata) {
  (void)userdata;
  enum {
    max_windows = 8,
  };

  struct gcmz_window_info windows[max_windows] = {0};
  bool success = false;
  struct ov_error err = {0};

  {
    if (!g_window_list) {
      success = true;
      goto cleanup;
    }

    void *window_handles[max_windows];
    size_t const count = gcmz_aviutl2_find_manager_windows(window_handles, max_windows, &err);
    if (count == SIZE_MAX) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    size_t found = 0;
    for (size_t i = 0; i < count && found < max_windows; ++i) {
      HWND const hwnd = (HWND)window_handles[i];
      if (!IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd)) {
        continue;
      }
      RECT rect;
      if (!GetClientRect(hwnd, &rect)) {
        continue;
      }
      int const w = rect.right - rect.left;
      int const h = rect.bottom - rect.top;
      if (w <= 0 || h <= 0) {
        continue;
      }
      windows[found++] = (struct gcmz_window_info){
          .window = hwnd,
          .width = w,
          .height = h,
      };
    }
    if (!found) {
      // There should always be at least one main window normally,
      // but on during finalization it might be gone.
      success = true;
    }
    switch (gcmz_window_list_update(g_window_list, windows, found, &err)) {
    case ov_false:
      // No changes, do nothing
      success = true;
      break;
    case ov_true:
      if (g_drop) {
        for (size_t i = 0; i < found; ++i) {
          if (!gcmz_drop_register_window(g_drop, windows[i].window, &err)) {
            gcmz_logf_warn(&err, "%s", "%s", "failed to register window for drag and drop");
            OV_ERROR_DESTROY(&err);
          }
        }
      }

      success = true;
      break;
    case ov_indeterminate:
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

static void finalize(void *const userdata) {
  (void)userdata;
  if (g_tray) {
    gcmz_tray_destroy(&g_tray);
  }
  if (g_api) {
    gcmz_api_destroy(&g_api);
  }
  if (g_drop) {
    gcmz_drop_destroy(&g_drop);
  }
  if (g_lua_ctx) {
    gcmz_lua_destroy(&g_lua_ctx);
  }
  if (g_config) {
    gcmz_config_destroy(&g_config);
  }
  gcmz_analyze_destroy(&g_capture);
  if (g_window_list) {
    gcmz_window_list_destroy(&g_window_list);
  }
  gcmz_delayed_cleanup_exit();
  gcmz_temp_remove_directory();
  gcmz_do_exit();
  gcmz_aviutl2_cleanup();
  if (g_mo) {
    mo_set_default(NULL);
    mo_free(&g_mo);
  }
  if (g_plugin_registered != ov_indeterminate) {
    cnd_destroy(&g_init_cond);
    mtx_destroy(&g_init_mtx);
    g_plugin_registered = ov_indeterminate;
  }
}

static NATIVE_CHAR const *project_path_provider_callback(void *userdata) {
  (void)userdata;
  return gcmz_aviutl2_get_project_path();
}

static bool get_script_directory_path(wchar_t **const script_dir, struct ov_error *const err) {
  if (!script_dir) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  static NATIVE_CHAR const script_subdir[] = NSTR("/" GCMZ_SCRIPT_SUBDIR);

  wchar_t *module_path = NULL;
  wchar_t *dir = NULL;
  void *hinstance = NULL;
  wchar_t const *last_slash = NULL;
  bool result = false;

  {
    if (!ovl_os_get_hinstance_from_fnptr((void *)get_script_directory_path, &hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!ovl_path_get_module_name(&module_path, hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    last_slash = ovl_path_find_last_path_sep(module_path);
    if (!last_slash) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "Failed to extract directory from module path");
      goto cleanup;
    }

    size_t dir_len = (size_t)(last_slash - module_path);
    size_t subdir_len = wcslen(script_subdir);

    if (!OV_ARRAY_GROW(&dir, dir_len + subdir_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    wcsncpy(dir, module_path, dir_len);
    wcscpy(dir + dir_len, script_subdir);
    dir[dir_len + subdir_len] = L'\0';

    *script_dir = dir;
    dir = NULL;
  }

  result = true;

cleanup:
  if (module_path) {
    OV_ARRAY_DESTROY(&module_path);
  }
  if (dir) {
    OV_ARRAY_DESTROY(&dir);
  }
  return result;
}

static HICON load_icon(struct ov_error *const err) {
  enum { IDI_APPICON = 101 };
  void *hinstance = NULL;
  if (!ovl_os_get_hinstance_from_fnptr((void *)load_icon, &hinstance, err)) {
    OV_ERROR_ADD_TRACE(err);
    return NULL;
  }
  return LoadIconW((HINSTANCE)hinstance, MAKEINTRESOURCEW(IDI_APPICON));
}

static void on_temp_cleanup(wchar_t const *const dir_path, void *const userdata) {
  (void)userdata;
  gcmz_logf_info(NULL, NULL, pgettext("cleanup_stale_temporary_directories", "removed: %1$ls"), dir_path);
}

static int delayed_initialization_thread(void *userdata) {
  (void)userdata;

  DWORD const start_tick = GetTickCount();
  enum { delayed_window_registration_ms = 1000 };

  struct ov_error err = {0};
  bool success = false;

  {
    gcmz_logf_info(NULL,
                   "%1$hs",
                   "%1$hs",
                   pgettext("cleanup_stale_temporary_directories", "Cleaning up stale temporary directories..."));
    if (!gcmz_temp_cleanup_stale_directories(on_temp_cleanup, NULL, &err)) {
      gcmz_logf_error(&err,
                      "%1$hs",
                      "%1$hs",
                      pgettext("cleanup_stale_temporary_directories", "failed to cleanup stale temporary directories"));
      OV_ERROR_DESTROY(&err); // clear error and continue
    }
    gcmz_logf_info(NULL,
                   "%1$hs",
                   "%1$hs",
                   pgettext("cleanup_stale_temporary_directories", "stale temporary directory cleanup complete"));

    mtx_lock(&g_init_mtx);
    while (g_plugin_registered == ov_false) {
      cnd_wait(&g_init_cond, &g_init_mtx);
    }
    mtx_unlock(&g_init_mtx);

    if (!g_config) {
      goto cleanup;
    }

    bool external_api_enabled = false;
    if (!gcmz_config_get_external_api(g_config, &external_api_enabled, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to get external API setting"));
      goto cleanup;
    }
    if (!external_api_enabled) {
      success = true;
      goto cleanup;
    }

    if (!create_external_api(false, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      gcmz_logf_warn(&err, "%1$hs", "%1$hs", gettext("failed to initialize external API, continuing without it."));
      goto cleanup;
    }
  }
  success = true;

cleanup:
  if (!success) {
    OV_ERROR_DESTROY(&err);
  }

  // Delayed window registration for right-click position tracking
  // Wait until few seconds have passed since thread start to ensure all windows are created
  DWORD const elapsed = GetTickCount() - start_tick;
  if (elapsed < delayed_window_registration_ms) {
    Sleep(delayed_window_registration_ms - elapsed);
  }
  gcmz_do(on_change_activate, NULL);

  return 0;
}

static bool initialize(struct ov_error *const err) {
  bool success = false;
  HWND main_window = NULL;
  wchar_t *script_dir = NULL;

  if (g_plugin_registered == ov_indeterminate) {
    if (mtx_init(&g_init_mtx, mtx_plain) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (cnd_init(&g_init_cond) != thrd_success) {
      mtx_destroy(&g_init_mtx);
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    g_plugin_registered = ov_false;

    thrd_t thread;
    if (thrd_create(&thread, delayed_initialization_thread, NULL) == thrd_success) {
      thrd_detach(thread);
    } else {
      gcmz_logf_warn(NULL, "%s", "%s", gettext("failed to create thread for delayed initialization"));
    }
  }

  if (!g_mo) {
    HINSTANCE hinst = NULL;
    if (!ovl_os_get_hinstance_from_fnptr((void *)initialize, (void **)&hinst, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    g_mo = mo_parse_from_resource(hinst, err);
    if (g_mo) {
      mo_set_default(g_mo);
    } else {
      gcmz_logf_warn(NULL, "%s", "%s", gettext("failed to load language resources, continuing without them."));
    }
  }

  switch (gcmz_aviutl2_init(err)) {
  case gcmz_aviutl2_status_success:
    gcmz_logf_info(
        NULL, "%1$s", gettext("detected AviUtl ExEdit2 version is %1$s"), gcmz_aviutl2_get_detected_version());
    break;
  case gcmz_aviutl2_status_signature_failed:
    gcmz_logf_warn(NULL,
                   "%1$s",
                   gettext("detected AviUtl ExEdit2 version is %1$s, but signature verification failed. "
                           "the data may not be from an official release."),
                   gcmz_aviutl2_get_detected_version());
    break;
  case gcmz_aviutl2_status_unknown_binary:
    g_unknown_binary = true;
    gcmz_logf_warn(
        NULL, "%s", "%s", gettext("unknown AviUtl ExEdit2 version detected. some features will be disabled."));
    break;
  case gcmz_aviutl2_status_error:
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  // If g_aviutl2_version was not set by InitializePlugin, get it from detected version
  if (g_aviutl2_version == 0) {
    g_aviutl2_version = gcmz_aviutl2_get_detected_version_uint32();
  }

  if (!g_logger) {
    g_logger = gcmz_aviutl2_create_simulated_log_handle();
    if (g_logger) {
      gcmz_logf_set_handle(g_logger);
    }
  }

  if (!g_edit) {
    g_edit = gcmz_aviutl2_create_simulated_edit_handle();
    if (g_edit) {
      gcmz_project_info_set_handle(g_edit);
    }
  }
  if (g_edit) {
    gcmz_project_info_set_extended_getter(gcmz_aviutl2_get_extended_project_info);
  }

  main_window = gcmz_aviutl2_get_main_window();
  if (!gcmz_do_init(
          &(struct gcmz_do_init_option){
              .window = main_window,
              .on_cleanup = finalize,
              .on_change_activate = on_change_activate,
          },
          err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_temp_create_directory(err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_delayed_cleanup_init(err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  g_config = gcmz_config_create(
      &(struct gcmz_config_options){
          .project_path_provider = project_path_provider_callback,
          .project_path_provider_userdata = NULL,
      },
      err);
  if (!g_config) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_config_load(g_config, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_analyze_create(&g_capture,
                           &(struct gcmz_analyze_options){
                               .capture = capture_window_callback,
                               .get_window_list = get_window_list_callback,
                               .get_style = analyze_get_style_callback,
                               .userdata = NULL,
                           },
                           err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  g_window_list = gcmz_window_list_create(err);
  if (!g_window_list) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!get_script_directory_path(&script_dir, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
      .temp_file_provider = lua_api_temp_file_callback,
      .save_path_provider = lua_api_save_path_callback,
      .get_project_data = get_project_data_callback,
      .userdata = NULL,
  });

  if (!gcmz_lua_create(&g_lua_ctx,
                       &(struct gcmz_lua_options){
                           .script_dir = script_dir,
                           .api_register_callback = gcmz_lua_api_register,
                           .schedule_cleanup_callback = schedule_cleanup_callback,
                           .create_temp_file_callback = lua_api_temp_file_callback,
                       },
                       err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  g_drop = gcmz_drop_create(drop_extract_callback,
                            schedule_cleanup_callback,
                            get_project_data_callback,
                            drop_file_manage_callback,
                            NULL,      // callback_userdata
                            g_lua_ctx, // lua_context
                            err);
  if (!g_drop) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  // Initial window list update and drop registration
  // Use gcmz_do to ensure this runs on the window thread for proper subclass installation
  gcmz_do(on_change_activate, NULL);

  {
    HICON icon = load_icon(err);
    if (!icon) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    g_tray = gcmz_tray_create(icon, err);
    if (!g_tray) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  if (!gcmz_tray_add_menu_item(g_tray, tray_menu_config_dialog_callback, NULL, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_tray_add_menu_item(g_tray, tray_menu_debug_capture_callback, NULL, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

#ifndef NDEBUG
  if (!gcmz_tray_add_menu_item(g_tray, tray_menu_debug_output_callback, NULL, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  if (!gcmz_tray_add_menu_item(g_tray, tray_menu_test_external_api, NULL, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
#endif

  // For non-plugin builds, signal that initialization is complete
  // (RegisterPlugin won't be called, so we signal here instead)
#if !TARGET_AVIUTL2_PLUGIN
  mtx_lock(&g_init_mtx);
  g_plugin_registered = ov_true;
  cnd_signal(&g_init_cond);
  mtx_unlock(&g_init_mtx);
#endif

  success = true;
cleanup:
  if (script_dir) {
    OV_ARRAY_DESTROY(&script_dir);
  }
  if (!success) {
    wchar_t title[128];
    wchar_t main_instruction[128];
    wchar_t content[512];
    ov_snprintf_wchar(title, sizeof(title) / sizeof(title[0]), L"%s", L"%s", gettext("GCMZDrops"));
    ov_snprintf_wchar(main_instruction,
                      sizeof(main_instruction) / sizeof(main_instruction[0]),
                      L"%s",
                      L"%s",
                      gettext("Failed to initialize GCMZDrops."));
    ov_snprintf_wchar(content,
                      sizeof(content) / sizeof(content[0]),
                      L"%s",
                      L"%s",
                      gettext("The plugin could not start correctly.\nGCMZDrops is unavailable at the moment."));
    gcmz_error_dialog(NULL, err, title, main_instruction, content, TD_ERROR_ICON, TDCBF_OK_BUTTON);
    finalize(NULL);
  }
  return success;
}

/**
 * Get plugin information string with version
 * @return Plugin information string (e.g., "GCMZDrops v1.0 ( xxxxxxxx ) by oov")
 */
static wchar_t const *get_information(void) {
  static wchar_t buf[64];
  if (buf[0] != L'\0') {
    return buf;
  }
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(buf[0]), L"%1$hs", L"GCMZDrops %1$s by oov", GCMZ_VERSION);
  return buf;
}

#if TARGET_LUA_MODULE
struct userdata {
  void *unused;
};

static int luafn_test(lua_State *L) {
  (void)L;
  // gcmz_aviutl2_debug_output();
  return 0;
}

static int luafn___gc(lua_State *L) {
  (void)L;
#  if 0
  if (lua_gettop(L) < 1) {
    return luaL_error(L, "invalid number of parameters");
  }
  struct userdata *ud = lua_touserdata(L, 1);
  if (!ud) {
    return luaL_error(L, "invalid arguments");
  }
#  endif
  finalize(NULL);
  return 0;
}

int __declspec(dllexport) luaopen_GCMZDrops(lua_State *L);
int __declspec(dllexport) luaopen_GCMZDrops(lua_State *L) {
  static char const name[] = "GCMZDrops";
  static char const meta_name[] = "GCMZDrops_Meta";

  struct ov_error err = {0};
  if (!initialize(&err)) {
    return gcmz_luafn_err(L, &err);
  }
  struct userdata *ud = (struct userdata *)lua_newuserdata(L, sizeof(struct userdata));
  if (!ud) {
    return luaL_error(L, "lua_newuserdata failed");
  }

  {
    luaL_newmetatable(L, meta_name);

    lua_pushstring(L, "__index");
    lua_newtable(L);
    static luaL_Reg funcs[] = {
        {"test", luafn_test},
        {NULL, NULL},
    };
    luaL_register(L, NULL, funcs);
    lua_settable(L, -3);

    // LuaJIT cleanup timing might be unstable?
    // Object destruction may fail and process may not terminate, so giving up on __gc cleanup
    // gcmz_do cleanup can be done at WM_NCDESTROY timing, which provides higher stability
#  if 0
    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, luafn___gc);
    lua_settable(L, -3);
#  else
    (void)luafn___gc;
#  endif

    lua_setmetatable(L, -2);
  }

  lua_pushvalue(L, -1);
  lua_setglobal(L, name);
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaded");
  lua_pushvalue(L, -3);
  lua_setfield(L, -2, name);
  lua_pop(L, 2);
  return 1;
}
#endif

void __declspec(dllexport) InitializeLogger(struct aviutl2_log_handle *logger);
void __declspec(dllexport) InitializeLogger(struct aviutl2_log_handle *logger) {
  if (logger) {
    g_logger = logger;
    gcmz_logf_set_handle(g_logger);
  }
}

#if TARGET_AVIUTL2_FILTER_PLUGIN || TARGET_AVIUTL2_SCRIPT_MODULE || TARGET_AVIUTL2_PLUGIN
BOOL __declspec(dllexport) InitializePlugin(DWORD version);
BOOL __declspec(dllexport) InitializePlugin(DWORD version) {
  g_aviutl2_version = version;
  struct ov_error err = {0};
  if (!initialize(&err)) {
    OV_ERROR_REPORT(&err, "failed to initialize GCMZDrops");
    return FALSE;
  }
  return TRUE;
}

void __declspec(dllexport) UninitializePlugin(void);
void __declspec(dllexport) UninitializePlugin(void) {
  // Cleanup is handled in finalize
}
#endif

#if TARGET_AVIUTL2_FILTER_PLUGIN
static bool dummy_proc_video(struct aviutl2_filter_proc_video *video) {
  (void)video;
  return true;
}

struct aviutl2_filter_plugin_table *__declspec(dllexport) GetFilterPluginTable(void);
struct aviutl2_filter_plugin_table *__declspec(dllexport) GetFilterPluginTable(void) {
  static void *items[] = {NULL};
  static struct aviutl2_filter_plugin_table plugin_table = {
      .flag = aviutl2_filter_plugin_table_flag_video,
      .name = L"GCMZDrops",
      .label = L"oov",
      .information = NULL,
      .items = items,
      .func_proc_video = dummy_proc_video,
  };
  if (plugin_table.information == NULL) {
    plugin_table.information = get_information();
  }
  return &plugin_table;
}
#endif

#if TARGET_AVIUTL2_SCRIPT_MODULE
struct aviutl2_script_module_table *__declspec(dllexport) GetScriptModuleTable(void);
struct aviutl2_script_module_table *__declspec(dllexport) GetScriptModuleTable(void) {
  static struct aviutl2_script_module_function functions[] = {
      {0},
  };
  static struct aviutl2_script_module_table script_module_table = {
      .information = NULL,
      .functions = functions,
  };
  if (script_module_table.information == NULL) {
    script_module_table.information = get_information();
  }
  return &script_module_table;
}
#endif

#if TARGET_AVIUTL2_PLUGIN

#  if 0
static void test_edit(struct aviutl2_edit_section *edit) {
  gcmz_logf_info(
      NULL,
      NULL,
      "width=%d, height=%d, rate=%d, scale=%d, sample_rate=%d, frame=%d, layer=%d, frame_max=%d, layer_max=%d",
      edit->info->width,
      edit->info->height,
      edit->info->rate,
      edit->info->scale,
      edit->info->sample_rate,
      edit->info->frame,
      edit->info->layer,
      edit->info->frame_max,
      edit->info->layer_max);
}
#  endif

static void project_load_handler(struct aviutl2_project_file *project) {
  (void)project;
  if (!g_api) {
    return;
  }
#  if 0
  // Trying to get it with the official API causes a deadlock,
  // and calling it from another thread randomly crashes...
  // Moreover, if the path to the project file is passed as a startup argument,
  // project_load_handler is not called.
  g_edit->call_edit_section(test_edit);
#  endif
  gcmz_do(update_api_project_data, NULL);
}

static int paste_from_clipboard_impl(void *userdata) {
  (void)userdata;
  struct ov_error err = {0};
  IDataObject *dataobj = NULL;
  bool success = false;

  {
    // Get right-click position
    void *window = NULL;
    int x = 0;
    int y = 0;
    if (!gcmz_drop_get_right_click_position(g_drop, &window, &x, &y, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Get IDataObject from clipboard
    HRESULT hr = OleGetClipboard(&dataobj);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(&err, hr);
      goto cleanup;
    }
    if (!dataobj) {
      OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "no data in clipboard");
      goto cleanup;
    }

    if (!gcmz_drop_inject_dataobject(g_drop, window, dataobj, x, y, false, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  success = true;
cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to paste from clipboard"));
    OV_ERROR_REPORT(&err, NULL);
  }
  if (dataobj) {
    IDataObject_Release(dataobj);
    dataobj = NULL;
  }
  return 0;
}

static void paste_from_clipboard_handler(struct aviutl2_edit_section *edit) {
  (void)edit;
  thrd_t thread;
  if (thrd_create(&thread, paste_from_clipboard_impl, NULL) == thrd_success) {
    thrd_detach(thread);
    return;
  }
}

void __declspec(dllexport) RegisterPlugin(struct aviutl2_host_app_table *host);
void __declspec(dllexport) RegisterPlugin(struct aviutl2_host_app_table *host) {
  host->set_plugin_information(get_information());
  host->register_project_load_handler(project_load_handler);
  static wchar_t layer_menu_name[64];
  ov_snprintf_wchar(layer_menu_name,
                    sizeof(layer_menu_name) / sizeof(layer_menu_name[0]),
                    L"%s",
                    L"%s",
                    gettext("[GCMZDrops] Paste from Clipboard"));
  host->register_layer_menu(layer_menu_name, paste_from_clipboard_handler);

  // It seems that calling call_edit_section will automatically stop playback if it is in progress.
  // It cannot be used for purposes such as being called periodically in the background.
  struct aviutl2_edit_handle *const edit = host->create_edit_handle();
  if (edit) {
    g_edit = edit;
    gcmz_project_info_set_handle(g_edit);
  }

  // Signal delayed initialization thread that RegisterPlugin is complete
  mtx_lock(&g_init_mtx);
  g_plugin_registered = ov_true;
  cnd_signal(&g_init_cond);
  mtx_unlock(&g_init_mtx);
}
#endif

static void error_output_hook(enum ov_error_severity severity, char const *str) {
  (void)severity;
  if (!str) {
    return;
  }
  wchar_t buf[1024];
  size_t const str_len = strlen(str);
  size_t pos = 0;
  while (pos < str_len) {
    size_t const remaining = str_len - pos;
    size_t bytes_read = 0;
    size_t const converted = ov_utf8_to_wchar(str + pos, remaining, buf, sizeof(buf) / sizeof(buf[0]) - 1, &bytes_read);
    if (converted == 0 || bytes_read == 0) {
      pos++;
      continue;
    }
    buf[converted] = L'\0';
    OutputDebugStringW(buf);
    pos += bytes_read;
  }
}

BOOL WINAPI DllMain(HINSTANCE const inst, DWORD const reason, LPVOID const reserved);
BOOL WINAPI DllMain(HINSTANCE const inst, DWORD const reason, LPVOID const reserved) {
  // trans: This dagger helps UTF-8 detection. You don't need to translate this.
  (void)gettext_noop("†");
  (void)reserved;
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(inst);
    ov_init();
    ov_error_set_output_hook(error_output_hook);
    return TRUE;
  case DLL_PROCESS_DETACH:
    ov_exit();
    return TRUE;
  }
  return TRUE;
}
