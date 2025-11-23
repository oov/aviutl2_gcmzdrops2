#include <ovtest.h>

#include "file.h"
#include "lua.h"

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

static void test_create_destroy(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  // Test successful creation
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(ctx != NULL)) {
    return;
  }

  // Verify we can get the lua_State
  lua_State *L = gcmz_lua_get_state(ctx);
  if (!TEST_CHECK(L != NULL)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  // Clean up
  gcmz_lua_destroy(&ctx);
  TEST_CHECK(ctx == NULL);

  // Test destroy with NULL (should not crash)
  gcmz_lua_destroy(NULL);

  // Test create with NULL pointer
  TEST_CHECK(!gcmz_lua_create(NULL, NULL, &err));
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));

  // Test create with already initialized pointer
  OV_ERROR_DESTROY(&err);
  ctx = (struct gcmz_lua_context *)0x1; // Non-NULL value
  TEST_CHECK(!gcmz_lua_create(&ctx, NULL, &err));
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));

  OV_ERROR_DESTROY(&err);
}

static void test_standard_libraries(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  if (!TEST_CHECK(L != NULL)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  // Test base library (print function)
  lua_getglobal(L, "print");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 1);

  // Test string library
  lua_getglobal(L, "string");
  TEST_CHECK(lua_istable(L, -1));
  lua_getfield(L, -1, "len");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 2);

  // Test table library
  lua_getglobal(L, "table");
  TEST_CHECK(lua_istable(L, -1));
  lua_getfield(L, -1, "insert");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 2);

  // Test math library
  lua_getglobal(L, "math");
  TEST_CHECK(lua_istable(L, -1));
  lua_getfield(L, -1, "sin");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 2);

  // Test io library
  lua_getglobal(L, "io");
  TEST_CHECK(lua_istable(L, -1));
  lua_getfield(L, -1, "write");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 2);

  // Test os library
  lua_getglobal(L, "os");
  TEST_CHECK(lua_istable(L, -1));
  lua_getfield(L, -1, "time");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 2);

  // Test package library
  lua_getglobal(L, "package");
  TEST_CHECK(lua_istable(L, -1));
  lua_pop(L, 1);

  // Test debug library
  lua_getglobal(L, "debug");
  TEST_CHECK(lua_istable(L, -1));
  lua_getfield(L, -1, "getinfo");
  TEST_CHECK(lua_isfunction(L, -1));
  lua_pop(L, 2);

  gcmz_lua_destroy(&ctx);
}

static void test_script_execution(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  if (!TEST_CHECK(L != NULL)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  if (TEST_CHECK(luaL_dostring(L, "return 2 + 3") == LUA_OK)) {
    TEST_CHECK(lua_isnumber(L, -1));
    TEST_CHECK(lua_tonumber(L, -1) == 5.0);
  }
  lua_pop(L, 1);

  if (TEST_CHECK(luaL_dostring(L, "return string.len('hello')") == LUA_OK)) {
    TEST_CHECK(lua_isnumber(L, -1));
    TEST_CHECK(lua_tonumber(L, -1) == 5.0);
  }
  lua_pop(L, 1);

  if (TEST_CHECK(luaL_dostring(L, "local t = {1, 2, 3}; return #t") == LUA_OK)) {
    TEST_CHECK(lua_isnumber(L, -1));
    TEST_CHECK(lua_tonumber(L, -1) == 3.0);
  }
  lua_pop(L, 1);

  gcmz_lua_destroy(&ctx);
}

static void test_null_pointer_handling(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  // Test gcmz_lua_create with NULL
  TEST_CHECK(!gcmz_lua_create(NULL, NULL, &err));
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
  OV_ERROR_DESTROY(&err);

  // Test gcmz_lua_destroy with NULL
  gcmz_lua_destroy(NULL); // Should not crash

  // Test gcmz_lua_destroy with pointer to NULL
  struct gcmz_lua_context *null_ctx = NULL;
  gcmz_lua_destroy(&null_ctx); // Should not crash

  // Test gcmz_lua_get_state with NULL
  lua_State *L = gcmz_lua_get_state(NULL);
  TEST_CHECK(L == NULL);

  // Create a valid context for additional tests
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Test that get_state works with valid context
  L = gcmz_lua_get_state(ctx);
  TEST_CHECK(L != NULL);

  gcmz_lua_destroy(&ctx);
}

static void test_memory_leak_detection(void) {
  // Perform multiple create/destroy cycles to detect memory leaks
  for (int i = 0; i < 100; ++i) {
    TEST_CASE_("iteration %d", i);

    struct gcmz_lua_context *ctx = NULL;
    struct ov_error err = {0};

    if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
      OV_ERROR_DESTROY(&err);
      return;
    }

    if (!TEST_CHECK(ctx != NULL)) {
      return;
    }

    // Verify the context is functional
    lua_State *L = gcmz_lua_get_state(ctx);
    if (!TEST_CHECK(L != NULL)) {
      gcmz_lua_destroy(&ctx);
      return;
    }

    if (TEST_CHECK(luaL_dostring(L, "return 42") == LUA_OK)) {
      TEST_CHECK(lua_isnumber(L, -1));
      TEST_CHECK(lua_tonumber(L, -1) == 42.0);
    }
    lua_pop(L, 1);

    gcmz_lua_destroy(&ctx);
    TEST_CHECK(ctx == NULL);
  }
}

static void test_lua_state_creation_failure(void) {
  struct ov_error err = {0};

  // Test with already initialized context pointer (should fail)
  struct gcmz_lua_context *ctx = (struct gcmz_lua_context *)0x1;
  TEST_CHECK(!gcmz_lua_create(&ctx, NULL, &err));
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
  OV_ERROR_DESTROY(&err);

  // Test normal creation to ensure it still works
  ctx = NULL;
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(ctx != NULL)) {
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  TEST_CHECK(L != NULL);

  gcmz_lua_destroy(&ctx);
}

static void test_context_state_after_destruction(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  // Create context
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  if (!TEST_CHECK(ctx != NULL)) {
    return;
  }

  // Verify it works before destruction
  lua_State *L = gcmz_lua_get_state(ctx);
  if (!TEST_CHECK(L != NULL)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  // Destroy context
  gcmz_lua_destroy(&ctx);

  // Verify pointer is set to NULL after destruction
  TEST_CHECK(ctx == NULL);

  // Verify get_state returns NULL for destroyed context
  L = gcmz_lua_get_state(ctx);
  TEST_CHECK(L == NULL);
}

static void test_script_dir_parameter(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};
  lua_State *L = NULL;

  // Test with NULL script_dir (should succeed)
  if (!TEST_CHECK(gcmz_lua_create(&ctx, NULL, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(ctx != NULL)) {
    return;
  }

  // Verify context is functional
  L = gcmz_lua_get_state(ctx);
  TEST_CHECK(L != NULL);

  gcmz_lua_destroy(&ctx);

  // Test with empty string script_dir (should succeed)
  if (!TEST_CHECK(
          gcmz_lua_create(&ctx, &(struct gcmz_lua_options){.script_dir = L"", .api_register_callback = NULL}, &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(ctx != NULL)) {
    return;
  }

  // Verify context is functional
  L = gcmz_lua_get_state(ctx);
  TEST_CHECK(L != NULL);

  gcmz_lua_destroy(&ctx);

  // Test with non-existent directory (should succeed - plugin loading is future functionality)
  if (!TEST_CHECK(gcmz_lua_create(
          &ctx,
          &(struct gcmz_lua_options){.script_dir = L"C:\\NonExistentDirectory", .api_register_callback = NULL},
          &err))) {
    OV_ERROR_DESTROY(&err);
    return;
  }
  if (!TEST_CHECK(ctx != NULL)) {
    return;
  }

  // Verify context is functional
  L = gcmz_lua_get_state(ctx);
  TEST_CHECK(L != NULL);

  gcmz_lua_destroy(&ctx);
}

static void test_multiple_contexts(void) {
  struct gcmz_lua_context *ctx1 = NULL;
  struct gcmz_lua_context *ctx2 = NULL;
  struct gcmz_lua_context *ctx3 = NULL;
  struct ov_error err1 = {0};
  struct ov_error err2 = {0};
  struct ov_error err3 = {0};

  // Create multiple contexts
  if (!TEST_CHECK(gcmz_lua_create(&ctx1, NULL, &err1))) {
    OV_ERROR_DESTROY(&err1);
  }
  if (!TEST_CHECK(gcmz_lua_create(&ctx2, NULL, &err2))) {
    OV_ERROR_DESTROY(&err2);
  }
  if (!TEST_CHECK(gcmz_lua_create(&ctx3, NULL, &err3))) {
    OV_ERROR_DESTROY(&err3);
  }

  if (!ctx1 || !ctx2 || !ctx3) {
    gcmz_lua_destroy(&ctx1);
    gcmz_lua_destroy(&ctx2);
    gcmz_lua_destroy(&ctx3);
    return;
  }

  // Verify all contexts are independent
  lua_State *L1 = gcmz_lua_get_state(ctx1);
  lua_State *L2 = gcmz_lua_get_state(ctx2);
  lua_State *L3 = gcmz_lua_get_state(ctx3);

  if (!TEST_CHECK(L1 != NULL) || !TEST_CHECK(L2 != NULL) || !TEST_CHECK(L3 != NULL)) {
    gcmz_lua_destroy(&ctx1);
    gcmz_lua_destroy(&ctx2);
    gcmz_lua_destroy(&ctx3);
    return;
  }

  // Verify they are different lua_State instances
  TEST_CHECK(L1 != L2);
  TEST_CHECK(L2 != L3);
  TEST_CHECK(L1 != L3);

  if (TEST_CHECK(luaL_dostring(L1, "x = 1; return x") == LUA_OK)) {
    TEST_CHECK(lua_tonumber(L1, -1) == 1.0);
  }
  lua_pop(L1, 1);

  if (TEST_CHECK(luaL_dostring(L2, "x = 2; return x") == LUA_OK)) {
    TEST_CHECK(lua_tonumber(L2, -1) == 2.0);
  }
  lua_pop(L2, 1);

  if (TEST_CHECK(luaL_dostring(L3, "x = 3; return x") == LUA_OK)) {
    TEST_CHECK(lua_tonumber(L3, -1) == 3.0);
  }
  lua_pop(L3, 1);

  // Clean up all contexts
  gcmz_lua_destroy(&ctx1);
  gcmz_lua_destroy(&ctx2);
  gcmz_lua_destroy(&ctx3);

  TEST_CHECK(ctx1 == NULL);
  TEST_CHECK(ctx2 == NULL);
  TEST_CHECK(ctx3 == NULL);
}

static void test_hook_functions_null_context(void) {
  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};

  file_list = gcmz_file_list_create(&err);
  if (!TEST_CHECK(file_list != NULL)) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Add a test file
  if (!TEST_CHECK(gcmz_file_list_add(file_list, L"C:\\test\\file.txt", L"text/plain", &err))) {
    gcmz_file_list_destroy(&file_list);
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Test all hook functions with NULL context (should return error)
  if (TEST_CHECK(!gcmz_lua_call_drag_enter(NULL, file_list, 100, 200, 0, &err))) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }

  if (TEST_CHECK(!gcmz_lua_call_drag_over(NULL, 100, 200, 0, &err))) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }

  if (TEST_CHECK(!gcmz_lua_call_drag_leave(NULL, &err))) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }

  if (TEST_CHECK(!gcmz_lua_call_drop(NULL, file_list, 100, 200, 0, &err))) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }

  gcmz_file_list_destroy(&file_list);
}

static void test_hook_functions_no_modules(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!gcmz_lua_create(&ctx, NULL, &err)) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  struct gcmz_file_list *file_list = NULL;
  file_list = gcmz_file_list_create(&err);
  if (!file_list) {
    gcmz_lua_destroy(&ctx);
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Add a test file
  if (!gcmz_file_list_add(file_list, L"C:\\test\\file.txt", L"text/plain", &err)) {
    gcmz_file_list_destroy(&file_list);
    gcmz_lua_destroy(&ctx);
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Test hook functions with valid context but no modules (should succeed)
  TEST_CHECK(gcmz_lua_call_drag_enter(ctx, file_list, 100, 200, 0, &err));

  TEST_CHECK(gcmz_lua_call_drag_over(ctx, 100, 200, 0, &err));

  TEST_CHECK(gcmz_lua_call_drag_leave(ctx, &err));

  TEST_CHECK(gcmz_lua_call_drop(ctx, file_list, 100, 200, 0, &err));

  gcmz_file_list_destroy(&file_list);
  gcmz_lua_destroy(&ctx);
}

static void test_hook_functions_null_file_list(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!gcmz_lua_create(&ctx, NULL, &err)) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Test hook functions that require file_list with NULL
  if (TEST_CHECK(!gcmz_lua_call_drag_enter(ctx, NULL, 100, 200, 0, &err))) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }

  if (TEST_CHECK(!gcmz_lua_call_drop(ctx, NULL, 100, 200, 0, &err))) {
    TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
    OV_ERROR_DESTROY(&err);
  }

  // Test hook functions that don't require file_list (should succeed)
  TEST_CHECK(gcmz_lua_call_drag_over(ctx, 100, 200, 0, &err));

  TEST_CHECK(gcmz_lua_call_drag_leave(ctx, &err));

  gcmz_lua_destroy(&ctx);
}

static void test_drag_session_workflow(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!gcmz_lua_create(&ctx, NULL, &err)) {
    OV_ERROR_DESTROY(&err);
    return;
  }

  struct gcmz_file_list *file_list = NULL;
  file_list = gcmz_file_list_create(&err);
  if (!file_list) {
    gcmz_lua_destroy(&ctx);
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Add test files
  if (!gcmz_file_list_add(file_list, L"C:\\test\\file1.psd", L"image/vnd.adobe.photoshop", &err)) {
    gcmz_file_list_destroy(&file_list);
    gcmz_lua_destroy(&ctx);
    OV_ERROR_DESTROY(&err);
    return;
  }

  if (!gcmz_file_list_add(file_list, L"C:\\test\\file2.txt", L"text/plain", &err)) {
    gcmz_file_list_destroy(&file_list);
    gcmz_lua_destroy(&ctx);
    OV_ERROR_DESTROY(&err);
    return;
  }

  // Simulate complete drag session
  TEST_CHECK(gcmz_lua_call_drag_enter(ctx, file_list, 100, 200, 0x08, &err)); // MK_CONTROL

  TEST_CHECK(gcmz_lua_call_drag_over(ctx, 150, 250, 0x08, &err));

  TEST_CHECK(gcmz_lua_call_drag_over(ctx, 200, 300, 0x10, &err)); // MK_SHIFT

  TEST_CHECK(gcmz_lua_call_drop(ctx, file_list, 200, 300, 0x10, &err));

  // Test drag_leave after drop (should still work)
  TEST_CHECK(gcmz_lua_call_drag_leave(ctx, &err));

  gcmz_file_list_destroy(&file_list);
  gcmz_lua_destroy(&ctx);
}

TEST_LIST = {
    {"create_destroy", test_create_destroy},
    {"standard_libraries", test_standard_libraries},
    {"script_execution", test_script_execution},
    {"null_pointer_handling", test_null_pointer_handling},
    {"memory_leak_detection", test_memory_leak_detection},
    {"lua_state_creation_failure", test_lua_state_creation_failure},
    {"context_state_after_destruction", test_context_state_after_destruction},
    {"script_dir_parameter", test_script_dir_parameter},
    {"multiple_contexts", test_multiple_contexts},
    {"hook_functions_null_context", test_hook_functions_null_context},
    {"hook_functions_no_modules", test_hook_functions_no_modules},
    {"hook_functions_null_file_list", test_hook_functions_null_file_list},
    {"drag_session_workflow", test_drag_session_workflow},
    {NULL, NULL},
};
