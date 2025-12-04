#include <lauxlib.h>
#include <lua.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  (void)hinstDLL;
  (void)fdwReason;
  (void)lpvReserved;
  return TRUE;
}

static int test_lua_hello(lua_State *L) {
  lua_pushstring(L, "Hello from 🌙 module!");
  return 1;
}

// Export name will be set via .def file to: luaopen_test_🌙
// Function name uses ASCII only due to C99 compatibility
int __declspec(dllexport) luaopen_test_lua_module(lua_State *L);
int __declspec(dllexport) luaopen_test_lua_module(lua_State *L) {
  lua_newtable(L);
  lua_pushcfunction(L, test_lua_hello);
  lua_setfield(L, -2, "🌙");
  return 1;
}
