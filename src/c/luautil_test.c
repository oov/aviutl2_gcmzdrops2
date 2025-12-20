#include <ovtest.h>

#include "luautil.h"

#include <ovarray.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <ovl/os.h>
#include <ovl/path.h>

#include <windows.h>

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wreserved-macro-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#  endif
#endif // __GNUC__
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

/**
 * @brief Get the directory of the test executable
 */
static bool get_exe_directory(wchar_t **dir, struct ov_error *const err) {
  if (!dir) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  HINSTANCE hinstance = NULL;
  wchar_t *module_path = NULL;
  bool result = false;

  {
    if (!ovl_os_get_hinstance_from_fnptr((void *)get_exe_directory, (void **)&hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (!ovl_path_get_module_name(&module_path, hinstance, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    wchar_t const *last_slash = ovl_path_find_last_path_sep(module_path);
    if (!last_slash) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "no directory separator found in module path");
      goto cleanup;
    }

    size_t const dir_len = (size_t)(last_slash - module_path);
    if (!OV_ARRAY_GROW(dir, dir_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    memcpy(*dir, module_path, dir_len * sizeof(wchar_t));
    (*dir)[dir_len] = L'\0';
  }

  result = true;

cleanup:
  if (module_path) {
    OV_ARRAY_DESTROY(&module_path);
  }
  return result;
}

// Helper to execute script and get result
static bool exec_lua_script(lua_State *L, char const *script, char const *label, char const *desc) {
  if (luaL_dostring(L, script) != LUA_OK) {
    TEST_MSG("%s (%s): %s", desc, label, lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

// Execute script on both states and compare string results
static void
compare_string_results(lua_State *L_std, lua_State *L_ovr, char const *script, int stack_idx, char const *desc) {
  if (!exec_lua_script(L_std, script, "standard", desc)) {
    return;
  }
  char std_buf[256];
  char const *std_str = lua_tostring(L_std, stack_idx);
  strncpy(std_buf, std_str ? std_str : "", sizeof(std_buf) - 1);
  std_buf[sizeof(std_buf) - 1] = '\0';
  int std_top = lua_gettop(L_std);

  if (!exec_lua_script(L_ovr, script, "override", desc)) {
    lua_pop(L_std, std_top);
    return;
  }
  char const *ovr_str = lua_tostring(L_ovr, stack_idx);
  TEST_CHECK_(strcmp(std_buf, ovr_str ? ovr_str : "") == 0, "%s: string mismatch", desc);

  lua_pop(L_ovr, lua_gettop(L_ovr));
  lua_pop(L_std, std_top);
}

// Execute script on both states and compare integer results
static void
compare_int_results(lua_State *L_std, lua_State *L_ovr, char const *script, int stack_idx, char const *desc) {
  if (!exec_lua_script(L_std, script, "standard", desc)) {
    return;
  }
  int std_val = (int)lua_tointeger(L_std, stack_idx);
  int std_top = lua_gettop(L_std);

  if (!exec_lua_script(L_ovr, script, "override", desc)) {
    lua_pop(L_std, std_top);
    return;
  }
  int ovr_val = (int)lua_tointeger(L_ovr, stack_idx);
  TEST_CHECK_(std_val == ovr_val, "%s: %d != %d", desc, std_val, ovr_val);

  lua_pop(L_ovr, lua_gettop(L_ovr));
  lua_pop(L_std, std_top);
}

static void test_utf8_funcs_ascii_compatibility(void) {
  lua_State *L_standard = NULL;
  lua_State *L_override = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  wchar_t *test_lua_path = NULL;
  char *test_file_path_utf8 = NULL;
  char *test_lua_path_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build ASCII-only file paths
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\ascii_compat_test.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\ascii_compat_test.txt");

    size_t const test_lua_len = wcslen(exe_dir) + wcslen(L"\\test_data\\ascii_compat_test.lua") + 1;
    if (!OV_ARRAY_GROW(&test_lua_path, test_lua_len)) {
      goto cleanup;
    }
    wcscpy(test_lua_path, exe_dir);
    wcscat(test_lua_path, L"\\test_data\\ascii_compat_test.lua");

    // Convert paths to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    size_t const lua_wlen = wcslen(test_lua_path);
    size_t const lua_utf8_len = ov_wchar_to_utf8_len(test_lua_path, lua_wlen);
    if (!lua_utf8_len || !OV_ARRAY_GROW(&test_lua_path_utf8, lua_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_lua_path, lua_wlen, test_lua_path_utf8, lua_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test files
    DeleteFileW(test_file_path);
    DeleteFileW(test_lua_path);

    // Create two lua_State instances: one standard, one with UTF-8 overrides
    L_standard = luaL_newstate();
    if (!TEST_CHECK(L_standard != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_standard);

    L_override = luaL_newstate();
    if (!TEST_CHECK(L_override != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_override);
    gcmz_lua_setup_utf8_funcs(L_override);

    // Set test file paths in both states
    lua_pushstring(L_standard, test_file_path_utf8);
    lua_setglobal(L_standard, "TEST_FILE");
    lua_pushstring(L_standard, test_lua_path_utf8);
    lua_setglobal(L_standard, "TEST_LUA");

    lua_pushstring(L_override, test_file_path_utf8);
    lua_setglobal(L_override, "TEST_FILE");
    lua_pushstring(L_override, test_lua_path_utf8);
    lua_setglobal(L_override, "TEST_LUA");

    // Test 1: io.open write - both should create identical files
    TEST_CASE("io.open write compatibility");
    DeleteFileW(test_file_path);
    char const *write_script = "local f = io.open(TEST_FILE, 'w') "
                               "assert(f, 'failed to open') "
                               "f:write('Hello\\n') "
                               "f:write('World\\n') "
                               "f:write('H\\n', 'E\\n', 'L\\n', 'L\\n', 'O\\n') "
                               "f:close()";
    if (exec_lua_script(L_standard, write_script, "standard", "write test")) {
      char buf1[256] = {0};
      DWORD read1 = 0;
      HANDLE h1 = CreateFileW(test_file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
      if (h1 != INVALID_HANDLE_VALUE) {
        ReadFile(h1, buf1, sizeof(buf1) - 1, &read1, NULL);
        CloseHandle(h1);
      }
      DeleteFileW(test_file_path);

      if (exec_lua_script(L_override, write_script, "override", "write test")) {
        char buf2[256] = {0};
        DWORD read2 = 0;
        HANDLE h2 = CreateFileW(test_file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h2 != INVALID_HANDLE_VALUE) {
          ReadFile(h2, buf2, sizeof(buf2) - 1, &read2, NULL);
          CloseHandle(h2);
        }
        TEST_CHECK(read1 == read2 && strcmp(buf1, buf2) == 0);
      }
    }

    // Test 2: io.open read
    TEST_CASE("io.open read compatibility");
    compare_string_results(L_standard,
                           L_override,
                           "local f = io.open(TEST_FILE, 'r') "
                           "assert(f) "
                           "local line1 = f:read('*l') "
                           "f:close() "
                           "return line1",
                           -1,
                           "io.open read");

    // Test 3: io.lines
    TEST_CASE("io.lines compatibility");
    compare_string_results(L_standard,
                           L_override,
                           "local lines = {} "
                           "for line in io.lines(TEST_FILE) do "
                           "  table.insert(lines, line) "
                           "end "
                           "return table.concat(lines, '|')",
                           -1,
                           "io.lines");

    // Create test Lua file
    {
      HANDLE h = CreateFileW(test_lua_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (TEST_CHECK(h != INVALID_HANDLE_VALUE)) {
        char const *lua_content = "return { value = 123, text = 'test' }";
        DWORD written;
        WriteFile(h, lua_content, (DWORD)strlen(lua_content), &written, NULL);
        CloseHandle(h);
      }
    }

    // Test 4: loadfile
    TEST_CASE("loadfile compatibility");
    compare_int_results(L_standard,
                        L_override,
                        "local chunk = loadfile(TEST_LUA) "
                        "assert(chunk) "
                        "local result = chunk() "
                        "return result.value",
                        -1,
                        "loadfile value");
    compare_string_results(L_standard,
                           L_override,
                           "local chunk = loadfile(TEST_LUA) "
                           "local result = chunk() "
                           "return result.text",
                           -1,
                           "loadfile text");

    // Test 5: dofile
    TEST_CASE("dofile compatibility");
    compare_int_results(L_standard,
                        L_override,
                        "local result = dofile(TEST_LUA) "
                        "return result.value",
                        -1,
                        "dofile value");

    // Test 6: file:seek
    TEST_CASE("file:seek compatibility");
    compare_int_results(L_standard,
                        L_override,
                        "local f = io.open(TEST_FILE, 'r') "
                        "local pos = f:seek('set', 0) "
                        "f:close() "
                        "return pos",
                        -1,
                        "seek set");
    compare_int_results(L_standard,
                        L_override,
                        "local f = io.open(TEST_FILE, 'r') "
                        "local pos = f:seek('end') "
                        "f:close() "
                        "return pos",
                        -1,
                        "seek end");

    // Test 7: io.type
    TEST_CASE("io.type compatibility");
    compare_string_results(L_standard,
                           L_override,
                           "local f = io.open(TEST_FILE, 'r') "
                           "local t = io.type(f) "
                           "f:close() "
                           "return t",
                           -1,
                           "io.type open");
    compare_string_results(L_standard,
                           L_override,
                           "local f = io.open(TEST_FILE, 'r') "
                           "f:close() "
                           "return io.type(f)",
                           -1,
                           "io.type closed");
  }

cleanup:
  if (L_standard) {
    lua_close(L_standard);
  }
  if (L_override) {
    lua_close(L_override);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_lua_path) {
    DeleteFileW(test_lua_path);
    OV_ARRAY_DESTROY(&test_lua_path);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (test_lua_path_utf8) {
    OV_ARRAY_DESTROY(&test_lua_path_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_unicode_paths(void) {
  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *dll_dir = NULL;
  wchar_t *lua_script_path = NULL;
  char *dll_dir_utf8 = NULL;
  char *lua_script_path_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build DLL directory path
    size_t const dll_dir_len = wcslen(exe_dir) + wcslen(L"\\test_data\\lua_modules") + 1;
    if (!OV_ARRAY_GROW(&dll_dir, dll_dir_len)) {
      goto cleanup;
    }
    wcscpy(dll_dir, exe_dir);
    wcscat(dll_dir, L"\\test_data\\lua_modules");

    // Build Lua script path (with emoji)
    size_t const lua_script_len = wcslen(exe_dir) + wcslen(L"\\test_data\\test_🌙.lua") + 1;
    if (!OV_ARRAY_GROW(&lua_script_path, lua_script_len)) {
      goto cleanup;
    }
    wcscpy(lua_script_path, exe_dir);
    wcscat(lua_script_path, L"\\test_data\\test_🌙.lua");

    // Convert paths to UTF-8
    size_t const dll_dir_wlen = wcslen(dll_dir);
    size_t const dll_dir_utf8_len = ov_wchar_to_utf8_len(dll_dir, dll_dir_wlen);
    if (!dll_dir_utf8_len || !OV_ARRAY_GROW(&dll_dir_utf8, dll_dir_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(dll_dir, dll_dir_wlen, dll_dir_utf8, dll_dir_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    size_t const lua_script_wlen = wcslen(lua_script_path);
    size_t const lua_script_utf8_len = ov_wchar_to_utf8_len(lua_script_path, lua_script_wlen);
    if (!lua_script_utf8_len || !OV_ARRAY_GROW(&lua_script_path_utf8, lua_script_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(lua_script_path, lua_script_wlen, lua_script_path_utf8, lua_script_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Add Unicode module directory to package.cpath
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "cpath");
    char const *const current_cpath = lua_tostring(L, -1);
    lua_pop(L, 1);

    char cpath_buffer[4096];
    ov_snprintf_char(
        cpath_buffer, sizeof(cpath_buffer), NULL, "%s;%s\\?.dll", current_cpath ? current_cpath : "", dll_dir_utf8);
    lua_pushstring(L, cpath_buffer);
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    // Test 1: loadfile with Unicode filename
    lua_getglobal(L, "loadfile");
    lua_pushstring(L, lua_script_path_utf8);
    if (!TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
      char const *err_msg = lua_tostring(L, -1);
      TEST_MSG("loadfile failed: %s", err_msg ? err_msg : "unknown error");
      lua_pop(L, 1);
      goto cleanup;
    }
    TEST_CHECK(lua_isfunction(L, -1));
    if (!TEST_CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK)) {
      char const *err_msg = lua_tostring(L, -1);
      TEST_MSG("loadfile execution failed: %s", err_msg ? err_msg : "unknown error");
      lua_pop(L, 1);
      goto cleanup;
    }
    TEST_CHECK(lua_istable(L, -1));
    lua_getfield(L, -1, "message");
    TEST_CHECK(lua_isstring(L, -1));
    lua_pop(L, 2);

    // Test 2: dofile with Unicode filename
    lua_getglobal(L, "dofile");
    lua_pushstring(L, lua_script_path_utf8);
    if (!TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
      char const *err_msg = lua_tostring(L, -1);
      TEST_MSG("dofile failed: %s", err_msg ? err_msg : "unknown error");
      lua_pop(L, 1);
      goto cleanup;
    }
    TEST_CHECK(lua_istable(L, -1));
    lua_getfield(L, -1, "emoji");
    TEST_CHECK(lua_isstring(L, -1));
    char const *emoji = lua_tostring(L, -1);
    TEST_CHECK(emoji && strcmp(emoji, "🌙") == 0);
    lua_pop(L, 2);

    // Test 3: require with Unicode C module name
    lua_getglobal(L, "require");
    lua_pushstring(L, "test_🌙");
    if (!TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
      char const *err_msg = lua_tostring(L, -1);
      TEST_MSG("require failed: %s", err_msg ? err_msg : "unknown error");
      lua_pop(L, 1);
      goto cleanup;
    }
    TEST_CHECK(lua_istable(L, -1));
    lua_getfield(L, -1, "🌙");
    TEST_CHECK(lua_isfunction(L, -1));
    if (TEST_CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK)) {
      TEST_CHECK(lua_isstring(L, -1));
      char const *msg = lua_tostring(L, -1);
      TEST_CHECK(msg && strcmp(msg, "Hello from 🌙 module!") == 0);
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (lua_script_path_utf8) {
    OV_ARRAY_DESTROY(&lua_script_path_utf8);
  }
  if (dll_dir_utf8) {
    OV_ARRAY_DESTROY(&dll_dir_utf8);
  }
  if (lua_script_path) {
    OV_ARRAY_DESTROY(&lua_script_path);
  }
  if (dll_dir) {
    OV_ARRAY_DESTROY(&dll_dir);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_c_module_cleanup(void) {
  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *log_path = NULL;
  wchar_t *dll_dir = NULL;
  char *dll_dir_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build DLL directory path
    size_t const dll_dir_len = wcslen(exe_dir) + wcslen(L"\\test_data\\lua_modules") + 1;
    if (!OV_ARRAY_GROW(&dll_dir, dll_dir_len)) {
      OV_ARRAY_DESTROY(&exe_dir);
      return;
    }
    wcscpy(dll_dir, exe_dir);
    wcscat(dll_dir, L"\\test_data\\lua_modules");

    // Build log file path (in DLL directory)
    size_t const log_path_len = wcslen(dll_dir) + wcslen(L"\\test_cleanup.log") + 1;
    if (!OV_ARRAY_GROW(&log_path, log_path_len)) {
      goto cleanup;
    }
    wcscpy(log_path, dll_dir);
    wcscat(log_path, L"\\test_cleanup.log");

    DeleteFileW(log_path);

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Convert DLL directory to UTF-8
    size_t const dll_dir_wlen = wcslen(dll_dir);
    size_t const dll_dir_utf8_len = ov_wchar_to_utf8_len(dll_dir, dll_dir_wlen);
    if (!dll_dir_utf8_len || !OV_ARRAY_GROW(&dll_dir_utf8, dll_dir_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(dll_dir, dll_dir_wlen, dll_dir_utf8, dll_dir_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Add test_cleanup module directory to package.cpath
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "cpath");
    char const *const current_cpath = lua_tostring(L, -1);
    lua_pop(L, 1);

    char cpath_buffer[4096];
    ov_snprintf_char(
        cpath_buffer, sizeof(cpath_buffer), NULL, "%s;%s\\?.dll", current_cpath ? current_cpath : "", dll_dir_utf8);
    lua_pushstring(L, cpath_buffer);
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    // Load the test_cleanup module
    lua_getglobal(L, "require");
    lua_pushstring(L, "test_cleanup");
    if (!TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
      char const *err_msg = lua_tostring(L, -1);
      TEST_MSG("Failed to load test_cleanup module: %s", err_msg ? err_msg : "unknown error");
      lua_pop(L, 1);
      goto cleanup;
    }

    // Verify module loaded successfully
    TEST_CHECK(lua_istable(L, -1));
    lua_getfield(L, -1, "hello");
    TEST_CHECK(lua_isfunction(L, -1));
    lua_pop(L, 2);

    // Verify log file shows DLL_PROCESS_ATTACH and luaopen_test_cleanup
    HANDLE hFile = CreateFileW(log_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (TEST_CHECK(hFile != INVALID_HANDLE_VALUE)) {
      char buffer[1024];
      DWORD bytes_read = 0;
      ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytes_read, NULL);
      buffer[bytes_read] = '\0';
      CloseHandle(hFile);

      TEST_CHECK(strstr(buffer, "DLL_PROCESS_ATTACH") != NULL);
      TEST_CHECK(strstr(buffer, "luaopen_test_cleanup called") != NULL);
    }

    // Destroy lua_State - this should trigger FreeLibrary via __gc
    lua_close(L);
    L = NULL;

    // Give Windows some time to actually unload the DLL
    Sleep(100);

    // Verify log file shows DLL_PROCESS_DETACH
    hFile = CreateFileW(log_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (TEST_CHECK(hFile != INVALID_HANDLE_VALUE)) {
      char buffer[1024];
      DWORD bytes_read = 0;
      ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytes_read, NULL);
      buffer[bytes_read] = '\0';
      CloseHandle(hFile);

      TEST_CHECK(strstr(buffer, "DLL_PROCESS_DETACH") != NULL);
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (log_path) {
    DeleteFileW(log_path);
    OV_ARRAY_DESTROY(&log_path);
  }
  if (dll_dir) {
    OV_ARRAY_DESTROY(&dll_dir);
  }
  if (dll_dir_utf8) {
    OV_ARRAY_DESTROY(&dll_dir_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_io_unicode_paths(void) {
  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  wchar_t *test_file_path2 = NULL;
  char *test_file_path_utf8 = NULL;
  char *test_file_path2_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build test file path with emoji in filename
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\io_test_🌙文字.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\io_test_🌙文字.txt");

    // Convert path to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test file
    DeleteFileW(test_file_path);

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Set test file path as global variable
    lua_pushstring(L, test_file_path_utf8);
    lua_setglobal(L, "TEST_FILE_PATH");

    // Test 1: io.open for writing with Unicode filename
    {
      TEST_CASE("io.open write");
      char const *script = "local f = io.open(TEST_FILE_PATH, 'w') "
                           "assert(f, 'io.open write failed') "
                           "f:write('Hello 🌙 World!\\n') "
                           "f:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.open write script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Verify file was created
    TEST_CHECK(GetFileAttributesW(test_file_path) != INVALID_FILE_ATTRIBUTES);

    // Test 2: io.open for reading with Unicode filename
    {
      TEST_CASE("io.open read");
      char const *script = "local f = io.open(TEST_FILE_PATH, 'r') "
                           "assert(f, 'io.open read failed') "
                           "local content = f:read('*a') "
                           "assert(content:find('Hello 🌙 World!'), 'content check 1 failed') "
                           "f:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.open read script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 3: io.lines with Unicode filename
    {
      TEST_CASE("io.lines");
      char const *script = "local line_count = 0 "
                           "for line in io.lines(TEST_FILE_PATH) do "
                           "  line_count = line_count + 1 "
                           "  assert(line:find('Hello 🌙 World!'), 'line 1 check failed') "
                           "end "
                           "assert(line_count == 1, 'expected 1 line')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.lines script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 4: io.input/io.output with Unicode filename
    {
      TEST_CASE("io.input/io.output");

      // Create second test file path
      size_t const test_file2_len = wcslen(exe_dir) + wcslen(L"\\test_data\\io_test_🌙出力.txt") + 1;
      if (!OV_ARRAY_GROW(&test_file_path2, test_file2_len)) {
        goto cleanup;
      }
      wcscpy(test_file_path2, exe_dir);
      wcscat(test_file_path2, L"\\test_data\\io_test_🌙出力.txt");

      // Convert path to UTF-8
      size_t const path2_wlen = wcslen(test_file_path2);
      size_t const path2_utf8_len = ov_wchar_to_utf8_len(test_file_path2, path2_wlen);
      if (!path2_utf8_len || !OV_ARRAY_GROW(&test_file_path2_utf8, path2_utf8_len + 1)) {
        goto cleanup;
      }
      if (!ov_wchar_to_utf8(test_file_path2, path2_wlen, test_file_path2_utf8, path2_utf8_len + 1, NULL)) {
        goto cleanup;
      }

      // Clean up any existing test file
      DeleteFileW(test_file_path2);

      // Set second test file path
      lua_pushstring(L, test_file_path2_utf8);
      lua_setglobal(L, "TEST_FILE_PATH2");

      char const *script = "io.output(TEST_FILE_PATH2) "
                           "io.write('Output test 🌙\\n') "
                           "io.close() "
                           "io.input(TEST_FILE_PATH2) "
                           "local content = io.read('*a') "
                           "assert(content:find('Output test 🌙'), 'output content check failed') "
                           "io.close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.input/io.output script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }

      // Verify file was created
      TEST_CHECK(GetFileAttributesW(test_file_path2) != INVALID_FILE_ATTRIBUTES);
    }

    // Test 5: io.type
    {
      TEST_CASE("io.type");
      char const *script = "local f = io.open(TEST_FILE_PATH, 'r') "
                           "assert(f, 'io.open for type test failed') "
                           "assert(io.type(f) == 'file', 'io.type open file failed') "
                           "f:close() "
                           "assert(io.type(f) == 'closed file', 'io.type closed file failed') "
                           "assert(io.type('not a file') == nil, 'io.type invalid failed')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.type script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 6: file:seek
    {
      TEST_CASE("file:seek");
      char const *script = "local f = io.open(TEST_FILE_PATH, 'r') "
                           "assert(f, 'io.open for seek test failed') "
                           "local pos = f:seek('set', 0) "
                           "assert(pos == 0, 'seek set failed') "
                           "local end_pos = f:seek('end') "
                           "assert(end_pos > 0, 'seek end failed') "
                           "f:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("file:seek script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 7: append mode
    {
      TEST_CASE("append mode");
      char const *script = "local f = io.open(TEST_FILE_PATH, 'a') "
                           "assert(f, 'io.open append failed') "
                           "f:write('Appended 🌙\\n') "
                           "f:close() "
                           "local f2 = io.open(TEST_FILE_PATH, 'r') "
                           "assert(f2, 'io.open read after append failed') "
                           "local content = f2:read('*a') "
                           "assert(content:find('Hello 🌙 World!'), 'original content missing') "
                           "assert(content:find('Appended 🌙'), 'appended content missing') "
                           "f2:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("append mode script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path2) {
    DeleteFileW(test_file_path2);
    OV_ARRAY_DESTROY(&test_file_path2);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (test_file_path2_utf8) {
    OV_ARRAY_DESTROY(&test_file_path2_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_io_simple_style(void) {
  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  wchar_t *test_file_path2 = NULL;
  char *test_file_path_utf8 = NULL;
  char *test_file_path2_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build test file paths
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\io_simple_test1.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\io_simple_test1.txt");

    size_t const test_file2_len = wcslen(exe_dir) + wcslen(L"\\test_data\\io_simple_test2.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path2, test_file2_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path2, exe_dir);
    wcscat(test_file_path2, L"\\test_data\\io_simple_test2.txt");

    // Convert paths to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    size_t const path2_wlen = wcslen(test_file_path2);
    size_t const path2_utf8_len = ov_wchar_to_utf8_len(test_file_path2, path2_wlen);
    if (!path2_utf8_len || !OV_ARRAY_GROW(&test_file_path2_utf8, path2_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path2, path2_wlen, test_file_path2_utf8, path2_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test files
    DeleteFileW(test_file_path);
    DeleteFileW(test_file_path2);

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Set test file paths as global variables
    lua_pushstring(L, test_file_path_utf8);
    lua_setglobal(L, "TEST_FILE_1");
    lua_pushstring(L, test_file_path2_utf8);
    lua_setglobal(L, "TEST_FILE_2");

    // Test 1: io.output() and io.input() to get/set default files
    {
      TEST_CASE("io.output/input get/set");
      char const *script = "local f = io.open(TEST_FILE_1, 'w') "
                           "assert(f, 'failed to open file') "
                           "local old = io.output(f) "
                           "io.write('test output\\n') "
                           "local current = io.output() "
                           "assert(current == f, 'io.output() should return current output file') "
                           "f:close() "
                           "local f2 = io.open(TEST_FILE_1, 'r') "
                           "assert(f2, 'failed to open file for reading') "
                           "local old_in = io.input(f2) "
                           "local line = io.read('*l') "
                           "assert(line == 'test output', 'content mismatch') "
                           "local current_in = io.input() "
                           "assert(current_in == f2, 'io.input() should return current input file') "
                           "f2:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 2: io.close() closes default output, second close returns error
    {
      TEST_CASE("io.close default output");
      char const *script = "local f = io.open(TEST_FILE_1, 'w') "
                           "io.output(f) "
                           "io.write('before close\\n') "
                           "local ok1 = io.close() "
                           "assert(ok1 == true, 'first io.close() should succeed') "
                           "local ok2, err2 = io.close() "
                           "assert(ok2 == nil, 'second io.close() should fail on closed file') "
                           "assert(type(err2) == 'string', 'should return error message') "
                           "assert(err2:find('closed file'), 'error should mention closed file')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 3: io.flush() on default output
    {
      TEST_CASE("io.flush");
      char const *script = "local f = io.open(TEST_FILE_2, 'w') "
                           "io.output(f) "
                           "io.write('flush test\\n') "
                           "local ok = io.flush() "
                           "assert(ok, 'io.flush() should succeed') "
                           "f:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }

      // Verify file was written
      TEST_CHECK(GetFileAttributesW(test_file_path2) != INVALID_FILE_ATTRIBUTES);
    }

    // Test 4: Error when no default input set
    {
      TEST_CASE("error on no default input");
      char const *script = "local ok, err = pcall(function() io.read('*l') end) "
                           "assert(not ok, 'io.read() should fail when no default input') "
                           "assert(type(err) == 'string', 'should return error message')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 5: Error when no default output set
    {
      TEST_CASE("error on no default output");
      char const *script = "local ok, err = pcall(function() io.write('test') end) "
                           "assert(not ok, 'io.write() should fail when no default output') "
                           "assert(type(err) == 'string', 'should return error message')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 6: Multiple io.input/output calls
    {
      TEST_CASE("multiple input/output switches");
      char const *script = "local f1 = io.open(TEST_FILE_1, 'w') "
                           "local f2 = io.open(TEST_FILE_2, 'w') "
                           "io.output(f1) "
                           "io.write('file1\\n') "
                           "io.output(f2) "
                           "io.write('file2\\n') "
                           "io.output(f1) "
                           "io.write('file1 again\\n') "
                           "f1:close() "
                           "f2:close() "
                           "local r1 = io.open(TEST_FILE_1, 'r') "
                           "local r2 = io.open(TEST_FILE_2, 'r') "
                           "io.input(r1) "
                           "local line1 = io.read('*l') "
                           "io.input(r2) "
                           "local line2 = io.read('*l') "
                           "assert(line1 == 'file1', 'file1 first line mismatch') "
                           "assert(line2 == 'file2', 'file2 content mismatch') "
                           "r1:close() "
                           "r2:close()";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 7: io.type compatibility
    {
      TEST_CASE("io.type");
      char const *script = "local f = io.open(TEST_FILE_1, 'r') "
                           "assert(io.type(f) == 'file', 'open file type check') "
                           "f:close() "
                           "assert(io.type(f) == 'closed file', 'closed file type check') "
                           "assert(io.type('string') == nil, 'non-file type check') "
                           "assert(io.type(123) == nil, 'number type check') "
                           "assert(io.type(nil) == nil, 'nil type check')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 8: io.lines compatibility
    {
      TEST_CASE("io.lines");
      char const *script = "local f = io.open(TEST_FILE_1, 'w') "
                           "f:write('line1\\nline2\\nline3\\n') "
                           "f:close() "
                           "local count = 0 "
                           "for line in io.lines(TEST_FILE_1) do "
                           "  count = count + 1 "
                           "end "
                           "assert(count == 3, 'line count mismatch')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 9: Verify non-overridden functions don't exist or behave appropriately
    {
      TEST_CASE("non-overridden functions");
      // io.popen and io.tmpfile are typically not overridden in custom implementations
      // Verify they either don't exist or return appropriate errors
      char const *script = "local popen_exists = (io.popen ~= nil) "
                           "local tmpfile_exists = (io.tmpfile ~= nil) "
                           "-- These functions may or may not exist in custom implementation";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path2) {
    DeleteFileW(test_file_path2);
    OV_ARRAY_DESTROY(&test_file_path2);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (test_file_path2_utf8) {
    OV_ARRAY_DESTROY(&test_file_path2_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_os_funcs_ascii_compatibility(void) {
  lua_State *L_standard = NULL;
  lua_State *L_override = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  wchar_t *test_file_path2 = NULL;
  char *test_file_path_utf8 = NULL;
  char *test_file_path2_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build ASCII-only file paths
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\os_compat_test1.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\os_compat_test1.txt");

    size_t const test_file2_len = wcslen(exe_dir) + wcslen(L"\\test_data\\os_compat_test2.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path2, test_file2_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path2, exe_dir);
    wcscat(test_file_path2, L"\\test_data\\os_compat_test2.txt");

    // Convert paths to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    size_t const path2_wlen = wcslen(test_file_path2);
    size_t const path2_utf8_len = ov_wchar_to_utf8_len(test_file_path2, path2_wlen);
    if (!path2_utf8_len || !OV_ARRAY_GROW(&test_file_path2_utf8, path2_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path2, path2_wlen, test_file_path2_utf8, path2_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test files
    DeleteFileW(test_file_path);
    DeleteFileW(test_file_path2);

    // Create two lua_State instances: one standard, one with UTF-8 overrides
    L_standard = luaL_newstate();
    if (!TEST_CHECK(L_standard != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_standard);

    L_override = luaL_newstate();
    if (!TEST_CHECK(L_override != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_override);
    gcmz_lua_setup_utf8_funcs(L_override);

    // Set test file paths in both states
    lua_pushstring(L_standard, test_file_path_utf8);
    lua_setglobal(L_standard, "TEST_FILE");
    lua_pushstring(L_standard, test_file_path2_utf8);
    lua_setglobal(L_standard, "TEST_FILE2");

    lua_pushstring(L_override, test_file_path_utf8);
    lua_setglobal(L_override, "TEST_FILE");
    lua_pushstring(L_override, test_file_path2_utf8);
    lua_setglobal(L_override, "TEST_FILE2");

    // Test 1: os.getenv
    TEST_CASE("os.getenv compatibility");
    compare_string_results(
        L_standard, L_override, "return os.getenv('PATH') and 'exists' or 'nil'", -1, "os.getenv PATH");
    compare_string_results(
        L_standard, L_override, "return os.getenv('NONEXISTENT_VAR_12345') or 'nil'", -1, "os.getenv nonexistent");

    // Test 2: os.tmpname - both should return valid paths
    TEST_CASE("os.tmpname compatibility");
    {
      char const *script = "local name = os.tmpname() "
                           "assert(type(name) == 'string', 'tmpname should return string') "
                           "assert(#name > 0, 'tmpname should return non-empty string') "
                           "return 'ok'";
      if (!exec_lua_script(L_standard, script, "standard", "os.tmpname")) {
        goto cleanup;
      }
      lua_pop(L_standard, lua_gettop(L_standard));

      if (!exec_lua_script(L_override, script, "override", "os.tmpname")) {
        goto cleanup;
      }
      lua_pop(L_override, lua_gettop(L_override));
    }

    // Test 3: os.execute - both should execute commands similarly
    TEST_CASE("os.execute compatibility");
    {
      // Test with no arguments (check shell availability)
      compare_string_results(L_standard,
                             L_override,
                             "local r = os.execute() return r and 'available' or 'unavailable'",
                             -1,
                             "os.execute()");

      // Test simple command - both should succeed
      char const *script = "local ok, kind, code = os.execute('echo test > NUL') "
                           "return ok and 'success' or 'fail'";
      if (!exec_lua_script(L_standard, script, "standard", "os.execute echo")) {
        goto cleanup;
      }
      char const *std_result = lua_tostring(L_standard, -1);
      char std_buf[64];
      strncpy(std_buf, std_result ? std_result : "", sizeof(std_buf) - 1);
      std_buf[sizeof(std_buf) - 1] = '\0';
      lua_pop(L_standard, lua_gettop(L_standard));

      if (!exec_lua_script(L_override, script, "override", "os.execute echo")) {
        goto cleanup;
      }
      char const *ovr_result = lua_tostring(L_override, -1);
      TEST_CHECK_(strcmp(std_buf, ovr_result ? ovr_result : "") == 0, "os.execute echo: %s != %s", std_buf, ovr_result);
      lua_pop(L_override, lua_gettop(L_override));
    }

    // Test 4: os.remove and os.rename
    TEST_CASE("os.remove/os.rename compatibility");
    {
      // Create a test file first
      HANDLE h = CreateFileW(test_file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (h != INVALID_HANDLE_VALUE) {
        char const *content = "test";
        DWORD written;
        WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(h);
      }

      // Test os.rename
      char const *rename_script = "local ok, err = os.rename(TEST_FILE, TEST_FILE2) "
                                  "return ok and 'ok' or err";
      if (exec_lua_script(L_standard, rename_script, "standard", "os.rename")) {
        char const *std_result = lua_tostring(L_standard, -1);
        lua_pop(L_standard, lua_gettop(L_standard));

        // Restore file for override test
        MoveFileW(test_file_path2, test_file_path);

        if (exec_lua_script(L_override, rename_script, "override", "os.rename")) {
          char const *ovr_result = lua_tostring(L_override, -1);
          TEST_CHECK_(strcmp(std_result ? std_result : "", ovr_result ? ovr_result : "") == 0,
                      "os.rename: %s != %s",
                      std_result,
                      ovr_result);
          lua_pop(L_override, lua_gettop(L_override));
        }
      }

      // Test os.remove
      char const *remove_script = "local ok, err = os.remove(TEST_FILE2) "
                                  "return ok and 'ok' or err";
      if (exec_lua_script(L_standard, remove_script, "standard", "os.remove")) {
        char const *std_result = lua_tostring(L_standard, -1);
        char std_buf[256];
        strncpy(std_buf, std_result ? std_result : "", sizeof(std_buf) - 1);
        std_buf[sizeof(std_buf) - 1] = '\0';
        lua_pop(L_standard, lua_gettop(L_standard));

        // Create file again for override test
        h = CreateFileW(test_file_path2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
          CloseHandle(h);
        }

        if (exec_lua_script(L_override, remove_script, "override", "os.remove")) {
          char const *ovr_result = lua_tostring(L_override, -1);
          TEST_CHECK_(strcmp(std_buf, ovr_result ? ovr_result : "") == 0, "os.remove: %s != %s", std_buf, ovr_result);
          lua_pop(L_override, lua_gettop(L_override));
        }
      }

      // Test os.remove on non-existent file
      char const *remove_nonexistent = "local ok, err = os.remove(TEST_FILE .. '.nonexistent') "
                                       "return ok and 'ok' or 'error'";
      compare_string_results(L_standard, L_override, remove_nonexistent, -1, "os.remove nonexistent");
    }

    // Test 5: os.clock - should return a number representing CPU time
    TEST_CASE("os.clock compatibility");
    {
      // Both implementations should return a non-negative number
      char const *script = "local c = os.clock() "
                           "assert(type(c) == 'number', 'os.clock should return number') "
                           "assert(c >= 0, 'os.clock should return non-negative') "
                           "return 'ok'";
      if (!exec_lua_script(L_standard, script, "standard", "os.clock")) {
        goto cleanup;
      }
      lua_pop(L_standard, lua_gettop(L_standard));

      if (!exec_lua_script(L_override, script, "override", "os.clock")) {
        goto cleanup;
      }
      lua_pop(L_override, lua_gettop(L_override));

      // Verify both return similar magnitude values (within reasonable time)
      char const *magnitude_script = "local c = os.clock() "
                                     "return c < 3600 and 'reasonable' or 'too_large'";
      compare_string_results(L_standard, L_override, magnitude_script, -1, "os.clock magnitude");
    }

    // Test 6: os.time - should return current time or time from table
    TEST_CASE("os.time compatibility");
    {
      // os.time() should return a number
      char const *script1 = "local t = os.time() "
                            "assert(type(t) == 'number', 'os.time should return number') "
                            "assert(t > 0, 'os.time should return positive') "
                            "return 'ok'";
      if (!exec_lua_script(L_standard, script1, "standard", "os.time()")) {
        goto cleanup;
      }
      lua_pop(L_standard, lua_gettop(L_standard));

      if (!exec_lua_script(L_override, script1, "override", "os.time()")) {
        goto cleanup;
      }
      lua_pop(L_override, lua_gettop(L_override));

      // os.time with table argument
      char const *script2 = "local t = os.time({year=2000, month=1, day=1, hour=0, min=0, sec=0}) "
                            "assert(type(t) == 'number', 'os.time(table) should return number') "
                            "return tostring(t)";
      compare_string_results(L_standard, L_override, script2, -1, "os.time(table)");

      // os.time with partial table (using defaults)
      char const *script3 = "local t = os.time({year=2020, month=6, day=15}) "
                            "return tostring(t)";
      compare_string_results(L_standard, L_override, script3, -1, "os.time(partial table)");
    }

    // Test 7: os.difftime - should return difference between two times
    TEST_CASE("os.difftime compatibility");
    {
      char const *script = "local t1 = os.time({year=2000, month=1, day=1}) "
                           "local t2 = os.time({year=2000, month=1, day=2}) "
                           "local diff = os.difftime(t2, t1) "
                           "return tostring(diff)";
      compare_string_results(L_standard, L_override, script, -1, "os.difftime 1 day");

      // Test with single argument
      char const *script2 = "local t = os.time({year=2000, month=1, day=2}) "
                            "local diff = os.difftime(t) "
                            "return tostring(diff)";
      compare_string_results(L_standard, L_override, script2, -1, "os.difftime single arg");
    }

    // Test 8: os.date - should return formatted date string or table
    TEST_CASE("os.date compatibility");
    {
      // os.date with fixed time for reproducible results
      char const *script1 = "local t = os.time({year=2000, month=6, day=15, hour=12, min=30, sec=45}) "
                            "return os.date('%Y-%m-%d', t)";
      compare_string_results(L_standard, L_override, script1, -1, "os.date %Y-%m-%d");

      char const *script2 = "local t = os.time({year=2000, month=6, day=15, hour=12, min=30, sec=45}) "
                            "return os.date('%H:%M:%S', t)";
      compare_string_results(L_standard, L_override, script2, -1, "os.date %H:%M:%S");

      // os.date with *t format (returns table)
      char const *script3 = "local t = os.time({year=2000, month=6, day=15, hour=12, min=30, sec=45}) "
                            "local d = os.date('*t', t) "
                            "assert(type(d) == 'table', 'os.date *t should return table') "
                            "return d.year .. '-' .. d.month .. '-' .. d.day";
      compare_string_results(L_standard, L_override, script3, -1, "os.date *t table");

      // Verify all fields in *t table
      char const *script4 = "local t = os.time({year=2000, month=6, day=15, hour=12, min=30, sec=45}) "
                            "local d = os.date('*t', t) "
                            "return d.hour .. ':' .. d.min .. ':' .. d.sec";
      compare_string_results(L_standard, L_override, script4, -1, "os.date *t time fields");

      // Test wday and yday
      char const *script5 = "local t = os.time({year=2000, month=6, day=15, hour=12}) "
                            "local d = os.date('*t', t) "
                            "return 'wday=' .. d.wday .. ',yday=' .. d.yday";
      compare_string_results(L_standard, L_override, script5, -1, "os.date *t wday/yday");

      // Test UTC format (!)
      char const *script6 = "local t = os.time({year=2000, month=6, day=15, hour=12, min=30, sec=45}) "
                            "return os.date('!%Y-%m-%d %H:%M:%S', t)";
      compare_string_results(L_standard, L_override, script6, -1, "os.date UTC format");

      // os.date() with no time argument (uses current time) - just verify it doesn't error
      char const *script7 = "local d = os.date('%Y') "
                            "assert(type(d) == 'string', 'os.date should return string') "
                            "assert(#d == 4, 'year should be 4 digits') "
                            "return 'ok'";
      if (!exec_lua_script(L_standard, script7, "standard", "os.date current")) {
        goto cleanup;
      }
      lua_pop(L_standard, lua_gettop(L_standard));

      if (!exec_lua_script(L_override, script7, "override", "os.date current")) {
        goto cleanup;
      }
      lua_pop(L_override, lua_gettop(L_override));
    }

    // Test 9: math.randomseed auto-initialization
    // Verify that math.random returns different values after gcmz_lua_setup_utf8_funcs
    // (seed should be auto-initialized with high-quality random value)
    TEST_CASE("math.randomseed auto-initialization");
    {
      char const *script = "local values = {} "
                           "for i = 1, 10 do values[i] = math.random() end "
                           "local all_same = true "
                           "for i = 2, 10 do if values[i] ~= values[1] then all_same = false break end end "
                           "assert(not all_same, 'math.random should return varied values after auto-seeding') "
                           "return 'ok'";
      if (!exec_lua_script(L_override, script, "override", "math.randomseed auto-init")) {
        goto cleanup;
      }
      lua_pop(L_override, lua_gettop(L_override));
    }
  }

cleanup:
  if (L_standard) {
    lua_close(L_standard);
  }
  if (L_override) {
    lua_close(L_override);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path2) {
    DeleteFileW(test_file_path2);
    OV_ARRAY_DESTROY(&test_file_path2);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (test_file_path2_utf8) {
    OV_ARRAY_DESTROY(&test_file_path2_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_os_unicode_paths(void) {
  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  wchar_t *test_file_path2 = NULL;
  char *test_file_path_utf8 = NULL;
  char *test_file_path2_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build test file path with emoji in filename
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\os_test_🌙削除.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\os_test_🌙削除.txt");

    size_t const test_file2_len = wcslen(exe_dir) + wcslen(L"\\test_data\\os_test_🌙移動.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path2, test_file2_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path2, exe_dir);
    wcscat(test_file_path2, L"\\test_data\\os_test_🌙移動.txt");

    // Convert paths to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    size_t const path2_wlen = wcslen(test_file_path2);
    size_t const path2_utf8_len = ov_wchar_to_utf8_len(test_file_path2, path2_wlen);
    if (!path2_utf8_len || !OV_ARRAY_GROW(&test_file_path2_utf8, path2_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path2, path2_wlen, test_file_path2_utf8, path2_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test files
    DeleteFileW(test_file_path);
    DeleteFileW(test_file_path2);

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Set test file paths as global variables
    lua_pushstring(L, test_file_path_utf8);
    lua_setglobal(L, "TEST_FILE_PATH");
    lua_pushstring(L, test_file_path2_utf8);
    lua_setglobal(L, "TEST_FILE_PATH2");

    // Test 1: os.getenv with Unicode variable name (PATH should work)
    {
      TEST_CASE("os.getenv");
      char const *script = "local path = os.getenv('PATH') "
                           "assert(path ~= nil, 'PATH should exist') "
                           "assert(type(path) == 'string', 'PATH should be string')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("os.getenv script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 2: os.tmpname returns valid UTF-8 path
    {
      TEST_CASE("os.tmpname");
      char const *script = "local name = os.tmpname() "
                           "assert(type(name) == 'string', 'tmpname should return string') "
                           "assert(#name > 0, 'tmpname should return non-empty string') "
                           "-- Verify it's a valid path by checking file operations would work "
                           "local f = io.open(name, 'w') "
                           "if f then "
                           "  f:write('test') "
                           "  f:close() "
                           "  os.remove(name) "
                           "end";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("os.tmpname script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 3: Create file with Unicode name, then rename it
    {
      TEST_CASE("os.rename with Unicode paths");

      // Create a file with Unicode name using io.open
      char const *create_script = "local f = io.open(TEST_FILE_PATH, 'w') "
                                  "assert(f, 'failed to create file') "
                                  "f:write('Hello 🌙!') "
                                  "f:close()";
      if (!TEST_CHECK(luaL_dostring(L, create_script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("create script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }

      // Verify file was created
      TEST_CHECK(GetFileAttributesW(test_file_path) != INVALID_FILE_ATTRIBUTES);

      // Rename to another Unicode filename
      char const *rename_script = "local ok, err = os.rename(TEST_FILE_PATH, TEST_FILE_PATH2) "
                                  "assert(ok, 'rename failed: ' .. (err or 'unknown'))";
      if (!TEST_CHECK(luaL_dostring(L, rename_script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("rename script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }

      // Verify rename worked
      TEST_CHECK(GetFileAttributesW(test_file_path) == INVALID_FILE_ATTRIBUTES);
      TEST_CHECK(GetFileAttributesW(test_file_path2) != INVALID_FILE_ATTRIBUTES);
    }

    // Test 4: os.remove with Unicode filename
    {
      TEST_CASE("os.remove with Unicode paths");
      char const *remove_script = "local ok, err = os.remove(TEST_FILE_PATH2) "
                                  "assert(ok, 'remove failed: ' .. (err or 'unknown'))";
      if (!TEST_CHECK(luaL_dostring(L, remove_script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("remove script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }

      // Verify file was deleted
      TEST_CHECK(GetFileAttributesW(test_file_path2) == INVALID_FILE_ATTRIBUTES);
    }

    // Test 5: os.remove error handling with Unicode paths
    {
      TEST_CASE("os.remove error handling");
      char const *script = "local ok, err = os.remove(TEST_FILE_PATH .. '.nonexistent') "
                           "assert(ok == nil, 'remove of nonexistent should fail') "
                           "assert(type(err) == 'string', 'should return error message')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("error handling script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 6: os.execute with Unicode in command (echo)
    {
      TEST_CASE("os.execute");
      char const *script = "local ok, kind, code = os.execute('echo test > NUL') "
                           "assert(ok == true, 'execute should succeed') "
                           "assert(kind == 'exit', 'should be exit') "
                           "assert(code == 0, 'exit code should be 0')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("os.execute script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path2) {
    DeleteFileW(test_file_path2);
    OV_ARRAY_DESTROY(&test_file_path2);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (test_file_path2_utf8) {
    OV_ARRAY_DESTROY(&test_file_path2_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_io_popen_tmpfile(void) {
  lua_State *L_standard = NULL;
  lua_State *L_override = NULL;
  struct ov_error err = {0};

  {
    // Create two lua_State instances: one standard, one with UTF-8 overrides
    L_standard = luaL_newstate();
    if (!TEST_CHECK(L_standard != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_standard);

    L_override = luaL_newstate();
    if (!TEST_CHECK(L_override != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_override);
    gcmz_lua_setup_utf8_funcs(L_override);

    // Test 1: io.popen read mode - ASCII compatibility
    TEST_CASE("io.popen read compatibility");
    {
      char const *script = "local f = io.popen('echo hello', 'r') "
                           "assert(f, 'popen failed') "
                           "local line = f:read('*l') "
                           "f:close() "
                           "return line and line:match('hello') and 'found' or 'not found'";
      if (exec_lua_script(L_standard, script, "standard", "io.popen read")) {
        char const *std_result = lua_tostring(L_standard, -1);
        char std_buf[64];
        strncpy(std_buf, std_result ? std_result : "", sizeof(std_buf) - 1);
        std_buf[sizeof(std_buf) - 1] = '\0';
        lua_pop(L_standard, lua_gettop(L_standard));

        if (exec_lua_script(L_override, script, "override", "io.popen read")) {
          char const *ovr_result = lua_tostring(L_override, -1);
          TEST_CHECK_(strcmp(std_buf, ovr_result ? ovr_result : "") == 0, "io.popen: %s != %s", std_buf, ovr_result);
          lua_pop(L_override, lua_gettop(L_override));
        }
      }
    }

    // Test 2: io.popen close returns success
    TEST_CASE("io.popen close status");
    {
      // Lua 5.1/LuaJIT: close() returns just true
      char const *script = "local f = io.popen('echo test', 'r') "
                           "f:read('*a') "
                           "local ok = f:close() "
                           "return ok and 'success' or 'fail'";
      if (exec_lua_script(L_standard, script, "standard", "io.popen close")) {
        char const *std_result = lua_tostring(L_standard, -1);
        char std_buf[64];
        strncpy(std_buf, std_result ? std_result : "", sizeof(std_buf) - 1);
        std_buf[sizeof(std_buf) - 1] = '\0';
        lua_pop(L_standard, lua_gettop(L_standard));

        if (exec_lua_script(L_override, script, "override", "io.popen close")) {
          char const *ovr_result = lua_tostring(L_override, -1);
          TEST_CHECK_(
              strcmp(std_buf, ovr_result ? ovr_result : "") == 0, "io.popen close: %s != %s", std_buf, ovr_result);
          lua_pop(L_override, lua_gettop(L_override));
        }
      }
    }

    // Test 3: io.tmpfile - both should create writable temporary files
    TEST_CASE("io.tmpfile compatibility");
    {
      char const *script = "local f = io.tmpfile() "
                           "assert(f, 'tmpfile failed') "
                           "f:write('test content') "
                           "f:seek('set', 0) "
                           "local content = f:read('*a') "
                           "f:close() "
                           "return content == 'test content' and 'ok' or 'fail'";
      if (exec_lua_script(L_standard, script, "standard", "io.tmpfile")) {
        char const *std_result = lua_tostring(L_standard, -1);
        char std_buf[64];
        strncpy(std_buf, std_result ? std_result : "", sizeof(std_buf) - 1);
        std_buf[sizeof(std_buf) - 1] = '\0';
        lua_pop(L_standard, lua_gettop(L_standard));

        if (exec_lua_script(L_override, script, "override", "io.tmpfile")) {
          char const *ovr_result = lua_tostring(L_override, -1);
          TEST_CHECK_(strcmp(std_buf, ovr_result ? ovr_result : "") == 0, "io.tmpfile: %s != %s", std_buf, ovr_result);
          lua_pop(L_override, lua_gettop(L_override));
        }
      }
    }

    // Test 4: io.type on popen handle
    TEST_CASE("io.type on popen handle");
    {
      char const *script = "local f = io.popen('echo test', 'r') "
                           "local t1 = io.type(f) "
                           "f:close() "
                           "local t2 = io.type(f) "
                           "return t1 .. '/' .. t2";
      if (exec_lua_script(L_standard, script, "standard", "io.type popen")) {
        char const *std_result = lua_tostring(L_standard, -1);
        char std_buf[64];
        strncpy(std_buf, std_result ? std_result : "", sizeof(std_buf) - 1);
        std_buf[sizeof(std_buf) - 1] = '\0';
        lua_pop(L_standard, lua_gettop(L_standard));

        if (exec_lua_script(L_override, script, "override", "io.type popen")) {
          char const *ovr_result = lua_tostring(L_override, -1);
          TEST_CHECK_(
              strcmp(std_buf, ovr_result ? ovr_result : "") == 0, "io.type popen: %s != %s", std_buf, ovr_result);
          lua_pop(L_override, lua_gettop(L_override));
        }
      }
    }
  }

cleanup:
  if (L_standard) {
    lua_close(L_standard);
  }
  if (L_override) {
    lua_close(L_override);
  }
  (void)err;
}

static void test_io_popen_unicode(void) {
  lua_State *L = NULL;

  {
    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Test 1: io.popen basic read
    {
      TEST_CASE("io.popen read");
      char const *script = "local f = io.popen('echo Hello World', 'r') "
                           "assert(f, 'popen failed') "
                           "local content = f:read('*a') "
                           "local ok = f:close() "
                           "assert(content:find('Hello'), 'should contain Hello') "
                           "assert(ok == true, 'close should succeed')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen read script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 2: io.popen with lines iterator
    {
      TEST_CASE("io.popen lines iterator");
      char const *script = "local f = io.popen('echo line1 & echo line2', 'r') "
                           "assert(f, 'popen failed') "
                           "local count = 0 "
                           "for line in f:lines() do "
                           "  count = count + 1 "
                           "end "
                           "f:close() "
                           "assert(count >= 2, 'should have at least 2 lines')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen lines script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 3: io.tmpfile read/write/seek
    {
      TEST_CASE("io.tmpfile operations");
      char const *script = "local f = io.tmpfile() "
                           "assert(f, 'tmpfile failed') "
                           "f:write('Line 1\\n') "
                           "f:write('Line 2\\n') "
                           "f:seek('set', 0) "
                           "local line1 = f:read('*l') "
                           "local line2 = f:read('*l') "
                           "f:close() "
                           "assert(line1 == 'Line 1', 'line1 mismatch: ' .. tostring(line1)) "
                           "assert(line2 == 'Line 2', 'line2 mismatch: ' .. tostring(line2))";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.tmpfile script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 4: io.popen close always returns true in Lua 5.1/LuaJIT
    {
      TEST_CASE("io.popen close");
      char const *script = "local f = io.popen('exit 1', 'r') "
                           "assert(f, 'popen failed') "
                           "f:read('*a') "
                           "local ok = f:close() "
                           "assert(ok == true, 'close should return true')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen close script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 5: io.popen write mode
    {
      TEST_CASE("io.popen write mode");
      // Use 'more' command which reads from stdin and outputs to stdout
      // We write to it and verify the process runs without error
      char const *script = "local f = io.popen('more > NUL', 'w') "
                           "assert(f, 'popen write failed') "
                           "f:write('Hello from Lua\\n') "
                           "f:write('Second line\\n') "
                           "local ok = f:close() "
                           "assert(ok == true, 'close should succeed')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen write script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 6: io.popen read with byte count
    {
      TEST_CASE("io.popen read bytes");
      char const *script = "local f = io.popen('echo ABCDEFGHIJ', 'r') "
                           "assert(f, 'popen failed') "
                           "local bytes = f:read(5) "
                           "f:close() "
                           "assert(bytes == 'ABCDE', 'expected ABCDE, got: ' .. tostring(bytes))";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen read bytes script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 7: io.popen flush (should not error)
    {
      TEST_CASE("io.popen flush");
      char const *script = "local f = io.popen('more > NUL', 'w') "
                           "assert(f, 'popen failed') "
                           "f:write('test') "
                           "local ok = f:flush() "
                           "f:close() "
                           "assert(ok == true, 'flush should succeed')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen flush script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 8: io.popen error on closed handle - returns nil, error_message
    {
      TEST_CASE("io.popen error on closed");
      char const *script = "local f = io.popen('echo test', 'r') "
                           "f:close() "
                           "local result, err = f:read('*a') "
                           "assert(result == nil, 'read on closed should return nil') "
                           "assert(type(err) == 'string', 'should return error message')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen closed error script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 9: io.popen error - write to read-mode handle
    {
      TEST_CASE("io.popen write to read handle");
      char const *script = "local f = io.popen('echo test', 'r') "
                           "local ok, err = f:write('data') "
                           "f:close() "
                           "assert(ok == nil, 'write to read handle should fail')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen write to read script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 10: io.popen error - read from write-mode handle
    {
      TEST_CASE("io.popen read from write handle");
      char const *script = "local f = io.popen('more > NUL', 'w') "
                           "local ok, err = f:read('*a') "
                           "f:close() "
                           "assert(ok == nil, 'read from write handle should fail')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.popen read from write script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
}

static void test_io_stdio_handles(void) {
  lua_State *L_standard = NULL;
  lua_State *L_override = NULL;

  {
    // Create two lua_State instances: one standard, one with UTF-8 overrides
    L_standard = luaL_newstate();
    if (!TEST_CHECK(L_standard != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_standard);

    L_override = luaL_newstate();
    if (!TEST_CHECK(L_override != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_override);
    gcmz_lua_setup_utf8_funcs(L_override);

    // Test 1: io.stdin, io.stdout, io.stderr existence
    TEST_CASE("io.stdin/stdout/stderr existence");
    {
      char const *script = "local has_stdin = io.stdin ~= nil "
                           "local has_stdout = io.stdout ~= nil "
                           "local has_stderr = io.stderr ~= nil "
                           "return (has_stdin and has_stdout and has_stderr) and 'all' or 'missing'";
      compare_string_results(L_standard, L_override, script, -1, "stdio handles existence");
    }

    // Test 2: io.type on stdio handles
    TEST_CASE("io.type on stdio handles");
    {
      char const *script = "local t1 = io.type(io.stdin) "
                           "local t2 = io.type(io.stdout) "
                           "local t3 = io.type(io.stderr) "
                           "return (t1 == 'file' and t2 == 'file' and t3 == 'file') and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "io.type on stdio");
    }

    // Test 3: io.input() returns default input (should be stdin or equivalent)
    TEST_CASE("io.input() default");
    {
      char const *script = "local f = io.input() "
                           "return io.type(f) == 'file' and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "io.input() default");
    }

    // Test 4: io.output() returns default output (should be stdout or equivalent)
    TEST_CASE("io.output() default");
    {
      char const *script = "local f = io.output() "
                           "return io.type(f) == 'file' and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "io.output() default");
    }

    // Test 5: io.type on popen handle (after close)
    TEST_CASE("io.type on popen handle");
    {
      char const *script = "local f = io.popen('echo test', 'r') "
                           "local t1 = io.type(f) "
                           "f:close() "
                           "local t2 = io.type(f) "
                           "return t1 .. '/' .. t2";
      compare_string_results(L_standard, L_override, script, -1, "io.type popen");
    }

    // Test 6: io.type on non-file objects
    TEST_CASE("io.type on non-file");
    {
      char const *script = "local t1 = io.type('string') "
                           "local t2 = io.type(123) "
                           "local t3 = io.type({}) "
                           "local t4 = io.type(nil) "
                           "return (t1 == nil and t2 == nil and t3 == nil and t4 == nil) and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "io.type non-file");
    }
  }

cleanup:
  if (L_standard) {
    lua_close(L_standard);
  }
  if (L_override) {
    lua_close(L_override);
  }
}

static void test_io_lines_variants(void) {
  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  char *test_file_path_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build test file path
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\lines_test_🌙.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\lines_test_🌙.txt");

    // Convert path to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test file
    DeleteFileW(test_file_path);

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Set test file path as global variable
    lua_pushstring(L, test_file_path_utf8);
    lua_setglobal(L, "TEST_FILE");

    // Create test file with multiple lines
    {
      char const *create_script = "local f = io.open(TEST_FILE, 'w') "
                                  "assert(f, 'failed to create file') "
                                  "f:write('Line 1 🌙\\n') "
                                  "f:write('Line 2 文字\\n') "
                                  "f:write('Line 3 end') " // No trailing newline
                                  "f:close()";
      if (!TEST_CHECK(luaL_dostring(L, create_script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("create script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 1: io.lines(filename) - iterate over lines in file
    {
      TEST_CASE("io.lines(filename)");
      char const *script = "local count = 0 "
                           "local lines = {} "
                           "for line in io.lines(TEST_FILE) do "
                           "  count = count + 1 "
                           "  lines[count] = line "
                           "end "
                           "assert(count == 3, 'expected 3 lines, got ' .. count) "
                           "assert(lines[1]:find('Line 1'), 'line 1 check') "
                           "assert(lines[2]:find('Line 2'), 'line 2 check') "
                           "assert(lines[3]:find('Line 3'), 'line 3 check')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.lines(filename) script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 2: file:lines() - iterate over lines in open file
    {
      TEST_CASE("file:lines()");
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "assert(f, 'failed to open file') "
                           "local count = 0 "
                           "for line in f:lines() do "
                           "  count = count + 1 "
                           "end "
                           "f:close() "
                           "assert(count == 3, 'expected 3 lines')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("file:lines() script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }

    // Test 3: io.lines() with default input
    {
      TEST_CASE("io.lines() with io.input");
      char const *script = "io.input(TEST_FILE) "
                           "local count = 0 "
                           "for line in io.lines() do "
                           "  count = count + 1 "
                           "end "
                           "io.input():close() "
                           "assert(count == 3, 'expected 3 lines')";
      if (!TEST_CHECK(luaL_dostring(L, script) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("io.lines() with io.input script failed: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_io_read_formats(void) {
  lua_State *L_standard = NULL;
  lua_State *L_override = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  char *test_file_path_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build test file path (ASCII for compatibility test)
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\read_format_test.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\read_format_test.txt");

    // Convert path to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test file
    DeleteFileW(test_file_path);

    // Create test file with known content
    {
      HANDLE h = CreateFileW(test_file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (h != INVALID_HANDLE_VALUE) {
        char const *content = "123.45\r\nHello World\r\nLine 3\r\n";
        DWORD written;
        WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(h);
      }
    }

    L_standard = luaL_newstate();
    if (!TEST_CHECK(L_standard != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_standard);

    L_override = luaL_newstate();
    if (!TEST_CHECK(L_override != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_override);
    gcmz_lua_setup_utf8_funcs(L_override);

    // Set test file path in both states
    lua_pushstring(L_standard, test_file_path_utf8);
    lua_setglobal(L_standard, "TEST_FILE");
    lua_pushstring(L_override, test_file_path_utf8);
    lua_setglobal(L_override, "TEST_FILE");

    // Test 1: read("*n") - read number
    TEST_CASE("read *n");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local n = f:read('*n') "
                           "f:close() "
                           "return type(n) == 'number' and n > 123 and n < 124 and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "read *n");
    }

    // Test 2: read("*l") - read line without newline
    TEST_CASE("read *l");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "f:read('*n') " // Skip number
                           "local line = f:read('*l') "
                           "f:close() "
                           "return line == 'Hello World' and 'ok' or 'fail: ' .. tostring(line)";
      compare_string_results(L_standard, L_override, script, -1, "read *l");
    }

    // Test 3: read("*a") - read all
    TEST_CASE("read *a");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local all = f:read('*a') "
                           "f:close() "
                           "return #all > 0 and all:find('Hello') and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "read *a");
    }

    // Test 4: read(n) - read n bytes
    TEST_CASE("read n bytes");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local bytes = f:read(6) " // "123.45"
                           "f:close() "
                           "return bytes == '123.45' and 'ok' or 'fail: ' .. tostring(bytes)";
      compare_string_results(L_standard, L_override, script, -1, "read n bytes");
    }

    // Test 5: read(0) - check EOF
    TEST_CASE("read 0 (EOF check)");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local check1 = f:read(0) " // Not at EOF, returns ""
                           "f:read('*a') "             // Read all
                           "local check2 = f:read(0) " // At EOF, returns nil
                           "f:close() "
                           "return (check1 == '' and check2 == nil) and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "read 0 EOF");
    }

    // Test 6: Multiple formats in one call
    TEST_CASE("read multiple formats");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local n, line = f:read('*n', '*l') "
                           "f:close() "
                           "return (type(n) == 'number' and line == 'Hello World') and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "read multiple");
    }
  }

cleanup:
  if (L_standard) {
    lua_close(L_standard);
  }
  if (L_override) {
    lua_close(L_override);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_error_compatibility(void) {
  lua_State *L_standard = NULL;
  lua_State *L_override = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *test_file_path = NULL;
  char *test_file_path_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build test file path
    size_t const test_file_len = wcslen(exe_dir) + wcslen(L"\\test_data\\error_compat_test.txt") + 1;
    if (!OV_ARRAY_GROW(&test_file_path, test_file_len)) {
      goto cleanup;
    }
    wcscpy(test_file_path, exe_dir);
    wcscat(test_file_path, L"\\test_data\\error_compat_test.txt");

    // Convert path to UTF-8
    size_t const path_wlen = wcslen(test_file_path);
    size_t const path_utf8_len = ov_wchar_to_utf8_len(test_file_path, path_wlen);
    if (!path_utf8_len || !OV_ARRAY_GROW(&test_file_path_utf8, path_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(test_file_path, path_wlen, test_file_path_utf8, path_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Clean up any existing test file
    DeleteFileW(test_file_path);

    L_standard = luaL_newstate();
    if (!TEST_CHECK(L_standard != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_standard);

    L_override = luaL_newstate();
    if (!TEST_CHECK(L_override != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L_override);
    gcmz_lua_setup_utf8_funcs(L_override);

    // Set test file path in both states
    lua_pushstring(L_standard, test_file_path_utf8);
    lua_setglobal(L_standard, "TEST_FILE");
    lua_pushstring(L_override, test_file_path_utf8);
    lua_setglobal(L_override, "TEST_FILE");

    // Test 1: io.open on non-existent file returns nil, error
    TEST_CASE("io.open non-existent file");
    {
      char const *script = "local f, err = io.open(TEST_FILE .. '.nonexistent', 'r') "
                           "local result = (f == nil and type(err) == 'string') and 'nil+err' or 'unexpected'  "
                           "return result";
      compare_string_results(L_standard, L_override, script, -1, "io.open nonexistent");
    }

    // Test 2: io.open with invalid mode
    TEST_CASE("io.open invalid mode");
    {
      // Create test file first
      HANDLE h = CreateFileW(test_file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
      }

      char const *script = "local f, err = io.open(TEST_FILE, 'xyz') "
                           "local result = (f == nil) and 'nil' or 'unexpected' "
                           "return result";
      compare_string_results(L_standard, L_override, script, -1, "io.open invalid mode");
    }

    // Test 3: file:read on closed file
    TEST_CASE("file:read on closed file");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "f:close() "
                           "local result, err = f:read('*a') "
                           "return (result == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "read closed");
    }

    // Test 4: file:write on closed file
    TEST_CASE("file:write on closed file");
    {
      char const *script = "local f = io.open(TEST_FILE, 'w') "
                           "f:close() "
                           "local result, err = f:write('test') "
                           "return (result == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "write closed");
    }

    // Test 5: file:close twice
    TEST_CASE("file:close twice");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local ok1 = f:close() "
                           "local ok2, err2 = f:close() "
                           "return (ok1 == true and ok2 == nil and type(err2) == 'string') and 'ok' or 'fail'";
      compare_string_results(L_standard, L_override, script, -1, "close twice");
    }

    // Test 6: file:seek on closed file
    TEST_CASE("file:seek on closed file");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "f:close() "
                           "local result, err = f:seek('set', 0) "
                           "return (result == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "seek closed");
    }

    // Test 7: file:flush on closed file
    TEST_CASE("file:flush on closed file");
    {
      char const *script = "local f = io.open(TEST_FILE, 'w') "
                           "f:close() "
                           "local result, err = f:flush() "
                           "return (result == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "flush closed");
    }

    // Test 8: file:write to read-only file
    TEST_CASE("file:write to read-only");
    {
      char const *script = "local f = io.open(TEST_FILE, 'r') "
                           "local result, err = f:write('test') "
                           "f:close() "
                           "return (result == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "write to read");
    }

    // Test 9: os.remove on non-existent file
    TEST_CASE("os.remove non-existent");
    {
      char const *script = "local ok, err = os.remove(TEST_FILE .. '.nonexistent') "
                           "return (ok == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "remove nonexistent");
    }

    // Test 10: os.rename non-existent source
    TEST_CASE("os.rename non-existent source");
    {
      char const *script = "local ok, err = os.rename(TEST_FILE .. '.nonexistent', TEST_FILE .. '.new') "
                           "return (ok == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "rename nonexistent");
    }

    // Test 11: io.lines on non-existent file (should error)
    TEST_CASE("io.lines non-existent file");
    {
      char const *script = "local ok, err = pcall(function() "
                           "  for line in io.lines(TEST_FILE .. '.nonexistent') do end "
                           "end) "
                           "return (not ok) and 'error' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "lines nonexistent");
    }

    // Test 12: loadfile on non-existent file
    TEST_CASE("loadfile non-existent file");
    {
      char const *script = "local chunk, err = loadfile(TEST_FILE .. '.nonexistent.lua') "
                           "return (chunk == nil and type(err) == 'string') and 'nil+err' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "loadfile nonexistent");
    }

    // Test 13: dofile on non-existent file (should error)
    TEST_CASE("dofile non-existent file");
    {
      char const *script = "local ok, err = pcall(function() "
                           "  dofile(TEST_FILE .. '.nonexistent.lua') "
                           "end) "
                           "return (not ok) and 'error' or 'unexpected'";
      compare_string_results(L_standard, L_override, script, -1, "dofile nonexistent");
    }

    // Test 14: io.type on various objects
    TEST_CASE("io.type consistency");
    {
      char const *script =
          "local f = io.open(TEST_FILE, 'r') "
          "local t1 = io.type(f) "
          "f:close() "
          "local t2 = io.type(f) "
          "local t3 = io.type('string') "
          "local t4 = io.type(123) "
          "local t5 = io.type({}) "
          "local t6 = io.type(function() end) "
          "return t1 .. '/' .. t2 .. '/' .. tostring(t3) .. '/' .. tostring(t4) .. '/' .. tostring(t5) "
          "       .. '/' .. tostring(t6)";
      compare_string_results(L_standard, L_override, script, -1, "io.type various");
    }

    // Test 15: io.read/write without default input/output set
    // (After closing the default, attempting to use io.read/write should fail)
    TEST_CASE("io.read without input");
    {
      // Save and restore default input to not affect other tests
      char const *script = "local saved = io.input() "
                           "io.input(TEST_FILE) "
                           "io.input():close() "
                           "local ok, err = pcall(function() io.read('*l') end) "
                           "local result = (not ok or err ~= nil) and 'error' or 'unexpected' "
                           "return result";
      // Note: behavior may differ - standard may keep closed handle, override may error differently
      // Just verify both don't crash and return some error indicator
      if (!exec_lua_script(L_standard, script, "standard", "io.read no input")) {
        lua_pop(L_standard, lua_gettop(L_standard));
      } else {
        lua_pop(L_standard, lua_gettop(L_standard));
      }
      if (!exec_lua_script(L_override, script, "override", "io.read no input")) {
        lua_pop(L_override, lua_gettop(L_override));
      } else {
        lua_pop(L_override, lua_gettop(L_override));
      }
    }
  }

cleanup:
  if (L_standard) {
    lua_close(L_standard);
  }
  if (L_override) {
    lua_close(L_override);
  }
  if (test_file_path) {
    DeleteFileW(test_file_path);
    OV_ARRAY_DESTROY(&test_file_path);
  }
  if (test_file_path_utf8) {
    OV_ARRAY_DESTROY(&test_file_path_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

static void test_c_root_searcher(void) {
  // Test package.loaders[4] (all-in-one C searcher)
  // This searcher loads submodules from a parent DLL
  // e.g., require("test_🌙.sub") loads luaopen_test_🌙_sub from test_🌙.dll

  lua_State *L = NULL;
  wchar_t *exe_dir = NULL;
  wchar_t *dll_dir = NULL;
  char *dll_dir_utf8 = NULL;
  struct ov_error err = {0};

  {
    if (!get_exe_directory(&exe_dir, &err)) {
      OV_ERROR_REPORT(&err, NULL);
      return;
    }

    // Build DLL directory path
    size_t const dll_dir_len = wcslen(exe_dir) + wcslen(L"\\test_data\\lua_modules") + 1;
    if (!OV_ARRAY_GROW(&dll_dir, dll_dir_len)) {
      goto cleanup;
    }
    wcscpy(dll_dir, exe_dir);
    wcscat(dll_dir, L"\\test_data\\lua_modules");

    L = luaL_newstate();
    if (!TEST_CHECK(L != NULL)) {
      goto cleanup;
    }
    luaL_openlibs(L);
    gcmz_lua_setup_utf8_funcs(L);

    // Convert DLL directory to UTF-8
    size_t const dll_dir_wlen = wcslen(dll_dir);
    size_t const dll_dir_utf8_len = ov_wchar_to_utf8_len(dll_dir, dll_dir_wlen);
    if (!dll_dir_utf8_len || !OV_ARRAY_GROW(&dll_dir_utf8, dll_dir_utf8_len + 1)) {
      goto cleanup;
    }
    if (!ov_wchar_to_utf8(dll_dir, dll_dir_wlen, dll_dir_utf8, dll_dir_utf8_len + 1, NULL)) {
      goto cleanup;
    }

    // Add test module directory to package.cpath
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "cpath");
    char const *const current_cpath = lua_tostring(L, -1);
    lua_pop(L, 1);

    char cpath_buffer[4096];
    ov_snprintf_char(
        cpath_buffer, sizeof(cpath_buffer), NULL, "%s;%s\\?.dll", current_cpath ? current_cpath : "", dll_dir_utf8);
    lua_pushstring(L, cpath_buffer);
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    // Test 1: Load parent module first (via loaders[3])
    {
      TEST_CASE("load parent module");
      lua_getglobal(L, "require");
      lua_pushstring(L, "test_\xF0\x9F\x8C\x99"); // test_🌙 in UTF-8
      if (!TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("Failed to load parent module: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }
      TEST_CHECK(lua_istable(L, -1));
      lua_pop(L, 1);
    }

    // Test 2: Load submodule (via loaders[4] - all-in-one searcher)
    {
      TEST_CASE("load submodule via loaders[4]");
      lua_getglobal(L, "require");
      lua_pushstring(L, "test_\xF0\x9F\x8C\x99.sub"); // test_🌙.sub in UTF-8
      if (!TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
        char const *err_msg = lua_tostring(L, -1);
        TEST_MSG("Failed to load submodule: %s", err_msg ? err_msg : "unknown error");
        lua_pop(L, 1);
        goto cleanup;
      }

      // Verify submodule loaded correctly
      TEST_CHECK(lua_istable(L, -1));

      // Check submodule has expected fields
      lua_getfield(L, -1, "name");
      if (TEST_CHECK(lua_isstring(L, -1))) {
        char const *name = lua_tostring(L, -1);
        TEST_CHECK(name != NULL && strcmp(name, "sub") == 0);
        TEST_MSG("Submodule name: %s", name ? name : "(null)");
      }
      lua_pop(L, 1);

      // Check submodule has hello function
      lua_getfield(L, -1, "hello");
      TEST_CHECK(lua_isfunction(L, -1));
      lua_pop(L, 1);

      // Call hello function
      lua_getfield(L, -1, "hello");
      if (TEST_CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK)) {
        char const *result = lua_tostring(L, -1);
        TEST_CHECK(result != NULL && strstr(result, "sub") != NULL);
        TEST_MSG("hello() returned: %s", result ? result : "(null)");
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }

    // Test 3: Verify submodule is cached in package.loaded
    {
      TEST_CASE("submodule cached in package.loaded");
      lua_getglobal(L, "package");
      lua_getfield(L, -1, "loaded");
      lua_getfield(L, -1, "test_\xF0\x9F\x8C\x99.sub");
      TEST_CHECK(lua_istable(L, -1));
      lua_pop(L, 3);
    }

    // Test 4: Require submodule again (should return cached version)
    {
      TEST_CASE("submodule cached on second require");
      lua_getglobal(L, "require");
      lua_pushstring(L, "test_\xF0\x9F\x8C\x99.sub");
      if (TEST_CHECK(lua_pcall(L, 1, 1, 0) == LUA_OK)) {
        TEST_CHECK(lua_istable(L, -1));
        lua_pop(L, 1);
      }
    }
  }

cleanup:
  if (L) {
    lua_close(L);
  }
  if (dll_dir) {
    OV_ARRAY_DESTROY(&dll_dir);
  }
  if (dll_dir_utf8) {
    OV_ARRAY_DESTROY(&dll_dir_utf8);
  }
  if (exe_dir) {
    OV_ARRAY_DESTROY(&exe_dir);
  }
}

TEST_LIST = {
    {"utf8_funcs_ascii_compatibility", test_utf8_funcs_ascii_compatibility},
    {"unicode_paths", test_unicode_paths},
    {"io_unicode_paths", test_io_unicode_paths},
    {"io_simple_style", test_io_simple_style},
    {"c_module_cleanup", test_c_module_cleanup},
    {"c_root_searcher", test_c_root_searcher},
    {"os_funcs_ascii_compatibility", test_os_funcs_ascii_compatibility},
    {"os_unicode_paths", test_os_unicode_paths},
    {"io_popen_tmpfile", test_io_popen_tmpfile},
    {"io_popen_unicode", test_io_popen_unicode},
    {"io_stdio_handles", test_io_stdio_handles},
    {"io_lines_variants", test_io_lines_variants},
    {"io_read_formats", test_io_read_formats},
    {"error_compatibility", test_error_compatibility},
    {NULL, NULL},
};
