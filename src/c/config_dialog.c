#include "config_dialog.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <ovarray.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/dialog.h>
#include <ovl/os.h>

#include "config.h"
#include "config_dialog_combo_tooltip.h"
#include "config_dialog_tooltip.h"
#include "gcmz_types.h"

enum {
  id_tab_control = 100,

  id_group_save_destination = 200,
  id_label_save_description = 201,
  id_label_processing_mode = 202,
  id_combo_processing_mode = 203,

  id_label_folder = 210,
  id_edit_new_path = 211,
  id_button_browse = 212,

  id_list_save_paths = 220,
  id_button_add_path = 221,
  id_button_move_up = 222,
  id_button_move_down = 223,
  id_button_remove_path = 224,

  id_check_create_directories = 230,

  id_group_external_api = 300,
  id_check_enable_external_api = 301,
  id_label_external_api_status = 302,

  id_group_debug = 400,
  id_check_show_debug_menu = 401,

  id_list_handlers = 500,
};

enum {
  tab_index_settings = 0,
  tab_index_scripts = 1,
};

// Script type constants for sorting
enum {
  script_type_handler = 1,
  script_type_module = 2,
};

static NATIVE_CHAR const g_config_dialog_prop_name[] = L"GCMZConfigDialogData";

struct dialog_data {
  struct gcmz_config *config;
  gcmz_config_dialog_enum_handlers_fn enum_handlers;
  void *enum_handlers_context;
  gcmz_config_dialog_enum_script_modules_fn enum_script_modules;
  void *enum_script_modules_context;
  struct config_dialog_tooltip *tooltip;
  struct config_dialog_combo_tooltip *combo_tooltip;
  bool external_api_running;
  HFONT dialog_font;
  int current_tab;
};

static char const *get_processing_mode_tooltip(int index, void *userdata) {
  (void)userdata;
  switch (index) {
  case 0: // Auto-detect
    return gettext(
        "Files in system folders (Temp, Program Files, etc.) are copied to the save destination before being dropped.\n"
        "Recommended for normal use.");
  case 1: // Prefer direct read
    return gettext("Files in the Temp folder are copied to the save destination before being dropped.");
  case 2: // Prefer copy
    return gettext("Any dropped files are copied to the save destination before being dropped.");
  default:
    return "";
  }
}

static void update_path_buttons_state(HWND dialog) {
  HWND list = GetDlgItem(dialog, id_list_save_paths);
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  int count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);

  // Delete button is always enabled when an item is selected (we show dialog for fallback entry)
  EnableWindow(GetDlgItem(dialog, id_button_remove_path), sel != LB_ERR);
  // Move up button is disabled when the first item or fallback entry is selected
  EnableWindow(GetDlgItem(dialog, id_button_move_up), sel > 0 && sel != count - 1);
  // -2 because we cannot move into fallback entry position
  EnableWindow(GetDlgItem(dialog, id_button_move_down), sel != LB_ERR && sel < count - 2);
}

static bool check_font_availability(wchar_t const *font_name, struct ov_error *const err) {
  if (!font_name || !font_name[0]) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t actual_name[LF_FACESIZE] = {0};
  HFONT hfont = NULL;
  HFONT old_font = NULL;
  bool result = false;

  HDC hdc = GetDC(NULL);
  if (!hdc) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  hfont = CreateFontW(0,
                      0,
                      0,
                      0,
                      FW_NORMAL,
                      FALSE,
                      FALSE,
                      FALSE,
                      DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS,
                      DEFAULT_QUALITY,
                      DEFAULT_PITCH | FF_DONTCARE,
                      font_name);
  if (!hfont) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  old_font = (HFONT)SelectObject(hdc, hfont);
  if (!old_font) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (!GetTextFaceW(hdc, LF_FACESIZE, actual_name)) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (wcscmp(font_name, actual_name) == 0) {
    result = true;
  }

cleanup:
  if (old_font && hdc) {
    SelectObject(hdc, old_font);
  }
  if (hfont) {
    DeleteObject(hfont);
  }
  if (hdc) {
    ReleaseDC(NULL, hdc);
  }
  return result;
}

static HFONT create_dialog_font(HWND dialog, char const *font_list_utf8, struct ov_error *const err) {
  if (!dialog || !font_list_utf8 || !font_list_utf8[0]) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  HDC hdc = NULL;
  HFONT hfont = NULL;
  int font_height = 0;

  {
    hdc = GetDC(NULL);
    if (!hdc) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    // Get font size from dialog resource
    HFONT current_font = (HFONT)(SendMessageW(dialog, WM_GETFONT, 0, 0));
    if (current_font) {
      LOGFONTW current_logfont = {0};
      if (GetObjectW(current_font, sizeof(LOGFONTW), &current_logfont)) {
        font_height = current_logfont.lfHeight;
      }
    }
    if (font_height == 0) {
      // Fallback to 9 point if we can't get current font size
      font_height = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    }

    char const *start = font_list_utf8;
    while (start && *start) {
      char const *end = strchr(start, '\n');
      size_t len = end ? (size_t)(end - start) : strlen(start);

      // Trim whitespace
      while (len > 0 && (*start == ' ' || *start == '\t' || *start == '\r')) {
        start++;
        len--;
      }
      while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\r')) {
        len--;
      }

      if (len > 0) {
        wchar_t font_name[LF_FACESIZE];
        if (ov_utf8_to_wchar(start, len, font_name, LF_FACESIZE, NULL) > 0) {
          if (check_font_availability(font_name, err)) {
            hfont = CreateFontW(font_height,
                                0,
                                0,
                                0,
                                FW_NORMAL,
                                FALSE,
                                FALSE,
                                FALSE,
                                DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                font_name);
            if (hfont) {
              goto cleanup;
            }
            OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
          }
          OV_ERROR_DESTROY(err);
        }
      }

      start = end ? end + 1 : NULL;
    }

    hfont = CreateFontW(font_height,
                        0,
                        0,
                        0,
                        FW_NORMAL,
                        FALSE,
                        FALSE,
                        FALSE,
                        DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE,
                        L"Tahoma");
  }

cleanup:
  if (hdc) {
    ReleaseDC(NULL, hdc);
  }
  return hfont;
}

static void set_dialog_font(HWND hwnd, HFONT hfont) {
  if (!hwnd || !hfont) {
    return;
  }
  SendMessageW(hwnd, WM_SETFONT, (WPARAM)hfont, FALSE);
  HWND child = GetWindow(hwnd, GW_CHILD);
  while (child) {
    set_dialog_font(child, hfont);
    child = GetWindow(child, GW_HWNDNEXT);
  }
}

static INT_PTR init_dialog(HWND dialog, struct dialog_data *data) {
  SetPropW(dialog, g_config_dialog_prop_name, data);

  // Enable visual styles for tab control background
  EnableThemeDialogTexture(dialog, ETDT_ENABLETAB);

  static wchar_t const ph[] = L"%1$s";
  struct ov_error err = {0};
  WCHAR buf[256];
  static char const font_list_key[] = gettext_noop("dialog_ui_font");
  char const *font_list = gettext(font_list_key);
  if (strcmp(font_list, font_list_key) == 0) {
    font_list = "Segoe UI\nTahoma\nMS Sans Serif";
  }
  data->dialog_font = create_dialog_font(dialog, font_list, &err);
  if (!data->dialog_font) {
    OV_ERROR_REPORT(&err, NULL);
  } else {
    set_dialog_font(dialog, data->dialog_font);
  }

  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("GCMZDrops Settings"));
  SetWindowTextW(dialog, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("OK"));
  SetWindowTextW(GetDlgItem(dialog, IDOK), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Cancel"));
  SetWindowTextW(GetDlgItem(dialog, IDCANCEL), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Save Destination"));
  SetWindowTextW(GetDlgItem(dialog, id_group_save_destination), buf);
  ov_snprintf_wchar(buf,
                    sizeof(buf) / sizeof(WCHAR),
                    ph,
                    ph,
                    gettext("Specifies where to create files when dropping images from the browser, etc.\n"
                            "If multiple paths are registered, they will be tried in order from the top."));
  SetWindowTextW(GetDlgItem(dialog, id_label_save_description), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Processing Mode:"));
  SetWindowTextW(GetDlgItem(dialog, id_label_processing_mode), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Folder:"));
  SetWindowTextW(GetDlgItem(dialog, id_label_folder), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("...(&I)"));
  SetWindowTextW(GetDlgItem(dialog, id_button_browse), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Add"));
  SetWindowTextW(GetDlgItem(dialog, id_button_add_path), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Move &Up"));
  SetWindowTextW(GetDlgItem(dialog, id_button_move_up), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Move &Down"));
  SetWindowTextW(GetDlgItem(dialog, id_button_move_down), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Remove"));
  SetWindowTextW(GetDlgItem(dialog, id_button_remove_path), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Make directories automatically"));
  SetWindowTextW(GetDlgItem(dialog, id_check_create_directories), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("External API"));
  SetWindowTextW(GetDlgItem(dialog, id_group_external_api), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Enable"));
  SetWindowTextW(GetDlgItem(dialog, id_check_enable_external_api), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Debug"));
  SetWindowTextW(GetDlgItem(dialog, id_group_debug), buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("&Show debug menu"));
  SetWindowTextW(GetDlgItem(dialog, id_check_show_debug_menu), buf);

  {
    enum gcmz_processing_mode processing_mode;
    if (!gcmz_config_get_processing_mode(data->config, &processing_mode, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      processing_mode = gcmz_processing_mode_auto;
    }
    HWND h = GetDlgItem(dialog, id_combo_processing_mode);
    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Auto-detect"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)buf);
    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Prefer direct read"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)buf);
    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, gettext("Prefer copy"));
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)buf);
    SendMessageW(h, CB_SETCURSEL, (WPARAM)gcmz_processing_mode_to_int(processing_mode), 0);
  }

  {
    bool allow_create_directories;
    if (!gcmz_config_get_allow_create_directories(data->config, &allow_create_directories, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      allow_create_directories = false;
    }
    HWND h = GetDlgItem(dialog, id_check_create_directories);
    SendMessageW(h, BM_SETCHECK, allow_create_directories ? BST_CHECKED : BST_UNCHECKED, 0);
  }

  {
    bool external_api;
    if (!gcmz_config_get_external_api(data->config, &external_api, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      external_api = false;
    }
    SendMessageW(
        GetDlgItem(dialog, id_check_enable_external_api), BM_SETCHECK, external_api ? BST_CHECKED : BST_UNCHECKED, 0);

    char const *status_text;
    if (!external_api) {
      status_text = pgettext("external_api_status", "Disabled");
    } else if (data->external_api_running) {
      status_text = pgettext("external_api_status", "Running");
    } else {
      status_text = pgettext("external_api_status", "Error");
    }
    ov_snprintf_wchar(buf,
                      sizeof(buf) / sizeof(WCHAR),
                      L"%1$s: %2$s",
                      L"%1$s: %2$s",
                      pgettext("external_api_status", "Current Status"),
                      status_text);
    SetWindowTextW(GetDlgItem(dialog, id_label_external_api_status), buf);
  }

  {
    bool show_debug_menu;
    if (!gcmz_config_get_show_debug_menu(data->config, &show_debug_menu, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      show_debug_menu = false;
    }
    SendMessageW(
        GetDlgItem(dialog, id_check_show_debug_menu), BM_SETCHECK, show_debug_menu ? BST_CHECKED : BST_UNCHECKED, 0);
  }

  {
    HWND h = GetDlgItem(dialog, id_list_save_paths);
    wchar_t const *const *config_paths = gcmz_config_get_save_paths(data->config);
    size_t const paths_count = OV_ARRAY_LENGTH(config_paths);
    for (size_t i = 0; i < paths_count; i++) {
      if (config_paths[i]) {
        SendMessageW(h, LB_ADDSTRING, 0, (LPARAM)config_paths[i]);
      }
    }
    // Add fallback entry
    SendMessageW(h, LB_ADDSTRING, 0, (LPARAM)(gcmz_config_get_fallback_save_path()));
  }

  {
    HWND list = GetDlgItem(dialog, id_list_save_paths);
    HWND edit = GetDlgItem(dialog, id_edit_new_path);
    data->tooltip = config_dialog_tooltip_create(data->config, dialog, list, edit, &err);
    if (!data->tooltip) {
      OV_ERROR_REPORT(&err, NULL);
    }
  }

  {
    // Create tooltip for combobox dropdown
    HWND combo = GetDlgItem(dialog, id_combo_processing_mode);
    data->combo_tooltip = config_dialog_combo_tooltip_create(dialog, combo, get_processing_mode_tooltip, NULL, &err);
    if (!data->combo_tooltip) {
      OV_ERROR_REPORT(&err, NULL);
    }
  }

  // Initialize tab control
  {
    HWND tab = GetDlgItem(dialog, id_tab_control);
    TCITEMW item = {0};
    item.mask = TCIF_TEXT;

    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, pgettext("config_dialog", "Settings"));
    item.pszText = buf;
    SendMessageW(tab, TCM_INSERTITEMW, (WPARAM)tab_index_settings, (LPARAM)&item);

    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, pgettext("config_dialog", "Script Info"));
    item.pszText = buf;
    SendMessageW(tab, TCM_INSERTITEMW, (WPARAM)tab_index_scripts, (LPARAM)&item);

    data->current_tab = tab_index_settings;
  }

  // Initialize handlers list view
  {
    HWND list = GetDlgItem(dialog, id_list_handlers);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, pgettext("script_info", "Type"));
    col.pszText = buf;
    col.cx = 90;
    col.iSubItem = 0;
    SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, pgettext("script_info", "Name"));
    col.pszText = buf;
    col.cx = 100;
    col.iSubItem = 1;
    SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, pgettext("script_info", "Priority"));
    col.pszText = buf;
    col.cx = 50;
    col.iSubItem = 2;
    SendMessageW(list, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);

    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), ph, ph, pgettext("script_info", "Source"));
    col.pszText = buf;
    col.cx = 150;
    col.iSubItem = 3;
    SendMessageW(list, LVM_INSERTCOLUMNW, 3, (LPARAM)&col);

    // Hide scripts list initially (Settings tab is shown first)
    ShowWindow(list, SW_HIDE);
  }

  update_path_buttons_state(dialog);
  return TRUE;
}

// Settings tab controls (shown/hidden when switching tabs)
static int const settings_tab_controls[] = {
    id_group_save_destination,
    id_label_save_description,
    id_label_processing_mode,
    id_combo_processing_mode,
    id_label_folder,
    id_edit_new_path,
    id_button_browse,
    id_list_save_paths,
    id_button_add_path,
    id_button_move_up,
    id_button_move_down,
    id_button_remove_path,
    id_check_create_directories,
    id_group_external_api,
    id_check_enable_external_api,
    id_label_external_api_status,
    id_group_debug,
    id_check_show_debug_menu,
};

// Handlers tab controls (shown/hidden when switching tabs)
static int const scripts_tab_controls[] = {
    id_list_handlers,
};

static void show_tab_controls(HWND dialog, int const *controls, size_t count, int show_cmd) {
  for (size_t i = 0; i < count; i++) {
    HWND h = GetDlgItem(dialog, controls[i]);
    if (h) {
      ShowWindow(h, show_cmd);
    }
  }
}

// Script entry structure for collecting and sorting
struct script_entry {
  int type;         // script_type_handler or script_type_module
  int priority;     // Only meaningful for handlers
  char name[256];   // Script/module name (UTF-8)
  char source[512]; // Source path (UTF-8)
};

static int compare_script_entries(void const *a, void const *b) {
  struct script_entry const *ea = (struct script_entry const *)a;
  struct script_entry const *eb = (struct script_entry const *)b;

  // First sort by type (handlers first, then modules)
  if (ea->type != eb->type) {
    return ea->type - eb->type;
  }

  // Within same type, sort by priority (ascending)
  if (ea->priority != eb->priority) {
    return ea->priority - eb->priority;
  }

  // Finally sort by name
  return strcmp(ea->name, eb->name);
}

struct script_enum_context {
  struct script_entry *entries;
  size_t count;
  size_t capacity;
};

static bool handler_collect_callback(char const *name, int priority, char const *source, void *userdata) {
  struct script_enum_context *ctx = (struct script_enum_context *)userdata;
  if (!ctx) {
    return false;
  }

  if (ctx->count >= ctx->capacity) {
    size_t const new_capacity = ctx->capacity == 0 ? 16 : ctx->capacity * 2;
    if (!OV_ARRAY_GROW(&ctx->entries, new_capacity)) {
      return false;
    }
    ctx->capacity = new_capacity;
  }

  struct script_entry *entry = &ctx->entries[ctx->count];
  entry->type = script_type_handler;
  entry->priority = priority;
  strncpy(entry->name, name ? name : "", sizeof(entry->name) - 1);
  entry->name[sizeof(entry->name) - 1] = '\0';
  strncpy(entry->source, source ? source : "", sizeof(entry->source) - 1);
  entry->source[sizeof(entry->source) - 1] = '\0';
  ctx->count++;

  return true;
}

static bool module_collect_callback(char const *name, char const *source, void *userdata) {
  struct script_enum_context *ctx = (struct script_enum_context *)userdata;
  if (!ctx) {
    return false;
  }

  if (ctx->count >= ctx->capacity) {
    size_t const new_capacity = ctx->capacity == 0 ? 16 : ctx->capacity * 2;
    if (!OV_ARRAY_GROW(&ctx->entries, new_capacity)) {
      return false;
    }
    ctx->capacity = new_capacity;
  }

  struct script_entry *entry = &ctx->entries[ctx->count];
  entry->type = script_type_module;
  entry->priority = 0; // Script modules have no priority
  strncpy(entry->name, name ? name : "", sizeof(entry->name) - 1);
  entry->name[sizeof(entry->name) - 1] = '\0';
  strncpy(entry->source, source ? source : "", sizeof(entry->source) - 1);
  entry->source[sizeof(entry->source) - 1] = '\0';
  ctx->count++;

  return true;
}

static void populate_scripts_list(HWND dialog, struct dialog_data *data) {
  HWND list = GetDlgItem(dialog, id_list_handlers);
  if (!list) {
    return;
  }

  SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);

  struct ov_error err = {0};
  struct script_enum_context ctx = {0};

  // Collect handlers
  if (data->enum_handlers) {
    if (!data->enum_handlers(data->enum_handlers_context, handler_collect_callback, &ctx, &err)) {
      OV_ERROR_REPORT(&err, NULL);
    }
  }

  // Collect script modules
  if (data->enum_script_modules) {
    if (!data->enum_script_modules(data->enum_script_modules_context, module_collect_callback, &ctx, &err)) {
      OV_ERROR_REPORT(&err, NULL);
    }
  }

  if (ctx.count == 0) {
    goto cleanup;
  }

  // Sort entries
  qsort(ctx.entries, ctx.count, sizeof(struct script_entry), compare_script_entries);

  // Add sorted entries to list view
  static wchar_t const ph[] = L"%1$s";
  for (size_t i = 0; i < ctx.count; i++) {
    struct script_entry const *entry = &ctx.entries[i];

    WCHAR type_w[64] = {0};
    WCHAR name_w[256] = {0};
    WCHAR priority_w[32] = {0};
    WCHAR source_w[MAX_PATH] = {0};

    // Type column
    if (entry->type == script_type_handler) {
      ov_snprintf_wchar(type_w, sizeof(type_w) / sizeof(type_w[0]), ph, ph, pgettext("script_type", "Handler"));
    } else {
      ov_snprintf_wchar(type_w, sizeof(type_w) / sizeof(type_w[0]), ph, ph, pgettext("script_type", "Script Module"));
    }

    // Name column
    ov_utf8_to_wchar(entry->name, strlen(entry->name), name_w, sizeof(name_w) / sizeof(name_w[0]) - 1, NULL);

    // Priority column (empty for script modules)
    if (entry->type == script_type_handler) {
      ov_snprintf_wchar(priority_w, sizeof(priority_w) / sizeof(priority_w[0]), L"%d", L"%d", entry->priority);
    }

    // Source column
    ov_utf8_to_wchar(entry->source, strlen(entry->source), source_w, sizeof(source_w) / sizeof(source_w[0]) - 1, NULL);

    LVITEMW item = {0};
    item.mask = LVIF_TEXT;
    item.iItem = (int)i;
    item.iSubItem = 0;
    item.pszText = type_w;
    SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);

    LVITEMW subitem = {0};
    subitem.mask = LVIF_TEXT;
    subitem.iItem = (int)i;

    subitem.iSubItem = 1;
    subitem.pszText = name_w;
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&subitem);

    subitem.iSubItem = 2;
    subitem.pszText = priority_w;
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&subitem);

    subitem.iSubItem = 3;
    subitem.pszText = source_w;
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&subitem);
  }

cleanup:
  if (ctx.entries) {
    OV_ARRAY_DESTROY(&ctx.entries);
  }
}

static void switch_tab(HWND dialog, struct dialog_data *data, int new_tab) {
  if (data->current_tab == new_tab) {
    return;
  }

  // Hide current tab controls
  if (data->current_tab == tab_index_settings) {
    show_tab_controls(
        dialog, settings_tab_controls, sizeof(settings_tab_controls) / sizeof(settings_tab_controls[0]), SW_HIDE);
  } else if (data->current_tab == tab_index_scripts) {
    show_tab_controls(
        dialog, scripts_tab_controls, sizeof(scripts_tab_controls) / sizeof(scripts_tab_controls[0]), SW_HIDE);
  }

  // Show new tab controls
  if (new_tab == tab_index_settings) {
    show_tab_controls(
        dialog, settings_tab_controls, sizeof(settings_tab_controls) / sizeof(settings_tab_controls[0]), SW_SHOW);
  } else if (new_tab == tab_index_scripts) {
    show_tab_controls(
        dialog, scripts_tab_controls, sizeof(scripts_tab_controls) / sizeof(scripts_tab_controls[0]), SW_SHOW);
    // Populate the scripts list when switching to this tab
    populate_scripts_list(dialog, data);
  }

  data->current_tab = new_tab;
}

static INT_PTR click_add_path(HWND dialog) {
  wchar_t *new_path = NULL;
  HWND edit = GetDlgItem(dialog, id_edit_new_path);
  HWND list = GetDlgItem(dialog, id_list_save_paths);
  int const len = GetWindowTextLengthW(edit);
  if (len < 0) {
    goto cleanup;
  }
  if (len == 0) {
    goto cleanup;
  }
  if (!OV_ARRAY_GROW(&new_path, (size_t)(len + 1))) {
    goto cleanup;
  }
  GetWindowTextW(edit, new_path, len + 1);
  SendMessageW(list, LB_INSERTSTRING, 0, (LPARAM)new_path);
  SetWindowTextW(edit, L"");
  update_path_buttons_state(dialog);

cleanup:
  if (new_path) {
    OV_ARRAY_DESTROY(&new_path);
  }
  return TRUE;
}

static INT_PTR click_remove_path(HWND dialog) {
  WCHAR buf[256];
  WCHAR buf_caption[256];

  HWND list = GetDlgItem(dialog, id_list_save_paths);
  int const sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  int const count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
  if (sel == LB_ERR) {
    goto cleanup;
  }
  if (sel == count - 1) {
    ov_snprintf_wchar(buf,
                      sizeof(buf) / sizeof(WCHAR),
                      L"%s",
                      L"%s",
                      gettext("This item cannot be deleted.\n\n"
                              "If none of the registered folders are available, "
                              "files will be stored in this folder as a last resort."));
    ov_snprintf_wchar(buf_caption, sizeof(buf_caption) / sizeof(WCHAR), L"%s", L"%s", gettext("GCMZDrops"));
    MessageBoxW(dialog, buf, buf_caption, MB_OK | MB_ICONINFORMATION);
    goto cleanup;
  }
  SendMessageW(list, LB_DELETESTRING, (WPARAM)sel, 0);
  update_path_buttons_state(dialog);

cleanup:
  return TRUE;
}

static INT_PTR click_move_up(HWND dialog) {
  wchar_t *text = NULL;

  HWND list = GetDlgItem(dialog, id_list_save_paths);
  int count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  if (sel == LB_ERR) {
    goto cleanup;
  }
  if (sel == count - 1) {
    // Protect fallback entry item from moving
    goto cleanup;
  }
  if (sel == 0) {
    // Cannot move the first item up
    goto cleanup;
  }

  {
    int const len = (int)SendMessageW(list, LB_GETTEXTLEN, (WPARAM)sel, 0);
    if (len < 0) {
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&text, (size_t)(len + 1))) {
      goto cleanup;
    }
  }
  SendMessageW(list, LB_GETTEXT, (WPARAM)sel, (LPARAM)text);
  SendMessageW(list, LB_DELETESTRING, (WPARAM)sel, 0);
  SendMessageW(list, LB_INSERTSTRING, (WPARAM)(sel - 1), (LPARAM)text);
  SendMessageW(list, LB_SETCURSEL, (WPARAM)(sel - 1), 0);
  update_path_buttons_state(dialog);

cleanup:
  if (text) {
    OV_ARRAY_DESTROY(&text);
  }
  return TRUE;
}

static INT_PTR click_move_down(HWND dialog) {
  wchar_t *text = NULL;

  HWND list = GetDlgItem(dialog, id_list_save_paths);
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  int count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
  if (sel == LB_ERR) {
    goto cleanup;
  }
  if (sel == count - 1) {
    // Protect fallback entry item from moving
    goto cleanup;
  }
  if (sel == count - 2) {
    // Cannot move the second-to-last item down
    goto cleanup;
  }

  {
    int const len = (int)SendMessageW(list, LB_GETTEXTLEN, (WPARAM)sel, 0);
    if (len < 0) {
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&text, (size_t)(len + 1))) {
      goto cleanup;
    }
  }
  SendMessageW(list, LB_GETTEXT, (WPARAM)sel, (LPARAM)text);
  SendMessageW(list, LB_DELETESTRING, (WPARAM)sel, 0);
  SendMessageW(list, LB_INSERTSTRING, (WPARAM)(sel + 1), (LPARAM)text);
  SendMessageW(list, LB_SETCURSEL, (WPARAM)(sel + 1), 0);
  update_path_buttons_state(dialog);

cleanup:
  if (text) {
    OV_ARRAY_DESTROY(&text);
  }
  return TRUE;
}

static INT_PTR click_browse(HWND dialog) {
  enum {
    id_menu_select_folder = 300,
    id_menu_insert_projectdir = 301,
    id_menu_insert_shareddir = 302,
    id_menu_insert_year = 303,
    id_menu_insert_month = 304,
    id_menu_insert_day = 305,
    id_menu_insert_hour = 306,
    id_menu_insert_minute = 307,
    id_menu_insert_second = 308,
    id_menu_insert_millisecond = 309,
  };

  static wchar_t const project_dir_name[] = L"%PROJECTDIR%";
  static wchar_t const shared_dir_name[] = L"%SHAREDDIR%";
  static wchar_t const year_name[] = L"%YEAR%";
  static wchar_t const month_name[] = L"%MONTH%";
  static wchar_t const day_name[] = L"%DAY%";
  static wchar_t const hour_name[] = L"%HOUR%";
  static wchar_t const minute_name[] = L"%MINUTE%";
  static wchar_t const second_name[] = L"%SECOND%";
  static wchar_t const millisecond_name[] = L"%MILLISECOND%";

  wchar_t *path = NULL;
  struct ov_error err = {0};
  HMENU menu = NULL;
  HMENU sub_menu = NULL;
  wchar_t buf[256];

  RECT button_rect;
  GetWindowRect(GetDlgItem(dialog, id_button_browse), &button_rect);

  menu = CreatePopupMenu();
  if (!menu) {
    OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  sub_menu = CreatePopupMenu();
  if (!sub_menu) {
    OV_ERROR_SET_HRESULT(&err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%s", L"%s", gettext("&Browse Folder"));
  AppendMenuW(menu, MF_STRING, id_menu_select_folder, buf);
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%s", L"%s", gettext("&Insert Placeholder"));
  AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub_menu, buf);
  static wchar_t const menu_fmt[] = L"%1$ls - %2$s";
  ov_snprintf_wchar(buf,
                    sizeof(buf) / sizeof(wchar_t),
                    menu_fmt,
                    menu_fmt,
                    project_dir_name,
                    gettext("Folder containing the &project file being edited"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_projectdir, buf);
  ov_snprintf_wchar(
      buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, shared_dir_name, gettext("&Shared folder for GCMZDrops"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_shareddir, buf);
  AppendMenuW(sub_menu, MF_SEPARATOR, 0, NULL);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, year_name, gettext("&Year"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_year, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, month_name, gettext("&Month"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_month, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, day_name, gettext("&Day"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_day, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, hour_name, gettext("&Hour"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_hour, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, minute_name, gettext("M&inute"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_minute, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, second_name, gettext("S&econd"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_second, buf);
  ov_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), menu_fmt, menu_fmt, millisecond_name, gettext("Mi&llisecond"));
  AppendMenuW(sub_menu, MF_STRING, id_menu_insert_millisecond, buf);

  {
    int selected =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, button_rect.left, button_rect.bottom, 0, dialog, NULL);
    if (selected == id_menu_select_folder) {
      static GUID const client_guid = {0x12345678, 0x1234, 0x5678, {0x90, 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78}};
      ov_snprintf_wchar(
          buf, sizeof(buf) / sizeof(wchar_t), L"%s", L"%s", gettext("Please select the destination folder"));
      if (!ovl_dialog_select_folder(dialog, buf, &client_guid, NULL, &path, &err)) {
        if (ov_error_is(&err, ov_error_type_hresult, HRESULT_FROM_WIN32(ERROR_CANCELLED))) {
          OV_ERROR_DESTROY(&err);
          goto cleanup;
        }
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
      if (path) {
        SetDlgItemTextW(dialog, id_edit_new_path, path);
      }
    } else if (selected == id_menu_insert_projectdir) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)project_dir_name);
    } else if (selected == id_menu_insert_shareddir) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)shared_dir_name);
    } else if (selected == id_menu_insert_year) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)year_name);
    } else if (selected == id_menu_insert_month) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)month_name);
    } else if (selected == id_menu_insert_day) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)day_name);
    } else if (selected == id_menu_insert_hour) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)hour_name);
    } else if (selected == id_menu_insert_minute) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)minute_name);
    } else if (selected == id_menu_insert_second) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)second_name);
    } else if (selected == id_menu_insert_millisecond) {
      HWND edit = GetDlgItem(dialog, id_edit_new_path);
      SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)millisecond_name);
    }
    SetFocus(GetDlgItem(dialog, id_edit_new_path));
  }

cleanup:
  if (sub_menu) {
    DestroyMenu(sub_menu);
  }
  if (menu) {
    DestroyMenu(menu);
  }
  if (path) {
    OV_ARRAY_DESTROY(&path);
  }
  OV_ERROR_REPORT(&err, NULL);
  return TRUE;
}

static bool click_ok(HWND dialog, struct dialog_data *data) {
  WCHAR buf[256];
  WCHAR buf_caption[256];

  wchar_t **paths = NULL;
  int paths_count = 0;
  bool result = false;
  struct ov_error err = {0};

  {
    // Save processing mode
    HWND h = GetDlgItem(dialog, id_combo_processing_mode);
    int const selection = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    enum gcmz_processing_mode const processing_mode = gcmz_processing_mode_from_int(selection);
    if (!gcmz_config_set_processing_mode(data->config, processing_mode, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  {
    // Save directory creation setting
    HWND h = GetDlgItem(dialog, id_check_create_directories);
    LRESULT const checked = SendMessageW(h, BM_GETCHECK, 0, 0);
    bool const allow_create_directories = (checked == BST_CHECKED);
    if (!gcmz_config_set_allow_create_directories(data->config, allow_create_directories, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  {
    // Save external API setting
    HWND h = GetDlgItem(dialog, id_check_enable_external_api);
    LRESULT const external_api_checked = SendMessageW(h, BM_GETCHECK, 0, 0);
    bool const external_api = (external_api_checked == BST_CHECKED);
    if (!gcmz_config_set_external_api(data->config, external_api, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  {
    // Save show debug menu setting
    HWND h = GetDlgItem(dialog, id_check_show_debug_menu);
    LRESULT const show_debug_menu_checked = SendMessageW(h, BM_GETCHECK, 0, 0);
    bool const show_debug_menu = (show_debug_menu_checked == BST_CHECKED);
    if (!gcmz_config_set_show_debug_menu(data->config, show_debug_menu, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  {
    // Set new save paths (excluding fallback)
    HWND list = GetDlgItem(dialog, id_list_save_paths);
    int const count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
    paths_count = count > 0 ? count - 1 : 0; // Last item is fallback entry, so exclude it
    if (!paths_count) {
      // No paths, set empty array
      if (!gcmz_config_set_save_paths(data->config, NULL, 0, &err)) {
        OV_ERROR_ADD_TRACE(&err);
        goto cleanup;
      }
      result = true;
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&paths, (size_t)paths_count)) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    for (int i = 0; i < paths_count; i++) {
      paths[i] = NULL;
      int const text_len = (int)SendMessageW(list, LB_GETTEXTLEN, (WPARAM)i, 0);
      if (text_len > 0) {
        if (!OV_ARRAY_GROW(&paths[i], (size_t)(text_len + 1))) {
          OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
          goto cleanup;
        }
        SendMessageW(list, LB_GETTEXT, (WPARAM)i, (LPARAM)paths[i]);
      }
    }
    OV_ARRAY_SET_LENGTH(paths, (size_t)paths_count);
    if (!gcmz_config_set_save_paths(data->config, (wchar_t const *const *)paths, (size_t)paths_count, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (paths) {
    for (int i = 0; i < paths_count; i++) {
      if (paths[i]) {
        OV_ARRAY_DESTROY(&paths[i]);
      }
    }
    OV_ARRAY_DESTROY(&paths);
  }
  if (!result) {
    ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), L"%s", L"%s", gettext("Failed to save settings."));
    ov_snprintf_wchar(buf_caption, sizeof(buf_caption) / sizeof(WCHAR), L"%s", L"%s", gettext("GCMZDrops"));
    MessageBoxW(dialog, buf, buf_caption, MB_OK | MB_ICONERROR);
  }
  return result;
}

static INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  WCHAR buf[256];
  WCHAR buf_caption[256];

  struct dialog_data *data = (struct dialog_data *)GetPropW(dialog, g_config_dialog_prop_name);

  switch (message) {
  case WM_INITDIALOG:
    return init_dialog(dialog, (struct dialog_data *)lParam);

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case id_button_add_path:
      return click_add_path(dialog);

    case id_button_remove_path:
      return click_remove_path(dialog);

    case id_button_move_up:
      return click_move_up(dialog);

    case id_button_move_down:
      return click_move_down(dialog);

    case id_list_save_paths:
      if (HIWORD(wParam) == LBN_SELCHANGE) {
        update_path_buttons_state(dialog);
      }
      return TRUE;

    case id_button_browse:
      return click_browse(dialog);

    case IDOK:
      if (click_ok(dialog, data)) {
        struct ov_error err = {0};
        if (!gcmz_config_save(data->config, &err)) {
          OV_ERROR_REPORT(&err, NULL);
          ov_snprintf_wchar(buf, sizeof(buf) / sizeof(WCHAR), L"%s", L"%s", gettext("Failed to save settings."));
          ov_snprintf_wchar(buf_caption, sizeof(buf_caption) / sizeof(WCHAR), L"%s", L"%s", gettext("GCMZDrops"));
          MessageBoxW(dialog, buf, buf_caption, MB_OK | MB_ICONERROR);
          return TRUE;
        }
        EndDialog(dialog, IDOK);
      }
      return TRUE;

    case IDCANCEL:
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    break;

  case WM_NOTIFY: {
    NMHDR *nmhdr = (NMHDR *)lParam;
    if (nmhdr->idFrom == id_tab_control && nmhdr->code == TCN_SELCHANGE) {
      HWND tab = GetDlgItem(dialog, id_tab_control);
      int new_tab = (int)SendMessageW(tab, TCM_GETCURSEL, 0, 0);
      if (data) {
        switch_tab(dialog, data, new_tab);
      }
      return TRUE;
    }
  } break;

  case WM_DESTROY:
    if (data) {
      config_dialog_tooltip_destroy(&data->tooltip);
      config_dialog_combo_tooltip_destroy(&data->combo_tooltip);

      // Cleanup font
      if (data->dialog_font) {
        DeleteObject(data->dialog_font);
        data->dialog_font = NULL;
      }

      RemovePropW(dialog, g_config_dialog_prop_name);
    }
    return TRUE;
  }

  return FALSE;
}

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

bool gcmz_config_dialog_show(struct gcmz_config_dialog_options const *const options, struct ov_error *const err) {
  if (!options || !options->config) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!err) {
    return false;
  }

  struct dialog_data data = {0};
  bool result = false;
  void *hinstance = NULL;
  HANDLE hActCtx = INVALID_HANDLE_VALUE;
  ULONG_PTR cookie = 0;
  bool activated = false;

  {
    data.config = options->config;
    data.enum_handlers = options->enum_handlers;
    data.enum_handlers_context = options->enum_handlers_context;
    data.enum_script_modules = options->enum_script_modules;
    data.enum_script_modules_context = options->enum_script_modules_context;
    data.external_api_running = options->external_api_running;

    if (!ovl_os_get_hinstance_from_fnptr((void *)gcmz_config_dialog_show, &hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    hActCtx = create_activation_context_for_comctl32();
    if (hActCtx == INVALID_HANDLE_VALUE) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    if (!ActivateActCtx(hActCtx, &cookie)) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    activated = true;

    INT_PTR dialog_result = DialogBoxParamW(
        (HINSTANCE)hinstance, L"GCMZCONFIGDIALOG", (HWND)options->parent_window, dialog_proc, (LPARAM)&data);
    if (dialog_result == -1) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (activated) {
    DeactivateActCtx(0, cookie);
  }
  if (hActCtx != INVALID_HANDLE_VALUE) {
    ReleaseActCtx(hActCtx);
  }
  return result;
}
