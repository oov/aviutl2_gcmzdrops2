#include <lauxlib.h>
#include <lua.h>

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

static wchar_t g_log_path[MAX_PATH] = {0};

static void write_log(char const *message) {
  if (g_log_path[0] == L'\0') {
    return;
  }

  FILE *f = _wfopen(g_log_path, L"a");
  if (f) {
    fprintf(f, "%s\n", message);
    fclose(f);
  }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  (void)lpvReserved;

  switch (fdwReason) {
  case DLL_PROCESS_ATTACH: {
    // Build log file path once during initialization
    wchar_t module_path[MAX_PATH];
    if (GetModuleFileNameW(hinstDLL, module_path, MAX_PATH)) {
      wchar_t *last_slash = wcsrchr(module_path, L'\\');
      if (last_slash) {
        size_t dir_len = (size_t)(last_slash - module_path + 1);
        wcsncpy(g_log_path, module_path, dir_len);
        g_log_path[dir_len] = L'\0';
        wcscat(g_log_path, L"test_cleanup.log");
      }
    }
    write_log("DLL_PROCESS_ATTACH");
    break;
  }
  case DLL_PROCESS_DETACH:
    write_log("DLL_PROCESS_DETACH");
    break;
  }
  return TRUE;
}

static int test_cleanup_hello(lua_State *L) {
  lua_pushstring(L, "Hello from test_cleanup module!");
  return 1;
}

int __declspec(dllexport) luaopen_test_cleanup(lua_State *L);
int __declspec(dllexport) luaopen_test_cleanup(lua_State *L) {
  write_log("luaopen_test_cleanup called");

  lua_newtable(L);
  lua_pushcfunction(L, test_cleanup_hello);
  lua_setfield(L, -2, "hello");

  return 1;
}
