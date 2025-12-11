
#include "gcmzdrops.h"

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
#include <ovl/source.h>
#include <ovl/source/file.h>

#include <aviutl2_logger2.h>
#include <aviutl2_plugin2.h>

#include "analyze.h"
#include "api.h"
#include "aviutl2.h"
#include "config.h"
#include "config_dialog.h"
#include "copy.h"
#include "dataobj.h"
#include "delayed_cleanup.h"
#include "do.h"
#include "do_sub.h"
#include "drop.h"
#include "error.h"
#include "file.h"
#include "gcmz_types.h"
#include "ini_reader.h"
#include "logf.h"
#include "lua.h"
#include "lua_api.h"
#include "luautil.h"
#include "style_config.h"
#include "temp.h"
#include "tray.h"
#include "version.h"
#include "window_list.h"

#ifndef GCMZ_SCRIPT_SUBDIR
#  define GCMZ_SCRIPT_SUBDIR "GCMZScript"
#endif

enum gcmzdrops_plugin_state {
  gcmzdrops_plugin_state_not_initialized, // sync primitives not initialized
  gcmzdrops_plugin_state_initializing,    // initialized, waiting for RegisterPlugin
  gcmzdrops_plugin_state_registered,      // RegisterPlugin completed successfully
  gcmzdrops_plugin_state_failed,          // initialization failed
};

struct gcmzdrops {
  struct gcmz_config *config;
  struct gcmz_api *api;
  struct gcmz_drop *drop;
  struct gcmz_lua_context *lua_ctx;
  struct gcmz_tray *tray;
  struct gcmz_analyze *capture;
  struct gcmz_window_list *window_list;
  struct gcmz_do_sub *do_sub;

  struct aviutl2_edit_handle *edit;
  bool unknown_binary;
  uint32_t aviutl2_version;
  wchar_t *project_path;

  enum gcmzdrops_plugin_state plugin_state;
  mtx_t init_mtx;
  cnd_t init_cond;
};

static bool get_style(struct gcmz_analyze_style *style, void *userdata, struct ov_error *const err) {
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

/**
 * @brief Context for set_display_layer_frame call
 */
struct set_display_layer_context {
  struct gcmzdrops *ctx; ///< Parent context
  int layer;             ///< Target display layer
};

/**
 * @brief Edit section callback for setting display layer via official API
 */
static void set_display_layer_edit_section(void *param, struct aviutl2_edit_section *edit) {
  struct set_display_layer_context *const sdlc = (struct set_display_layer_context *)param;
  if (!sdlc || !edit || !edit->set_display_layer_frame) {
    return;
  }
  // Keep current display frame, only change layer
  int const display_frame = edit->info ? edit->info->display_frame_start : 0;
  edit->set_display_layer_frame(sdlc->layer, display_frame);
}

/**
 * @brief Set display layer via official API
 *
 * @param ctx GCMZDrops context
 * @param layer Target display layer
 */
static void set_display_layer_via_api(struct gcmzdrops *const ctx, int const layer) {
  if (!ctx || !ctx->edit || !ctx->edit->call_edit_section_param) {
    return;
  }
  struct set_display_layer_context sdlc = {.ctx = ctx, .layer = layer};
  ctx->edit->call_edit_section_param(&sdlc, set_display_layer_edit_section);
}

/**
 * @brief Context for set_cursor_layer_frame call
 */
struct set_cursor_frame_context {
  struct gcmzdrops *ctx; ///< Parent context
  int frame;             ///< Target cursor frame
};

/**
 * @brief Edit section callback for setting cursor frame via official API
 */
static void set_cursor_frame_edit_section(void *param, struct aviutl2_edit_section *edit) {
  struct set_cursor_frame_context *const scfc = (struct set_cursor_frame_context *)param;
  if (!scfc || !edit || !edit->set_cursor_layer_frame) {
    return;
  }
  // Keep current layer, only change frame
  int const layer = edit->info ? edit->info->layer : 0;
  edit->set_cursor_layer_frame(layer, scfc->frame);
}

/**
 * @brief Set cursor frame via official API
 *
 * @param ctx GCMZDrops context
 * @param frame Target cursor frame
 */
static void set_cursor_frame_via_api(struct gcmzdrops *const ctx, int const frame) {
  if (!ctx || !ctx->edit || !ctx->edit->call_edit_section_param) {
    return;
  }
  struct set_cursor_frame_context scfc = {.ctx = ctx, .frame = frame};
  ctx->edit->call_edit_section_param(&scfc, set_cursor_frame_edit_section);
}

struct cursor_position_params {
  int x;
  int y;
  void *window;
};

static bool determine_cursor_position(struct gcmzdrops *const ctx,
                                      int const target_layer,
                                      struct cursor_position_params *const params,
                                      struct ov_error *const err) {
  if (!ctx || !params) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  int display_zoom;
  struct aviutl2_edit_info edit_info = {0};
  struct gcmz_analyze_result capture_result = {0};
  int drop_layer_offset = 0;

  {
    ctx->edit->get_edit_info(&edit_info, sizeof(edit_info));
    if (!gcmz_aviutl2_get_display_zoom(&display_zoom, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!gcmz_analyze_run(ctx->capture, display_zoom, &capture_result, NULL, NULL, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    int layer;
    if (target_layer == 0) {
      layer = edit_info.layer; // Current selected layer
    } else if (target_layer < 0) {
      layer = -target_layer + edit_info.display_layer_start;
    } else {
      layer = target_layer - 1;
    }

    int const n_layer = capture_result.effective_area.height / capture_result.layer_height;
    if (layer < edit_info.display_layer_start || edit_info.display_layer_start + n_layer <= layer) {
      // Scroll to bring the target layer on-screen with minimum scroll distance
      int to;
      if (layer < edit_info.display_layer_start) {
        to = layer;
      } else {
        to = layer < n_layer - 1 ? 0 : layer - (n_layer - 1);
      }
      set_display_layer_via_api(ctx, to);
      ctx->edit->get_edit_info(&edit_info, sizeof(edit_info));
      if (edit_info.display_layer_start != to) {
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to scroll");
        goto cleanup;
      }
    }
    drop_layer_offset = layer - edit_info.display_layer_start;

    if (capture_result.cursor.width == 0 || capture_result.cursor.height == 0) {
      // Move the cursor to bring it on-screen as it appears to be off-screen
      int const pos = edit_info.frame;
      set_cursor_frame_via_api(ctx, pos ? pos - 1 : pos + 1);
      set_cursor_frame_via_api(ctx, pos);
    }

    if (!gcmz_analyze_run(ctx->capture, display_zoom, &capture_result, NULL, NULL, err)) {
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
  }

  result = true;
cleanup:
  return result;
}

/**
 * @brief Check if file has [Object] section in INI format
 *
 * @param filepath Path to the file to check
 * @return true if file has [Object] section
 */
static bool has_object_section(wchar_t const *const filepath) {
  if (!filepath) {
    return false;
  }

  struct gcmz_ini_reader *reader = NULL;
  bool result = false;

  {
    if (!gcmz_ini_reader_create(&reader, NULL)) {
      goto cleanup;
    }
    if (!gcmz_ini_reader_load_file(reader, filepath, NULL)) {
      goto cleanup;
    }
    struct gcmz_ini_iter iter = {0};
    while (gcmz_ini_reader_iter_sections(reader, &iter)) {
      if (iter.name_len == 6 && strncmp(iter.name, "Object", 6) == 0) {
        result = true;
        break;
      }
    }
  }

cleanup:
  gcmz_ini_reader_destroy(&reader);
  return result;
}

/**
 * @brief Check if file list contains exactly one .object file with [Object] section
 *
 * @param list File list to check
 * @return true if list contains exactly one valid .object file
 */
static bool is_single_object_file(struct gcmz_file_list const *const list) {
  if (!list || gcmz_file_list_count(list) != 1) {
    return false;
  }
  struct gcmz_file const *const file = gcmz_file_list_get(list, 0);
  if (!file || !file->path) {
    return false;
  }
  wchar_t const *const ext = wcsrchr(file->path, L'.');
  if (!ext || _wcsicmp(ext, L".object") != 0) {
    return false;
  }
  return has_object_section(file->path);
}

/**
 * @brief Context for create_object_from_alias call
 */
struct create_object_context {
  char const *alias_content; ///< UTF-8 alias content
  int layer;                 ///< Target layer (0-based)
  int frame;                 ///< Target frame (0 means current cursor position)
  int length;                ///< Object length
  bool success;              ///< Result of operation
};

/**
 * @brief Edit section callback for creating object via official API
 */
static void create_object_edit_section(void *param, struct aviutl2_edit_section *edit) {
  struct create_object_context *const coc = (struct create_object_context *)param;
  if (!coc || !edit || !edit->create_object_from_alias) {
    return;
  }
  int const frame = (coc->frame == 0 && edit->info) ? edit->info->frame : coc->frame;
  int const layer = (coc->layer == 0 && edit->info) ? edit->info->layer : coc->layer;
  aviutl2_object_handle obj = edit->create_object_from_alias(coc->alias_content, layer, frame, coc->length);
  coc->success = (obj != NULL);
}

/**
 * @brief Create object using official API
 *
 * @param ctx GCMZDrops context
 * @param files File list containing single .object file
 * @param layer Target layer (1-based, following external API convention)
 * @param err Error information on failure
 * @return true on success
 */
static bool create_object_via_official_api(struct gcmzdrops *const ctx,
                                           struct gcmz_file_list const *const files,
                                           int const layer,
                                           struct ov_error *const err) {
  if (!ctx || !files || !ctx->edit) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_file const *const file = gcmz_file_list_get(files, 0);
  if (!file || !file->path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_source *source = NULL;
  char *content = NULL;
  bool result = false;

  {
    // Read .object file content (UTF-8)
    if (!ovl_source_file_create(file->path, &source, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const file_size = ovl_source_size(source);
    if (file_size == UINT64_MAX || file_size > SIZE_MAX - 1) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    size_t const size = (size_t)file_size;
    if (!OV_ARRAY_GROW(&content, size + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const bytes_read = ovl_source_read(source, content, 0, size);
    if (bytes_read != size) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    content[size] = '\0';

    struct create_object_context coc = {
        .alias_content = content,
        .layer = layer,
        .frame = 0,  // Use current cursor position
        .length = 1, // Default length (alias content may override)
        .success = false,
    };

    if (!ctx->edit->call_edit_section_param) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
      goto cleanup;
    }

    if (!ctx->edit->call_edit_section_param(&coc, create_object_edit_section)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "call_edit_section_param failed");
      goto cleanup;
    }

    if (!coc.success) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "create_object_from_alias returned NULL");
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  if (content) {
    OV_ARRAY_DESTROY(&content);
  }
  return result;
}

/**
 * @brief Context for external API drop completion callback
 */
struct external_api_drop_context {
  struct gcmzdrops *ctx;                           ///< Parent context
  int layer;                                       ///< Target layer (1-based)
  int frame_advance;                               ///< Frame advance after drop
  gcmz_api_request_complete_func request_complete; ///< Callback to signal request completion
  struct gcmz_api_request_params *request_params;  ///< Original request params for completion callback
};

/**
 * @brief Completion callback for external API drop operations
 *
 * Called after Lua processing completes. Receives the complete context and
 * can either:
 * 1. Handle single .object files via official API (call complete with false)
 * 2. Modify coordinates and call complete with true for traditional drop
 */
static void on_drop_completion(struct gcmz_drop_complete_context *const dcc,
                               gcmz_drop_complete_func const complete,
                               void *const userdata) {
  if (!dcc || !complete) {
    return;
  }

  // userdata is only set for external API drops (heap-allocated, must be freed)
  struct external_api_drop_context *api_ctx = (struct external_api_drop_context *)userdata;
  if (!api_ctx || !api_ctx->ctx) {
    // invalid state, just complete drop
    if (complete) {
      complete(dcc, true);
    }
    return;
  }

  struct gcmzdrops *const ctx = api_ctx->ctx;
  struct ov_error err = {0};
  bool execute_drop = true;

  {
    // Handle single .object files via official API
    // When api_ctx->layer is negative, it means relative positioning from the current scroll position.
    // To obtain that position, data retrieval in an environment where ctx->unknown_binary == false is required,
    // but it seems unnecessary to support that functionality, so it is excluded.
    if (is_single_object_file(dcc->final_files) && api_ctx->layer >= 0) {
      // Try to create object via official API
      if (create_object_via_official_api(ctx, dcc->final_files, api_ctx->layer, &err)) {
        // Success: drop handled via official API, call complete with false (DragLeave)
        execute_drop = false;
        goto cleanup;
      }
      // On failure, fall back to traditional drop with proper coordinates
      gcmz_logf_warn(
          &err, "%1$hs", "%1$hs", gettext("drop via official API failed, falling back to traditional method"));
      OV_ERROR_DESTROY(&err);
    }

    // For non-.object files or official API failure, calculate proper coordinates
    // using determine_cursor_position for traditional drop flow
    struct cursor_position_params pos_params = {0};
    if (!determine_cursor_position(ctx, api_ctx->layer, &pos_params, &err)) {
      // Failed to determine cursor position - log warning and use default coordinates
      gcmz_logf_warn(
          &err, "%1$hs", "%1$hs", gettext("failed to determine cursor position, using default drop coordinates"));
      OV_ERROR_DESTROY(&err);
      // Execute drop with original coordinates
      goto cleanup;
    }

    dcc->window = pos_params.window;
    dcc->x = pos_params.x;
    dcc->y = pos_params.y;
    gcmz_logf_verbose(NULL, NULL, "drop to: window=%1$px x=%2$d, y=%3$d", dcc->window, dcc->x, dcc->y);
  }

cleanup:
  complete(dcc, execute_drop);

  // Handle frame advance after drop completes
  if (api_ctx->frame_advance != 0) {
    struct aviutl2_edit_info edit_info = {0};
    ctx->edit->get_edit_info(&edit_info, sizeof(edit_info));
    int const move_to = edit_info.frame + api_ctx->frame_advance;
    set_cursor_frame_via_api(ctx, move_to);
    ctx->edit->get_edit_info(&edit_info, sizeof(edit_info));
    if (move_to != edit_info.frame) {
      gcmz_logf_warn(&err, "%1$hs", "%1$hs", gettext("failed to move cursor after drop"));
      OV_ERROR_DESTROY(&err);
    }
  }

  if (api_ctx->request_complete) {
    api_ctx->request_complete(api_ctx->request_params);
  }
  OV_FREE(&api_ctx);
}

static void request_api(struct gcmz_api_request_params *const params, gcmz_api_request_complete_func const complete) {
  if (!params || !complete) {
    return;
  }

  struct gcmzdrops *const ctx = (struct gcmzdrops *)params->userdata;
  if (!ctx) {
    return;
  }

  struct ov_error err = {0};
  bool success = false;
  struct external_api_drop_context *api_ctx = NULL;

  {
    if (!gcmz_file_list_count(params->files)) {
      success = true;
      goto cleanup;
    }

    // Allocate context on heap for async callback
    if (!OV_REALLOC(&api_ctx, 1, sizeof(*api_ctx))) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    *api_ctx = (struct external_api_drop_context){
        .ctx = ctx,
        .layer = params->layer,
        .frame_advance = params->frame_advance,
        .request_complete = complete,
        .request_params = params,
    };
    if (!gcmz_file_list_count(params->files)) {
      success = true;
      goto cleanup;
    }

    // Get window from window list - use first available window
    size_t num_windows = 0;
    struct gcmz_window_info const *const windows = gcmz_window_list_get(ctx->window_list, &num_windows);
    if (!windows || num_windows == 0) {
      OV_ERROR_SET(
          &err, ov_error_type_generic, ov_error_generic_fail, "no registered windows available for drop target");
      goto cleanup;
    }

    // Use window center coordinates
    // The completion callback will use official API for single .object files,
    // which doesn't need accurate coordinates
    void *const window = windows[0].window;
    int const x = windows[0].width / 2;
    int const y = windows[0].height / 2;

    // Create IDataObject from file list
    void *dataobj = gcmz_drop_create_file_list_dataobj(params->files, x, y, &err);
    if (!dataobj) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // External API uses gcmz_drop_simulate_drop_external to call Lua handlers
    // independently before passing to the original IDropTarget (bypassing hook chain)
    bool const drop_ok = gcmz_drop_simulate_drop_external(
        ctx->drop, window, dataobj, x, y, params->use_exo_converter, on_drop_completion, api_ctx, &err);
    IDataObject_Release((IDataObject *)dataobj);
    dataobj = NULL;
    if (!drop_ok) {
      OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "simulated drop failed");
      goto cleanup;
    }
    api_ctx = NULL; // Ownership transferred to callback
  }

  success = true;

cleanup:
  if (api_ctx) {
    OV_FREE(&api_ctx);
  }
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to drop from external API request"));
    OV_ERROR_REPORT(&err, NULL);
    if (complete) {
      complete(params);
    }
  }
}

static void update_api_project_data(void *userdata) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx || !ctx->edit || !ctx->api) {
    return;
  }

  struct ov_error err = {0};
  struct aviutl2_edit_info ei = {0};
  bool success = false;

  ctx->edit->get_edit_info(&ei, sizeof(ei));
  if (!gcmz_api_set_project_data(ctx->api,
                                 &(struct gcmz_project_data){
                                     .width = ei.width,
                                     .height = ei.height,
                                     .video_rate = ei.rate,
                                     .video_scale = ei.scale,
                                     .sample_rate = ei.sample_rate,
                                     .audio_ch = 2,
                                     .project_path = ctx->project_path,
                                 },
                                 &err)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  } else {
    gcmz_logf_verbose(NULL,
                      "%1$d%2$d%3$d%4$d%5$d",
                      "set project info: %1$dx%2$d, %3$d/%4$d fps, %5$d Hz",
                      ei.width,
                      ei.height,
                      ei.rate,
                      ei.scale,
                      ei.sample_rate);
    gcmz_logf_verbose(NULL, "%1$ls", "project path: %1$ls", ctx->project_path ? ctx->project_path : L"(NULL)");
  }
  success = true;
cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to update external api project information"));
  }
}

static bool create_external_api_once(struct gcmzdrops *const ctx, struct ov_error *const err) {
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  ctx->api = gcmz_api_create(
      &(struct gcmz_api_options){
          .request_callback = request_api,
          .update_callback = NULL,
          .userdata = ctx,
          .aviutl2_ver = ctx->aviutl2_version,
          .gcmz_ver = GCMZ_VERSION_UINT32,
      },
      err);
  if (!ctx->api) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }
  gcmz_logf_verbose(NULL, "%1$hs", "%1$hs", pgettext("external_api", "external API initialized successfully"));
  gcmz_do(update_api_project_data, ctx);
  result = true;

cleanup:
  return result;
}

static bool create_external_api(struct gcmzdrops *const ctx, bool const use_retry, struct ov_error *const err) {
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (ctx->api) {
    OV_ERROR_SET(
        err, ov_error_type_generic, ov_error_generic_fail, pgettext("external_api", "external API already exists"));
    return false;
  }

  if (ctx->unknown_binary) {
    gcmz_logf_warn(NULL,
                   "%s",
                   "%s",
                   pgettext("external_api", "external API is disabled because the AviUtl ExEdit2 version is unknown"));
    return true;
  }

  if (!use_retry) {
    return create_external_api_once(ctx, err);
  }

  wchar_t title[256];
  wchar_t main_instruction[256];
  wchar_t content[1024];

  while (true) {
    if (create_external_api_once(ctx, err)) {
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

static bool complete_analyze(struct gcmz_analyze_save_context *const ctx,
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

static void tray_menu_debug_capture(void *userdata, struct gcmz_tray_callback_event *const event) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  static wchar_t label[64];
  switch (event->type) {
  case gcmz_tray_callback_query_info:
    if (label[0] == L'\0') {
      ov_snprintf_wchar(
          label, sizeof(label) / sizeof(label[0]), NULL, L"%s", gettext("Save Timeline Screenshot (Debug)"));
    }
    event->result.query_info.label = label;
    event->result.query_info.enabled = true;
    break;

  case gcmz_tray_callback_clicked: {
    if (!ctx) {
      break;
    }
    struct ov_error err = {0};
    struct gcmz_analyze_result result = {0};
    int display_zoom = -1;
    bool success = false;

    if (!gcmz_aviutl2_get_display_zoom(&display_zoom, &err)) {
      gcmz_logf_error(&err, "%s", "%s", "failed to get display zoom for debug capture");
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_analyze_run(ctx->capture, display_zoom, &result, complete_analyze, NULL, &err)) {
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

static void update_tray_visibility(struct gcmzdrops *const ctx) {
  if (!ctx) {
    return;
  }
  struct ov_error err = {0};
  bool show_debug_menu = false;
  if (!gcmz_config_get_show_debug_menu(ctx->config, &show_debug_menu, &err)) {
    gcmz_logf_warn(&err, "%1$hs", "%1$hs", gettext("failed to get debug menu visibility setting"));
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!gcmz_tray_set_visible(ctx->tray, show_debug_menu, &err)) {
    gcmz_logf_warn(&err, "%1$hs", "%1$hs", gettext("failed to update tray icon visibility"));
    OV_ERROR_DESTROY(&err);
  }
}

void gcmzdrops_show_config_dialog(struct gcmzdrops *const ctx, void *const hwnd, void *const dll_hinst) {
  (void)dll_hinst;
  if (!ctx) {
    return;
  }
  struct ov_error err = {0};
  bool const running = ctx->api != NULL;
  bool external_api_enabled = false;
  bool success = false;

  {
    if (!gcmz_config_dialog_show(ctx->config, (HWND)hwnd, running, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    update_tray_visibility(ctx);

    if (!gcmz_config_get_external_api(ctx->config, &external_api_enabled, &err)) {
      gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to get external API setting"));
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (external_api_enabled == running) {
      success = true;
      goto cleanup;
    }
    if (external_api_enabled) {
      if (!create_external_api(ctx, true, &err)) {
        gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to initialize external API"));
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
    } else {
      gcmz_api_destroy(&ctx->api);
    }
  }

  success = true;

cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to update settings"));
    OV_ERROR_REPORT(&err, NULL);
  }
}

#ifndef NDEBUG

static void debug_output_info(struct gcmzdrops *const ctx) {
  gcmz_logf_verbose(NULL, "%1$s", "† verbose output †");
  gcmz_logf_info(NULL, "%1$s", "† info output †");
  gcmz_logf_warn(NULL, "%1$s", "† warn output †");
  gcmz_logf_error(NULL, "%1$s", "† error output †");

  struct ov_error err = {0};

  if (!ctx) {
    gcmz_logf_warn(NULL, NULL, "ctx is NULL");
    return;
  }

  gcmz_logf_info(NULL, NULL, "--- ctx->edit (0x%p) ---", (void *)ctx->edit);
  if (ctx->edit) {
    struct aviutl2_edit_info info = {0};
    ctx->edit->get_edit_info(&info, sizeof(info));
    gcmz_logf_info(NULL,
                   NULL,
                   "[edit_section] width: %d / height: %d / rate: %d / scale: %d / sample_rate: %d",
                   info.width,
                   info.height,
                   info.rate,
                   info.scale,
                   info.sample_rate);
    gcmz_logf_info(NULL,
                   NULL,
                   "[edit_section] frame: %d / layer: %d / frame_max: %d / layer_max: %d",
                   info.frame,
                   info.layer,
                   info.frame_max,
                   info.layer_max);
    gcmz_logf_info(NULL,
                   NULL,
                   "ctx->project_path: %ls",
                   (ctx->project_path && ctx->project_path[0] != L'\0') ? ctx->project_path : L"(null)");
  } else {
    gcmz_logf_warn(NULL, NULL, "ctx->edit is not available");
  }

  gcmz_logf_info(NULL, NULL, "--- display_zoom ---");
  int display_zoom = 0;
  if (gcmz_aviutl2_get_display_zoom(&display_zoom, &err)) {
    gcmz_logf_info(NULL, NULL, "display_zoom: %d", display_zoom);
  } else {
    gcmz_logf_warn(&err, NULL, "gcmz_aviutl2_get_display_zoom failed");
    OV_ERROR_DESTROY(&err);
  }
}

static void tray_menu_debug_output(void *userdata, struct gcmz_tray_callback_event *const event) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
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
    if (!ctx) {
      break;
    }
    struct ov_error err = {0};
    struct gcmz_analyze_result capture = {0};
    int display_zoom = 0;
    bool success = false;

    if (!gcmz_aviutl2_get_display_zoom(&display_zoom, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_analyze_run(ctx->capture, display_zoom, &capture, NULL, NULL, &err)) {
      gcmz_logf_error(&err, "%s", "%s", "failed to capture for debug output");
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    log_analyze(&capture);
    debug_output_info(ctx);
    success = true;

  cleanup:
    if (!success) {
      OV_ERROR_REPORT(&err, NULL);
    }
    break;
  }
  }
}

static void tray_menu_test_complete_external_api(struct gcmz_api_request_params *const params) {
  gcmz_logf_info(NULL, "%s", "%s", "API request test completed");
  if (params && params->files) {
    gcmz_file_list_destroy(&params->files);
  }
}

static void tray_menu_test_external_api(void *userdata, struct gcmz_tray_callback_event *const event) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
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
    if (!ctx) {
      break;
    }
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
          .userdata = ctx,
      };

      request_api(&params, tray_menu_test_complete_external_api);
      files = NULL;
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

static void tray_menu_test_complete_external_api_object(struct gcmz_api_request_params *const params) {
  gcmz_logf_info(NULL, "%s", "%s", "API request test (object) completed");
  if (params && params->files) {
    gcmz_file_list_destroy(&params->files);
  }
}

static void tray_menu_test_external_api_object(void *userdata, struct gcmz_tray_callback_event *const event) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  static wchar_t label[64];
  switch (event->type) {
  case gcmz_tray_callback_query_info:
    if (label[0] == L'\0') {
      ov_snprintf_wchar(label, sizeof(label) / sizeof(label[0]), L"%s", L"%s", "Test API Request (Object)");
    }
    event->result.query_info.label = label;
    event->result.query_info.enabled = true;
    break;

  case gcmz_tray_callback_clicked: {
    if (!ctx) {
      break;
    }
    struct ov_error err = {0};
    struct ovl_file *file = NULL;
    wchar_t *temp_path = NULL;
    struct gcmz_file_list *files = NULL;
    bool result = false;

    {
      // clang-format off
      char const *utf8_text =
          "[0]\r\n"
          "layer=3\r\n"
          "frame=187,267\r\n"
          "[0.0]\r\n"
          "effect.name=図形\r\n"
          "図形の種類=四角形\r\n"
          "サイズ=100\r\n"
          "縦横比=0.00\r\n"
          "ライン幅=4000\r\n"
          "色=ffffff\r\n"
          "角を丸くする=0\r\n"
          "[0.1]\r\n"
          "effect.name=標準描画\r\n"
          "X=0.00\r\n"
          "Y=0.00\r\n"
          "Z=0.00\r\n"
          "Group=1\r\n"
          "中心X=0.00\r\n"
          "中心Y=0.00\r\n"
          "中心Z=0.00\r\n"
          "X軸回転=0.00\r\n"
          "Y軸回転=0.00\r\n"
          "Z軸回転=0.00\r\n"
          "拡大率=100.000\r\n"
          "縦横比=0.000\r\n"
          "透明度=0.00\r\n"
          "合成モード=通常\r\n"
          "[1]\r\n"
          "layer=2\r\n"
          "frame=187,267\r\n"
          "[1.0]\r\n"
          "effect.name=図形\r\n"
          "図形の種類=円\r\n"
          "サイズ=100\r\n"
          "縦横比=0.00\r\n"
          "ライン幅=4000\r\n"
          "色=ffffff\r\n"
          "角を丸くする=0\r\n"
          "[1.1]\r\n"
          "effect.name=標準描画\r\n"
          "X=0.00\r\n"
          "Y=0.00\r\n"
          "Z=0.00\r\n"
          "Group=1\r\n"
          "中心X=0.00\r\n"
          "中心Y=0.00\r\n"
          "中心Z=0.00\r\n"
          "X軸回転=0.00\r\n"
          "Y軸回転=0.00\r\n"
          "Z軸回転=0.00\r\n"
          "拡大率=100.000\r\n"
          "縦横比=0.000\r\n"
          "透明度=0.00\r\n"
          "合成モード=通常\r\n";
      // clang-format on
      size_t const utf8_len = strlen(utf8_text);
      size_t written = 0;

      if (!ovl_file_create_temp(L"test.object", &file, &temp_path, &err)) {
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

      if (!gcmz_file_list_add_temporary(files, temp_path, L"application/x-aviutl-object", &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }

      struct gcmz_api_request_params params = {
          .files = files,
          .layer = 0,
          .frame_advance = 0,
          .use_exo_converter = false,
          .err = &err,
          .userdata = ctx,
      };

      request_api(&params, tray_menu_test_complete_external_api_object);
      files = NULL;
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
      gcmz_logf_error(&err, "%s", "%s", "failed to test API request (object)");
      OV_ERROR_REPORT(&err, NULL);
    }
    break;
  }
  }
}
#endif // NDEBUG

static bool
extract_from_dataobj(void *dataobj, struct gcmz_file_list *dest, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!gcmz_dataobj_extract_from_dataobj(dataobj, dest, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static bool schedule_cleanup(wchar_t const *const path, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!gcmz_delayed_cleanup_schedule_file(path, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static bool get_project_data_utf8(struct aviutl2_edit_info *edit_info,
                                  char **project_path,
                                  void *userdata,
                                  struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  bool success = false;

  if (edit_info) {
    ctx->edit->get_edit_info(edit_info, sizeof(*edit_info));
  }
  if (project_path) {
    if (ctx->project_path && ctx->project_path[0] != L'\0') {
      size_t const len = wcslen(ctx->project_path);
      size_t const utf8_len = ov_wchar_to_utf8_len(ctx->project_path, len);
      if (utf8_len == 0) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
        goto cleanup;
      }
      if (!OV_ARRAY_GROW(project_path, utf8_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      ov_wchar_to_utf8(ctx->project_path, len, *project_path, utf8_len + 1, NULL);
    } else {
      *project_path = NULL;
    }
  }
  success = true;
cleanup:
  return success;
}

static wchar_t *get_save_path(wchar_t const *filename, void *userdata, struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  wchar_t *const r = gcmz_config_get_save_path(ctx->config, filename, err);
  if (!r) {
    OV_ERROR_ADD_TRACE(err);
    return NULL;
  }
  return r;
}

static bool copy_file(wchar_t const *source_file, wchar_t **final_file, void *userdata, struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  enum gcmz_processing_mode mode;
  if (!gcmz_config_get_processing_mode(ctx->config, &mode, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  // gcmz_copy does not necessarily copy the file.
  // If a file with the same hash value exists at the destination, it returns that path.
  if (!gcmz_copy(source_file, mode, get_save_path, ctx, final_file, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static void lua_debug_print(void *userdata, char const *message) {
  (void)userdata;
  gcmz_logf_info(NULL, NULL, "[LUA] %1$hs", message);
}

static bool register_lua_api(struct lua_State *const L, void *userdata, struct ov_error *const err) {
  (void)userdata;
  if (!gcmz_lua_api_register(L, err)) {
    OV_ERROR_ADD_TRACE(err);
    return false;
  }
  return true;
}

static char *create_temp_file_utf8(void *userdata, char const *filename, struct ov_error *err) {
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

static char *get_save_path_utf8(void *userdata, char const *filename, struct ov_error *err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
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
    dest_path_w = gcmz_config_get_save_path(ctx->config, filename_w, err);
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

static size_t
get_window_list(struct gcmz_window_info *windows, size_t window_len, void *userdata, struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!windows || window_len == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return SIZE_MAX;
  }
  if (!ctx || !ctx->window_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
    return SIZE_MAX;
  }
  size_t num_items = 0;
  struct gcmz_window_info const *const items = gcmz_window_list_get(ctx->window_list, &num_items);
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
static bool capture_window_core(
    HWND window, uint8_t **const data, int *const width, int *const height, struct ov_error *const err) {
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
static bool
capture_window(void *window, uint8_t **data, int *width, int *height, void *userdata, struct ov_error *const err) {
  (void)userdata;
  return capture_window_core((HWND)window, data, width, height, err);
}

/**
 * Callback function invoked when the active window state changes.
 *
 * This callback is called frequently by gcmz_do_init, so performance is important.
 * Heavy processing should be avoided to prevent UI lag.
 *
 * @param userdata User-defined data pointer (struct gcmzdrops*)
 */
static void on_change_activate(void *const userdata) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  enum {
    max_windows = 8,
  };

  struct gcmz_window_info windows[max_windows] = {0};
  bool success = false;
  struct ov_error err = {0};

  {
    if (!ctx || !ctx->window_list) {
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
    switch (gcmz_window_list_update(ctx->window_list, windows, found, &err)) {
    case ov_false:
      // No changes, do nothing
      success = true;
      break;
    case ov_true:
      if (ctx->drop) {
        for (size_t i = 0; i < found; ++i) {
          if (!gcmz_drop_register_window(ctx->drop, windows[i].window, &err)) {
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
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    return;
  }
  if (ctx->tray) {
    gcmz_tray_destroy(&ctx->tray);
  }
  if (ctx->api) {
    gcmz_api_destroy(&ctx->api);
  }
  if (ctx->drop) {
    gcmz_drop_destroy(&ctx->drop);
  }
  if (ctx->config) {
    gcmz_config_destroy(&ctx->config);
  }
  gcmz_analyze_destroy(&ctx->capture);
  if (ctx->window_list) {
    gcmz_window_list_destroy(&ctx->window_list);
  }
  if (ctx->project_path) {
    OV_ARRAY_DESTROY(&ctx->project_path);
  }
  gcmz_delayed_cleanup_exit();
  gcmz_temp_remove_directory();
  gcmz_do_exit();
  gcmz_aviutl2_cleanup();
  gcmz_do_sub_destroy(&ctx->do_sub);
  if (ctx->plugin_state != gcmzdrops_plugin_state_not_initialized) {
    cnd_destroy(&ctx->init_cond);
    mtx_destroy(&ctx->init_mtx);
    ctx->plugin_state = gcmzdrops_plugin_state_not_initialized;
  }
}

static NATIVE_CHAR *get_project_path(void *userdata) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx || !ctx->project_path || ctx->project_path[0] == L'\0') {
    return NULL;
  }
  size_t const len = wcslen(ctx->project_path);
  NATIVE_CHAR *result = NULL;
  if (!OV_ARRAY_GROW(&result, len + 1)) {
    return NULL;
  }
  wcscpy(result, ctx->project_path);
  return result;
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

static char *get_script_directory_utf8(void *userdata, struct ov_error *err) {
  (void)userdata;
  wchar_t *script_dir_w = NULL;
  char *script_dir = NULL;

  {
    if (!get_script_directory_path(&script_dir_w, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    int const dest_len = WideCharToMultiByte(CP_UTF8, 0, script_dir_w, -1, NULL, 0, NULL, NULL);
    if (dest_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&script_dir, (size_t)dest_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, script_dir_w, -1, script_dir, dest_len, NULL, NULL) <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }

  OV_ARRAY_DESTROY(&script_dir_w);
  return script_dir;

cleanup:
  if (script_dir_w) {
    OV_ARRAY_DESTROY(&script_dir_w);
  }
  if (script_dir) {
    OV_ARRAY_DESTROY(&script_dir);
  }
  return NULL;
}

/**
 * @brief Context for get_media_info_utf8 edit section callback
 */
struct get_media_info_context {
  wchar_t const *filepath_w;
  struct gcmz_lua_api_media_info *info;
  bool success;
};

/**
 * @brief Edit section callback for getting media info via official API
 */
static void get_media_info_edit_section(void *param, struct aviutl2_edit_section *edit) {
  struct get_media_info_context *const ctx = (struct get_media_info_context *)param;
  if (!ctx || !edit || !edit->get_media_info) {
    return;
  }
  struct aviutl2_media_info media_info = {0};
  if (edit->get_media_info(ctx->filepath_w, &media_info, sizeof(media_info))) {
    ctx->info->video_track_num = media_info.video_track_num;
    ctx->info->audio_track_num = media_info.audio_track_num;
    ctx->info->total_time = media_info.total_time;
    ctx->info->width = media_info.width;
    ctx->info->height = media_info.height;
    ctx->success = true;
  }
}

static bool
get_media_info_utf8(char const *filepath, struct gcmz_lua_api_media_info *info, void *userdata, struct ov_error *err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!filepath || !info) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (!ctx || !ctx->edit || !ctx->edit->call_edit_section_param) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "edit handle not available");
    return false;
  }

  wchar_t *filepath_w = NULL;
  bool result = false;

  {
    int const filepath_len = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    if (filepath_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&filepath_w, (size_t)filepath_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, filepath, -1, filepath_w, filepath_len) <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    struct get_media_info_context gmic = {
        .filepath_w = filepath_w,
        .info = info,
        .success = false,
    };

    if (!ctx->edit->call_edit_section_param(&gmic, get_media_info_edit_section)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "call_edit_section_param failed");
      goto cleanup;
    }

    if (!gmic.success) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "unsupported media file");
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (filepath_w) {
    OV_ARRAY_DESTROY(&filepath_w);
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

static void delayed_initialization(void *userdata) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    return;
  }

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

    mtx_lock(&ctx->init_mtx);
    while (ctx->plugin_state == gcmzdrops_plugin_state_initializing) {
      cnd_wait(&ctx->init_cond, &ctx->init_mtx);
    }
    enum gcmzdrops_plugin_state const state = ctx->plugin_state;
    mtx_unlock(&ctx->init_mtx);

    if (state != gcmzdrops_plugin_state_registered) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
      goto cleanup;
    }

    // Wait until AviUtl ready
    if (!ctx->unknown_binary) {
      while (!gcmz_aviutl2_internal_object_ptr_is_valid()) {
        Sleep(50);
      }
    }

    bool external_api_enabled = false;
    if (!gcmz_config_get_external_api(ctx->config, &external_api_enabled, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to get external API setting"));
      goto cleanup;
    }
    if (!external_api_enabled) {
      success = true;
      goto cleanup;
    }

    if (!create_external_api(ctx, false, &err)) {
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
  gcmz_do(on_change_activate, ctx);
}

// =============================================================================
// Public API functions (called from dllmain.c)
// =============================================================================

bool gcmzdrops_create(struct gcmzdrops **const ctx,
                      struct gcmz_lua_context *const lua_ctx,
                      uint32_t const version,
                      struct ov_error *const err) {
  if (!ctx || !lua_ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  // Check minimum required AviUtl ExEdit2 version
  if (version < 2002300) {
    OV_ERROR_SETF(err,
                  ov_error_type_generic,
                  ov_error_generic_fail,
                  "%1$s",
                  gettext("GCMZDrops requires AviUtl ExEdit2 %1$s or later."),
                  "version2.0beta23");
    return false;
  }

  struct gcmzdrops *c = NULL;
  HWND main_window = NULL;
  wchar_t *script_dir = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&c, 1, sizeof(struct gcmzdrops))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    *c = (struct gcmzdrops){0};
    c->lua_ctx = lua_ctx;
    c->aviutl2_version = version;

    // Initialize mutex and condition variable
    if (mtx_init(&c->init_mtx, mtx_plain) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (cnd_init(&c->init_cond) != thrd_success) {
      mtx_destroy(&c->init_mtx);
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    c->plugin_state = gcmzdrops_plugin_state_initializing;

    c->do_sub = gcmz_do_sub_create(err);
    if (!c->do_sub) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    gcmz_do_sub_do(c->do_sub, delayed_initialization, c);

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
      c->unknown_binary = true;
      gcmz_logf_warn(
          NULL, "%s", "%s", gettext("unknown AviUtl ExEdit2 version detected. some features will be disabled."));
      break;
    case gcmz_aviutl2_status_error:
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
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

    c->config = gcmz_config_create(
        &(struct gcmz_config_options){
            .project_path_provider = get_project_path,
            .project_path_provider_userdata = c,
        },
        err);
    if (!c->config) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!gcmz_config_load(c->config, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!gcmz_analyze_create(&c->capture,
                             &(struct gcmz_analyze_options){
                                 .capture = capture_window,
                                 .get_window_list = get_window_list,
                                 .get_style = get_style,
                                 .userdata = c,
                             },
                             err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    c->window_list = gcmz_window_list_create(err);
    if (!c->window_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!get_script_directory_path(&script_dir, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    gcmz_lua_api_set_options(&(struct gcmz_lua_api_options){
        .temp_file_provider = create_temp_file_utf8,
        .save_path_provider = get_save_path_utf8,
        .get_project_data = get_project_data_utf8,
        .debug_print = lua_debug_print,
        .script_dir_provider = get_script_directory_utf8,
        .get_media_info = get_media_info_utf8,
        .userdata = c,
        .aviutl2_ver = c->aviutl2_version,
        .gcmz_ver = GCMZ_VERSION_UINT32,
    });

    if (!c->lua_ctx) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
      goto cleanup;
    }
    if (!gcmz_lua_setup(c->lua_ctx,
                        &(struct gcmz_lua_options){
                            .script_dir = script_dir,
                            .api_register_callback = register_lua_api,
                            .schedule_cleanup_callback = schedule_cleanup,
                            .create_temp_file_callback = create_temp_file_utf8,
                        },
                        err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    c->drop = gcmz_drop_create(extract_from_dataobj,
                               schedule_cleanup,
                               copy_file,
                               c,          // callback_userdata
                               c->lua_ctx, // lua_context
                               err);
    if (!c->drop) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Initial window list update and drop registration
    // Use gcmz_do to ensure this runs on the window thread for proper subclass installation
    gcmz_do(on_change_activate, c);

    {
      HICON icon = load_icon(err);
      if (!icon) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      c->tray = gcmz_tray_create(icon, err);
      if (!c->tray) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
    }

    if (!gcmz_tray_add_menu_item(c->tray, tray_menu_debug_capture, c, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

#ifndef NDEBUG
    if (!gcmz_tray_add_menu_item(c->tray, tray_menu_debug_output, c, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!gcmz_tray_add_menu_item(c->tray, tray_menu_test_external_api, c, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!gcmz_tray_add_menu_item(c->tray, tray_menu_test_external_api_object, c, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
#endif

    update_tray_visibility(c);
  }

  *ctx = c;
  c = NULL;
  result = true;

cleanup:
  if (script_dir) {
    OV_ARRAY_DESTROY(&script_dir);
  }
  if (!result) {
    if (c && c->do_sub) {
      mtx_lock(&c->init_mtx);
      c->plugin_state = gcmzdrops_plugin_state_failed;
      cnd_signal(&c->init_cond);
      mtx_unlock(&c->init_mtx);
    }

    gcmzdrops_destroy(&c);
  }
  return result;
}

void gcmzdrops_destroy(struct gcmzdrops **const ctx) {
  if (!ctx || !*ctx) {
    return;
  }
  finalize(*ctx);
  OV_FREE(ctx);
}

void gcmzdrops_on_project_load(struct gcmzdrops *const ctx, wchar_t const *const project_path) {
  if (!ctx) {
    return;
  }
  struct ov_error err = {0};
  bool success = false;
  size_t const path_len = project_path ? wcslen(project_path) : 0;
  if (!path_len) {
    if (ctx->project_path) {
      ctx->project_path[0] = L'\0';
    }
  } else {
    if (!OV_ARRAY_GROW(&ctx->project_path, path_len + 1)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(ctx->project_path, project_path);
  }
  gcmz_do_sub_do(ctx->do_sub, update_api_project_data, ctx);
  success = true;
cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to handle project load"));
    OV_ERROR_DESTROY(&err);
  }
}

static void paste_from_clipboard_impl(void *userdata) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    return;
  }
  struct ov_error err = {0};
  IDataObject *dataobj = NULL;
  bool success = false;

  {
    void *window = NULL;
    int x = 0;
    int y = 0;
    if (!gcmz_drop_get_right_click_position(ctx->drop, &window, &x, &y, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    HRESULT hr = OleGetClipboard(&dataobj);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(&err, hr);
      goto cleanup;
    }
    if (!dataobj) {
      OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "no data in clipboard");
      goto cleanup;
    }
    if (!gcmz_drop_simulate_drop(ctx->drop, window, dataobj, x, y, false, false, &err)) {
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
}

void gcmzdrops_paste_from_clipboard(struct gcmzdrops *const ctx) {
  if (!ctx) {
    return;
  }
  gcmz_do_sub_do(ctx->do_sub, paste_from_clipboard_impl, ctx);
}

void gcmzdrops_register(struct gcmzdrops *const ctx, struct aviutl2_host_app_table *const host) {
  if (!ctx || !host) {
    return;
  }

  struct aviutl2_edit_handle *const edit = host->create_edit_handle();
  if (edit) {
    ctx->edit = edit;
  }

  // Signal delayed initialization thread that RegisterPlugin is complete
  mtx_lock(&ctx->init_mtx);
  ctx->plugin_state = gcmzdrops_plugin_state_registered;
  cnd_signal(&ctx->init_cond);
  mtx_unlock(&ctx->init_mtx);
}
