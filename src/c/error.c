#include "error.h"
#include "logf.h"

#include <ovarray.h>
#include <ovutf.h>

#include <ovl/os.h>

#include "aviutl2.h"

static HANDLE create_activation_context_for_comctl32(void) {
  ACTCTXW actctx = {
      .cbSize = sizeof(ACTCTXW),
      .dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID,
      .lpResourceName = MAKEINTRESOURCEW(1),
      .hModule = NULL,
  };
  if (!ovl_os_get_hinstance_from_fnptr(
          (void *)create_activation_context_for_comctl32, (void **)&actctx.hModule, NULL)) {
    return INVALID_HANDLE_VALUE;
  }
  return CreateActCtxW(&actctx);
}

/**
 * Get a suitable owner window for the error dialog
 * @return HWND to use as owner window (never returns NULL)
 */
static HWND get_owner_window(void) {
  HWND wnd = gcmz_aviutl2_get_main_window();
  if (wnd) {
    return wnd;
  }
  if (gcmz_aviutl2_find_manager_windows((void **)&wnd, 1, NULL)) {
    return wnd;
  }
  return GetDesktopWindow();
}

int gcmz_error_dialog(HWND owner,
                      struct ov_error const *const err,
                      wchar_t const *const window_title,
                      wchar_t const *const main_instruction,
                      wchar_t const *const content,
                      PCWSTR icon,
                      TASKDIALOG_COMMON_BUTTON_FLAGS buttons) {
  if (!err || !window_title || !main_instruction) {
    return 0;
  }

  if (!owner) {
    owner = get_owner_window();
  }

  char *msg_utf8 = NULL;
  wchar_t *msg_wchar = NULL;
  HANDLE hActCtx = INVALID_HANDLE_VALUE;
  ULONG_PTR cookie = 0;
  bool activated = false;
  int button_id = 0;
  bool success = false;
  struct ov_error err2 = {0};

  {
    if (!ov_error_to_string(err, &msg_utf8, true, &err2)) {
      OV_ERROR_ADD_TRACE(&err2);
      goto cleanup;
    }

    gcmz_logf_error(NULL,
                    "%1$ls%2$ls%3$hs",
                    "%1$ls\n%2$ls\n----------------\n%3$hs",
                    main_instruction,
                    content ? content : L"",
                    msg_utf8);

    size_t const len = strlen(msg_utf8);
    size_t const wchar_len = ov_utf8_to_wchar_len(msg_utf8, len);
    if (wchar_len == 0) {
      OV_ERROR_SET_GENERIC(&err2, ov_error_generic_fail);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&msg_wchar, wchar_len + 1)) {
      OV_ERROR_SET_GENERIC(&err2, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    ov_utf8_to_wchar(msg_utf8, len, msg_wchar, wchar_len + 1, NULL);

    hActCtx = create_activation_context_for_comctl32();
    if (hActCtx == INVALID_HANDLE_VALUE) {
      OV_ERROR_SET_HRESULT(&err2, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    if (!ActivateActCtx(hActCtx, &cookie)) {
      OV_ERROR_SET_HRESULT(&err2, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    activated = true;

    HRESULT hr = TaskDialogIndirect(
        &(TASKDIALOGCONFIG){
            .cbSize = sizeof(TASKDIALOGCONFIG),
            .hwndParent = owner,
            .dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_EXPAND_FOOTER_AREA,
            .dwCommonButtons = buttons,
            .pszWindowTitle = window_title,
            .pszMainIcon = icon,
            .pszMainInstruction = main_instruction,
            .pszContent = content,
            .pszExpandedInformation = msg_wchar,
        },
        &button_id,
        NULL,
        NULL);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(&err2, hr);
      goto cleanup;
    }
    success = true;
  }

cleanup:
  if (activated) {
    DeactivateActCtx(0, cookie);
    activated = false;
  }
  if (hActCtx != INVALID_HANDLE_VALUE) {
    ReleaseActCtx(hActCtx);
  }
  if (msg_wchar) {
    OV_ARRAY_DESTROY(&msg_wchar);
  }
  if (msg_utf8) {
    OV_ARRAY_DESTROY(&msg_utf8);
  }
  if (!success) {
    OV_ERROR_REPORT(&err2, NULL);
  }
  return button_id;
}
