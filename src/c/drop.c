#include "drop.h"

#include <ovarray.h>
#include <ovmo.h>
#include <ovprintf.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

#include <commctrl.h>

#include "datauri.h"
#include "error.h"
#include "file.h"
#include "gcmz_dataobj.h"
#include "logf.h"
#include "lua.h"
#include "temp.h"

#define GCMZ_DEBUG 0

enum {
  gcmz_drop_subclass_id = 0x8002,
};

static wchar_t const gcmz_drop_subclass_message_name[] = L"GCMZDropsSubclassMessage";
static UINT g_subclass_message_id = 0;

struct wrapped_drop_target {
  IDropTarget drop_target; ///< IDropTarget interface (must be first)
  LONG ref_count;

  IDropTarget *original;
  struct gcmz_drop *d;

  void *main_window;
  IDataObject *current_original;               ///< Original IDataObject from drag source
  IDataObject *current_replacement;            ///< Replacement IDataObject with converted files
  struct gcmz_file_list *current_file_list;    ///< Extracted and converted file list
  struct placeholder_entry *placeholder_cache; ///< Placeholder cache for lazy file creation
  wchar_t *shared_placeholder_path;            ///< Shared placeholder file path
  CRITICAL_SECTION cs;                         ///< Window-specific lock for drag state

  HHOOK subclass_hook; ///< Hook for cross-thread subclass installation (NULL if already subclassed)
};

struct placeholder_entry {
  wchar_t *path;   ///< Original file path (cache key)
  bool accessible; ///< Cached file accessibility result
};

struct gcmz_drop {
  gcmz_drop_dataobj_extract_fn extract_fn;
  gcmz_drop_cleanup_temp_file_fn cleanup_fn;
  gcmz_drop_project_data_provider_fn project_data_fn;
  gcmz_drop_file_manage_fn file_manage_fn;
  void *callback_userdata;
  struct gcmz_lua_context *lua_context;

  struct wrapped_drop_target **wrapped_targets;
  CRITICAL_SECTION targets_cs;

  // Last right-click position tracking (global across all windows)
  void *last_rbutton_window; ///< Window handle where last right-click occurred
  int last_rbutton_x;        ///< Last right-click X coordinate (client coordinates)
  int last_rbutton_y;        ///< Last right-click Y coordinate (client coordinates)
};

static inline struct wrapped_drop_target *get_impl_from_drop_target(IDropTarget *const This) {
  return (struct wrapped_drop_target *)This;
}

#if GCMZ_DEBUG
static void dump_data_object(IDataObject *pDataObj) {
  if (!pDataObj) {
    OutputDebugStringW(L"dump_data_object: pDataObj is NULL\n");
    return;
  }

  OutputDebugStringW(L"=== IDataObject Data Dump ===\n");

  // Enumerate supported formats using EnumFormatEtc
  IEnumFORMATETC *pEnumFmtEtc = NULL;
  HRESULT hr = IDataObject_EnumFormatEtc(pDataObj, DATADIR_GET, &pEnumFmtEtc);
  if (SUCCEEDED(hr) && pEnumFmtEtc) {
    OutputDebugStringW(L"Supported formats:\n");

    FORMATETC fmtEtc;
    ULONG fetched;
    while (IEnumFORMATETC_Next(pEnumFmtEtc, 1, &fmtEtc, &fetched) == S_OK) {
      wchar_t format_name[256] = L"Unknown";
      switch (fmtEtc.cfFormat) {
      case CF_TEXT:
        wcscpy(format_name, L"CF_TEXT");
        break;
      case CF_UNICODETEXT:
        wcscpy(format_name, L"CF_UNICODETEXT");
        break;
      case CF_HDROP:
        wcscpy(format_name, L"CF_HDROP");
        break;
      default: {
        int len = GetClipboardFormatNameW(fmtEtc.cfFormat, format_name, 255);
        if (len == 0) {
          ov_snprintf_wchar(format_name, 256, NULL, L"Format_%u", fmtEtc.cfFormat);
        }
        break;
      }
      }

      wchar_t debug_msg[512];
      ov_snprintf_wchar(debug_msg,
                        512,
                        NULL,
                        L"  Format: %ls (cfFormat=%u, dwAspect=%u, lindex=%d, tymed=%u)\n",
                        format_name,
                        fmtEtc.cfFormat,
                        fmtEtc.dwAspect,
                        fmtEtc.lindex,
                        fmtEtc.tymed);
      OutputDebugStringW(debug_msg);
    }

    IEnumFORMATETC_Release(pEnumFmtEtc);
  } else {
    OutputDebugStringW(L"Failed to enumerate formats\n");
  }

  STGMEDIUM stgMedium;
  hr = IDataObject_GetData(pDataObj,
                           (&(FORMATETC){
                               .cfFormat = CF_HDROP,
                               .dwAspect = DVASPECT_CONTENT,
                               .lindex = -1,
                               .tymed = TYMED_HGLOBAL,
                           }),
                           &stgMedium);
  if (SUCCEEDED(hr)) {
    OutputDebugStringW(L"CF_HDROP data found:\n");

    if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal) {
      HDROP hDrop = (HDROP)stgMedium.hGlobal;
      UINT file_count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

      wchar_t count_msg[128];
      ov_snprintf_wchar(count_msg, 128, NULL, L"  File count: %u\n", file_count);
      OutputDebugStringW(count_msg);

      enum { files_limits = 10 };
      for (UINT i = 0; i < file_count && i < files_limits; ++i) {
        wchar_t file_path[MAX_PATH];
        UINT len = DragQueryFileW(hDrop, i, file_path, MAX_PATH);
        if (len > 0) {
          wchar_t file_msg[MAX_PATH + 64];
          ov_snprintf_wchar(file_msg, MAX_PATH + 64, NULL, L"  File[%u]: %ls\n", i, file_path);
          OutputDebugStringW(file_msg);
        }
      }

      if (file_count > 10) {
        wchar_t more_msg[64];
        ov_snprintf_wchar(more_msg, 64, NULL, L"  ... and %u more files\n", file_count - 10);
        OutputDebugStringW(more_msg);
      }
    }

    ReleaseStgMedium(&stgMedium);
  } else {
    OutputDebugStringW(L"No CF_HDROP data available\n");
  }

  OutputDebugStringW(L"=== End IDataObject Dump ===\n");
}
#endif // GCMZ_DEBUG

/**
 * @brief Check file accessibility with internal cache management
 *
 * This function checks whether a file exists and is accessible, utilizing an internal
 * cache in d to avoid repeated file system queries for the same path. On first access,
 * the result is cached; subsequent calls for the same path return the cached result
 * immediately without performing file system operations.
 *
 * @param wdt Wrapped drop target containing the accessibility cache
 * @param path File path to check
 * @param err Error structure for reporting failures
 * @return ov_true if file exists and is accessible,
 *         ov_false if file does not exist or is not a regular file,
 *         ov_indeterminate if an error occurred during the check
 */
static ov_tribool is_file_accessible(struct wrapped_drop_target *wdt, wchar_t const *path, struct ov_error *const err) {
  if (!wdt || !path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return ov_indeterminate;
  }

  struct placeholder_entry entry = {0};
  ov_tribool result = ov_indeterminate;

  // Search cache first
  if (wdt->placeholder_cache) {
    size_t const count = OV_ARRAY_LENGTH(wdt->placeholder_cache);
    for (size_t i = 0; i < count; i++) {
      if (wdt->placeholder_cache[i].path && wcscmp(wdt->placeholder_cache[i].path, path) == 0) {
        result = wdt->placeholder_cache[i].accessible ? ov_true : ov_false;
        goto cleanup;
      }
    }
  }

  {
    // Cache miss: perform actual file accessibility check using GetFileAttributes
    DWORD const attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      HRESULT const hr = HRESULT_FROM_WIN32(GetLastError());
      if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && hr != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        OV_ERROR_SET_HRESULT(err, hr);
        goto cleanup;
      }
    }

    bool const accessible = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);

    // Add result to cache
    size_t const path_len = wcslen(path);
    if (!OV_ARRAY_GROW(&entry.path, path_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(entry.path, path);
    entry.accessible = accessible;

    size_t const idx = OV_ARRAY_LENGTH(wdt->placeholder_cache);
    if (!OV_ARRAY_GROW(&wdt->placeholder_cache, idx + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    OV_ARRAY_SET_LENGTH(wdt->placeholder_cache, idx + 1);

    wdt->placeholder_cache[idx] = entry;
    entry = (struct placeholder_entry){0};

    result = accessible ? ov_true : ov_false;
  }

cleanup:
  if (entry.path) {
    OV_ARRAY_DESTROY(&entry.path);
  }
  return result;
}

/**
 * @brief Clean up temporary files in a file list using cleanup callback
 *
 * Iterates through the file list and calls the cleanup callback for each
 * temporary file. Errors are reported but do not stop iteration.
 *
 * @param wdt Wrapped drop target containing parent context with cleanup callback
 * @param file_list File list to process
 */
static void cleanup_temporary_files_in_list(struct wrapped_drop_target *wdt, struct gcmz_file_list *file_list) {
  if (!wdt || !file_list) {
    return;
  }
  struct gcmz_drop *d = wdt->d;
  if (!d || !d->cleanup_fn) {
    return;
  }
  struct ov_error err = {0};
  size_t const file_count = gcmz_file_list_count(file_list);
  for (size_t i = 0; i < file_count; i++) {
    struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
    if (file && file->temporary && file->path) {
      if (!d->cleanup_fn(file->path, d->callback_userdata, &err)) {
        gcmz_logf_error(&err, "%1$hs", "%1$hs", gettext("failed to clean up temporary file"));
        gcmz_logf_warn(NULL, NULL, "Failed to clean up temporary file: %ls", file->path);
        OV_ERROR_REPORT(&err, NULL);
      }
    }
  }
}

// Context for write_dropfiles_paths function
struct write_dropfiles_paths_context {
  bool all_accessible; ///< Set during first pass, used to optimize second pass
};

/**
 * @brief Write file paths in DROPFILES format with placeholder substitution
 *
 * This function operates in two passes:
 * - First pass (dest=NULL): Calculate buffer size and set wctx->all_accessible flag
 * - Second pass (dest!=NULL): Write actual data, using wctx->all_accessible for optimization
 *
 * When all files are accessible (common case), the second pass skips file accessibility
 * checks entirely, improving performance.
 *
 * @param wdt Wrapped drop target containing cache and configuration
 * @param file_list List of files to process
 * @param dest Destination buffer (NULL for first pass to calculate size only)
 * @param wctx Context structure shared between first and second pass
 * @param err Error structure for reporting failures
 * @return Number of wchar_t written/required (including null terminators), or 0 on error
 */
static size_t write_dropfiles_paths(struct wrapped_drop_target *wdt,
                                    struct gcmz_file_list *file_list,
                                    wchar_t *dest,
                                    struct write_dropfiles_paths_context *wctx,
                                    struct ov_error *const err) {
  if (!wdt || !file_list || !wctx) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return 0;
  }

  bool const first_pass = !dest;
  size_t total_len = 0;
  wchar_t *current = dest;
  size_t result = 0;

  if (first_pass) {
    wctx->all_accessible = true;
  }

  {
    size_t const file_count = gcmz_file_list_count(file_list);
    for (size_t i = 0; i < file_count; i++) {
      struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
      if (!file || !file->path) {
        continue;
      }

      wchar_t const *path_to_use = NULL;

      if (!first_pass && wctx->all_accessible) {
        path_to_use = file->path;
      } else {
        switch (is_file_accessible(wdt, file->path, err)) {
        case ov_indeterminate:
          OV_ERROR_ADD_TRACE(err);
          goto cleanup;

        case ov_true:
          path_to_use = file->path;
          break;

        case ov_false:
          if (first_pass) {
            wctx->all_accessible = false;
          }
          if (!wdt->shared_placeholder_path) {
            if (!gcmz_temp_create_unique_file(L"placeholder.txt", &wdt->shared_placeholder_path, err)) {
              OV_ERROR_ADD_TRACE(err);
              goto cleanup;
            }
          }
          path_to_use = wdt->shared_placeholder_path;
          break;
        }
      }

      size_t const len = wcslen(path_to_use);
      total_len += len + 1; // +1 for null terminator

      if (current) {
        wcscpy(current, path_to_use);
        current += len + 1;
      }
    }

    total_len += 1; // +1 for null terminator
    if (current) {
      *current = L'\0';
    }
  }

  result = total_len;

cleanup:
  return result;
}

static struct gcmz_file_list *extract_and_convert_files(struct wrapped_drop_target *const wdt,
                                                        IDataObject *original_dataobj,
                                                        struct ov_error *const err) {
  if (!wdt || !original_dataobj) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  struct gcmz_drop *const d = wdt->d;

  struct gcmz_file_list *file_list = NULL;
  bool success = false;

  {
    file_list = gcmz_file_list_create(err);
    if (!file_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!d->extract_fn(original_dataobj, file_list, d->callback_userdata, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (gcmz_dataobj_is_exo_convert_enabled(original_dataobj) && d->lua_context) {
#if GCMZ_DEBUG
      gcmz_logf_verbose(NULL, NULL, "Invoking EXO file conversion via Lua");
#endif
      struct ov_error exo_err = {0};
      if (!gcmz_lua_call_exo_convert(d->lua_context, file_list, &exo_err)) {
        gcmz_logf_warn(
            &exo_err, "%1$hs", "%1$hs", gettext("EXO file conversion failed, proceeding with original files"));
        OV_ERROR_DESTROY(&exo_err);
      }
    } else {
#if GCMZ_DEBUG
      gcmz_logf_verbose(NULL, NULL, "EXO file conversion skipped");
#endif
    }

    if (gcmz_file_list_count(file_list) == 0) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
  }
  success = true;

cleanup:
  if (!success) {
    if (file_list) {
      gcmz_file_list_destroy(&file_list);
    }
  }
  return file_list;
}

static IDataObject *create_dataobj_from_file_list(struct wrapped_drop_target *const wdt,
                                                  struct gcmz_file_list *file_list,
                                                  struct ov_error *const err) {
  if (!wdt || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  HGLOBAL h = NULL;
  DROPFILES *drop_files = NULL;
  IDataObject *pDataObj = NULL;
  IDataObject *result = NULL;

  {
    // calculate required buffer size for DROPFILES paths
    struct write_dropfiles_paths_context wctx = {0};
    size_t const path_buffer_len = write_dropfiles_paths(wdt, file_list, NULL, &wctx, err);
    if (path_buffer_len == 0) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    size_t const total_size = sizeof(DROPFILES) + path_buffer_len * sizeof(wchar_t);

    h = GlobalAlloc(GMEM_MOVEABLE, total_size);
    if (!h) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    drop_files = (DROPFILES *)GlobalLock(h);
    if (!drop_files) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    *drop_files = (DROPFILES){
        .pFiles = sizeof(DROPFILES),
        .fWide = TRUE,
    };

    wchar_t *path_buffer = (wchar_t *)(void *)(drop_files + 1);
    size_t const written = write_dropfiles_paths(wdt, file_list, path_buffer, &wctx, err);
    if (written == 0) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    HRESULT hr = SHCreateDataObject(NULL, 0, NULL, NULL, &IID_IDataObject, (void **)&pDataObj);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }

    GlobalUnlock(h);
    drop_files = NULL;
    hr = IDataObject_SetData(pDataObj,
                             (&(FORMATETC){
                                 .cfFormat = CF_HDROP,
                                 .dwAspect = DVASPECT_CONTENT,
                                 .lindex = -1,
                                 .tymed = TYMED_HGLOBAL,
                             }),
                             (&(STGMEDIUM){
                                 .tymed = TYMED_HGLOBAL,
                                 .hGlobal = h,
                             }),
                             TRUE);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }
    h = NULL;

    result = pDataObj;
    pDataObj = NULL;
  }

cleanup:
  if (drop_files) {
    GlobalUnlock(h);
    drop_files = NULL;
  }
  if (h) {
    GlobalFree(h);
    h = NULL;
  }
  if (pDataObj) {
    IDataObject_Release(pDataObj);
  }
  return result;
}

static void call_lua_drag_enter_hook(
    struct gcmz_drop *const d, struct gcmz_file_list *file_list, int x, int y, uint32_t key_state) {
  if (!d || !d->lua_context || !file_list) {
    return;
  }

  struct ov_error err = {0};
  bool ok = gcmz_lua_call_drag_enter(d->lua_context, file_list, x, y, key_state, &err);
  if (!ok) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"call_lua_drag_enter_hook: Lua drag_enter hook failed\n");
#endif
    OV_ERROR_REPORT(&err, NULL);
    return;
  }

#if GCMZ_DEBUG
  OutputDebugStringW(L"call_lua_drag_enter_hook: Lua drag_enter hook called successfully\n");
#endif
}

static void call_lua_drag_over_hook(struct gcmz_drop *const d, int x, int y, uint32_t key_state) {
  if (!d || !d->lua_context) {
    return;
  }

  struct ov_error err = {0};
  bool ok = gcmz_lua_call_drag_over(d->lua_context, x, y, key_state, &err);
  if (!ok) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"call_lua_drag_over_hook: Lua drag_over hook failed\n");
#endif
    OV_ERROR_REPORT(&err, NULL);
  }
}

static void call_lua_drag_leave_hook(struct gcmz_drop *const d) {
  if (!d || !d->lua_context) {
    return;
  }

  struct ov_error err = {0};
  bool ok = gcmz_lua_call_drag_leave(d->lua_context, &err);
  if (!ok) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"call_lua_drag_leave_hook: Lua drag_leave hook failed\n");
#endif
    OV_ERROR_REPORT(&err, NULL);
  }
}

static void
call_lua_drop_hook(struct gcmz_drop *const d, struct gcmz_file_list *file_list, int x, int y, uint32_t key_state) {
  if (!d || !d->lua_context || !file_list) {
    return;
  }

  struct ov_error err = {0};
  bool ok = gcmz_lua_call_drop(d->lua_context, file_list, x, y, key_state, &err);
  if (!ok) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"call_lua_drop_hook: Lua drop hook failed\n");
#endif
    OV_ERROR_REPORT(&err, NULL);
    return;
  }

#if GCMZ_DEBUG
  OutputDebugStringW(L"call_lua_drop_hook: Lua drop hook called successfully\n");
#endif
}

static void cleanup_current_entry(struct wrapped_drop_target *const wdt) {
  if (!wdt) {
    return;
  }
  struct gcmz_drop *d = wdt->d;
  if (wdt->shared_placeholder_path && d && d->cleanup_fn) {
    struct ov_error err = {0};
    if (!d->cleanup_fn(wdt->shared_placeholder_path, d->callback_userdata, &err)) {
      OV_ERROR_REPORT(&err, NULL);
    }
    OV_ARRAY_DESTROY(&wdt->shared_placeholder_path);
  }

  if (wdt->placeholder_cache) {
    size_t const count = OV_ARRAY_LENGTH(wdt->placeholder_cache);
    for (size_t i = 0; i < count; i++) {
      struct placeholder_entry *entry = &wdt->placeholder_cache[i];
      if (entry->path) {
        OV_ARRAY_DESTROY(&entry->path);
      }
    }
    OV_ARRAY_DESTROY(&wdt->placeholder_cache);
  }

  cleanup_temporary_files_in_list(wdt, wdt->current_file_list);
  gcmz_file_list_destroy(&wdt->current_file_list);

  if (wdt->current_original) {
    IDataObject_Release(wdt->current_original);
    wdt->current_original = NULL;
  }
  if (wdt->current_replacement) {
    IDataObject_Release(wdt->current_replacement);
    wdt->current_replacement = NULL;
  }
}

// Forward declaration for subclass proc
static LRESULT CALLBACK
drop_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

static void uninstall_drop_subclass(HWND hwnd) {
  if (IsWindow(hwnd)) {
    RemoveWindowSubclass(hwnd, drop_subclass_proc, gcmz_drop_subclass_id);
  }
}

static bool install_drop_subclass(struct wrapped_drop_target *const wdt, struct ov_error *const err) {
  if (!wdt || !wdt->main_window) {
    return true;
  }
  HWND hwnd = (HWND)wdt->main_window;
  if (!SetWindowSubclass(hwnd, drop_subclass_proc, gcmz_drop_subclass_id, (DWORD_PTR)wdt)) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    return false;
  }
  return true;
}

static LRESULT CALLBACK msghook(int nCode, WPARAM wParam, LPARAM lParam) {
  MSG const *const msg = (MSG *)lParam;
  if (g_subclass_message_id == 0 || nCode < 0 || !msg || !msg->hwnd) {
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
  if (msg->message != g_subclass_message_id || msg->wParam != 0 || msg->lParam == 0) {
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
  struct wrapped_drop_target *const wdt = (struct wrapped_drop_target *)msg->lParam;
  struct ov_error err = {0};
  if (!install_drop_subclass(wdt, &err)) {
    OV_ERROR_REPORT(&err, "Failed to install drop subclass");
  }
  LRESULT r = CallNextHookEx(wdt->subclass_hook, nCode, wParam, lParam);
  UnhookWindowsHookEx(wdt->subclass_hook);
  wdt->subclass_hook = NULL;
  return r;
}

static LRESULT CALLBACK
drop_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  (void)uIdSubclass;
  struct wrapped_drop_target *wdt = (struct wrapped_drop_target *)dwRefData;
  if (!wdt) {
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
  }

  switch (uMsg) {
  case WM_NCDESTROY:
    uninstall_drop_subclass(hwnd);
    break;
  case WM_RBUTTONDOWN: {
    int const x = (int)(short)LOWORD(lParam);
    int const y = (int)(short)HIWORD(lParam);
    struct gcmz_drop *d = wdt->d;
    if (d) {
      d->last_rbutton_window = (void *)hwnd;
      d->last_rbutton_x = x;
      d->last_rbutton_y = y;
    }
#if GCMZ_DEBUG
    wchar_t buf[256];
    ov_snprintf_wchar(buf, 256, NULL, L"GCMZDROPS: WM_RBUTTONDOWN hwnd=%p x=%d y=%d\n", hwnd, x, y);
    OutputDebugStringW(buf);
#endif
    break;
  }
  }

  return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static HRESULT STDMETHODCALLTYPE wrapped_drop_target_query_interface(IDropTarget *const This,
                                                                     REFIID riid,
                                                                     void **const ppvObject) {
  if (!This || !ppvObject) {
    return E_POINTER;
  }

  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);

  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
    *ppvObject = &impl->drop_target;
    IDropTarget_AddRef(&impl->drop_target);
    return S_OK;
  }

  *ppvObject = NULL;
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE wrapped_drop_target_add_ref(IDropTarget *const This) {
  if (!This) {
    return 0;
  }
  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);
  return (ULONG)InterlockedIncrement(&impl->ref_count);
}

static ULONG STDMETHODCALLTYPE wrapped_drop_target_release(IDropTarget *const This) {
  if (!This) {
    return 0;
  }
  struct wrapped_drop_target *impl = get_impl_from_drop_target(This);
  ULONG const ref = (ULONG)InterlockedDecrement(&impl->ref_count);
  if (ref == 0) {
    struct gcmz_drop *d = impl->d;
    EnterCriticalSection(&d->targets_cs);
    size_t const len = OV_ARRAY_LENGTH(d->wrapped_targets);
    for (size_t i = 0; i < len; i++) {
      if (d->wrapped_targets[i] == impl) {
        d->wrapped_targets[i] = d->wrapped_targets[len - 1];
        d->wrapped_targets[len - 1] = NULL;
        OV_ARRAY_SET_LENGTH(d->wrapped_targets, len - 1);
        break;
      }
    }
    LeaveCriticalSection(&d->targets_cs);

    // Uninstall subclass if installed
    if (impl->subclass_hook) {
      UnhookWindowsHookEx(impl->subclass_hook);
      impl->subclass_hook = NULL;
    }
    if (impl->main_window) {
      uninstall_drop_subclass((HWND)impl->main_window);
    }

    cleanup_current_entry(impl);
    DeleteCriticalSection(&impl->cs);
    if (impl->original) {
      IDropTarget_Release(impl->original);
      impl->original = NULL;
    }
    OV_FREE(&impl);
  }
  return ref;
}

static IDataObject *prepare_drag_enter_dataobj(struct wrapped_drop_target *const wdt,
                                               IDataObject *original_dataobj,
                                               POINTL pt,
                                               DWORD grfKeyState,
                                               struct ov_error *const err) {
  if (!wdt || !original_dataobj) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  struct gcmz_drop *d = wdt->d;

  struct gcmz_file_list *file_list = NULL;
  IDataObject *replacement_dataobj = NULL;
  IDataObject *result = NULL;

  EnterCriticalSection(&wdt->cs);
  cleanup_current_entry(wdt);

  {
    file_list = extract_and_convert_files(wdt, original_dataobj, err);
    if (!file_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    call_lua_drag_enter_hook(d, file_list, pt.x, pt.y, grfKeyState);
    replacement_dataobj = create_dataobj_from_file_list(wdt, file_list, err);
    if (!replacement_dataobj) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    wdt->current_original = original_dataobj;
    IDataObject_AddRef(wdt->current_original);
    wdt->current_replacement = replacement_dataobj;
    IDataObject_AddRef(wdt->current_replacement);
    wdt->current_file_list = file_list;
    file_list = NULL;

    result = replacement_dataobj;
    replacement_dataobj = NULL;
  }

cleanup:
  LeaveCriticalSection(&wdt->cs);
  if (file_list) {
    gcmz_file_list_destroy(&file_list);
  }
  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }
  return result;
}

static HRESULT STDMETHODCALLTYPE wrapped_drop_target_drag_enter(
    IDropTarget *This, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {

#if GCMZ_DEBUG
  wchar_t debug_msg[256];
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drag_enter: This=%p pt=(%d,%d), grfKeyState=0x%08x, *pdwEffect=0x%08x\n",
                    This,
                    pt.x,
                    pt.y,
                    grfKeyState,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
  dump_data_object(pDataObj);
#endif

  if (!This) {
    return E_INVALIDARG;
  }
  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);
  if (!impl || !impl->original) {
    return E_FAIL;
  }

  IDataObject *replacement_dataobj = NULL;
  IDataObject *data_to_use = pDataObj;
  HRESULT hr = E_FAIL;
  struct ov_error err = {0};
  bool success = false;

  if (!pDataObj || !pdwEffect) {
    OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
    goto cleanup;
  }
  replacement_dataobj = prepare_drag_enter_dataobj(impl, pDataObj, pt, grfKeyState, &err);
  if (!replacement_dataobj) {
    if (ov_error_is(&err, ov_error_type_generic, ov_error_generic_not_found)) {
      // No files extracted, proceed with original data object
      OV_ERROR_DESTROY(&err);
      success = true;
    }
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  data_to_use = replacement_dataobj;
  success = true;

cleanup:
  if (!success) {
    gcmz_logf_error(&err, NULL, "%1$hs", gettext("DragEnter hook processing failed"));
    OV_ERROR_DESTROY(&err);
  }

  hr = IDropTarget_DragEnter(impl->original, data_to_use, grfKeyState, pt, pdwEffect);

#if GCMZ_DEBUG
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drag_enter: hooked call returned hr=0x%08x, *pdwEffect=0x%08x\n",
                    hr,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
#endif

  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }

  return hr;
}

static IDataObject *prepare_drag_over_dataobj(struct wrapped_drop_target *const wdt,
                                              POINTL pt,
                                              DWORD grfKeyState,
                                              struct ov_error *const err) {
  if (!wdt) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  struct gcmz_drop *const d = wdt->d;
  IDataObject *result = NULL;

  call_lua_drag_over_hook(d, pt.x, pt.y, grfKeyState);
  EnterCriticalSection(&wdt->cs);
  // DragOver doesn't provide the IDataObject directly,
  // but we can check if we have a current replacement from DragEnter
  if (wdt->current_original && wdt->current_replacement) {
    result = wdt->current_replacement;
    IDataObject_AddRef(result);
#if GCMZ_DEBUG
    OutputDebugStringW(L"prepare_drag_over_dataobj: Using replacement data object\n");
#endif
  }
  LeaveCriticalSection(&wdt->cs);
  return result;
}

static HRESULT STDMETHODCALLTYPE wrapped_drop_target_drag_over(IDropTarget *This,
                                                               DWORD grfKeyState,
                                                               POINTL pt,
                                                               DWORD *pdwEffect) {
#if GCMZ_DEBUG
  wchar_t debug_msg[256];
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drag_over: pt=(%d,%d), grfKeyState=0x%08x, *pdwEffect=0x%08x\n",
                    pt.x,
                    pt.y,
                    grfKeyState,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
#endif

  if (!This) {
    return E_INVALIDARG;
  }

  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);

  if (!impl || !impl->main_window) {
    if (impl && impl->original) {
      HRESULT hr = IDropTarget_DragOver(impl->original, grfKeyState, pt, pdwEffect);
#if GCMZ_DEBUG
      ov_snprintf_wchar(debug_msg,
                        256,
                        NULL,
                        L"wrapped_drop_target_drag_over: original call returned hr=0x%08x, *pdwEffect=0x%08x\n",
                        hr,
                        pdwEffect ? *pdwEffect : 0);
      OutputDebugStringW(debug_msg);
#endif
      return hr;
    }
    return E_FAIL;
  }

  IDataObject *replacement_dataobj = NULL;
  HRESULT hr = E_FAIL;
  struct ov_error err = {0};

  // Try to prepare hook processing and get replacement data object
  // Note: replacement_dataobj can be NULL if no replacement is available (not an error)
  replacement_dataobj = prepare_drag_over_dataobj(impl, pt, grfKeyState, &err);
  hr = IDropTarget_DragOver(impl->original, grfKeyState, pt, pdwEffect);
  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }

#if GCMZ_DEBUG
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drag_over: hooked call returned hr=0x%08x, *pdwEffect=0x%08x\n",
                    hr,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
#endif
  return hr;
}

static bool prepare_drag_leave(struct wrapped_drop_target *const wdt, struct ov_error *const err) {
  if (!wdt) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  struct gcmz_drop *const d = wdt->d;
  call_lua_drag_leave_hook(d);
  EnterCriticalSection(&wdt->cs);
  cleanup_current_entry(wdt);
  LeaveCriticalSection(&wdt->cs);
  return true;
}

static HRESULT STDMETHODCALLTYPE wrapped_drop_target_drag_leave(IDropTarget *This) {
#if GCMZ_DEBUG
  OutputDebugStringW(L"wrapped_drop_target_drag_leave: called\n");
#endif

  if (!This) {
    return E_INVALIDARG;
  }

  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);

  if (!impl || !impl->main_window) {
    if (impl && impl->original) {
      HRESULT hr = IDropTarget_DragLeave(impl->original);
#if GCMZ_DEBUG
      wchar_t debug_msg[256];
      ov_snprintf_wchar(
          debug_msg, 256, NULL, L"wrapped_drop_target_drag_leave: original call returned hr=0x%08x\n", hr);
      OutputDebugStringW(debug_msg);
#endif
      return hr;
    }
    return E_FAIL;
  }

  HRESULT hr = E_FAIL;
  struct ov_error err = {0};

  if (!prepare_drag_leave(impl, &err)) {
    OV_ERROR_REPORT(&err, NULL);
    goto cleanup;
  }

cleanup:
  hr = IDropTarget_DragLeave(impl->original);

#if GCMZ_DEBUG
  wchar_t debug_msg[256];
  ov_snprintf_wchar(debug_msg, 256, NULL, L"wrapped_drop_target_drag_leave: hooked call returned hr=0x%08x\n", hr);
  OutputDebugStringW(debug_msg);
#endif
  return hr;
}

static IDataObject *prepare_drop_dataobj(struct wrapped_drop_target *const wdt,
                                         IDataObject *original_dataobj,
                                         POINTL pt,
                                         DWORD grfKeyState,
                                         struct ov_error *const err) {
  if (!wdt || !original_dataobj) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  struct gcmz_drop *d = wdt->d;

  struct gcmz_file_list *file_list = NULL;
  IDataObject *replacement_dataobj = NULL;
  wchar_t *managed_path = NULL;
  IDataObject *result = NULL;

  EnterCriticalSection(&wdt->cs);
  cleanup_current_entry(wdt);

  {
    file_list = extract_and_convert_files(wdt, original_dataobj, err);
    if (!file_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    call_lua_drop_hook(d, file_list, pt.x, pt.y, grfKeyState);
    if (d->file_manage_fn) {
      size_t const file_count = gcmz_file_list_count(file_list);
      for (size_t i = 0; i < file_count; i++) {
        struct gcmz_file *file = gcmz_file_list_get_mutable(file_list, i);
        if (!file || !file->path) {
          continue;
        }

        managed_path = NULL;
        if (!d->file_manage_fn(file->path, &managed_path, d->callback_userdata, err)) {
          // Report error but continue processing other files
          OV_ERROR_REPORT(err, NULL);
          continue;
        }

        // If path changed, update the file list
        if (wcscmp(file->path, managed_path) != 0) {
          // If the old path was temporary, clean it up before replacing
          if (file->temporary && d->cleanup_fn) {
            struct ov_error cleanup_err = {0};
            if (!d->cleanup_fn(file->path, d->callback_userdata, &cleanup_err)) {
              OV_ERROR_REPORT(&cleanup_err, NULL);
            }
          }
          OV_ARRAY_DESTROY(&file->path);
          file->path = managed_path;
          managed_path = NULL;
          file->temporary = false;
        } else {
          OV_ARRAY_DESTROY(&managed_path);
        }
      }
    }

    replacement_dataobj = create_dataobj_from_file_list(wdt, file_list, err);
    if (!replacement_dataobj) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    wdt->current_original = original_dataobj;
    IDataObject_AddRef(wdt->current_original);
    wdt->current_replacement = replacement_dataobj;
    IDataObject_AddRef(wdt->current_replacement);
    wdt->current_file_list = file_list;
    file_list = NULL; // Ownership transferred

    result = replacement_dataobj;
    replacement_dataobj = NULL;
  }

cleanup:
  LeaveCriticalSection(&wdt->cs);
  if (managed_path) {
    OV_ARRAY_DESTROY(&managed_path);
  }
  if (file_list) {
    gcmz_file_list_destroy(&file_list);
  }
  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }
  return result;
}

static HRESULT STDMETHODCALLTYPE
wrapped_drop_target_drop(IDropTarget *This, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
#if GCMZ_DEBUG
  wchar_t debug_msg[256];
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drop: pt=(%d,%d), grfKeyState=0x%08x, *pdwEffect=0x%08x\n",
                    pt.x,
                    pt.y,
                    grfKeyState,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
#endif

  if (!This) {
    return E_INVALIDARG;
  }

  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);

  if (!impl || !impl->main_window) {
    if (impl && impl->original) {
      HRESULT hr = IDropTarget_Drop(impl->original, pDataObj, grfKeyState, pt, pdwEffect);
#if GCMZ_DEBUG
      ov_snprintf_wchar(debug_msg,
                        256,
                        NULL,
                        L"wrapped_drop_target_drop: original call returned hr=0x%08x, *pdwEffect=0x%08x\n",
                        hr,
                        pdwEffect ? *pdwEffect : 0);
      OutputDebugStringW(debug_msg);
#endif
      return hr;
    }
    return E_FAIL;
  }

  if (!pDataObj) {
    return IDropTarget_Drop(impl->original, pDataObj, grfKeyState, pt, pdwEffect);
  }

  IDataObject *replacement_dataobj = NULL;
  IDataObject *data_to_use = pDataObj;
  HRESULT hr = E_FAIL;
  struct ov_error err = {0};

  replacement_dataobj = prepare_drop_dataobj(impl, pDataObj, pt, grfKeyState, &err);
  if (!replacement_dataobj) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"wrapped_drop_target_drop: No replacement, passing through original\n");
#endif
    goto cleanup;
  }

  // Workaround for AviUtl2's IDropTarget implementation limitation
  //
  // Problem:
  //   AviUtl2 internally stores file paths during DragEnter and reuses them during Drop,
  //   ignoring any IDataObject changes made at Drop time. This causes two major issues:
  //   1. Files created lazily by drag sources (e.g., 7-zip File Manager) may not exist during DragEnter
  //   2. Lua-based file processing cannot modify files before AviUtl2 accesses them
  //
  // Solution:
  //   Execute Leave->Enter->Over->Drop sequence at Drop time to force AviUtl2 to
  //   re-capture file paths from the fully prepared IDataObject during the second DragEnter.
  //   This ensures all files exist and Lua processing is complete before AviUtl2 stores paths.
#if GCMZ_DEBUG
  OutputDebugStringW(L"wrapped_drop_target_drop: Executing Leave->Enter->Over->Drop sequence\n");
#endif

  hr = IDropTarget_DragLeave(impl->original);
#if GCMZ_DEBUG
  ov_snprintf_wchar(debug_msg, 256, NULL, L"wrapped_drop_target_drop: DragLeave returned hr=0x%08x\n", hr);
  OutputDebugStringW(debug_msg);
#endif
  hr = IDropTarget_DragEnter(impl->original, replacement_dataobj, grfKeyState, pt, pdwEffect);
#if GCMZ_DEBUG
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drop: DragEnter returned hr=0x%08x, effect=0x%08x\n",
                    hr,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
#endif
  hr = IDropTarget_DragOver(impl->original, grfKeyState, pt, pdwEffect);
#if GCMZ_DEBUG
  ov_snprintf_wchar(debug_msg,
                    256,
                    NULL,
                    L"wrapped_drop_target_drop: DragOver returned hr=0x%08x, effect=0x%08x\n",
                    hr,
                    pdwEffect ? *pdwEffect : 0);
  OutputDebugStringW(debug_msg);
#endif
  data_to_use = replacement_dataobj;
cleanup:
  hr = IDropTarget_Drop(impl->original, data_to_use, grfKeyState, pt, pdwEffect);

  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }
  return hr;
}

struct gcmz_drop *gcmz_drop_create(gcmz_drop_dataobj_extract_fn const extract_fn,
                                   gcmz_drop_cleanup_temp_file_fn const cleanup_fn,
                                   gcmz_drop_project_data_provider_fn const project_data_fn,
                                   gcmz_drop_file_manage_fn const file_manage_fn,
                                   void *const callback_userdata,
                                   struct gcmz_lua_context *const lua_context,
                                   struct ov_error *const err) {
  if (!err || !extract_fn || !cleanup_fn) {
    if (err) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    }
    return NULL;
  }

  struct gcmz_drop *d = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&d, 1, sizeof(*d))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    *d = (struct gcmz_drop){
        .extract_fn = extract_fn,
        .cleanup_fn = cleanup_fn,
        .project_data_fn = project_data_fn,
        .file_manage_fn = file_manage_fn,
        .callback_userdata = callback_userdata,
        .lua_context = lua_context,
    };

    InitializeCriticalSection(&d->targets_cs);

    // Register subclass message ID for cross-thread subclass installation
    g_subclass_message_id = RegisterWindowMessageW(gcmz_drop_subclass_message_name);
    if (g_subclass_message_id == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    result = true;
  }

cleanup:
  if (!result && d) {
    DeleteCriticalSection(&d->targets_cs);
    OV_FREE(&d);
  }
  return result ? d : NULL;
}

void gcmz_drop_destroy(struct gcmz_drop **const d) {
  if (!d || !*d) {
    return;
  }

  struct gcmz_drop *c = *d;
  EnterCriticalSection(&c->targets_cs);
  {
    while (OV_ARRAY_LENGTH(c->wrapped_targets) > 0) {
      IDropTarget_Release(&c->wrapped_targets[0]->drop_target);
    }
    if (c->wrapped_targets) {
      OV_ARRAY_DESTROY(&c->wrapped_targets);
    }
  }
  LeaveCriticalSection(&c->targets_cs);

#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_drop_destroy: Drop system cleaned up successfully\n");
#endif

  DeleteCriticalSection(&c->targets_cs);
  OV_FREE(d);
}

bool gcmz_drop_register_window(struct gcmz_drop *const ctx, void *const window, struct ov_error *const err) {
  if (!ctx || !window) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct wrapped_drop_target *wdt = NULL;
  bool success = false;

  EnterCriticalSection(&ctx->targets_cs);

  {
    size_t const len = OV_ARRAY_LENGTH(ctx->wrapped_targets);
    for (size_t i = 0; i < len; i++) {
      if (ctx->wrapped_targets[i] && ctx->wrapped_targets[i]->main_window == window) {
        success = true;
        goto cleanup;
      }
    }
    if (!OV_REALLOC(&wdt, 1, sizeof(*wdt))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    static IDropTargetVtbl const drop_target_vtbl = {
        .QueryInterface = wrapped_drop_target_query_interface,
        .AddRef = wrapped_drop_target_add_ref,
        .Release = wrapped_drop_target_release,
        .DragEnter = wrapped_drop_target_drag_enter,
        .DragOver = wrapped_drop_target_drag_over,
        .DragLeave = wrapped_drop_target_drag_leave,
        .Drop = wrapped_drop_target_drop,
    };
    *wdt = (struct wrapped_drop_target){
        .drop_target =
            {
                .lpVtbl = &drop_target_vtbl,
            },
        .d = ctx,
        .ref_count = 1,
        .main_window = window,
    };
    InitializeCriticalSection(&wdt->cs);

    wdt->original = (IDropTarget *)GetPropW((HWND)window, L"OleDropTargetInterface");
    if (!wdt->original) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to get IDropTarget interface");
      goto cleanup;
    }
    IDropTarget_AddRef(wdt->original);

    size_t const new_idx = OV_ARRAY_LENGTH(ctx->wrapped_targets);
    if (!OV_ARRAY_GROW(&ctx->wrapped_targets, new_idx + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ctx->wrapped_targets[new_idx] = wdt;
    OV_ARRAY_SET_LENGTH(ctx->wrapped_targets, new_idx + 1);

    HRESULT hr = RevokeDragDrop((HWND)window);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }
    hr = RegisterDragDrop((HWND)window, &wdt->drop_target);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }

    // Install subclass for right-click position tracking
    DWORD const window_thread_id = GetWindowThreadProcessId((HWND)window, NULL);
    if (window_thread_id == GetCurrentThreadId()) {
      // Same thread: install subclass directly
      if (!install_drop_subclass(wdt, err)) {
        gcmz_logf_warn(err, "%s", "%s", "failed to install subclass for right-click tracking");
        OV_ERROR_DESTROY(err);
        // Continue without subclass - not a fatal error
      }
    } else {
      // Different thread: use hook to install subclass
      wdt->subclass_hook = SetWindowsHookExW(WH_GETMESSAGE, msghook, NULL, window_thread_id);
      if (!wdt->subclass_hook) {
        gcmz_logf_warn(NULL, "%s", "%s", "failed to set hook for cross-thread subclass installation");
        // Continue without subclass - not a fatal error
      } else {
        if (!PostMessageW((HWND)window, g_subclass_message_id, 0, (LPARAM)wdt)) {
          gcmz_logf_warn(NULL, "%s", "%s", "failed to post message for cross-thread subclass installation");
          UnhookWindowsHookEx(wdt->subclass_hook);
          wdt->subclass_hook = NULL;
        }
      }
    }

    success = true;
  }

cleanup:
  LeaveCriticalSection(&ctx->targets_cs);
  if (wdt) {
    IDropTarget_Release(&wdt->drop_target);
  }
  return success;
}

static IDataObject *create_simulation_dataobj(struct gcmz_file_list const *const file_list,
                                              int const x,
                                              int const y,
                                              struct ov_error *const err) {
  if (!file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  size_t const file_count = gcmz_file_list_count(file_list);
  HGLOBAL hGlobal = NULL;
  DROPFILES *pDropFiles = NULL;
  IDataObject *pDataObject = NULL;
  wchar_t *pFiles = NULL;
  bool success = false;
  HRESULT hr = E_FAIL;

  // Calculate total size needed for DROPFILES structure
  size_t total_size = sizeof(DROPFILES);
  for (size_t i = 0; i < file_count; i++) {
    struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
    if (file && file->path) {
      total_size += (wcslen(file->path) + 1) * sizeof(wchar_t);
    }
  }
  total_size += sizeof(wchar_t); // Final null terminator

  hGlobal = GlobalAlloc(GMEM_MOVEABLE, total_size);
  if (!hGlobal) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  pDropFiles = (DROPFILES *)GlobalLock(hGlobal);
  if (!pDropFiles) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  *pDropFiles = (DROPFILES){
      .pFiles = sizeof(DROPFILES),
      .pt = (POINT){x, y},
      .fNC = FALSE,
      .fWide = TRUE,
  };

  pFiles = (wchar_t *)(void *)(pDropFiles + 1);
  for (size_t i = 0; i < file_count; i++) {
    struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
    if (file && file->path) {
      wcscpy(pFiles, file->path);
      pFiles += wcslen(file->path) + 1;
    }
  }
  *pFiles = L'\0';

  GlobalUnlock(hGlobal);
  pDropFiles = NULL;

  hr = SHCreateDataObject(NULL, 0, NULL, NULL, &IID_IDataObject, (void **)&pDataObject);
  if (FAILED(hr)) {
    OV_ERROR_SET_HRESULT(err, hr);
    goto cleanup;
  }

  hr = IDataObject_SetData(pDataObject,
                           (&(FORMATETC){
                               .cfFormat = CF_HDROP,
                               .dwAspect = DVASPECT_CONTENT,
                               .lindex = -1,
                               .tymed = TYMED_HGLOBAL,
                           }),
                           (&(STGMEDIUM){
                               .tymed = TYMED_HGLOBAL,
                               .hGlobal = hGlobal,
                               .pUnkForRelease = NULL,
                           }),
                           TRUE);
  if (FAILED(hr)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  hGlobal = NULL; // Ownership transferred

  success = true;

cleanup:
  if (pDropFiles) {
    GlobalUnlock(hGlobal);
  }
  if (hGlobal) {
    GlobalFree(hGlobal);
  }
  if (!success && pDataObject) {
    IDataObject_Release(pDataObject);
    pDataObject = NULL;
  }
  return pDataObject;
}

bool gcmz_drop_inject_dataobject(struct gcmz_drop *const d,
                                 void *const window,
                                 void *const dataobj,
                                 int const x,
                                 int const y,
                                 bool const use_exo_converter,
                                 struct ov_error *const err) {
  if (!d || !dataobj || !window) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  // Find the specified window in registered targets
  EnterCriticalSection(&d->targets_cs);
  struct wrapped_drop_target *wdt = NULL;
  size_t const len = OV_ARRAY_LENGTH(d->wrapped_targets);
  for (size_t i = 0; i < len; ++i) {
    if (d->wrapped_targets[i] && d->wrapped_targets[i]->main_window == window) {
      wdt = d->wrapped_targets[i];
      break;
    }
  }
  if (!wdt) {
    LeaveCriticalSection(&d->targets_cs);
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "window is not registered");
    return false;
  }
  IDropTarget *drop_target = &wdt->drop_target;
  void *main_window = wdt->main_window;
  LeaveCriticalSection(&d->targets_cs);

  bool result = false;
  IDataObject *pDataObject = (IDataObject *)dataobj;
  IDataObject *wrapped = NULL;
  DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
  POINT pt0 = {0};
  POINTL ptl = {0};
  HRESULT hr = E_FAIL;

  // Wrap
  wrapped = (IDataObject *)gcmz_dataobj_create(pDataObject, use_exo_converter, err);
  if (!wrapped) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  ClientToScreen((HWND)main_window, &pt0);
  ptl.x = x + pt0.x;
  ptl.y = y + pt0.y;
  hr = IDropTarget_DragEnter(drop_target, wrapped, MK_LBUTTON, ptl, &dwEffect);
  if (hr == S_OK) {
    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hr = IDropTarget_DragOver(drop_target, MK_LBUTTON, ptl, &dwEffect);
    if (hr == S_OK && dwEffect != DROPEFFECT_NONE) {
      hr = IDropTarget_Drop(drop_target, wrapped, 0, ptl, &dwEffect);
    } else {
      hr = IDropTarget_DragLeave(drop_target);
    }
  }
  if (FAILED(hr)) {
    OV_ERROR_SET_HRESULT(err, hr);
    goto cleanup;
  }
  result = true;

cleanup:
  if (wrapped) {
    IDataObject_Release(wrapped);
  }
  return result;
}

bool gcmz_drop_simulate_drop(struct gcmz_drop *const d,
                             void *const window,
                             struct gcmz_file_list const *const file_list,
                             int const x,
                             int const y,
                             bool const use_exo_converter,
                             struct ov_error *const err) {
  if (!d || !file_list || !window) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  size_t const file_count = gcmz_file_list_count(file_list);
  if (file_count == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  IDataObject *pDataObject = NULL;

  pDataObject = create_simulation_dataobj(file_list, x, y, err);
  if (!pDataObject) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_drop_inject_dataobject(d, window, pDataObject, x, y, use_exo_converter, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;
cleanup:
  if (pDataObject) {
    IDataObject_Release(pDataObject);
    pDataObject = NULL;
  }
  return result;
}

bool gcmz_drop_get_right_click_position(
    struct gcmz_drop *const ctx, void **const window, int *const x, int *const y, struct ov_error *const err) {
  if (!ctx || !window || !x || !y) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (!ctx->last_rbutton_window) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_not_found, "no right-click recorded yet");
    return false;
  }

  *window = ctx->last_rbutton_window;
  *x = ctx->last_rbutton_x;
  *y = ctx->last_rbutton_y;
  return true;
}
