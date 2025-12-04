#include "drop.h"

#include "gcmz_types.h"

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
  bool current_from_external_api;              ///< Whether current drop originated from external API
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
 * @param d Drop context containing cleanup callback
 * @param file_list File list to process
 */
static void cleanup_temporary_files_in_list(struct gcmz_drop *d, struct gcmz_file_list *file_list) {
  if (!d || !d->cleanup_fn || !file_list) {
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

/**
 * @brief Callback type for writing file paths to DROPFILES buffer
 *
 * @param dest Destination buffer (NULL for size calculation pass)
 * @param userdata User-provided context (includes file list and other data)
 * @param err Error structure for reporting failures
 * @return Number of wchar_t written/required (including null terminators), or 0 on error
 */
typedef size_t (*dropfiles_path_writer_fn)(wchar_t *dest, void *userdata, struct ov_error *err);

/**
 * @brief Create IDataObject with CF_HDROP format using custom path writer
 *
 * This is a common helper that handles DROPFILES structure creation, GlobalAlloc,
 * and IDataObject setup. The actual path writing is delegated to the callback.
 *
 * @param x X coordinate to store in DROPFILES
 * @param y Y coordinate to store in DROPFILES
 * @param writer_fn Callback function to write paths
 * @param writer_userdata Context passed to writer callback (includes file list)
 * @param err Error structure for reporting failures
 * @return IDataObject on success, NULL on failure
 */
static IDataObject *create_dropfiles_dataobj(
    int x, int y, dropfiles_path_writer_fn writer_fn, void *writer_userdata, struct ov_error *const err) {
  if (!writer_fn) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  HGLOBAL h = NULL;
  DROPFILES *drop_files = NULL;
  IDataObject *pDataObj = NULL;
  IDataObject *result = NULL;

  {
    // First pass: calculate required buffer size
    size_t const path_buffer_len = writer_fn(NULL, writer_userdata, err);
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
        .pt = {x, y},
        .fNC = FALSE,
        .fWide = TRUE,
    };

    // Second pass: write actual paths
    wchar_t *path_buffer = (wchar_t *)(void *)(drop_files + 1);
    size_t const written = writer_fn(path_buffer, writer_userdata, err);
    if (written == 0) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    GlobalUnlock(h);
    drop_files = NULL;

    HRESULT hr = SHCreateDataObject(NULL, 0, NULL, NULL, &IID_IDataObject, (void **)&pDataObj);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }

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
    h = NULL; // Ownership transferred

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

struct placeholder_writer_context {
  struct wrapped_drop_target *wdt;
  struct gcmz_file_list *file_list;
  bool all_accessible; ///< Set during first pass, used to optimize second pass
};

/**
 * @brief Path writer with placeholder substitution for inaccessible files
 *
 * This function operates in two passes:
 * - First pass (dest=NULL): Calculate buffer size and set ctx->all_accessible flag
 * - Second pass (dest!=NULL): Write actual data, using ctx->all_accessible for optimization
 *
 * When all files are accessible (common case), the second pass skips file accessibility
 * checks entirely, improving performance.
 */
static size_t placeholder_path_writer(wchar_t *dest, void *userdata, struct ov_error *err) {
  struct placeholder_writer_context *ctx = (struct placeholder_writer_context *)userdata;
  if (!ctx || !ctx->wdt || !ctx->file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return 0;
  }

  struct wrapped_drop_target *wdt = ctx->wdt;
  struct gcmz_file_list *file_list = ctx->file_list;
  bool const first_pass = !dest;
  size_t total_len = 0;
  wchar_t *current = dest;
  size_t result = 0;

  if (first_pass) {
    ctx->all_accessible = true;
  }

  {
    size_t const file_count = gcmz_file_list_count(file_list);
    for (size_t i = 0; i < file_count; i++) {
      struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
      if (!file || !file->path) {
        continue;
      }

      wchar_t const *path_to_use = NULL;

      if (!first_pass && ctx->all_accessible) {
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
            ctx->all_accessible = false;
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

static IDataObject *create_dataobj_with_placeholders(
    struct wrapped_drop_target *const wdt, struct gcmz_file_list *file_list, int x, int y, struct ov_error *const err) {
  if (!wdt || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  IDataObject *result = create_dropfiles_dataobj(x,
                                                 y,
                                                 placeholder_path_writer,
                                                 &(struct placeholder_writer_context){
                                                     .wdt = wdt,
                                                     .file_list = file_list,
                                                     .all_accessible = false,
                                                 },
                                                 err);
  if (!result) {
    OV_ERROR_ADD_TRACE(err);
    return NULL;
  }
  return result;
}

/**
 * @brief Capture current modifier key states
 *
 * @return Modifier key flags (gcmz_modifier_key_flags)
 */
static inline uint32_t capture_modifier_keys(void) {
  uint32_t modifier_keys = 0;
  if (GetAsyncKeyState(VK_MENU) & 0x8000) {
    modifier_keys |= gcmz_modifier_alt;
  }
  if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) {
    modifier_keys |= gcmz_modifier_win;
  }
  return modifier_keys;
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

  cleanup_temporary_files_in_list(wdt->d, wdt->current_file_list);
  gcmz_file_list_destroy(&wdt->current_file_list);

  if (wdt->current_original) {
    IDataObject_Release(wdt->current_original);
    wdt->current_original = NULL;
  }
  if (wdt->current_replacement) {
    IDataObject_Release(wdt->current_replacement);
    wdt->current_replacement = NULL;
  }
  wdt->current_from_external_api = false;
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
    bool const from_external_api = gcmz_dataobj_is_from_external_api(original_dataobj);
    wdt->current_from_external_api = from_external_api;

    file_list = extract_and_convert_files(wdt, original_dataobj, err);
    if (!file_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    {
      struct ov_error lua_err = {0};
      if (!gcmz_lua_call_drag_enter(
              d->lua_context, file_list, grfKeyState, capture_modifier_keys(), from_external_api, &lua_err)) {
        gcmz_logf_warn(&lua_err, "%1$s", gettext("error occurred while executing %1$s script handler"), "drag_enter");
        OV_ERROR_DESTROY(&lua_err);
      }
    }
    replacement_dataobj = create_dataobj_with_placeholders(wdt, file_list, pt.x, pt.y, err);
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

static HRESULT STDMETHODCALLTYPE wrapped_drop_target_drag_over(IDropTarget *This,
                                                               DWORD grfKeyState,
                                                               POINTL pt,
                                                               DWORD *pdwEffect) {
  if (!This) {
    return E_INVALIDARG;
  }
  struct wrapped_drop_target *const impl = get_impl_from_drop_target(This);
  if (!impl || !impl->original) {
    return E_FAIL;
  }
  return IDropTarget_DragOver(impl->original, grfKeyState, pt, pdwEffect);
}

static bool prepare_drag_leave(struct wrapped_drop_target *const wdt, struct ov_error *const err) {
  if (!wdt) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  struct gcmz_drop *const d = wdt->d;
  {
    struct ov_error lua_err = {0};
    if (!gcmz_lua_call_drag_leave(d->lua_context, &lua_err)) {
      gcmz_logf_warn(&lua_err, "%1$s", gettext("error occurred while executing %1$s script handler"), "drag_leave");
      OV_ERROR_DESTROY(&lua_err);
    }
  }
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

  // Get from_external_api flag before cleanup_current_entry resets it
  bool const from_external_api = gcmz_dataobj_is_from_external_api(original_dataobj);

  EnterCriticalSection(&wdt->cs);
  cleanup_current_entry(wdt);

  {
    file_list = extract_and_convert_files(wdt, original_dataobj, err);
    if (!file_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    {
      struct ov_error lua_err = {0};
      if (!gcmz_lua_call_drop(
              d->lua_context, file_list, grfKeyState, capture_modifier_keys(), from_external_api, &lua_err)) {
        gcmz_logf_warn(&lua_err, "%1$s", gettext("error occurred while executing %1$s script handler"), "drop");
        OV_ERROR_DESTROY(&lua_err);
      }
    }
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

    replacement_dataobj = create_dataobj_with_placeholders(wdt, file_list, pt.x, pt.y, err);
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
  if (FAILED(hr)) {
    goto cleanup;
  }

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
  if (FAILED(hr)) {
    IDropTarget_DragLeave(impl->original);
    goto cleanup;
  }

  if (pdwEffect && *pdwEffect == DROPEFFECT_NONE) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"wrapped_drop_target_drop: Drop not allowed (DROPEFFECT_NONE), cancelling\n");
#endif
    IDropTarget_DragLeave(impl->original);
    hr = S_OK;
    goto cleanup;
  }

  data_to_use = replacement_dataobj;
  hr = IDropTarget_Drop(impl->original, data_to_use, grfKeyState, pt, pdwEffect);

cleanup:

  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }
  return hr;
}

struct gcmz_drop *gcmz_drop_create(gcmz_drop_dataobj_extract_fn const extract_fn,
                                   gcmz_drop_cleanup_temp_file_fn const cleanup_fn,
                                   gcmz_drop_file_manage_fn const file_manage_fn,
                                   void *const callback_userdata,
                                   struct gcmz_lua_context *const lua_context,
                                   struct ov_error *const err) {
  if (!err || !extract_fn || !cleanup_fn || !lua_context) {
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

// Context for simple path writer
struct simple_writer_context {
  struct gcmz_file_list const *file_list;
};

/**
 * @brief Simple path writer that writes paths directly without placeholder substitution
 */
static size_t simple_path_writer(wchar_t *dest, void *userdata, struct ov_error *err) {
  struct simple_writer_context const *ctx = (struct simple_writer_context const *)userdata;
  if (!ctx || !ctx->file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return 0;
  }

  struct gcmz_file_list const *file_list = ctx->file_list;
  size_t total_len = 0;
  wchar_t *current = dest;

  size_t const file_count = gcmz_file_list_count(file_list);
  for (size_t i = 0; i < file_count; i++) {
    struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
    if (!file || !file->path) {
      continue;
    }

    size_t const len = wcslen(file->path);
    total_len += len + 1; // +1 for null terminator

    if (current) {
      wcscpy(current, file->path);
      current += len + 1;
    }
  }

  total_len += 1; // Final null terminator
  if (current) {
    *current = L'\0';
  }

  return total_len;
}

void *gcmz_drop_create_file_list_dataobj(struct gcmz_file_list const *const file_list,
                                         int const x,
                                         int const y,
                                         struct ov_error *const err) {
  if (!file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  size_t const file_count = gcmz_file_list_count(file_list);
  if (file_count == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }
  IDataObject *pDataObject = create_dropfiles_dataobj(x,
                                                      y,
                                                      simple_path_writer,
                                                      &(struct simple_writer_context){
                                                          .file_list = file_list,
                                                      },
                                                      err);
  if (!pDataObject) {
    OV_ERROR_ADD_TRACE(err);
    return NULL;
  }
  return pDataObject;
}

bool gcmz_drop_simulate_drop(struct gcmz_drop *const d,
                             void *const window,
                             void *const dataobj,
                             int const x,
                             int const y,
                             bool const use_exo_converter,
                             bool const from_external_api,
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
  IDataObject *replacement_dataobj = NULL;
  DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
  POINT pt0 = {0};
  POINTL ptl = {0};
  HRESULT hr = E_FAIL;

  {
    wrapped = (IDataObject *)gcmz_dataobj_create(pDataObject, use_exo_converter, from_external_api, err);
    if (!wrapped) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    ClientToScreen((HWND)main_window, &pt0);
    ptl.x = x + pt0.x;
    ptl.y = y + pt0.y;
    hr = IDropTarget_DragEnter(drop_target, wrapped, MK_LBUTTON, ptl, &dwEffect);
    if (hr != S_OK) {
      if (FAILED(hr)) {
        OV_ERROR_SET_HRESULT(err, hr);
      }
      goto cleanup;
    }

    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hr = IDropTarget_DragOver(drop_target, MK_LBUTTON, ptl, &dwEffect);
    if (hr != S_OK || dwEffect == DROPEFFECT_NONE) {
      // Drop not allowed at this position
      IDropTarget_DragLeave(drop_target);
      if (FAILED(hr)) {
        OV_ERROR_SET_HRESULT(err, hr);
      }
      goto cleanup;
    }

    // Process files through Lua hooks and file management
    replacement_dataobj = prepare_drop_dataobj(wdt, wrapped, ptl, 0, err);
    if (!replacement_dataobj) {
      IDropTarget_DragLeave(wdt->original);
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Execute drop synchronously
    hr = IDropTarget_Drop(wdt->original, replacement_dataobj, 0, ptl, &dwEffect);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }
  }
  result = true;

cleanup:
  if (replacement_dataobj) {
    IDataObject_Release(replacement_dataobj);
  }
  if (wrapped) {
    IDataObject_Release(wrapped);
  }
  return result;
}

/**
 * @brief Internal context for external drop completion
 *
 * Similar to complete_context_internal but for external API drops.
 * Manages file list and drop target state for deferred completion.
 */
struct complete_context_external {
  struct gcmz_drop_complete_context pub; ///< Public context (must be first for casting)
  struct gcmz_drop *d;                   ///< Drop context for looking up target
  struct gcmz_file_list *file_list;      ///< Processed file list (owned)
  IDataObject *dataobj;                  ///< Data object for drop
};

/**
 * @brief Complete the external drop operation
 *
 * This function performs either IDropTarget DragEnter→DragOver→Drop sequence or
 * just releases resources based on execute_drop, then releases all resources and
 * frees the context.
 *
 * @param ctx Complete context (will be freed after this call)
 * @param execute_drop true to execute DragEnter→DragOver→Drop sequence, false to skip drop
 */
static void complete_external_drop_impl(struct gcmz_drop_complete_context *ctx, bool execute_drop) {
  if (!ctx) {
    return;
  }

  struct complete_context_external *ectx = (struct complete_context_external *)ctx;
  struct gcmz_drop *d = ectx->d;
  struct wrapped_drop_target *wdt = NULL;

  POINT pt0 = {0};
  ClientToScreen((HWND)ctx->window, &pt0);
  POINTL ptl = {.x = ctx->x + pt0.x, .y = ctx->y + pt0.y};
  DWORD effect = ctx->drop_effect;
  HRESULT hr = E_FAIL;
  struct ov_error err = {0};
  bool success = false;

  if (!d || !ectx->dataobj) {
    OV_ERROR_SET_GENERIC(&err, ov_error_generic_invalid_argument);
    goto cleanup;
  }

  {
    // Find the drop target
    EnterCriticalSection(&d->targets_cs);
    size_t const len = OV_ARRAY_LENGTH(d->wrapped_targets);
    for (size_t i = 0; i < len; ++i) {
      if (d->wrapped_targets[i] && d->wrapped_targets[i]->main_window == ctx->window) {
        wdt = d->wrapped_targets[i];
        break;
      }
    }
    LeaveCriticalSection(&d->targets_cs);
  }

  if (!wdt) {
    OV_ERROR_SET(&err, ov_error_type_generic, ov_error_generic_fail, "window is not registered");
    goto cleanup;
  }
  if (!execute_drop) {
    goto cleanup;
  }

  hr = IDropTarget_DragEnter(wdt->original, ectx->dataobj, ctx->key_state, ptl, &effect);
  if (FAILED(hr)) {
    OV_ERROR_SET(&err, ov_error_type_hresult, hr, "IDropTarget_DragEnter failed");
    goto cleanup;
  }
  if (effect == DROPEFFECT_NONE) {
    gcmz_logf_warn(NULL,
                   NULL,
                   "DragEnter rejected drop: window=0x%1$08lX effect=0x%2$08lX",
                   (unsigned long)(uintptr_t)wdt->main_window,
                   (unsigned long)effect);
    IDropTarget_DragLeave(wdt->original);
    success = true;
    goto cleanup;
  }
  hr = IDropTarget_DragOver(wdt->original, ctx->key_state, ptl, &effect);
  if (FAILED(hr)) {
    IDropTarget_DragLeave(wdt->original);
    OV_ERROR_SET(&err, ov_error_type_hresult, hr, "IDropTarget_DragOver failed");
    goto cleanup;
  }
  if (effect == DROPEFFECT_NONE) {
    gcmz_logf_warn(NULL,
                   NULL,
                   "DragOver rejected drop: window=0x%1$08lX effect=0x%2$08lX",
                   (unsigned long)(uintptr_t)wdt->main_window,
                   (unsigned long)effect);
    IDropTarget_DragLeave(wdt->original);
    success = true;
    goto cleanup;
  }
  hr = IDropTarget_Drop(wdt->original, ectx->dataobj, ctx->key_state, ptl, &effect);
  if (FAILED(hr)) {
    OV_ERROR_SET(&err, ov_error_type_hresult, hr, "IDropTarget_Drop failed");
    goto cleanup;
  }
  success = true;

cleanup:
  if (ectx->dataobj) {
    IDataObject_Release(ectx->dataobj);
    ectx->dataobj = NULL;
  }
  if (ectx->file_list) {
    cleanup_temporary_files_in_list(d, ectx->file_list);
    gcmz_file_list_destroy(&ectx->file_list);
  }
  OV_FREE(&ectx);
  if (!success) {
    gcmz_logf_warn(&err, NULL, "%1$hs", "External drop completion failed");
    OV_ERROR_REPORT(&err, NULL);
  }
}

bool gcmz_drop_simulate_drop_external(struct gcmz_drop *const d,
                                      void *const window,
                                      void *const dataobj,
                                      int const x,
                                      int const y,
                                      bool const use_exo_converter,
                                      gcmz_drop_completion_callback const completion_callback,
                                      void *const completion_userdata,
                                      struct ov_error *const err) {
  if (!d || !dataobj || !window || !completion_callback) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  IDataObject *pDataObject = (IDataObject *)dataobj;
  struct gcmz_file_list *file_list = NULL;
  IDataObject *final_dataobj = NULL;
  struct complete_context_external *ectx = NULL;
  wchar_t *managed_path = NULL;
  DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
  POINTL ptl = {0};

  {
    // Keep client coordinates for now - they will be converted to screen coordinates later
    ptl.x = x;
    ptl.y = y;

    // Step 1: Extract files from IDataObject
    file_list = gcmz_file_list_create(err);
    if (!file_list) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!d->extract_fn(pDataObject, file_list, d->callback_userdata, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Step 2: EXO conversion (if enabled)
    if (use_exo_converter && d->lua_context) {
#if GCMZ_DEBUG
      gcmz_logf_verbose(NULL, NULL, "External API: Invoking EXO file conversion via Lua");
#endif
      struct ov_error exo_err = {0};
      if (!gcmz_lua_call_exo_convert(d->lua_context, file_list, &exo_err)) {
        gcmz_logf_warn(
            &exo_err, "%1$hs", "%1$hs", gettext("EXO file conversion failed, proceeding with original files"));
        OV_ERROR_DESTROY(&exo_err);
      }
    }

    if (gcmz_file_list_count(file_list) == 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "no files to drop");
      goto cleanup;
    }

    // Step 3: Call Lua handlers in sequence (Enter → Drop)
    {
      struct ov_error lua_err = {0};
      if (!gcmz_lua_call_drag_enter(d->lua_context, file_list, 0, 0, true, &lua_err)) {
        gcmz_logf_warn(&lua_err, "%1$s", gettext("error occurred while executing %1$s script handler"), "drag_enter");
        OV_ERROR_DESTROY(&lua_err);
      }
      if (!gcmz_lua_call_drop(d->lua_context, file_list, 0, 0, true, &lua_err)) {
        gcmz_logf_warn(&lua_err, "%1$s", gettext("error occurred while executing %1$s script handler"), "drop");
        OV_ERROR_DESTROY(&lua_err);
      }
    }

    // Step 4: Apply file management (copying, etc.)
    if (d->file_manage_fn) {
      size_t const file_count = gcmz_file_list_count(file_list);
      for (size_t i = 0; i < file_count; i++) {
        struct gcmz_file *file = gcmz_file_list_get_mutable(file_list, i);
        if (!file || !file->path) {
          continue;
        }

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
          wchar_t *old_path = file->path;
          file->path = managed_path;
          managed_path = old_path; // Will be freed in cleanup
          file->temporary = false;
        }
        OV_ARRAY_DESTROY(&managed_path);
      }
    }

    // Step 5: Create IDataObject with final file paths
    final_dataobj = (IDataObject *)gcmz_drop_create_file_list_dataobj(file_list, ptl.x, ptl.y, err);
    if (!final_dataobj) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    // Step 6: Call completion callback (DragEnter→DragOver→Drop is done in complete_external_drop_impl)
    // Allocate context on heap for async support
    if (!OV_REALLOC(&ectx, 1, sizeof(*ectx))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    *ectx = (struct complete_context_external){
        .pub =
            {
                .final_files = file_list,
                .window = window,
                .x = ptl.x,
                .y = ptl.y,
                .key_state = 0,
                .modifier_keys = 0,
                .drop_effect = dwEffect,
                .userdata = completion_userdata,
            },
        .d = d,
        .file_list = file_list,
        .dataobj = final_dataobj,
    };
    // Transfer ownership to callback - callback MUST call complete_external_drop_impl
    file_list = NULL;
    final_dataobj = NULL;
    completion_callback(&ectx->pub, complete_external_drop_impl, completion_userdata);
    ectx = NULL; // Ownership transferred
  }
  result = true;

cleanup:
  if (ectx) {
    OV_FREE(&ectx);
  }
  if (managed_path) {
    OV_ARRAY_DESTROY(&managed_path);
  }
  if (final_dataobj) {
    IDataObject_Release(final_dataobj);
  }
  if (file_list) {
    cleanup_temporary_files_in_list(d, file_list);
    gcmz_file_list_destroy(&file_list);
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
