#include "project_info.h"

#include "aviutl2.h"
#include "aviutl2_sdk_c/aviutl2_plugin2.h"
#include "gcmz_types.h"

static struct aviutl2_edit_handle *g_edit = NULL;
static struct gcmz_project_data *g_data = NULL;
static gcmz_extended_project_info_getter g_extended_getter = NULL;
static wchar_t g_project_path_buffer[4096] = {0};

void gcmz_project_info_set_handle(struct aviutl2_edit_handle *handle) { g_edit = handle; }

void gcmz_project_info_set_extended_getter(gcmz_extended_project_info_getter getter) { g_extended_getter = getter; }

static void project_info_edit_callback(struct aviutl2_edit_section *edit) {
  if (!edit || !edit->info) {
    return;
  }
  struct ov_error err = {0};
  struct aviutl2_edit_info const *info = edit->info;
  *g_data = (struct gcmz_project_data){
      .width = info->width,
      .height = info->height,
      .video_rate = info->rate,
      .video_scale = info->scale,
      .sample_rate = info->sample_rate,
      .audio_ch = 2, // AviUtl2 does not expose audio channel count so assume stereo
      .cursor_frame = info->frame,
      .selected_layer = info->layer,
      .flags = 0,
  };

  if (g_extended_getter) {
    wchar_t const *project_path = NULL;
    if (!g_extended_getter(
            &g_data->display_frame, &g_data->display_layer, &g_data->display_zoom, &project_path, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (project_path) {
      wcsncpy(g_project_path_buffer, project_path, sizeof(g_project_path_buffer) / sizeof(wchar_t) - 1);
      g_project_path_buffer[sizeof(g_project_path_buffer) / sizeof(wchar_t) - 1] = L'\0';
    } else {
      g_project_path_buffer[0] = L'\0';
    }
    g_data->project_path = g_project_path_buffer;
  }
cleanup:
  OV_ERROR_REPORT(&err, NULL);
}

bool gcmz_project_info_get(struct gcmz_project_data *const data, struct ov_error *const err) {
  if (!data) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool success = false;
  struct aviutl2_edit_handle *handle = g_edit;

  if (!handle) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  if (!handle->call_edit_section) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
    goto cleanup;
  }

  g_data = data;
  if (!handle->call_edit_section(project_info_edit_callback)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  g_data = NULL;
  success = true;

cleanup:
  return success;
}
