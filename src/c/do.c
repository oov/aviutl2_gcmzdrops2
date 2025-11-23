#include "do.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>

#include <ovthreads.h>

enum {
  gcmz_subclass_id = 0x8001,
};

static wchar_t const gcmz_do_window_message_name[] = L"GCMZDropsDoMessage";
static UINT g_window_message_id = 0;

/**
 * Internal instance structure for gcmz_do system
 */
struct gcmz_do {
  HWND window;
  HHOOK msg_hook;
  DWORD window_thread_id;

  struct cndvar blocking_completion;
  bool blocking_initialized;
  mtx_t blocking_mutex;

  gcmz_do_func cleanup_callback;
  gcmz_do_func activate_callback;
  void *userdata;
};

static struct gcmz_do *g_do = NULL;

static LRESULT CALLBACK
subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

static bool install_subclass(struct gcmz_do *const d, HWND hwnd, struct ov_error *const err) {
  if (!d || !hwnd) {
    return true;
  }
  if (!SetWindowSubclass(hwnd, subclass_proc, gcmz_subclass_id, (DWORD_PTR)d)) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    return false;
  }
  return true;
}

static void uninstall_subclass(HWND hwnd) {
  if (IsWindow(hwnd)) {
    RemoveWindowSubclass(hwnd, subclass_proc, gcmz_subclass_id);
  }
}

static LRESULT CALLBACK
subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  (void)uIdSubclass;
  struct gcmz_do *d = (struct gcmz_do *)dwRefData;
  if (!d) {
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
  }

  if (g_window_message_id != 0 && uMsg == g_window_message_id) {
    gcmz_do_func func = (gcmz_do_func)wParam;
    if (!func) {
      return 0;
    }
    func((void *)lParam);
    return 0;
  }
  switch (uMsg) {
  case WM_DESTROY:
    if (d->cleanup_callback) {
      d->cleanup_callback(d->userdata);
    }
    uninstall_subclass(hwnd);
    break;
  case WM_ACTIVATE:
    if (d->activate_callback) {
      d->activate_callback(d->userdata);
    }
    break;
  case WM_ACTIVATEAPP:
    if (d->activate_callback) {
      d->activate_callback(d->userdata);
    }
    break;
  }

  return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK msghook(int nCode, WPARAM wParam, LPARAM lParam) {
  MSG const *const msg = (MSG *)lParam;
  if (g_window_message_id == 0 || nCode < 0 || !msg || !msg->hwnd) {
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
  if (msg->message != g_window_message_id || msg->wParam != 0 || msg->lParam == 0) {
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
  struct gcmz_do *const d = (struct gcmz_do *)msg->lParam;
  struct ov_error err = {0};
  if (!install_subclass(d, msg->hwnd, &err)) {
    OV_ERROR_REPORT(&err, "Failed to install subclass");
  }
  LRESULT r = CallNextHookEx(d->msg_hook, nCode, wParam, lParam);
  UnhookWindowsHookEx(d->msg_hook);
  d->msg_hook = NULL;
  return r;
}

static struct gcmz_do *do_create(struct gcmz_do_init_option const *const option, struct ov_error *const err) {
  if (!option) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  HWND window = (HWND)option->window;
  if (!window || !IsWindow(window)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return NULL;
  }

  if (InterlockedCompareExchange((LONG volatile *)&g_window_message_id, 0, 0) == 0) {
    UINT const msg_id = RegisterWindowMessageW(gcmz_do_window_message_name);
    if (msg_id == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      return NULL;
    }
    InterlockedCompareExchange((LONG volatile *)&g_window_message_id, (LONG)msg_id, 0);
  }

  struct gcmz_do *d = NULL;
  struct gcmz_do *result = NULL;

  {
    if (!OV_REALLOC(&d, 1, sizeof(struct gcmz_do))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    *d = (struct gcmz_do){0};

    cndvar_init(&d->blocking_completion);
    mtx_init(&d->blocking_mutex, mtx_plain);
    d->window = window;
    d->cleanup_callback = option->on_cleanup;
    d->userdata = option->userdata;
    d->activate_callback = option->on_change_activate;
    d->blocking_initialized = true;

    d->window_thread_id = GetWindowThreadProcessId(d->window, NULL);
    if (d->window_thread_id == GetCurrentThreadId()) {
      if (!install_subclass(d, d->window, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
    } else {
      d->msg_hook = SetWindowsHookExW(WH_GETMESSAGE, msghook, NULL, d->window_thread_id);
      if (!d->msg_hook) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
      if (!PostMessageW(d->window, g_window_message_id, 0, (LPARAM)d)) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
    }
  }

  result = d;

cleanup:
  if (!result) {
    if (d) {
      if (d->blocking_initialized) {
        cndvar_exit(&d->blocking_completion);
        mtx_destroy(&d->blocking_mutex);
      }
      OV_FREE(&d);
    }
  }
  return result;
}

static void do_destroy(struct gcmz_do **const dp) {
  if (!dp || !*dp) {
    return;
  }
  struct gcmz_do *d = *dp;
  if (d->msg_hook) {
    UnhookWindowsHookEx(d->msg_hook);
    d->msg_hook = NULL;
  }
  uninstall_subclass(d->window);
  if (d->blocking_initialized) {
    cndvar_exit(&d->blocking_completion);
    mtx_destroy(&d->blocking_mutex);
    d->blocking_initialized = false;
  }
  OV_FREE(dp);
}

static void do_do(struct gcmz_do *const d, gcmz_do_func func, void *data) {
  if (!d || !d->window || !g_window_message_id || !func) {
    return;
  }
  if (GetCurrentThreadId() == d->window_thread_id) {
    func(data);
    return;
  }
  if (!PostMessageW(d->window, g_window_message_id, (WPARAM)func, (LPARAM)data)) {
    struct ov_error err = {0};
    OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
    OV_ERROR_REPORT(&err, "Failed to post message to main thread");
  }
}

struct do_blocking_context {
  struct gcmz_do *d;
  gcmz_do_func func;
  void *data;
};

static void do_blocking_core(void *userdata) {
  struct do_blocking_context *ctx = (struct do_blocking_context *)userdata;
  ctx->func(ctx->data);
  cndvar_lock(&ctx->d->blocking_completion);
  cndvar_signal(&ctx->d->blocking_completion, 1);
  cndvar_unlock(&ctx->d->blocking_completion);
}

static void do_do_blocking(struct gcmz_do *const d, gcmz_do_func func, void *data) {
  if (!d || !d->window || !g_window_message_id || !func) {
    return;
  }
  if (GetCurrentThreadId() == d->window_thread_id) {
    func(data);
    return;
  }

  struct do_blocking_context ctx = {
      .d = d,
      .func = func,
      .data = data,
  };

  mtx_lock(&d->blocking_mutex);
  cndvar_lock(&d->blocking_completion);
  d->blocking_completion.var = 0;
  do_do(d, do_blocking_core, &ctx);
  cndvar_wait_until(&d->blocking_completion, 1);
  cndvar_unlock(&d->blocking_completion);
  mtx_unlock(&d->blocking_mutex);
}

NODISCARD bool gcmz_do_init(struct gcmz_do_init_option const *const option, struct ov_error *const err) {
  if (g_do) {
    do_destroy(&g_do);
  }
  g_do = do_create(option, err);
  return g_do != NULL;
}

void gcmz_do_exit(void) { do_destroy(&g_do); }

void gcmz_do(gcmz_do_func func, void *data) { do_do(g_do, func, data); }

void gcmz_do_blocking(gcmz_do_func func, void *data) { do_do_blocking(g_do, func, data); }
