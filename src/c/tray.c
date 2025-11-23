#include "tray.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>

#include <ovarray.h>
#include <ovmo.h>
#include <ovthreads.h>

enum {
  wm_tray_notify = WM_USER + 1,
  tray_icon_id = 1,
  gcmz_tray_max_menu_items = 128,
};

struct gcmz_tray_menu_item_internal {
  gcmz_tray_callback callback; ///< Callback function (also used as menu command ID)
  void *userdata;              ///< User data for callback
};

struct gcmz_tray {
  struct gcmz_tray_menu_item_internal *menu_items;
  HWND hwnd;
  UINT taskbar_created_msg;
  thrd_t thread;
  mtx_t mutex; ///< Protects menu_items and hwnd
  cnd_t cond;  ///< Signals when hwnd is created
  HICON icon;  ///< Icon to display in system tray
};

static BOOL register_tray_icon(HWND const hwnd, HICON const icon) {
  static wchar_t const tray_tip[] = L"GCMZDrops";
  HICON hicon = icon ? icon : LoadIconW(NULL, MAKEINTRESOURCEW(32512));
  NOTIFYICONDATAW nid = {
      .cbSize = sizeof(NOTIFYICONDATAW),
      .hWnd = hwnd,
      .uID = tray_icon_id,
      .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP,
      .uCallbackMessage = wm_tray_notify,
      .hIcon = hicon,
  };
  wcscpy(nid.szTip, tray_tip);
  return Shell_NotifyIconW(NIM_ADD, &nid);
}

static void unregister_tray_icon(HWND const hwnd) {
  Shell_NotifyIconW(NIM_DELETE,
                    &(NOTIFYICONDATAW){
                        .cbSize = sizeof(NOTIFYICONDATAW),
                        .hWnd = hwnd,
                        .uID = tray_icon_id,
                    });
}

static void show_context_menu(struct gcmz_tray *const tray, HWND const hwnd) {
  if (!tray) {
    return;
  }

  POINT pt;
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }

  gcmz_tray_callback callbacks[gcmz_tray_max_menu_items];
  size_t num_callbacks = 0;

  // Build menu from all items
  mtx_lock(&tray->mutex);
  size_t const count = OV_ARRAY_LENGTH(tray->menu_items);
  for (size_t i = 0; i < count && num_callbacks < gcmz_tray_max_menu_items; i++) {
    struct gcmz_tray_menu_item_internal *const item = &tray->menu_items[i];
    if (!item->callback) {
      continue;
    }
    struct gcmz_tray_callback_event event = {
        .type = gcmz_tray_callback_query_info,
    };
    item->callback(item->userdata, &event);
    if (!event.result.query_info.label) {
      continue;
    }
    callbacks[num_callbacks] = item->callback;
    AppendMenuW(menu,
                MF_STRING | (event.result.query_info.enabled ? 0 : MF_GRAYED | MF_DISABLED),
                (UINT_PTR)(++num_callbacks),
                event.result.query_info.label);
  }
  mtx_unlock(&tray->mutex);

  SetForegroundWindow(hwnd);
  int const cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
  DestroyMenu(menu);
  PostMessageW(hwnd, WM_NULL, 0, 0);

  if (cmd <= 0 || (size_t)cmd > num_callbacks) {
    return; // invalid index
  }

  gcmz_tray_callback const selected_callback = callbacks[cmd - 1];

  // Verify the callback still exists in menu_items
  mtx_lock(&tray->mutex);
  size_t const count2 = OV_ARRAY_LENGTH(tray->menu_items);
  for (size_t i = 0; i < count2; i++) {
    struct gcmz_tray_menu_item_internal *const item = &tray->menu_items[i];
    if (item->callback == selected_callback) {
      item->callback(item->userdata,
                     &(struct gcmz_tray_callback_event){
                         .type = gcmz_tray_callback_clicked,
                     });
      break;
    }
  }
  mtx_unlock(&tray->mutex);
}

static LRESULT CALLBACK tray_window_proc(HWND const hwnd, UINT const msg, WPARAM const wparam, LPARAM const lparam) {
  static wchar_t const prop_name[] = L"gcmz_tray";
  struct gcmz_tray *const tray = msg == WM_NCCREATE ? (struct gcmz_tray *)((CREATESTRUCTW *)lparam)->lpCreateParams
                                                    : (struct gcmz_tray *)GetPropW(hwnd, prop_name);

  switch (msg) {
  case WM_NCCREATE:
    if (tray) {
      static wchar_t const taskbar_created[] = L"TaskbarCreated";
      tray->taskbar_created_msg = RegisterWindowMessageW(taskbar_created);
      SetPropW(hwnd, prop_name, (HANDLE)tray);
      register_tray_icon(hwnd, tray->icon);
    }
    break;

  case wm_tray_notify:
    if (LOWORD(lparam) == WM_RBUTTONUP && tray) {
      show_context_menu(tray, hwnd);
    }
    return 0;

  case WM_DESTROY:
    unregister_tray_icon(hwnd);
    RemovePropW(hwnd, prop_name);
    PostQuitMessage(0);
    return 0;

  default:
    if (tray && msg == tray->taskbar_created_msg) {
      register_tray_icon(hwnd, tray->icon);
      return 0;
    }
    break;
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int tray_thread_proc(void *const arg) {
  struct gcmz_tray *const tray = (struct gcmz_tray *)arg;
  if (!tray) {
    return -1;
  }

  struct ov_error err = {0};
  static wchar_t const class_name[] = L"GCMZDropsTrayWindow";
  static wchar_t const window_name[] = L"GCMZDrops";
  HMODULE const h = GetModuleHandleW(NULL);
  ATOM class_atom = 0;
  int result = -1;

  class_atom = RegisterClassExW(&(WNDCLASSEXW){
      .cbSize = sizeof(WNDCLASSEXW),
      .lpfnWndProc = tray_window_proc,
      .hInstance = h,
      .lpszClassName = class_name,
  });
  if (!class_atom) {
    OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  {
    HWND hwnd = CreateWindowExW(0, class_name, window_name, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, h, tray);
    if (!hwnd) {
      OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    mtx_lock(&tray->mutex);
    tray->hwnd = hwnd;
    cnd_signal(&tray->cond);
    mtx_unlock(&tray->mutex);
  }

  MSG msg;
  while (true) {
    BOOL ret = GetMessageW(&msg, NULL, 0, 0);
    if (ret == -1 || ret == 0) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  result = 0;

cleanup:
  if (tray->hwnd) {
    DestroyWindow(tray->hwnd);
    tray->hwnd = NULL;
  }
  if (class_atom) {
    UnregisterClassW(class_name, h);
  }
  if (result != 0) {
    OV_ERROR_REPORT(&err, NULL);
  }
  return result;
}

NODISCARD struct gcmz_tray *gcmz_tray_create(void *const icon, struct ov_error *const err) {

  struct gcmz_tray *tray = NULL;
  bool mutex_initialized = false;
  bool cond_initialized = false;
  bool result = false;

  if (!OV_REALLOC(&tray, 1, sizeof(struct gcmz_tray))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  *tray = (struct gcmz_tray){
      .icon = (HICON)icon,
  };

  // mtx_recursive is required to prevent deadlock when callback shows modal dialog
  // and user clicks tray menu again (same thread attempts to relock)
  if (mtx_init(&tray->mutex, mtx_recursive) != thrd_success) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("failed to initialize mutex"));
    goto cleanup;
  }
  mutex_initialized = true;

  if (cnd_init(&tray->cond) != thrd_success) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("failed to initialize condition variable"));
    goto cleanup;
  }
  cond_initialized = true;

  if (thrd_create(&tray->thread, tray_thread_proc, tray) != thrd_success) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, gettext("failed to create thread"));
    goto cleanup;
  }

  mtx_lock(&tray->mutex);
  while (tray->hwnd == NULL) {
    cnd_wait(&tray->cond, &tray->mutex);
  }
  mtx_unlock(&tray->mutex);

  result = true;

cleanup:
  if (!result) {
    if (tray) {
      if (tray->menu_items) {
        OV_ARRAY_DESTROY(&tray->menu_items);
      }
      if (cond_initialized) {
        cnd_destroy(&tray->cond);
      }
      if (mutex_initialized) {
        mtx_destroy(&tray->mutex);
      }
      OV_FREE(&tray);
    }
  }
  return tray;
}

void gcmz_tray_destroy(struct gcmz_tray **const traypp) {
  if (!traypp || !*traypp) {
    return;
  }

  struct gcmz_tray *const tray = *traypp;

  if (tray->hwnd) {
    SendMessageW(tray->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    thrd_join(tray->thread, NULL);
  }

  if (tray->menu_items) {
    OV_ARRAY_DESTROY(&tray->menu_items);
  }

  cnd_destroy(&tray->cond);
  mtx_destroy(&tray->mutex);

  OV_FREE(traypp);
}

bool gcmz_tray_add_menu_item(struct gcmz_tray *const tray,
                             gcmz_tray_callback const callback,
                             void *const userdata,
                             struct ov_error *const err) {
  if (!tray || !callback) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  mtx_lock(&tray->mutex);

  size_t const count = OV_ARRAY_LENGTH(tray->menu_items);
  if (count >= gcmz_tray_max_menu_items) {
    OV_ERROR_SETF(err,
                  ov_error_type_generic,
                  ov_error_generic_fail,
                  "%1$d",
                  gettext("maximum number of menu items(%1$d) exceeded"),
                  gcmz_tray_max_menu_items);
    goto cleanup;
  }

  if (!OV_ARRAY_GROW(&tray->menu_items, count + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  tray->menu_items[count] = (struct gcmz_tray_menu_item_internal){
      .callback = callback,
      .userdata = userdata,
  };
  OV_ARRAY_SET_LENGTH(tray->menu_items, count + 1);

  result = true;

cleanup:
  mtx_unlock(&tray->mutex);
  return result;
}

void gcmz_tray_remove_menu_item(struct gcmz_tray *const tray, gcmz_tray_callback const callback) {
  if (!tray || !callback) {
    return;
  }

  mtx_lock(&tray->mutex);

  size_t const count = OV_ARRAY_LENGTH(tray->menu_items);
  for (size_t i = 0; i < count; i++) {
    if (tray->menu_items[i].callback == callback) {
      if (i < count - 1) {
        memmove(&tray->menu_items[i],
                &tray->menu_items[i + 1],
                (count - i - 1) * sizeof(struct gcmz_tray_menu_item_internal));
      }
      OV_ARRAY_SET_LENGTH(tray->menu_items, count - 1);
      break;
    }
  }

  mtx_unlock(&tray->mutex);
}
