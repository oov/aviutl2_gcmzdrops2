
// Plugin startup sequence:
// The exported functions are called by AviUtl ExEdit2 in the following order:
// 1. DllMain(DLL_PROCESS_ATTACH) - Standard Windows DLL entry point
// 2. InitializeLogger - Called to set up logging functionality
// 3. InitializePlugin - Called to initialize the plugin with AviUtl ExEdit2 version info
// 4. RegisterPlugin - Called to register callbacks and handlers with AviUtl ExEdit2

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovarray.h>
#include <ovbase.h>
#include <ovmo.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/os.h>
#include <ovl/path.h>

#include <aviutl2_logger2.h>
#include <aviutl2_module2.h>
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

/**
 * @brief Get UTF-8 module path from function address
 * @param fnptr Function address to get module path for
 * @param err [out] Error information on failure
 * @return UTF-8 module path on success, or NULL on failure (caller owns, free with OV_ARRAY_DESTROY)
 */
static char *get_caller_module_path(void *const fnptr, struct ov_error *const err) {
  if (!fnptr) {
    OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to get return address");
    return NULL;
  }

  void *hinst = NULL;
  wchar_t *module_path = NULL;
  char *module_path_utf8 = NULL;
  char *result = NULL;

  {
    if (!ovl_os_get_hinstance_from_fnptr(fnptr, &hinst, err)) {
      OV_ERROR_PUSH(err, ov_error_type_generic, ov_error_generic_fail, "failed to get caller module instance");
      goto cleanup;
    }
    if (!ovl_path_get_module_name(&module_path, hinst, err)) {
      OV_ERROR_PUSH(err, ov_error_type_generic, ov_error_generic_fail, "failed to get caller module path");
      goto cleanup;
    }
    size_t const len = ov_wchar_to_utf8_len(module_path, OV_ARRAY_LENGTH(module_path));
    if (!OV_ARRAY_GROW(&module_path_utf8, len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(module_path, OV_ARRAY_LENGTH(module_path), module_path_utf8, len + 1, NULL)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
  }

  result = module_path_utf8;
  module_path_utf8 = NULL;

cleanup:
  if (module_path_utf8) {
    OV_ARRAY_DESTROY(&module_path_utf8);
  }
  if (module_path) {
    OV_ARRAY_DESTROY(&module_path);
  }
  return result;
}

DWORD __declspec(dllexport) GetVersion(void);
DWORD __declspec(dllexport) GetVersion(void) { return GCMZ_VERSION_UINT32; }

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
      gcmz_logf_warn(NULL, "%s", "%s", "failed to load language resources, continuing without them.");
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
    ov_snprintf_wchar(title, sizeof(title) / sizeof(title[0]), L"%1$hs", L"%1$hs", gettext("GCMZDrops"));
    ov_snprintf_wchar(main_instruction,
                      sizeof(main_instruction) / sizeof(main_instruction[0]),
                      L"%1$hs",
                      L"%1$hs",
                      gettext("Failed to initialize GCMZDrops."));
    ov_snprintf_wchar(content,
                      sizeof(content) / sizeof(content[0]),
                      L"%1$hs",
                      L"%1$hs",
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
  gcmzdrops_on_project_load(g_gcmzdrops, project);
}

static void project_save_handler(struct aviutl2_project_file *project) {
  gcmzdrops_on_project_save(g_gcmzdrops, project);
}

static void paste_from_clipboard_handler(struct aviutl2_edit_section *edit) {
  gcmzdrops_paste_from_clipboard(g_gcmzdrops, edit);
}

void __declspec(dllexport) RegisterPlugin(struct aviutl2_host_app_table *host);
void __declspec(dllexport) RegisterPlugin(struct aviutl2_host_app_table *host) {
  static wchar_t information[64];
  ov_snprintf_wchar(
      information, sizeof(information) / sizeof(information[0]), L"%1$hs", L"GCMZDrops %1$hs by oov", GCMZ_VERSION);
  host->set_plugin_information(information);

  static wchar_t layer_menu_name[64];
  ov_snprintf_wchar(layer_menu_name,
                    sizeof(layer_menu_name) / sizeof(layer_menu_name[0]),
                    L"%1$hs",
                    L"%1$hs",
                    gettext("[GCMZDrops] Paste from Clipboard"));
  host->register_layer_menu(layer_menu_name, paste_from_clipboard_handler);

  static wchar_t config_menu_name[64];
  ov_snprintf_wchar(config_menu_name,
                    sizeof(config_menu_name) / sizeof(config_menu_name[0]),
                    L"%1$hs",
                    L"%1$hs",
                    gettext("GCMZDrops Settings..."));
  host->register_config_menu(config_menu_name, config_menu_handler);

  host->register_project_load_handler(project_load_handler);
  host->register_project_save_handler(project_save_handler);

  gcmzdrops_register(g_gcmzdrops, host);
}

bool __declspec(dllexport) AddHandlerScript(char const *const script, size_t const script_len);
bool __declspec(dllexport) AddHandlerScript(char const *const script, size_t const script_len) {
  if (!script) {
    return false;
  }

  struct ov_error err = {0};
  char *module_path_utf8 = NULL;
  bool result = false;

  {
    module_path_utf8 = get_caller_module_path(__builtin_return_address(0), &err);
    if (!module_path_utf8) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }
  {
    struct gcmz_lua_context *const lua = get_lua(&err);
    if (!lua) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_lua_add_handler_script(lua, script, script_len, module_path_utf8, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (!result) {
    gcmz_logf_warn(&err,
                   "%1$hs",
                   gettext("failed to add handler script from %1$hs"),
                   module_path_utf8 ? module_path_utf8 : "<unknown>");
    OV_ERROR_DESTROY(&err);
  }
  if (module_path_utf8) {
    OV_ARRAY_DESTROY(&module_path_utf8);
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

bool __declspec(dllexport) RegisterScriptModule(struct aviutl2_script_module_table *const table,
                                                char const *const module_name);
bool __declspec(dllexport) RegisterScriptModule(struct aviutl2_script_module_table *const table,
                                                char const *const module_name) {
  if (!table || !module_name) {
    return false;
  }

  struct ov_error err = {0};
  char *module_path_utf8 = NULL;
  bool result = false;

  {
    module_path_utf8 = get_caller_module_path(__builtin_return_address(0), &err);
    if (!module_path_utf8) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }
  {
    struct gcmz_lua_context *const lua = get_lua(&err);
    if (!lua) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!gcmz_lua_register_script_module(lua, table, module_name, module_path_utf8, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (!result) {
    gcmz_logf_warn(&err,
                   "%1$hs",
                   gettext("failed to register script module %1$hs from %2$hs"),
                   module_name,
                   module_path_utf8 ? module_path_utf8 : "<unknown>");
    OV_ERROR_DESTROY(&err);
  }
  if (module_path_utf8) {
    OV_ARRAY_DESTROY(&module_path_utf8);
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
