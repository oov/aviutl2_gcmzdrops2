
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

#include "file_ext.h"

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

#include "api.h"
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
#include "temp.h"
#include "tray.h"
#include "version.h"
#include "window_list.h"

#ifndef GCMZ_SCRIPT_SUBDIR
#  define GCMZ_SCRIPT_SUBDIR "GCMZScript"
#endif

/**
 * @brief Find all aviutl2Manager windows in the current process
 *
 * @param window [out] Array to store found window handles
 * @param window_len Maximum number of handles to store
 * @param err [out] Error information on failure
 * @return Number of windows found, SIZE_MAX on error
 */
static size_t find_manager_windows(HWND *window, size_t const window_len, struct ov_error *const err) {
  if (!window || window_len == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return SIZE_MAX;
  }

  DWORD const pid = GetCurrentProcessId();
  wchar_t const class_name[] = L"aviutl2Manager";

  size_t count = 0;
  HWND h = NULL;
  DWORD wpid;
  while ((h = FindWindowExW(NULL, h, class_name, NULL)) != NULL) {
    GetWindowThreadProcessId(h, &wpid);
    if (wpid != pid) {
      continue;
    }
    if (count >= window_len) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "too many AviUtl2 manager windows found");
      return SIZE_MAX;
    }
    window[count++] = h;
  }
  if (count < window_len) {
    window[count] = NULL;
  }
  return count;
}

/**
 * @brief Get a suitable owner window for error dialogs
 *
 * This function is called lazily when an error dialog needs to be shown.
 *
 * @return HWND to use as owner window, or NULL if not found
 */
static HWND get_error_dialog_owner_window(void) {
  HWND wnd = NULL;
  if (find_manager_windows(&wnd, 1, NULL) && wnd) {
    return wnd;
  }
  return NULL;
}

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
  struct gcmz_window_list *window_list;
  struct gcmz_do_sub *do_sub;

  struct aviutl2_edit_handle *edit;
  struct aviutl2_edit_section *current_edit_section; ///< Current edit section when in Lua callback (deadlock avoidance)
  uint32_t aviutl2_version;
  wchar_t *project_path;

  enum gcmzdrops_plugin_state plugin_state;
  mtx_t init_mtx;
  cnd_t init_cond;
};

/**
 * @brief Calculate the number of layers an .object file occupies
 *
 * Parses the .object file and finds the minimum and maximum layer values
 * to determine how many layers the objects span.
 *
 * @param filepath Path to the .object file
 * @param layer_count [out] Number of layers occupied (max_layer - min_layer + 1)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
static bool get_object_layer_count(wchar_t const *const filepath, int *const layer_count, struct ov_error *const err) {
  if (!filepath || !layer_count) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_ini_reader *reader = NULL;
  bool result = false;
  int min_layer = INT_MAX;
  int max_layer = INT_MIN;
  bool found_layer = false;

  {
    if (!gcmz_ini_reader_create(&reader, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!gcmz_ini_reader_load_file(reader, filepath, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Iterate through all sections to find layer values
    // Object sections are named [0], [1], etc. with layer=N entries
    struct gcmz_ini_iter section_iter = {0};
    while (gcmz_ini_reader_iter_sections(reader, &section_iter)) {
      // Check if section name is a number (e.g., "0", "1", "2")
      // Object sections have format [N] where N is the object index
      bool is_object_section = true;
      for (size_t i = 0; i < section_iter.name_len; ++i) {
        if (section_iter.name[i] < '0' || section_iter.name[i] > '9') {
          is_object_section = false;
          break;
        }
      }
      if (!is_object_section || section_iter.name_len == 0) {
        continue;
      }

      // Create null-terminated section name for lookup
      char section_name[32];
      if (section_iter.name_len >= sizeof(section_name)) {
        continue;
      }
      memcpy(section_name, section_iter.name, section_iter.name_len);
      section_name[section_iter.name_len] = '\0';

      // Get the layer value from this section
      struct gcmz_ini_value layer_value = gcmz_ini_reader_get_value(reader, section_name, "layer");
      if (!layer_value.ptr || layer_value.size == 0) {
        continue;
      }

      // Parse the layer number (format: "layer=N" where N is 0-based)
      int layer = 0;
      for (size_t i = 0; i < layer_value.size && layer_value.ptr[i] >= '0' && layer_value.ptr[i] <= '9'; ++i) {
        layer = layer * 10 + (layer_value.ptr[i] - '0');
      }

      found_layer = true;
      if (layer < min_layer) {
        min_layer = layer;
      }
      if (layer > max_layer) {
        max_layer = layer;
      }
    }

    if (!found_layer) {
      // No layers found - treat as single layer
      *layer_count = 1;
    } else {
      *layer_count = max_layer - min_layer + 1;
    }
  }

  result = true;

cleanup:
  gcmz_ini_reader_destroy(&reader);
  return result;
}

/**
 * @brief Context for external API drop completion callback
 */
struct external_api_drop_context {
  struct gcmzdrops *ctx;                           ///< Parent context
  struct aviutl2_edit_section *edit;               ///< Edit section (valid during callback)
  int layer;                                       ///< Target layer (0-based)
  int frame;                                       ///< Target frame position
  int frame_advance;                               ///< Frame advance after drop
  int margin;                                      ///< Margin parameter for collision handling (-1 = disabled)
  gcmz_api_request_complete_func request_complete; ///< Callback to signal request completion
  struct gcmz_api_request_params *request_params;  ///< Original request params for completion callback
};

/**
 * @brief Context for clipboard paste completion callback
 */
struct clipboard_paste_context {
  struct gcmzdrops *ctx;             ///< Parent context
  struct aviutl2_edit_section *edit; ///< Edit section (valid during callback)
  int layer;                         ///< Target layer (0-based, captured early)
  int frame;                         ///< Target frame position (captured early)
};

/**
 * @brief Create text object from file
 *
 * Reads a text file, creates a text object alias, and inserts it into the timeline.
 *
 * @param file_path Path to text file
 * @param edit Edit section
 * @param layer Layer number (0-based)
 * @param frame Frame position
 * @param err Error output
 * @return Object handle on success, NULL on failure
 */
static aviutl2_object_handle create_text_object(wchar_t const *const file_path,
                                                struct aviutl2_edit_section *const edit,
                                                int const layer,
                                                int const frame,
                                                struct ov_error *const err) {
  if (!file_path || !edit || !edit->create_object_from_alias) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  struct ovl_source *source = NULL;
  char *content = NULL;
  char *escaped_content = NULL;
  char *alias = NULL;
  aviutl2_object_handle obj = NULL;

  {
    if (!ovl_source_file_create(file_path, &source, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const file_size = ovl_source_size(source);
    if (file_size == UINT64_MAX || file_size > SIZE_MAX - 1) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "file size too large or invalid");
      goto cleanup;
    }

    size_t const size = (size_t)file_size;
    if (!OV_ARRAY_GROW(&content, size + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const bytes_read = ovl_source_read(source, content, 0, size);
    if (bytes_read != size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read file");
      goto cleanup;
    }
    content[size] = '\0';

    // Escape newlines in content for alias format (replace \n with \\n)
    size_t escaped_len = 0;
    for (size_t idx = 0; content[idx]; ++idx) {
      escaped_len += (content[idx] == '\n') ? 2 : 1;
    }

    if (!OV_ARRAY_GROW(&escaped_content, escaped_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t j = 0;
    for (size_t idx = 0; content[idx]; ++idx) {
      if (content[idx] == '\n') {
        escaped_content[j++] = '\\';
        escaped_content[j++] = 'n';
      } else {
        escaped_content[j++] = content[idx];
      }
    }
    escaped_content[j] = '\0';

    // Create alias string
    static char const *const alias_format = "[Object]\n"
                                            "[Object.0]\n"
                                            "effect.name=テキスト\n"
                                            "テキスト=%s\n";
    int const alias_len = ov_snprintf_char(NULL, 0, NULL, alias_format, escaped_content);
    if (alias_len <= 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to format alias");
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&alias, (size_t)alias_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    ov_snprintf_char(alias, (size_t)alias_len + 1, NULL, alias_format, escaped_content);

    obj = edit->create_object_from_alias(alias, layer, frame, 0);
    if (!obj) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "create_object_from_alias failed");
    }
  }

cleanup:
  OV_ARRAY_DESTROY(&alias);
  OV_ARRAY_DESTROY(&escaped_content);
  OV_ARRAY_DESTROY(&content);
  ovl_source_destroy(&source);

  return obj;
}

/**
 * @brief Create an object from .object file
 *
 * Reads an .object file and creates an AviUtl2 object using its content.
 *
 * @param file_path Path to the .object file
 * @param edit Edit section interface
 * @param layer Layer number (0-based)
 * @param frame Frame position
 * @param err Error output
 * @return Object handle on success, NULL on failure
 */
static aviutl2_object_handle create_object_from_file(wchar_t const *const file_path,
                                                     struct aviutl2_edit_section *const edit,
                                                     int const layer,
                                                     int const frame,
                                                     struct ov_error *const err) {
  if (!file_path || !edit || !edit->create_object_from_alias) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  struct ovl_source *source = NULL;
  char *content = NULL;
  aviutl2_object_handle obj = NULL;

  {
    if (!ovl_source_file_create(file_path, &source, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const file_size = ovl_source_size(source);
    if (file_size == UINT64_MAX || file_size > SIZE_MAX - 1) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "file size too large or invalid");
      goto cleanup;
    }

    size_t const size = (size_t)file_size;
    if (!OV_ARRAY_GROW(&content, size + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (ovl_source_read(source, content, 0, size) != size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read file");
      goto cleanup;
    }
    content[size] = '\0';

    obj = edit->create_object_from_alias(content, layer, frame, 1);
    if (!obj) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "create_object_from_alias failed");
    }
  }

cleanup:
  if (content) {
    OV_ARRAY_DESTROY(&content);
  }
  if (source) {
    ovl_source_destroy(&source);
  }
  return obj;
}

/**
 * @brief Insert files from file list into timeline
 *
 * Processes each file in the list and inserts it into the timeline:
 * - .object files are inserted using create_object_from_alias
 * - .txt files are inserted as text objects
 * - Media files are inserted using create_object_from_media_file
 * - Other files are skipped with a warning
 *
 * @param file_list List of files to insert
 * @param edit Edit section
 * @param start_layer Starting layer (0-based)
 * @param frame Frame position
 * @return Result first object handle on success, NULL on failure
 */
static aviutl2_object_handle insert_files_to_timeline(struct gcmz_file_list const *const file_list,
                                                      struct aviutl2_edit_section *const edit,
                                                      int const start_layer,
                                                      int const frame,
                                                      struct ov_error *const err) {
  if (!file_list || !edit) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  size_t const count = gcmz_file_list_count(file_list);
  aviutl2_object_handle first_obj = NULL;
  int current_layer = start_layer;

  for (size_t i = 0; i < count; ++i) {
    struct gcmz_file const *const file = gcmz_file_list_get(file_list, i);
    if (!file || !file->path) {
      gcmz_logf_warn(NULL, "%1$ls", gettext("skipping invalid file in list"));
      continue;
    }
    wchar_t const *const ext = wcsrchr(file->path, L'.');
    if (!ext) {
      gcmz_logf_warn(NULL, "%1$ls", gettext("skipping file with no extension: %1$ls"), file->path);
      continue;
    }
    if (gcmz_extension_equals(ext, L".object")) {
      aviutl2_object_handle obj = create_object_from_file(file->path, edit, current_layer, frame, err);
      if (!obj) {
        gcmz_logf_warn(err, "%1$ls", gettext("failed to insert file: %1$ls"), file->path);
        OV_ERROR_DESTROY(err);
        continue;
      }
      if (!first_obj) {
        first_obj = obj;
      }
      int layer_count = 1;
      if (get_object_layer_count(file->path, &layer_count, NULL)) {
        current_layer += layer_count;
      } else {
        ++current_layer;
      }
      continue;
    }
    if (gcmz_extension_equals(ext, L".txt")) {
      aviutl2_object_handle obj = create_text_object(file->path, edit, current_layer, frame, err);
      if (!obj) {
        gcmz_logf_warn(err, "%1$ls", gettext("failed to insert file: %1$ls"), file->path);
        OV_ERROR_DESTROY(err);
        continue;
      }
      if (!first_obj) {
        first_obj = obj;
      }
      ++current_layer;
      continue;
    }
    if (edit->is_support_media_file(file->path, false)) {
      aviutl2_object_handle obj = edit->create_object_from_media_file(file->path, current_layer, frame, 0);
      if (!obj) {
        gcmz_logf_warn(err, "%1$ls", gettext("failed to insert file: %1$ls"), file->path);
        OV_ERROR_DESTROY(err);
        continue;
      }
      if (!first_obj) {
        first_obj = obj;
      }
      ++current_layer;
      continue;
    }
    gcmz_logf_warn(NULL, "%1$ls", gettext("skipping unsupported file: %1$ls"), file->path);
  }

  return first_obj;
}

/**
 * @brief Completion callback for clipboard paste operations
 *
 * Called after Lua processing completes. Receives the processed file list
 * and handles insertion via official API using stored layer/frame position.
 */
static void on_clipboard_paste_completion(struct gcmz_file_list const *const file_list, void *const userdata) {
  if (!file_list) {
    return;
  }
  struct clipboard_paste_context *paste_ctx = (struct clipboard_paste_context *)userdata;
  if (!paste_ctx || !paste_ctx->ctx || !paste_ctx->edit) {
    return;
  }
  struct ov_error err = {0};
  aviutl2_object_handle obj =
      insert_files_to_timeline(file_list, paste_ctx->edit, paste_ctx->layer, paste_ctx->frame, &err);
  if (!obj) {
    gcmz_logf_error(NULL, "%1$ls", "%1$ls", gettext("failed to insert files into timeline"));
    OV_ERROR_DESTROY(&err);
    return;
  }
  paste_ctx->edit->set_focus_object(obj);
}

/**
 * @brief Completion callback for external API drop operations
 *
 * Called after Lua processing completes. Receives the processed file list
 * and handles insertion via official API using stored edit section.
 */
static void on_drop_completion(struct gcmz_file_list const *const file_list, void *const userdata) {
  if (!file_list) {
    return;
  }
  gcmz_logf_verbose(NULL, NULL, "on_drop_completion called");

  struct external_api_drop_context *api_ctx = (struct external_api_drop_context *)userdata;
  if (!api_ctx || !api_ctx->ctx || !api_ctx->edit) {
    return;
  }

  struct aviutl2_edit_section *const edit = api_ctx->edit;
  int const layer = api_ctx->layer;
  int frame = api_ctx->frame;

  gcmz_logf_verbose(NULL, "%1$d%2$d", "external API drop target: layer %1$d, frame %2$d", layer, frame);

  // Check for collision at insertion position and adjust if needed
  if (api_ctx->margin >= 0 && layer >= 0 && edit->find_object && edit->get_object_layer_frame) {
    // `layer` is 0-based for find_object
    int const target_layer = layer;
    aviutl2_object_handle obj = edit->find_object(target_layer, frame);
    if (obj) {
      struct aviutl2_object_layer_frame olf = edit->get_object_layer_frame(obj);
      if (frame >= olf.start && frame <= olf.end) {
        // Collision detected - try to adjust
        int const new_frame = olf.end + 1 + api_ctx->margin;

        // Check if the new position also has a collision
        aviutl2_object_handle next_obj = edit->find_object(target_layer, new_frame);
        if (next_obj) {
          struct aviutl2_object_layer_frame next_olf = edit->get_object_layer_frame(next_obj);
          if (new_frame >= next_olf.start && new_frame <= next_olf.end) {
            // Still collision after adjustment - cannot solve
            gcmz_logf_error(NULL,
                            "%1$hs",
                            "%1$hs",
                            gettext("insertion position collision detected, cannot insert with specified margin"));
            return;
          }
        }

        gcmz_logf_verbose(
            NULL, "%1$d%2$d", "collision detected, adjusting insertion frame from %1$d to %2$d", frame, new_frame);
        frame = new_frame;
      }
    }
  }

  struct ov_error err = {0};
  aviutl2_object_handle const obj = insert_files_to_timeline(file_list, edit, layer, frame, &err);
  if (!obj) {
    gcmz_logf_error(NULL, "%1$ls", "%1$ls", gettext("failed to insert files into timeline"));
    OV_ERROR_DESTROY(&err);
    return;
  }
  edit->set_focus_object(obj);
  if (api_ctx->frame_advance != 0) {
    int const move_to = frame + api_ctx->frame_advance;
    edit->set_cursor_layer_frame(layer - 1, move_to);
  }
}

/**
 * @brief Context for request_api edit section callback
 */
struct request_api_context {
  struct gcmzdrops *ctx;
  struct gcmz_api_request_params *params;
  gcmz_api_request_complete_func complete;
};

/**
 * @brief Edit section callback for request_api
 *
 * Performs Lua processing and file insertion within a single EDIT_SECTION.
 */
static void request_api_edit_section(void *param, struct aviutl2_edit_section *edit) {
  struct request_api_context *const rac = (struct request_api_context *)param;
  if (!rac || !rac->ctx || !rac->params || !edit) {
    return;
  }

  struct gcmzdrops *const ctx = rac->ctx;
  struct gcmz_api_request_params *const params = rac->params;
  struct ov_error err = {0};

  int layer = params->layer;
  int frame = edit->info->frame;

  // Handle layer value:
  // - layer < 0: relative to display_layer_start (e.g., -1 = first visible layer)
  // - layer = 0: use currently selected layer
  // - layer > 0: absolute layer number (1-based input)
  if (layer < 0) {
    // Convert negative (relative) layer to absolute 0-based layer
    layer = edit->info->display_layer_start + (-layer) - 1;
  } else if (layer == 0) {
    layer = edit->info->layer; // edit->info->layer is already 0-based
  } else {
    layer = layer - 1; // convert to 0-based
  }

  ctx->current_edit_section = edit;
  bool const r = gcmz_drop_simulate_drop(ctx->drop,
                                         params->files,
                                         params->use_exo_converter,
                                         on_drop_completion,
                                         &(struct external_api_drop_context){
                                             .ctx = ctx,
                                             .edit = edit,
                                             .layer = layer,
                                             .frame = frame,
                                             .frame_advance = params->frame_advance,
                                             .margin = params->margin,
                                             .request_complete = rac->complete,
                                             .request_params = params,
                                         },
                                         &err);
  ctx->current_edit_section = NULL;

  if (!r) {
    OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "simulated drop failed");
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to drop from external API request"));
    OV_ERROR_DESTROY(&err);
    return;
  }
}

static void request_api(struct gcmz_api_request_params *const params, gcmz_api_request_complete_func const complete) {
  if (!params || !complete) {
    return;
  }
  struct gcmzdrops *const ctx = (struct gcmzdrops *)params->userdata;
  if (!ctx || !ctx->edit || !ctx->edit->call_edit_section_param) {
    complete(params);
    return;
  }
  if (!gcmz_file_list_count(params->files)) {
    complete(params);
    return;
  }
  ctx->edit->call_edit_section_param(
      &(struct request_api_context){
          .ctx = ctx,
          .params = params,
          .complete = complete,
      },
      request_api_edit_section);
  complete(params);
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
  if (!gcmz_api_set_project_data(ctx->api, &ei, ctx->project_path, &err)) {
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

static bool enum_handlers_callback(void *callback_context,
                                   gcmz_config_dialog_handler_enum_fn fn,
                                   void *userdata,
                                   struct ov_error *err) {
  struct gcmz_lua_context *lua_ctx = (struct gcmz_lua_context *)callback_context;
  return gcmz_lua_enum_handlers(lua_ctx, fn, userdata, err);
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
    if (!gcmz_config_dialog_show(ctx->config, enum_handlers_callback, ctx->lua_ctx, (HWND)hwnd, running, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

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
    if (ctx) {
      debug_output_info(ctx);
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
  if (!edit_info || !project_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
    return false;
  }
  if (ctx->current_edit_section) {
    memcpy(edit_info, ctx->current_edit_section->info, sizeof(*edit_info));
  } else if (ctx->edit) {
    ctx->edit->get_edit_info(edit_info, sizeof(*edit_info));
  } else {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }
  bool success = false;

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

static bool lua_exo_convert_adapter(struct gcmz_file_list *file_list, void *userdata, struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx || !ctx->lua_ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  return gcmz_lua_call_exo_convert(ctx->lua_ctx, file_list, err);
}

static bool lua_drag_enter_adapter(struct gcmz_file_list *file_list,
                                   uint32_t key_state,
                                   uint32_t modifier_keys,
                                   bool from_api,
                                   void *userdata,
                                   struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx || !ctx->lua_ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  return gcmz_lua_call_drag_enter(ctx->lua_ctx, file_list, key_state, modifier_keys, from_api, err);
}

static bool lua_drop_adapter(struct gcmz_file_list *file_list,
                             uint32_t key_state,
                             uint32_t modifier_keys,
                             bool from_api,
                             void *userdata,
                             struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx || !ctx->lua_ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  return gcmz_lua_call_drop(ctx->lua_ctx, file_list, key_state, modifier_keys, from_api, err);
}

static bool lua_drag_leave_adapter(void *userdata, struct ov_error *const err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx || !ctx->lua_ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  return gcmz_lua_call_drag_leave(ctx->lua_ctx, err);
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
  if (!filename) {
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

/**
 * Callback function invoked when the host is ready
 *
 * This signals that the main window is fully initialized and user interaction has begun.
 *
 * @param userdata User-defined data pointer (struct gcmzdrops*)
 */
static void on_ready(void *const userdata) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!ctx) {
    return;
  }

  // FIXME:
  // As of version2.0beta24a, even if you register a handler with host->register_project_load_handler() in
  // RegisterPlugin, the handler is not called only when a new project is automatically created at the beginning of a
  // normal startup. Also, calling host->create_edit_handle()->get_edit_info() in RegisterPlugin causes a crash. The
  // same applies to host->create_edit_handle()->call_edit_section() and
  // host->create_edit_handle()->call_edit_section_param(). Because of this, there is no appropriate timing to get
  // project settings only when creating a new project, and there is no appropriate initialization timing for the
  // external cooperation API. Therefore, at present, this is avoided by treating the timing of WM_USER or WM_MOUSEMOVE
  // coming to the main window as the completion of initialization.
  mtx_lock(&ctx->init_mtx);
  ctx->plugin_state = gcmzdrops_plugin_state_registered;
  cnd_signal(&ctx->init_cond);
  mtx_unlock(&ctx->init_mtx);
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

  void *windows[max_windows] = {0};
  bool success = false;
  struct ov_error err = {0};

  {
    if (!ctx || !ctx->window_list) {
      success = true;
      goto cleanup;
    }

    HWND window_handles[max_windows];
    size_t const count = find_manager_windows(window_handles, max_windows, &err);
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
      windows[found++] = hwnd;
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
          if (!gcmz_drop_register_window(ctx->drop, windows[i], &err)) {
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
  if (ctx->window_list) {
    gcmz_window_list_destroy(&ctx->window_list);
  }
  if (ctx->project_path) {
    OV_ARRAY_DESTROY(&ctx->project_path);
  }
  gcmz_delayed_cleanup_exit();
  gcmz_temp_remove_directory();
  gcmz_do_exit();
  gcmz_error_set_owner_window_callback(NULL);
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
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to extract directory from module path");
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

static bool
get_media_info_utf8(char const *filepath, struct aviutl2_media_info *info, void *userdata, struct ov_error *err) {
  struct gcmzdrops *const ctx = (struct gcmzdrops *)userdata;
  if (!filepath || !info) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (!ctx) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "edit handle not available");
    return false;
  }
  if (!ctx->current_edit_section) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "edit section not available");
    return false;
  }

  wchar_t *filepath_w = NULL;
  bool result = false;

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

  if (!ctx->current_edit_section->get_media_info(filepath_w, info, sizeof(*info))) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "unsupported media file");
    goto cleanup;
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

    bool external_api_enabled = false;
    if (!gcmz_config_get_external_api(ctx->config, &external_api_enabled, &err)) {
      OV_ERROR_ADD_TRACEF(&err, "%1$hs", "%1$hs", gettext("failed to get external API setting"));
      goto cleanup;
    }
    if (!external_api_enabled) {
      success = true;
      goto cleanup;
    }

    if (!create_external_api(ctx, false, &err)) {
      OV_ERROR_ADD_TRACEF(&err, "%1$hs", "%1$hs", gettext("failed to initialize external API, continuing without it."));
      goto cleanup;
    }
  }
  success = true;

cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%s", "%s", gettext("failed to complete delayed initialization"));
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

NODISCARD bool gcmzdrops_create(struct gcmzdrops **const ctx,
                                struct gcmz_lua_context *const lua_ctx,
                                uint32_t const version,
                                struct ov_error *const err) {
  if (!ctx || !lua_ctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  // Check minimum required AviUtl ExEdit2 version
  if (version < 2002401) {
    OV_ERROR_SETF(err,
                  ov_error_type_generic,
                  ov_error_generic_fail,
                  "%1$s",
                  gettext("GCMZDrops requires AviUtl ExEdit2 %1$s or later."),
                  "version2.0beta24a");
    return false;
  }

  struct gcmzdrops *c = NULL;
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

    {
      HWND main_window = NULL;
      if (!find_manager_windows(&main_window, 1, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      if (!gcmz_do_init(
              &(struct gcmz_do_init_option){
                  .window = main_window,
                  .on_change_activate = on_change_activate,
                  .on_ready = on_ready,
                  .userdata = c,
              },
              err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
    }
    gcmz_error_set_owner_window_callback(get_error_dialog_owner_window);

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
            .userdata = c,
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
        .script_modules_key = gcmz_lua_get_script_modules_key(),
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

    c->drop = gcmz_drop_create(
        &(struct gcmz_drop_options){
            .extract = extract_from_dataobj,
            .cleanup = schedule_cleanup,
            .file_manage = copy_file,
            .exo_convert = lua_exo_convert_adapter,
            .drag_enter = lua_drag_enter_adapter,
            .drop = lua_drop_adapter,
            .drag_leave = lua_drag_leave_adapter,
            .userdata = c,
        },
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
#ifdef NDEBUG
      bool const show_tray = false;
#else
      bool const show_tray = true;
#endif
      if (!gcmz_tray_set_visible(c->tray, show_tray, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
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

void gcmzdrops_on_project_load(struct gcmzdrops *const ctx, struct aviutl2_project_file *const project) {
  if (!ctx) {
    return;
  }
  struct ov_error err = {0};
  bool success = false;
  wchar_t const *const project_path = project ? project->get_project_file_path() : NULL;
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

  {
    mtx_lock(&ctx->init_mtx);
    bool const initialized = ctx->plugin_state == gcmzdrops_plugin_state_registered;
    mtx_unlock(&ctx->init_mtx);
    if (initialized) {
      gcmz_do_sub_do(ctx->do_sub, update_api_project_data, ctx);
    }
  }
  success = true;
cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to handle project load"));
    OV_ERROR_DESTROY(&err);
  }
}

void gcmzdrops_paste_from_clipboard(struct gcmzdrops *const ctx, struct aviutl2_edit_section *const edit) {
  if (!ctx || !edit) {
    return;
  }

  int layer = 0;
  int frame = 0;
  if (edit->get_mouse_layer_frame) {
    edit->get_mouse_layer_frame(&layer, &frame);
  }

  struct ov_error err = {0};
  IDataObject *dataobj = NULL;
  struct gcmz_file_list *file_list = NULL;
  bool success = false;

  HRESULT hr = OleGetClipboard(&dataobj);
  if (FAILED(hr)) {
    OV_ERROR_SET_HRESULT(&err, hr);
    goto cleanup;
  }
  if (!dataobj) {
    success = true;
    goto cleanup;
  }

  file_list = gcmz_file_list_create(&err);
  if (!file_list) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  if (!gcmz_dataobj_extract_from_dataobj(dataobj, file_list, &err)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  if (gcmz_file_list_count(file_list) == 0) {
    success = true;
    goto cleanup;
  }

  {
    ctx->current_edit_section = edit;
    bool const r = gcmz_drop_simulate_drop(ctx->drop,
                                           file_list,
                                           false,
                                           on_clipboard_paste_completion,
                                           &(struct clipboard_paste_context){
                                               .ctx = ctx,
                                               .edit = edit,
                                               .layer = layer,
                                               .frame = frame,
                                           },
                                           &err);
    ctx->current_edit_section = NULL;
    if (!r) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  success = true;
cleanup:
  if (!success) {
    gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to paste from clipboard"));
    OV_ERROR_DESTROY(&err);
  }
  if (file_list) {
    gcmz_file_list_destroy(&file_list);
  }
  if (dataobj) {
    IDataObject_Release(dataobj);
    dataobj = NULL;
  }
}

void gcmzdrops_register(struct gcmzdrops *const ctx, struct aviutl2_host_app_table *const host) {
  if (!ctx || !host) {
    return;
  }
  struct aviutl2_edit_handle *const edit = host->create_edit_handle();
  if (edit) {
    ctx->edit = edit;
  }
}
