#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>

#include <ovbase.h>

/**
 * @brief Display an error dialog with detailed error information
 *
 * @param owner Parent window handle (NULL for auto-detect)
 * @param err Error information to display
 * @param window_title Window title text
 * @param main_instruction Main instruction text displayed prominently
 * @param content Content text (can be NULL)
 * @param icon Icon to display (e.g., TD_ERROR_ICON, TD_WARNING_ICON)
 * @param buttons Button flags (e.g., TDCBF_OK_BUTTON, TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON)
 * @return Button ID that was clicked (e.g., IDOK, IDRETRY, IDCANCEL), or 0 on failure
 */
int gcmz_error_dialog(HWND owner,
                      struct ov_error const *const err,
                      wchar_t const *const window_title,
                      wchar_t const *const main_instruction,
                      wchar_t const *const content,
                      PCWSTR icon,
                      TASKDIALOG_COMMON_BUTTON_FLAGS buttons);
