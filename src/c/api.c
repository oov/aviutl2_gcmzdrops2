#include "api.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <ovarray.h>
#include <ovmo.h>
#include <ovnum.h>
#include <ovthreads.h>

#include "file.h"
#include "gcmz_types.h"
#include "json.h"

enum {
  // 0 - AviUtl 1.00/1.10 with ExEdit 0.92 + GCMZDrops v0.3      or later
  //     * Used by GCMZDrops released between 2018-04-08 and 2020-06-25
  //     * IMPORTANT: Version 0 has no gcmz_api_ver field, making version detection impossible
  //     * WM_COPYDATA data transfer uses non-JSON format (only COPYDATASTRUCT.dwData = 0 supported)
  // 1 - AviUtl 1.00/1.10 with ExEdit 0.92 + GCMZDrops v0.3.12   or later
  //     * Used by GCMZDrops released between 2020-06-25 and 2021-08-02
  //     * Added gcmz_api_ver and project_path fields
  //     * Enabled retrieval of current project file path
  //     * WM_COPYDATA now supports JSON format (COPYDATASTRUCT.dwData = 1)
  // 2 - AviUtl 1.00/1.10 with ExEdit 0.92 + GCMZDrops v0.3.23   or later
  //     * Used by GCMZDrops for AviUtl1 released from 2021-08-02 onwards
  //     * Added flags field to detect English/Simplified Chinese translation patches
  //     * Translation-patched environments require specialized *.exo files (workaround added in GCMZDrops v0.4.0)
  // 3 - AviUtl ExEdit2.0 or later    + GCMZDrops v2.0alpha1 or later
  //     * Added aviutl2_ver and gcmz_ver fields
  //     * Version bumped due to major changes in AviUtl ExEdit2 (*.exo no longer supported, etc.)
  //     * COPYDATASTRUCT.dwData = 1 enables automatic *.exo -> *.object conversion
  //     * COPYDATASTRUCT.dwData = 2 disables automatic *.exo conversion
  api_version = 3,

  max_files_per_request = 100,
  max_file_path_length = 1024,
  request_timeout_ms = 5000,
  timer_interval_ms = 5000,
  timer_id = 1,
};

enum {
  WM_COMPLETION_CALLBACK = WM_USER + 1,
};

enum state {
  state_none,
  state_allocated,
  state_mtx_initialized,
  state_cond_initialized,
  state_thread_created,
  state_thread_running,
  state_thread_stopping,
  state_thread_stopped,
};

static wchar_t const g_api_window_class_name[] = L"GCMZDropsAPI";

struct gcmzdrops_fmo {
  uint32_t window;
  int32_t width;
  int32_t height;
  int32_t video_rate;
  int32_t video_scale;
  int32_t audio_rate;
  int32_t audio_ch;
  int32_t gcmz_api_ver;
  wchar_t project_path[MAX_PATH];
  uint32_t flags;
  uint32_t aviutl2_ver;
  uint32_t gcmz_ver;
};

struct request_context {
  struct gcmz_api_request_params params; ///< Must be first for casting
  struct gcmz_file_list *files;
  struct gcmz_api *api;
  HWND window;
};

struct gcmz_api {
  HWND window;
  HANDLE mutex;
  HANDLE fmo;

  thrd_t thread;
  mtx_t mtx;
  cnd_t cond;
  enum state state;

  gcmz_api_request_func request;
  gcmz_api_update_request_func update_request;
  void *userdata;

  struct gcmz_project_data current_data;
  bool has_current_data;

  uint32_t aviutl2_ver;
  uint32_t gcmz_ver;
};

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void gcmz_api_complete_callback(struct gcmz_api_request_params *const params) {
  if (!params) {
    return;
  }
  struct request_context *ctx = (struct request_context *)params;
  if (!ctx->window) {
    gcmz_file_list_destroy(&ctx->files);
    OV_FREE(&ctx);
    return;
  }
  if (!PostMessageW(ctx->window, WM_COMPLETION_CALLBACK, 0, (LPARAM)ctx)) {
    gcmz_file_list_destroy(&ctx->files);
    OV_FREE(&ctx);
  }
}

static bool is_data_changed(struct gcmz_project_data const *const old_data,
                            struct gcmz_project_data const *const new_data) {
  if (!old_data || !new_data) {
    return true;
  }
  if (old_data->width != new_data->width || old_data->height != new_data->height ||
      old_data->video_rate != new_data->video_rate || old_data->video_scale != new_data->video_scale ||
      old_data->sample_rate != new_data->sample_rate || old_data->audio_ch != new_data->audio_ch ||
      old_data->cursor_frame != new_data->cursor_frame || old_data->display_frame != new_data->display_frame ||
      old_data->display_layer != new_data->display_layer || old_data->display_zoom != new_data->display_zoom ||
      old_data->flags != new_data->flags) {
    return true;
  }
  if (old_data->project_path == NULL && new_data->project_path == NULL) {
    return false;
  }
  if (old_data->project_path == NULL || new_data->project_path == NULL) {
    return true;
  }
  return wcscmp(old_data->project_path, new_data->project_path) != 0;
}

static NODISCARD bool update_mapped_data(struct gcmz_api *const api, struct ov_error *const err) {
  if (!api || !api->fmo || !api->mutex) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmzdrops_fmo *shared_data = NULL;
  bool mutex_acquired = false;
  bool result = false;

  {
    DWORD const wait_result = WaitForSingleObject(api->mutex, request_timeout_ms);
    if (wait_result != WAIT_OBJECT_0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    mutex_acquired = true;
  }

  shared_data =
      (struct gcmzdrops_fmo *)MapViewOfFile(api->fmo, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct gcmzdrops_fmo));
  if (!shared_data) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  shared_data->window = (uint32_t)(uintptr_t)api->window;
  shared_data->gcmz_api_ver = api_version;

  if (!api->has_current_data) {
    memset(shared_data, 0, sizeof(struct gcmzdrops_fmo));
    shared_data->gcmz_api_ver = api_version;
    shared_data->aviutl2_ver = api->aviutl2_ver;
    shared_data->gcmz_ver = api->gcmz_ver;
    goto cleanup;
  }

  shared_data->width = (int32_t)api->current_data.width;
  shared_data->height = (int32_t)api->current_data.height;
  shared_data->video_rate = api->current_data.video_rate;
  shared_data->video_scale = api->current_data.video_scale;
  shared_data->audio_rate = (int32_t)api->current_data.sample_rate;
  shared_data->audio_ch = (int32_t)api->current_data.audio_ch;
  shared_data->flags = api->current_data.flags;
  shared_data->aviutl2_ver = api->aviutl2_ver;
  shared_data->gcmz_ver = api->gcmz_ver;

  if (!api->current_data.project_path) {
    shared_data->project_path[0] = L'\0';
    result = true;
    goto cleanup;
  }
  {
    size_t const path_len_orig = wcslen(api->current_data.project_path);
    size_t const path_len = (path_len_orig >= MAX_PATH) ? MAX_PATH - 1 : path_len_orig;
    wcsncpy(shared_data->project_path, api->current_data.project_path, path_len);
    shared_data->project_path[path_len] = L'\0';
  }

  result = true;

cleanup:
  if (shared_data) {
    UnmapViewOfFile(shared_data);
    shared_data = NULL;
  }
  if (mutex_acquired) {
    ReleaseMutex(api->mutex);
  }
  return result;
}

static bool is_safe_file_path(wchar_t const *const path) {
  if (!path) {
    return false;
  }

  bool result = false;
  size_t const path_len = wcslen(path);
  if (path_len == 0) {
    goto cleanup;
  }
  if (wcsstr(path, L"..") != NULL) {
    goto cleanup;
  }
  if (path_len < 3 || path[1] != L':') {
    goto cleanup;
  }
  if (path_len > max_file_path_length) {
    goto cleanup;
  }

  result = true;

cleanup:
  return result;
}

static bool validate_request_limits(struct gcmz_api_request_params const *const params) {
  if (!params) {
    return false;
  }

  bool result = false;
  size_t const file_count = gcmz_file_list_count(params->files);
  if (file_count > max_files_per_request) {
    goto cleanup;
  }
  for (size_t i = 0; i < file_count; ++i) {
    struct gcmz_file const *const file = gcmz_file_list_get(params->files, i);
    if (!file || !file->path) {
      goto cleanup;
    }

    if (!is_safe_file_path(file->path)) {
      goto cleanup;
    }
  }
  if (params->layer == 0) {
    goto cleanup;
  }
  if (params->frame_advance < 0) {
    goto cleanup;
  }

  result = true;

cleanup:
  return result;
}

static NODISCARD bool
utf8_to_wstr(char const *const utf8_str, size_t const utf8_len, wchar_t **const ws, struct ov_error *const err) {
  if (!utf8_str || !ws) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  if (utf8_len == 0) {
    if (!OV_ARRAY_GROW(ws, 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    (*ws)[0] = L'\0';
    result = true;
    goto cleanup;
  }

  {
    int const ws_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, (int)utf8_len, NULL, 0);
    if (ws_len <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(ws, (size_t)ws_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    int const converted = MultiByteToWideChar(CP_UTF8, 0, utf8_str, (int)utf8_len, *ws, ws_len);
    if (converted <= 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    if (converted != ws_len) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "Failed to convert UTF-8 to wide character");
      goto cleanup;
    }

    (*ws)[ws_len] = L'\0';
  }

  result = true;

cleanup:
  if (!result && ws && *ws) {
    OV_ARRAY_DESTROY(ws);
  }
  return result;
}

static NODISCARD HANDLE create_mutex(wchar_t const *const name, struct ov_error *const err) {
  if (!name) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  HANDLE mutex = CreateMutexW(NULL, FALSE, name);
  HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
  if (!mutex || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    if (mutex) {
      CloseHandle(mutex);
      mutex = NULL;
    }
  }
  return mutex;
}

static NODISCARD HANDLE create_file_mapping_object(wchar_t const *const name,
                                                   size_t const size,
                                                   struct ov_error *const err) {
  if (!name || !size) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  HANDLE fmo = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)size, name);
  HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
  if (!fmo || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    if (fmo) {
      CloseHandle(fmo);
      fmo = NULL;
    }
  }
  return fmo;
}

static NODISCARD bool initialize_shared_data(struct gcmz_api *const api, struct ov_error *const err) {
  if (!api || !api->fmo || !api->mutex) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmzdrops_fmo *shared_data = NULL;
  bool mutex_acquired = false;
  bool result = false;

  DWORD const r = WaitForSingleObject(api->mutex, request_timeout_ms);
  if (r != WAIT_OBJECT_0) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  mutex_acquired = true;

  shared_data =
      (struct gcmzdrops_fmo *)MapViewOfFile(api->fmo, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct gcmzdrops_fmo));
  if (!shared_data) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  memset(shared_data, 0, sizeof(struct gcmzdrops_fmo));
  shared_data->gcmz_api_ver = api_version;
  shared_data->aviutl2_ver = api->aviutl2_ver;
  shared_data->gcmz_ver = api->gcmz_ver;

  result = true;

cleanup:
  if (shared_data) {
    UnmapViewOfFile(shared_data);
    shared_data = NULL;
  }
  if (mutex_acquired) {
    ReleaseMutex(api->mutex);
  }
  return result;
}

static NODISCARD bool parse_v0_request(wchar_t const *data,
                                       size_t data_len,
                                       struct gcmz_api_request_params *params,
                                       struct ov_error *const err) {
  if (!data || data_len == 0 || !params) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  params->layer = 0;
  params->frame_advance = 0;
  params->err = NULL;
  params->userdata = NULL;

  {
    if (data_len < 3) { // Minimum: layer + '\0' + frame_advance + '\0'
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "version 0 data too short");
      goto cleanup;
    }

    wchar_t const *ptr = data;
    wchar_t const *end = data + data_len;
    wchar_t const *layer_end = wmemchr(ptr, L'\0', (size_t)(end - ptr));
    if (!layer_end) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "version 0 layer parameter not found");
      goto cleanup;
    }

    int64_t layer_value;
    if (!OV_ATOI(ptr, &layer_value, 10)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "version 0 invalid layer parameter");
      goto cleanup;
    }
    params->layer = (int)layer_value;
    ptr = layer_end + 1;
    if (ptr >= end) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "version 0 frame advance parameter not found");
      goto cleanup;
    }
    wchar_t const *frame_advance_end = wmemchr(ptr, L'\0', (size_t)(end - ptr));
    if (!frame_advance_end) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "version 0 frame advance parameter not found");
      goto cleanup;
    }

    int64_t frame_advance_value;
    if (!OV_ATOI(ptr, &frame_advance_value, 10)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "version 0 invalid frame advance parameter");
      goto cleanup;
    }
    params->frame_advance = (int)frame_advance_value;
    ptr = frame_advance_end + 1;
    while (ptr < end) {
      wchar_t const *file_end = wmemchr(ptr, L'\0', (size_t)(end - ptr));
      if (!file_end) {
        // Last file might not have null terminator at end of data
        file_end = end;
      }

      if (file_end > ptr) { // Non-empty file path
        size_t path_len = (size_t)(file_end - ptr);
        wchar_t *file_path = NULL;

        if (!OV_ARRAY_GROW(&file_path, path_len + 1)) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
          goto cleanup;
        }

        wcsncpy(file_path, ptr, path_len);
        file_path[path_len] = L'\0';
        if (!is_safe_file_path(file_path)) {
          OV_ARRAY_DESTROY(&file_path);
          OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "Unsafe file path detected in version 0");
          goto cleanup;
        }
        if (!gcmz_file_list_add(params->files, file_path, L"application/octet-stream", err)) {
          OV_ARRAY_DESTROY(&file_path);
          OV_ERROR_ADD_TRACE(err);
          goto cleanup;
        }
        OV_ARRAY_DESTROY(&file_path);
      }

      ptr = file_end + 1;
    }

    result = true;
  }

cleanup:
  return result;
}

static NODISCARD bool parse_v1_request(char const *json_data,
                                       size_t json_size,
                                       struct gcmz_api_request_params *params,
                                       struct ov_error *const err) {
  if (!json_data || json_size == 0 || !params) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  yyjson_doc *doc = NULL;

  params->layer = 0;
  params->frame_advance = 0;
  params->err = NULL;
  params->userdata = NULL;

  {
    doc = yyjson_read_opts((char *)ov_deconster_(json_data), json_size, 0, gcmz_json_get_alc(), NULL);
    if (!doc) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "Failed to parse JSON data");
      goto cleanup;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "JSON root is not an object");
      goto cleanup;
    }

    yyjson_val *layer_val = yyjson_obj_get(root, "layer");
    if (layer_val) {
      if (!yyjson_is_int(layer_val)) {
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "layer parameter must be an integer");
        goto cleanup;
      }
      params->layer = (int)yyjson_get_int(layer_val);
    }

    yyjson_val *frame_advance_val = yyjson_obj_get(root, "frameAdvance");
    if (frame_advance_val) {
      if (!yyjson_is_int(frame_advance_val)) {
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "frameAdvance parameter must be an integer");
        goto cleanup;
      }
      params->frame_advance = (int)yyjson_get_int(frame_advance_val);
    }

    yyjson_val *files_val = yyjson_obj_get(root, "files");
    if (!files_val || !yyjson_is_arr(files_val)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "files parameter must be an array");
      goto cleanup;
    }

    size_t arr_len = yyjson_arr_size(files_val);
    if (arr_len == 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "files array cannot be empty");
      goto cleanup;
    }

    if (arr_len > max_files_per_request) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "too many files in request");
      goto cleanup;
    }

    yyjson_val *file_val;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(files_val, &iter);
    while ((file_val = yyjson_arr_iter_next(&iter))) {
      if (!yyjson_is_str(file_val)) {
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "each file must be a string");
        goto cleanup;
      }

      char const *file_path_utf8 = yyjson_get_str(file_val);
      size_t file_path_len = yyjson_get_len(file_val);
      wchar_t *file_path_wstr = NULL;
      if (!utf8_to_wstr(file_path_utf8, file_path_len, &file_path_wstr, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      if (!is_safe_file_path(file_path_wstr)) {
        OV_ARRAY_DESTROY(&file_path_wstr);
        OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "unsafe file path detected");
        goto cleanup;
      }
      if (!gcmz_file_list_add(params->files, file_path_wstr, L"application/octet-stream", err)) {
        OV_ARRAY_DESTROY(&file_path_wstr);
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      OV_ARRAY_DESTROY(&file_path_wstr);
    }
  }

  result = true;

cleanup:
  if (doc) {
    yyjson_doc_free(doc);
  }
  return result;
}

static int api_thread(void *const userdata) {
  struct gcmz_api *const api = (struct gcmz_api *)userdata;
  if (!api) {
    return -1;
  }

  ATOM window_class_atom = 0;
  HWND window = NULL;
  int result = -1;
  struct ov_error err = {0};

  window_class_atom = RegisterClassExW(&(WNDCLASSEXW){
      .cbSize = sizeof(WNDCLASSEXW),
      .lpfnWndProc = window_proc,
      .hInstance = GetModuleHandleW(NULL),
      .lpszClassName = g_api_window_class_name,
  });
  if (!window_class_atom) {
    OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  window = CreateWindowExW(0,                       // Extended window style
                           g_api_window_class_name, // Window class name
                           L"GCMZDrops API Window", // Window name
                           0,                       // Window style
                           0,
                           0,
                           0,
                           0,                      // Position and size (not used for message-only window)
                           HWND_MESSAGE,           // Parent window (HWND_MESSAGE for message-only window)
                           NULL,                   // Menu
                           GetModuleHandleW(NULL), // Instance handle
                           api                     // Creation parameter (passed to WM_CREATE)
  );
  if (!window) {
    OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  mtx_lock(&api->mtx);
  api->window = window;
  api->state = state_thread_running;
  cnd_signal(&api->cond);
  mtx_unlock(&api->mtx);

  if (api->update_request) {
    if (SetTimer(window, timer_id, timer_interval_ms, NULL)) {
      PostMessageW(window, WM_TIMER, timer_id, 0);
    }
  }

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  result = 0;

cleanup:
  if (window) {
    KillTimer(window, timer_id);
    DestroyWindow(window);
    mtx_lock(&api->mtx);
    api->window = NULL;
    mtx_unlock(&api->mtx);
  }
  if (window_class_atom != 0) {
    UnregisterClassW(g_api_window_class_name, GetModuleHandleW(NULL));
  }
  mtx_lock(&api->mtx);
  api->state = state_thread_stopped;
  cnd_signal(&api->cond);
  mtx_unlock(&api->mtx);
  OV_ERROR_REPORT(&err, NULL);
  return result;
}

static NODISCARD LRESULT handle_wm_copydata(struct gcmz_api *const api, HWND const hwnd, LPARAM const lparam) {
  struct ov_error err = {0};
  struct request_context *ctx = NULL;
  bool result = false;
  COPYDATASTRUCT *cds = NULL;

  if (!api || !api->request) {
    OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
    goto cleanup;
  }

  cds = (COPYDATASTRUCT *)lparam;
  if (!cds || !cds->lpData || cds->cbData == 0) {
    OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "received data broken");
    goto cleanup;
  }

  if (!OV_REALLOC(&ctx, 1, sizeof(struct request_context))) {
    OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  *ctx = (struct request_context){
      .api = api,
      .window = hwnd,
  };
  ctx->files = gcmz_file_list_create(&err);
  if (!ctx->files) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  ctx->params.files = ctx->files;
  switch (cds->dwData) {
  case 0: // deprecated format
    if (!parse_v0_request((wchar_t const *)cds->lpData, cds->cbData / sizeof(wchar_t), &ctx->params, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    ctx->params.use_exo_converter = true; // Legacy format: EXO conversion enabled
    break;
  case 1: // JSON format with EXO conversion
    if (!parse_v1_request((char const *)cds->lpData, cds->cbData, &ctx->params, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    ctx->params.use_exo_converter = true; // JSON format: EXO conversion enabled
    break;
  case 2: // JSON format without EXO conversion
    if (!parse_v1_request((char const *)cds->lpData, cds->cbData, &ctx->params, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    ctx->params.use_exo_converter = false; // JSON format: EXO conversion disabled
    break;
  default:
    OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "invalid dwData value");
    goto cleanup;
  }

  // Validate the complete request
  if (!validate_request_limits(&ctx->params)) {
    OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "Request validation failed");
    goto cleanup;
  }

  ctx->params.userdata = api->userdata;
  ctx->params.err = &err;
  api->request(&ctx->params, gcmz_api_complete_callback);
  ctx = NULL;
  result = true;

cleanup:
  if (ctx) {
    if (ctx->files) {
      gcmz_file_list_destroy(&ctx->files);
    }
    OV_FREE(&ctx);
  }
  if (!result) {
    OV_ERROR_REPORT(&err, NULL);
    return FALSE;
  }
  return TRUE;
}

static LRESULT CALLBACK window_proc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) {
  struct gcmz_api *api = NULL;

  if (msg == WM_CREATE) {
    CREATESTRUCTW *const cs = (CREATESTRUCTW *)lparam;
    api = (struct gcmz_api *)cs->lpCreateParams;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)api);
  } else {
    LONG_PTR const ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    api = (struct gcmz_api *)(uintptr_t)ptr;
  }

  switch (msg) {
  case WM_COPYDATA:
    return handle_wm_copydata(api, hwnd, lparam);
  case WM_TIMER:
    if (wparam != timer_id || !api) {
      return 0;
    }
    if (api->update_request) {
      api->update_request(api, api->userdata);
    }
    return 0;
  case WM_COMPLETION_CALLBACK: {
    struct request_context *ctx = (struct request_context *)lparam;
    if (ctx) {
      gcmz_file_list_destroy(&ctx->files);
      OV_FREE(&ctx);
    }
    return 0;
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

NODISCARD struct gcmz_api *gcmz_api_create(struct gcmz_api_options const *const options, struct ov_error *const err) {
  static NATIVE_CHAR const api_shared_memory_name[] = NSTR("GCMZDrops");
  static NATIVE_CHAR const api_mutex_name[] = NSTR("GCMZDropsMutex");

  struct gcmz_api *a = NULL;
  struct gcmz_api *result = NULL;

  if (!OV_REALLOC(&a, 1, sizeof(struct gcmz_api))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  *a = (struct gcmz_api){
      .request = options ? options->request_callback : NULL,
      .update_request = options ? options->update_callback : NULL,
      .userdata = options ? options->userdata : NULL,
      .state = state_allocated,
      .aviutl2_ver = options ? options->aviutl2_ver : 0,
      .gcmz_ver = options ? options->gcmz_ver : 0,
  };

  a->mutex = create_mutex(api_mutex_name, err);
  if (!a->mutex) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  a->fmo = create_file_mapping_object(api_shared_memory_name, sizeof(struct gcmzdrops_fmo), err);
  if (!a->fmo) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!initialize_shared_data(a, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (mtx_init(&a->mtx, mtx_plain) != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  a->state = state_mtx_initialized;

  if (cnd_init(&a->cond) != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  a->state = state_cond_initialized;

  mtx_lock(&a->mtx);
  if (thrd_create(&a->thread, api_thread, a) != thrd_success) {
    mtx_unlock(&a->mtx);
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  a->state = state_thread_created;

  while (a->state == state_thread_created) {
    if (cnd_wait(&a->cond, &a->mtx) != thrd_success) {
      mtx_unlock(&a->mtx);
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
  }
  if (a->state != state_thread_running) {
    mtx_unlock(&a->mtx);
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  mtx_unlock(&a->mtx);

  result = a;
  a = NULL;

cleanup:
  if (a) {
    gcmz_api_destroy(&a);
  }
  return result;
}

void gcmz_api_destroy(struct gcmz_api **const api) {
  if (!api || !*api) {
    return;
  }
  struct gcmz_api *const a = *api;

  HWND window = NULL;
  if (a->state >= state_mtx_initialized) {
    mtx_lock(&a->mtx);
    window = a->window;
    mtx_unlock(&a->mtx);
  }

  if (a->state >= state_thread_running && window) {
    SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
  }

  if (a->state >= state_thread_created) {
    int thread_result;
    thrd_join(a->thread, &thread_result);
  }

  if (a->state >= state_cond_initialized) {
    cnd_destroy(&a->cond);
  }

  if (a->state >= state_mtx_initialized) {
    mtx_destroy(&a->mtx);
  }

  if (a->fmo) {
    CloseHandle(a->fmo);
    a->fmo = NULL;
  }

  if (a->mutex) {
    CloseHandle(a->mutex);
    a->mutex = NULL;
  }

  if (a->current_data.project_path) {
    OV_ARRAY_DESTROY(&a->current_data.project_path);
  }
  OV_FREE(api);
}

bool gcmz_api_set_project_data(struct gcmz_api *const api,
                               struct gcmz_project_data const *const proj,
                               struct ov_error *const err) {
  if (!api || !proj) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  bool mutex_locked = false;

  if (mtx_lock(&api->mtx) != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  mutex_locked = true;

  if (api->has_current_data && !is_data_changed(&api->current_data, proj)) {
    result = true;
    goto cleanup;
  }

  api->has_current_data = true;

  api->current_data.width = proj->width;
  api->current_data.height = proj->height;
  api->current_data.video_rate = proj->video_rate;
  api->current_data.video_scale = proj->video_scale;
  api->current_data.sample_rate = proj->sample_rate;
  api->current_data.audio_ch = proj->audio_ch;
  api->current_data.cursor_frame = proj->cursor_frame;
  api->current_data.display_frame = proj->display_frame;
  api->current_data.display_layer = proj->display_layer;
  api->current_data.display_zoom = proj->display_zoom;
  api->current_data.flags = proj->flags;

  if (proj->project_path) {
    size_t path_len = wcslen(proj->project_path);
    if (!OV_ARRAY_GROW(&api->current_data.project_path, path_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(api->current_data.project_path, proj->project_path);
  } else {
    if (api->current_data.project_path) {
      api->current_data.project_path[0] = L'\0';
    }
  }

  if (!update_mapped_data(api, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (mutex_locked) {
    mtx_unlock(&api->mtx);
  }
  return result;
}
