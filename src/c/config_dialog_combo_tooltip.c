#include "config_dialog_combo_tooltip.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>

#include <ovbase.h>
#include <ovmo.h>
#include <ovprintf.h>

enum {
  subclass_id_parent = 100,
  subclass_id_listbox = 101,
};

struct config_dialog_combo_tooltip {
  HWND tooltip_window;
  HWND parent;
  HWND combobox;
  HWND listbox;
  int hover_item;
  config_dialog_combo_tooltip_callback callback;
  void *userdata;
};

static void update_tooltip(struct config_dialog_combo_tooltip *state, bool force_hide) {
  if (!state || !state->tooltip_window || !state->listbox || !state->callback) {
    return;
  }

  // Get current selection from listbox
  LRESULT const sel = SendMessageW(state->listbox, LB_GETCURSEL, 0, 0);
  int const item_index = (sel == LB_ERR) ? -1 : (int)sel;

  // Hide tooltip if no valid item/text and force_hide is true
  if (item_index < 0 || (force_hide && state->hover_item >= 0)) {
    SendMessageW(state->tooltip_window,
                 TTM_TRACKACTIVATE,
                 FALSE,
                 (LPARAM) & (TOOLINFOW){
                                .cbSize = sizeof(TOOLINFOW),
                                .hwnd = state->listbox,
                                .uId = (UINT_PTR)GetDlgCtrlID(state->listbox),
                            });
    state->hover_item = -1;
    return;
  }

  // Same item - nothing to do
  if (state->hover_item == item_index) {
    return;
  }

  state->hover_item = item_index;

  // Update tooltip text
  {
    // Get tooltip text from callback
    char const *text = state->callback(item_index, state->userdata);
    if (!text || !text[0]) {
      text = "No description available.";
    }

    // Convert UTF-8 to wchar_t
    wchar_t buf[256];
    wchar_t const ph[] = L"%1$s";
    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), ph, ph, text);
    SendMessageW(state->tooltip_window,
                 TTM_UPDATETIPTEXTW,
                 0,
                 (LPARAM) & (TOOLINFOW){
                                .cbSize = sizeof(TOOLINFOW),
                                .hwnd = state->listbox,
                                .uId = (UINT_PTR)GetDlgCtrlID(state->listbox),
                                .lpszText = buf,
                            });
  }

  // Calculate position
  RECT rc;
  GetWindowRect(state->listbox, &rc);
  SendMessageW(state->tooltip_window, TTM_TRACKPOSITION, 0, MAKELPARAM(rc.left, rc.bottom + 4));

  SendMessageW(state->tooltip_window,
               TTM_TRACKACTIVATE,
               TRUE,
               (LPARAM) & (TOOLINFOW){
                              .cbSize = sizeof(TOOLINFOW),
                              .hwnd = state->listbox,
                              .uId = (UINT_PTR)GetDlgCtrlID(state->listbox),
                          });
}

static LRESULT CALLBACK listbox_subclass_proc(
    HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  struct config_dialog_combo_tooltip *state = (struct config_dialog_combo_tooltip *)dwRefData;
  switch (message) {
  case WM_MOUSEMOVE:
    update_tooltip(state, false);
    break;
  case WM_KEYDOWN:
    update_tooltip(state, false);
    break;
  case WM_NCDESTROY:
    RemoveWindowSubclass(hWnd, listbox_subclass_proc, uIdSubclass);
    break;
  }

  return DefSubclassProc(hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK
parent_subclass_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  struct config_dialog_combo_tooltip *state = (struct config_dialog_combo_tooltip *)dwRefData;
  (void)uIdSubclass;

  switch (message) {
  case WM_COMMAND:
    if ((HWND)lParam == state->combobox) {
      if (HIWORD(wParam) == CBN_DROPDOWN) {
        // Get combobox listbox when dropdown opens
        COMBOBOXINFO cbi = {0};
        cbi.cbSize = sizeof(COMBOBOXINFO);
        if (GetComboBoxInfo(state->combobox, &cbi)) {
          state->listbox = cbi.hwndList;
          if (state->listbox && state->tooltip_window) {
            // Register listbox with tooltip
            SendMessageW(state->tooltip_window,
                         TTM_ADDTOOLW,
                         0,
                         (LPARAM) & (TOOLINFOW){
                                        .cbSize = sizeof(TOOLINFOW),
                                        .uFlags = TTF_ABSOLUTE | TTF_TRACK,
                                        .hwnd = state->listbox,
                                        .uId = (UINT_PTR)GetDlgCtrlID(state->listbox),
                                        .lpszText = L"",
                                    });

            // Subclass the listbox
            SetWindowSubclass(state->listbox, listbox_subclass_proc, subclass_id_listbox, (DWORD_PTR)state);
          }
        }
      } else if (HIWORD(wParam) == CBN_CLOSEUP) {
        // Clean up when dropdown closes
        if (state->listbox) {
          update_tooltip(state, true);
          RemoveWindowSubclass(state->listbox, listbox_subclass_proc, subclass_id_listbox);
          state->listbox = NULL;
        }
      }
    }
    break;

  case WM_NCDESTROY:
    RemoveWindowSubclass(hWnd, parent_subclass_proc, uIdSubclass);
    break;
  }

  return DefSubclassProc(hWnd, message, wParam, lParam);
}

struct config_dialog_combo_tooltip *config_dialog_combo_tooltip_create(void *parent,
                                                                       void *combobox,
                                                                       config_dialog_combo_tooltip_callback callback,
                                                                       void *userdata,
                                                                       struct ov_error *const err) {
  if (!parent || !combobox || !callback) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  HWND const hParent = (HWND)parent;
  HWND const hCombobox = (HWND)combobox;

  struct config_dialog_combo_tooltip *tt = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&tt, 1, sizeof(struct config_dialog_combo_tooltip))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    *tt = (struct config_dialog_combo_tooltip){
        .hover_item = -1,
        .parent = hParent,
        .combobox = hCombobox,
        .callback = callback,
        .userdata = userdata,
    };

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
    SendMessageW(tt->tooltip_window, TTM_SETMAXTIPWIDTH, 0, 600);

    // Subclass parent to detect dropdown/closeup
    if (!SetWindowSubclass(hParent, parent_subclass_proc, subclass_id_parent, (DWORD_PTR)tt)) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (!result) {
    if (tt) {
      if (tt->parent) {
        RemoveWindowSubclass(tt->parent, parent_subclass_proc, subclass_id_parent);
      }
      if (tt->listbox) {
        RemoveWindowSubclass(tt->listbox, listbox_subclass_proc, subclass_id_listbox);
      }
      if (tt->tooltip_window) {
        DestroyWindow(tt->tooltip_window);
      }
      OV_FREE(&tt);
    }
    return NULL;
  }
  return tt;
}

void config_dialog_combo_tooltip_destroy(struct config_dialog_combo_tooltip **ttpp) {
  if (!ttpp || !*ttpp) {
    return;
  }
  struct config_dialog_combo_tooltip *ttp = *ttpp;

  if (ttp->parent) {
    RemoveWindowSubclass(ttp->parent, parent_subclass_proc, subclass_id_parent);
    ttp->parent = NULL;
  }
  if (ttp->listbox) {
    RemoveWindowSubclass(ttp->listbox, listbox_subclass_proc, subclass_id_listbox);
    ttp->listbox = NULL;
  }

  if (ttp->tooltip_window) {
    DestroyWindow(ttp->tooltip_window);
    ttp->tooltip_window = NULL;
  }
  OV_FREE(ttpp);
}
