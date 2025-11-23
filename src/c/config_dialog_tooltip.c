#include "config_dialog_tooltip.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>

#include <ovarray.h>
#include <ovbase.h>

#include "config.h"

static wchar_t const g_property_name[] = L"GCMZDropsTooltip";

enum {
  subclass_id_listbox = 1,
  subclass_id_edit = 2,
  timer_id_refresh = 100,
  refresh_delay_ms = 100,
  tooltip_text_buffer_size = 1024,
};

struct config_dialog_tooltip {
  HWND tooltip_window;
  wchar_t *tooltip_text;
  HWND active_control;
  int active_listbox_item;
  bool mouse_hovering;
  bool needs_refresh;
  struct gcmz_config *config;
  HWND parent;
  HWND listbox;
  HWND edit_control;
};

static bool
get_listbox_item_text(HWND hList, int item_index, wchar_t *buffer, size_t buffer_size, struct ov_error *const err) {
  if (!hList || item_index < 0 || !buffer || buffer_size == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  int const item_count = (int)SendMessageW(hList, LB_GETCOUNT, 0, 0);
  if (item_index >= item_count) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  if (item_index < item_count - 1) {
    LRESULT const r = SendMessageW(hList, LB_GETTEXT, (WPARAM)item_index, (LPARAM)buffer);
    if (r == LB_ERR) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    result = true;
    goto cleanup;
  }

  wcsncpy(buffer, gcmz_config_get_fallback_save_path(), buffer_size - 1);
  buffer[buffer_size - 1] = L'\0';
  result = true;

cleanup:
  return result;
}

// Update tooltip to show content for the specified hwnd+index
// If hwnd is NULL, hides the tooltip
static void update_tooltip(struct config_dialog_tooltip *state, HWND hwnd, int item_index) {
  if (!state || !state->tooltip_window) {
    return;
  }

  struct ov_error err = {0};
  bool success = false;

  {
    // Hide if no control specified
    if (!hwnd) {
      if (state->active_control) {
        UINT_PTR const tool_id = (UINT_PTR)GetDlgCtrlID(state->active_control);
        SendMessageW(state->tooltip_window,
                     TTM_TRACKACTIVATE,
                     FALSE,
                     (LPARAM) & (TOOLINFOW){
                                    .cbSize = sizeof(TOOLINFOW),
                                    .hwnd = state->active_control,
                                    .uId = tool_id,
                                });
        state->active_control = NULL;
        state->active_listbox_item = LB_ERR;
      }
      success = true;
      goto cleanup;
    }

    // Same state - nothing to do
    if (state->active_control == hwnd && state->active_listbox_item == item_index) {
      success = true;
      goto cleanup;
    }

    // State changed - update tooltip
    wchar_t text[tooltip_text_buffer_size] = {0};
    bool has_text = false;

    if (hwnd == state->listbox && item_index != LB_ERR) {
      has_text = get_listbox_item_text(hwnd, item_index, text, tooltip_text_buffer_size, &err);
      if (!has_text) {
        OV_ERROR_DESTROY(&err);
      }
    } else if (hwnd == state->edit_control) {
      int const text_len = GetWindowTextLengthW(hwnd);
      if (text_len > 0 && text_len < tooltip_text_buffer_size) {
        GetWindowTextW(hwnd, text, tooltip_text_buffer_size);
        has_text = true;
      }
    }

    if (!has_text || text[0] == L'\0') {
      update_tooltip(state, NULL, LB_ERR);
      success = true;
      goto cleanup;
    }

    // Expand placeholders and show
    if (!gcmz_config_expand_placeholders(state->config, text, &state->tooltip_text, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }

    // Deactivate current tooltip if switching controls
    if (state->active_control && state->active_control != hwnd) {
      UINT_PTR const old_tool_id = (UINT_PTR)GetDlgCtrlID(state->active_control);
      SendMessageW(state->tooltip_window,
                   TTM_TRACKACTIVATE,
                   FALSE,
                   (LPARAM) & (TOOLINFOW){
                                  .cbSize = sizeof(TOOLINFOW),
                                  .hwnd = state->active_control,
                                  .uId = old_tool_id,
                              });
    }

    state->active_control = hwnd;
    state->active_listbox_item = item_index;

    UINT_PTR const tool_id = (UINT_PTR)GetDlgCtrlID(hwnd);

    SendMessageW(state->tooltip_window,
                 TTM_UPDATETIPTEXTW,
                 0,
                 (LPARAM) & (TOOLINFOW){
                                .cbSize = sizeof(TOOLINFOW),
                                .hwnd = hwnd,
                                .uId = tool_id,
                                .lpszText = state->tooltip_text,
                            });

    // Calculate position
    int x = 0;
    int y = 0;
    static int const y_margin = 4;
    RECT rc;
    GetWindowRect(hwnd, &rc);

    if (item_index == LB_ERR) {
      x = rc.left;
      y = rc.bottom + y_margin;
    } else {
      int const item_height = (int)SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0);
      int const top_index = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
      int const item_offset = (item_index - top_index) * item_height;
      x = rc.left;
      y = rc.top + item_offset + item_height + y_margin;
    }

    SendMessageW(state->tooltip_window, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    SendMessageW(state->tooltip_window,
                 TTM_TRACKACTIVATE,
                 TRUE,
                 (LPARAM) & (TOOLINFOW){
                                .cbSize = sizeof(TOOLINFOW),
                                .hwnd = hwnd,
                                .uId = tool_id,
                                .lpszText = state->tooltip_text,
                            });
  }

  success = true;

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
  }
}

// Update tooltip based on focused control (only if mouse not hovering)
static void update_tooltip_for_focus(struct config_dialog_tooltip *state) {
  if (!state || state->mouse_hovering) {
    return; // Mouse hover takes priority
  }

  HWND const hFocus = GetFocus();
  if (!hFocus) {
    update_tooltip(state, NULL, LB_ERR);
    return;
  }

  if (hFocus == state->edit_control) {
    update_tooltip(state, state->edit_control, LB_ERR);
  } else if (hFocus == state->listbox) {
    int const sel = (int)SendMessageW(state->listbox, LB_GETCURSEL, 0, 0);
    update_tooltip(state, state->listbox, sel);
  } else {
    update_tooltip(state, NULL, LB_ERR);
  }
}

// Timer callback to perform delayed tooltip refresh
static void CALLBACK refresh_timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
  (void)msg;
  (void)time;
  if (id != timer_id_refresh) {
    return;
  }

  struct config_dialog_tooltip *state = (struct config_dialog_tooltip *)GetPropW(hwnd, g_property_name);
  if (!state) {
    KillTimer(hwnd, timer_id_refresh);
    return;
  }

  if (state->needs_refresh) {
    state->needs_refresh = false;

    if (state->active_control) {
      // Force refresh - directly call update without clearing state first
      // update_tooltip will handle state changes internally
      HWND const ctrl = state->active_control;
      int const item = state->active_listbox_item;

      // Get current text to check if empty
      wchar_t text[tooltip_text_buffer_size] = {0};
      bool has_text = false;

      if (ctrl == state->listbox && item != LB_ERR) {
        struct ov_error err = {0};
        has_text = get_listbox_item_text(ctrl, item, text, tooltip_text_buffer_size, &err);
        OV_ERROR_DESTROY(&err);
      } else if (ctrl == state->edit_control) {
        int const text_len = GetWindowTextLengthW(ctrl);
        if (text_len > 0 && text_len < tooltip_text_buffer_size) {
          GetWindowTextW(ctrl, text, tooltip_text_buffer_size);
          has_text = true;
        }
      }

      // Clear state before update to force refresh
      state->active_control = NULL;
      state->active_listbox_item = LB_ERR;

      if (has_text && text[0] != L'\0') {
        update_tooltip(state, ctrl, item);
      } else {
        // Text is empty, but need to hide with correct control info
        UINT_PTR const tool_id = (UINT_PTR)GetDlgCtrlID(ctrl);
        SendMessageW(state->tooltip_window,
                     TTM_TRACKACTIVATE,
                     FALSE,
                     (LPARAM) & (TOOLINFOW){
                                    .cbSize = sizeof(TOOLINFOW),
                                    .hwnd = ctrl,
                                    .uId = tool_id,
                                });
      }
    } else {
      // No active control, update based on focus
      update_tooltip_for_focus(state);
    }
  }

  KillTimer(hwnd, timer_id_refresh);
}

// Schedule a delayed refresh of the tooltip
static void schedule_refresh(struct config_dialog_tooltip *state) {
  if (!state || !state->parent) {
    return;
  }
  state->needs_refresh = true;
  SetTimer(state->parent, timer_id_refresh, refresh_delay_ms, refresh_timer_proc);
}

static LRESULT CALLBACK listbox_subclass_proc(
    HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  struct config_dialog_tooltip *state = (struct config_dialog_tooltip *)dwRefData;
  (void)uIdSubclass;

  switch (message) {
  case WM_MOUSEMOVE: {
    TrackMouseEvent(&(TRACKMOUSEEVENT){
        .cbSize = sizeof(TRACKMOUSEEVENT),
        .dwFlags = TME_LEAVE,
        .hwndTrack = hWnd,
    });

    POINT const pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
    LRESULT const item_index = SendMessageW(hWnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    int const item = HIWORD(item_index) ? LB_ERR : LOWORD(item_index);

    if (item == LB_ERR) {
      // No item at this position - switch to focus-based display
      state->mouse_hovering = false;
      update_tooltip_for_focus(state);
      break;
    }

    state->mouse_hovering = true;
    update_tooltip(state, hWnd, item);
    break;
  }

  case WM_MOUSELEAVE: {
    state->mouse_hovering = false;
    update_tooltip_for_focus(state);
    break;
  }

  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    update_tooltip_for_focus(state);
    break;

  case LB_DELETESTRING:
  case LB_INSERTSTRING:
  case LB_ADDSTRING:
  case LB_RESETCONTENT: {
    // Listbox content changed - schedule delayed refresh
    LRESULT const result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (state->active_control == hWnd && state->active_listbox_item != LB_ERR) {
      schedule_refresh(state);
    }
    return result;
  }

  case WM_NCDESTROY:
    RemoveWindowSubclass(hWnd, listbox_subclass_proc, uIdSubclass);
    break;
  }

  return DefSubclassProc(hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK
edit_subclass_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  struct config_dialog_tooltip *state = (struct config_dialog_tooltip *)dwRefData;
  (void)uIdSubclass;

  switch (message) {
  case WM_MOUSEMOVE: {
    TrackMouseEvent(&(TRACKMOUSEEVENT){
        .cbSize = sizeof(TRACKMOUSEEVENT),
        .dwFlags = TME_LEAVE,
        .hwndTrack = hWnd,
    });

    state->mouse_hovering = true;
    update_tooltip(state, hWnd, LB_ERR);
    break;
  }

  case WM_SETTEXT:
  case WM_CHAR:
  case WM_CUT:
  case WM_PASTE:
  case WM_CLEAR:
    // Text changed - if this control should show tooltip, schedule refresh
    if (GetFocus() == hWnd || state->active_control == hWnd) {
      schedule_refresh(state);
    }
    break;

  case WM_MOUSELEAVE: {
    state->mouse_hovering = false;
    update_tooltip_for_focus(state);
    break;
  }

  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    update_tooltip_for_focus(state);
    break;

  case WM_NCDESTROY:
    RemoveWindowSubclass(hWnd, edit_subclass_proc, uIdSubclass);
    break;
  }

  return DefSubclassProc(hWnd, message, wParam, lParam);
}

struct config_dialog_tooltip *config_dialog_tooltip_create(
    struct gcmz_config *config, void *parent, void *listbox, void *edit_control, struct ov_error *const err) {
  if (!parent || !config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  HWND const hParent = (HWND)parent;
  HWND const hListbox = (HWND)listbox;
  HWND const hEditControl = (HWND)edit_control;

  struct config_dialog_tooltip *tt = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&tt, 1, sizeof(struct config_dialog_tooltip))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    *tt = (struct config_dialog_tooltip){
        .active_listbox_item = LB_ERR,
        .config = config,
        .parent = hParent,
        .listbox = hListbox,
        .edit_control = hEditControl,
    };

    // Store state pointer in parent window for timer callback
    if (!SetPropW(hParent, g_property_name, tt)) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    tt->tooltip_window = CreateWindowExW(WS_EX_TOPMOST,
                                         TOOLTIPS_CLASSW,
                                         NULL,
                                         WS_POPUP | TTS_NOPREFIX,
                                         CW_USEDEFAULT,
                                         CW_USEDEFAULT,
                                         CW_USEDEFAULT,
                                         CW_USEDEFAULT,
                                         hParent,
                                         NULL,
                                         NULL,
                                         NULL);
    if (!tt->tooltip_window) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    SetWindowPos(tt->tooltip_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(tt->tooltip_window, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(32767, 0));
    SendMessageW(tt->tooltip_window, TTM_SETMAXTIPWIDTH, 0, 600);

    // Register listbox with tooltip
    if (hListbox) {
      SendMessageW(tt->tooltip_window,
                   TTM_ADDTOOLW,
                   0,
                   (LPARAM) & (TOOLINFOW){
                                  .cbSize = sizeof(TOOLINFOW),
                                  .uFlags = TTF_ABSOLUTE | TTF_TRACK,
                                  .hwnd = hListbox,
                                  .uId = (UINT_PTR)GetDlgCtrlID(hListbox),
                                  .lpszText = L"",
                              });

      // Subclass listbox
      if (!SetWindowSubclass(hListbox, listbox_subclass_proc, subclass_id_listbox, (DWORD_PTR)tt)) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
    }

    // Register edit control with tooltip
    if (hEditControl) {
      SendMessageW(tt->tooltip_window,
                   TTM_ADDTOOLW,
                   0,
                   (LPARAM) & (TOOLINFOW){
                                  .cbSize = sizeof(TOOLINFOW),
                                  .uFlags = TTF_ABSOLUTE | TTF_TRACK,
                                  .hwnd = hEditControl,
                                  .uId = (UINT_PTR)GetDlgCtrlID(hEditControl),
                                  .lpszText = L"",
                              });

      // Subclass edit control
      if (!SetWindowSubclass(hEditControl, edit_subclass_proc, subclass_id_edit, (DWORD_PTR)tt)) {
        OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
        goto cleanup;
      }
    }
  }

  result = true;

cleanup:
  if (!result) {
    if (tt) {
      if (tt->edit_control) {
        RemoveWindowSubclass(tt->edit_control, edit_subclass_proc, subclass_id_edit);
      }
      if (tt->listbox) {
        RemoveWindowSubclass(tt->listbox, listbox_subclass_proc, subclass_id_listbox);
      }
      if (tt->tooltip_window) {
        DestroyWindow(tt->tooltip_window);
      }
      if (tt->parent) {
        RemovePropW(tt->parent, g_property_name);
      }
      OV_FREE(&tt);
    }
    return NULL;
  }
  return tt;
}

void config_dialog_tooltip_destroy(struct config_dialog_tooltip **ttpp) {
  if (!ttpp || !*ttpp) {
    return;
  }
  struct config_dialog_tooltip *ttp = *ttpp;

  if (ttp->parent) {
    KillTimer(ttp->parent, timer_id_refresh);
    RemovePropW(ttp->parent, g_property_name);
    ttp->parent = NULL;
  }
  if (ttp->listbox) {
    RemoveWindowSubclass(ttp->listbox, listbox_subclass_proc, subclass_id_listbox);
    ttp->listbox = NULL;
  }
  if (ttp->edit_control) {
    RemoveWindowSubclass(ttp->edit_control, edit_subclass_proc, subclass_id_edit);
    ttp->edit_control = NULL;
  }

  if (ttp->tooltip_window) {
    DestroyWindow(ttp->tooltip_window);
    ttp->tooltip_window = NULL;
  }
  if (ttp->tooltip_text) {
    OV_ARRAY_DESTROY(&ttp->tooltip_text);
  }
  OV_FREE(ttpp);
}
