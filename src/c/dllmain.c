
// Plugin startup sequence:
// The exported functions are called by AviUtl ExEdit2 in the following order:
// 1. DllMain(DLL_PROCESS_ATTACH) - Standard Windows DLL entry point
// 2. InitializeLogger - Called to set up logging functionality
// 3. InitializePlugin - Called to initialize the plugin with AviUtl ExEdit2 version info
// 4. RegisterPlugin - Called to register callbacks and handlers with AviUtl ExEdit2

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovbase.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/os.h>

#include <aviutl2_logger2.h>
#include <aviutl2_plugin2.h>

#include "error.h"
#include "gcmzdrops.h"
#include "logf.h"
#include "lua.h"
#include "version.h"

static struct gcmz_lua_context *g_lua = NULL;
static struct gcmzdrops *g_gcmzdrops = NULL;
static struct mo *g_mo = NULL;

/**
 * @brief Get or create the Lua context
 *
 * @param err [out] Error information on failure
 * @return Lua context instance, or NULL on failure
 */
static struct gcmz_lua_context *get_lua(struct ov_error *const err) {
  static bool create_failed = false;
  if (g_lua) {
    return g_lua;
  }
  if (create_failed) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return NULL;
  }
  if (!gcmz_lua_create(&g_lua, err)) {
    OV_ERROR_ADD_TRACE(err);
    create_failed = true;
    return NULL;
  }
  return g_lua;
}

void __declspec(dllexport) InitializeLogger(struct aviutl2_log_handle *logger);
void __declspec(dllexport) InitializeLogger(struct aviutl2_log_handle *logger) { gcmz_logf_set_handle(logger); }

BOOL __declspec(dllexport) InitializePlugin(DWORD version);
BOOL __declspec(dllexport) InitializePlugin(DWORD version) {
  struct ov_error err = {0};

  // Initialize language resources
  if (!g_mo) {
    HINSTANCE hinst = NULL;
    if (!ovl_os_get_hinstance_from_fnptr((void *)InitializePlugin, (void **)&hinst, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    g_mo = mo_parse_from_resource(hinst, &err);
    if (g_mo) {
      mo_set_default(g_mo);
    } else {
      gcmz_logf_warn(NULL, "%s", "%s", gettext("failed to load language resources, continuing without them."));
    }
  }

  {
    struct gcmz_lua_context *const lua = get_lua(&err);
    if (!lua) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmzdrops_create(&g_gcmzdrops, lua, version, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

cleanup:
  if (!g_gcmzdrops) {
    wchar_t title[128];
    wchar_t main_instruction[128];
    wchar_t content[512];
    ov_snprintf_wchar(title, sizeof(title) / sizeof(title[0]), L"%s", L"%s", gettext("GCMZDrops"));
    ov_snprintf_wchar(main_instruction,
                      sizeof(main_instruction) / sizeof(main_instruction[0]),
                      L"%s",
                      L"%s",
                      gettext("Failed to initialize GCMZDrops."));
    ov_snprintf_wchar(content,
                      sizeof(content) / sizeof(content[0]),
                      L"%s",
                      L"%s",
                      gettext("The plugin could not start correctly.\nGCMZDrops is unavailable at the moment."));
    gcmz_error_dialog(NULL, &err, title, main_instruction, content, TD_ERROR_ICON, TDCBF_OK_BUTTON);
    OV_ERROR_DESTROY(&err);
    return FALSE;
  }
  return TRUE;
}

void __declspec(dllexport) UninitializePlugin(void);
void __declspec(dllexport) UninitializePlugin(void) {
  gcmzdrops_destroy(&g_gcmzdrops);
  if (g_lua) {
    gcmz_lua_destroy(&g_lua);
  }
  if (g_mo) {
    mo_set_default(NULL);
    mo_free(&g_mo);
  }
}

static void config_menu_handler(HWND const hwnd, HINSTANCE const dll_hinst) {
  gcmzdrops_show_config_dialog(g_gcmzdrops, hwnd, dll_hinst);
}

static void project_load_handler(struct aviutl2_project_file *project) {
  gcmzdrops_on_project_load(g_gcmzdrops, project->get_project_file_path());
}

static void paste_from_clipboard_handler(struct aviutl2_edit_section *edit) {
  (void)edit;
  gcmzdrops_paste_from_clipboard(g_gcmzdrops);
}

void __declspec(dllexport) RegisterPlugin(struct aviutl2_host_app_table *host);
void __declspec(dllexport) RegisterPlugin(struct aviutl2_host_app_table *host) {
  static wchar_t information[64];
  ov_snprintf_wchar(
      information, sizeof(information) / sizeof(information[0]), L"%1$hs", L"GCMZDrops %1$s by oov", GCMZ_VERSION);
  host->set_plugin_information(information);

  static wchar_t layer_menu_name[64];
  ov_snprintf_wchar(layer_menu_name,
                    sizeof(layer_menu_name) / sizeof(layer_menu_name[0]),
                    L"%s",
                    L"%s",
                    gettext("[GCMZDrops] Paste from Clipboard"));
  host->register_layer_menu(layer_menu_name, paste_from_clipboard_handler);

  static wchar_t config_menu_name[64];
  ov_snprintf_wchar(config_menu_name,
                    sizeof(config_menu_name) / sizeof(config_menu_name[0]),
                    L"%s",
                    L"%s",
                    gettext("GCMZDrops Settings..."));
  host->register_config_menu(config_menu_name, config_menu_handler);

  host->register_project_load_handler(project_load_handler);

  gcmzdrops_register(g_gcmzdrops, host);
}

bool __declspec(dllexport) AddHandlerScript(char const *const name, char const *const script, size_t const script_len);
bool __declspec(dllexport) AddHandlerScript(char const *const name, char const *const script, size_t const script_len) {
  if (!name || !script) {
    return false;
  }

  struct ov_error err = {0};
  bool result = false;

  struct gcmz_lua_context *const lua = get_lua(&err);
  if (!lua) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  if (!gcmz_lua_add_handler_script(lua, name, script, script_len, &err)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (!result) {
    gcmz_logf_warn(&err, "%1$hs", gettext("failed to add handler script %1$hs"), name);
    OV_ERROR_DESTROY(&err);
  }
  return result;
}

bool __declspec(dllexport) AddHandlerScriptFile(wchar_t const *const filepath);
bool __declspec(dllexport) AddHandlerScriptFile(wchar_t const *const filepath) {
  if (!filepath) {
    return false;
  }

  struct ov_error err = {0};
  bool result = false;

  struct gcmz_lua_context *const lua = get_lua(&err);
  if (!lua) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }
  if (!gcmz_lua_add_handler_script_file(lua, filepath, &err)) {
    OV_ERROR_ADD_TRACE(&err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (!result) {
    gcmz_logf_warn(&err, "%1$ls", gettext("failed to add handler script file %1$ls"), filepath);
    OV_ERROR_DESTROY(&err);
  }
  return result;
}

static void error_output_hook(enum ov_error_severity severity, char const *str) {
  (void)severity;
  if (!str) {
    return;
  }
  wchar_t buf[1024];
  size_t const str_len = strlen(str);
  size_t pos = 0;
  while (pos < str_len) {
    size_t const remaining = str_len - pos;
    size_t bytes_read = 0;
    size_t const converted = ov_utf8_to_wchar(str + pos, remaining, buf, sizeof(buf) / sizeof(buf[0]) - 1, &bytes_read);
    if (converted == 0 || bytes_read == 0) {
      pos++;
      continue;
    }
    buf[converted] = L'\0';
    OutputDebugStringW(buf);
    pos += bytes_read;
  }
}

BOOL WINAPI DllMain(HINSTANCE const inst, DWORD const reason, LPVOID const reserved);
BOOL WINAPI DllMain(HINSTANCE const inst, DWORD const reason, LPVOID const reserved) {
  // trans: This dagger helps UTF-8 detection. You don't need to translate this.
  (void)gettext_noop("†");
  (void)reserved;
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(inst);
    ov_init();
    ov_error_set_output_hook(error_output_hook);
    return TRUE;
  case DLL_PROCESS_DETACH:
    ov_exit();
    return TRUE;
  }
  return TRUE;
}
