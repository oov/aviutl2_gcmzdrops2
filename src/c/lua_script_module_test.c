/**
 * @file lua_script_module_test.c
 * @brief Comprehensive tests for script module functionality in lua.c
 *
 * Tests AviUtl ExEdit2 script module API compatibility including:
 * - Module registration and lookup
 * - Parameter passing (primitives, tables, arrays)
 * - Result returning (primitives, tables, arrays)
 * - Error handling
 * - Protection against modification
 */

#include <ovtest.h>

#include "lua.h"

#include <ovarray.h>
#include <ovprintf.h>
#include <ovutf.h>

#include <aviutl2_module2.h>

#include <math.h>
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

// ============================================================================
// Test Helper Functions and Mock Script Module Functions
// ============================================================================

// Storage for captured parameters during tests
static struct {
  int num_params;
  int int_values[16];
  double double_values[16];
  char string_values[16][256];
  bool bool_values[16];
  void *data_values[16];

  // Table values
  int table_int_values[16];
  double table_double_values[16];
  char table_string_values[16][256];
  bool table_bool_values[16];

  // Array values
  int array_num;
  int array_int_values[16];
  double array_double_values[16];
  char array_string_values[16][256];

  // Error info
  bool error_was_set;
  char error_message[256];
} g_captured = {0};

static void clear_captured(void) { memset(&g_captured, 0, sizeof(g_captured)); }

// ============================================================================
// Mock Script Module Functions
// ============================================================================

/**
 * @brief Simple function that returns the number of parameters
 */
static void func_get_param_count(struct aviutl2_script_module_param *param) {
  int count = param->get_param_num();
  param->push_result_int(count);
}

/**
 * @brief Function that captures all integer parameters and returns their sum
 */
static void func_sum_integers(struct aviutl2_script_module_param *param) {
  int count = param->get_param_num();
  g_captured.num_params = count;
  int sum = 0;
  for (int i = 0; i < count && i < 16; i++) {
    g_captured.int_values[i] = param->get_param_int(i);
    sum += g_captured.int_values[i];
  }
  param->push_result_int(sum);
}

/**
 * @brief Function that captures all double parameters and returns their sum
 */
static void func_sum_doubles(struct aviutl2_script_module_param *param) {
  int count = param->get_param_num();
  g_captured.num_params = count;
  double sum = 0.0;
  for (int i = 0; i < count && i < 16; i++) {
    g_captured.double_values[i] = param->get_param_double(i);
    sum += g_captured.double_values[i];
  }
  param->push_result_double(sum);
}

/**
 * @brief Function that captures a string parameter and returns it uppercase
 */
static void func_string_param(struct aviutl2_script_module_param *param) {
  char const *str = param->get_param_string(0);
  if (str) {
    strncpy(g_captured.string_values[0], str, sizeof(g_captured.string_values[0]) - 1);
    g_captured.string_values[0][sizeof(g_captured.string_values[0]) - 1] = '\0';
  }
  param->push_result_string(str);
}

/**
 * @brief Function that captures a boolean parameter
 */
static void func_boolean_param(struct aviutl2_script_module_param *param) {
  g_captured.bool_values[0] = param->get_param_boolean(0);
  param->push_result_boolean(g_captured.bool_values[0]);
}

/**
 * @brief Function that captures a data pointer parameter
 */
static void func_data_param(struct aviutl2_script_module_param *param) {
  g_captured.data_values[0] = param->get_param_data(0);
  param->push_result_data(g_captured.data_values[0]);
}

/**
 * @brief Function that reads table fields
 */
static void func_table_param(struct aviutl2_script_module_param *param) {
  g_captured.table_int_values[0] = param->get_param_table_int(0, "int_field");
  g_captured.table_double_values[0] = param->get_param_table_double(0, "double_field");
  char const *str = param->get_param_table_string(0, "string_field");
  if (str) {
    strncpy(g_captured.table_string_values[0], str, sizeof(g_captured.table_string_values[0]) - 1);
  }
  g_captured.table_bool_values[0] = param->get_param_table_boolean(0, "bool_field");
  param->push_result_int(1);
}

/**
 * @brief Function that reads array elements
 */
static void func_array_param(struct aviutl2_script_module_param *param) {
  g_captured.array_num = param->get_param_array_num(0);
  for (int i = 0; i < g_captured.array_num && i < 16; i++) {
    g_captured.array_int_values[i] = param->get_param_array_int(0, i);
  }
  param->push_result_int(g_captured.array_num);
}

/**
 * @brief Function that reads array of doubles
 */
static void func_array_double_param(struct aviutl2_script_module_param *param) {
  g_captured.array_num = param->get_param_array_num(0);
  for (int i = 0; i < g_captured.array_num && i < 16; i++) {
    g_captured.array_double_values[i] = param->get_param_array_double(0, i);
  }
  param->push_result_int(g_captured.array_num);
}

/**
 * @brief Function that reads array of strings
 */
static void func_array_string_param(struct aviutl2_script_module_param *param) {
  g_captured.array_num = param->get_param_array_num(0);
  for (int i = 0; i < g_captured.array_num && i < 16; i++) {
    char const *str = param->get_param_array_string(0, i);
    if (str) {
      strncpy(g_captured.array_string_values[i], str, sizeof(g_captured.array_string_values[i]) - 1);
    }
  }
  param->push_result_int(g_captured.array_num);
}

/**
 * @brief Function that returns multiple values
 */
static void func_multi_return(struct aviutl2_script_module_param *param) {
  (void)param;
  param->push_result_int(42);
  param->push_result_double(3.14);
  param->push_result_string("hello");
  param->push_result_boolean(true);
}

/**
 * @brief Function that returns a table with int values
 */
static void func_return_table_int(struct aviutl2_script_module_param *param) {
  (void)param;
  char const *keys[] = {"a", "b", "c"};
  int values[] = {10, 20, 30};
  param->push_result_table_int(keys, values, 3);
}

/**
 * @brief Function that returns a table with double values
 */
static void func_return_table_double(struct aviutl2_script_module_param *param) {
  (void)param;
  char const *keys[] = {"x", "y", "z"};
  double values[] = {1.1, 2.2, 3.3};
  param->push_result_table_double(keys, values, 3);
}

/**
 * @brief Function that returns a table with string values
 */
static void func_return_table_string(struct aviutl2_script_module_param *param) {
  (void)param;
  char const *keys[] = {"name", "type"};
  char const *values[] = {"test", "module"};
  param->push_result_table_string(keys, values, 2);
}

/**
 * @brief Function that returns an array of ints
 */
static void func_return_array_int(struct aviutl2_script_module_param *param) {
  (void)param;
  int values[] = {1, 2, 3, 4, 5};
  param->push_result_array_int(values, 5);
}

/**
 * @brief Function that returns an array of doubles
 */
static void func_return_array_double(struct aviutl2_script_module_param *param) {
  (void)param;
  double values[] = {1.5, 2.5, 3.5};
  param->push_result_array_double(values, 3);
}

/**
 * @brief Function that returns an array of strings
 */
static void func_return_array_string(struct aviutl2_script_module_param *param) {
  (void)param;
  char const *values[] = {"one", "two", "three"};
  param->push_result_array_string(values, 3);
}

/**
 * @brief Function that sets an error
 */
static void func_set_error(struct aviutl2_script_module_param *param) {
  char const *msg = param->get_param_string(0);
  if (msg) {
    param->set_error(msg);
  } else {
    param->set_error("default error");
  }
}

/**
 * @brief Function that handles edge cases (out of bounds, NULL, etc.)
 */
static void func_edge_cases(struct aviutl2_script_module_param *param) {
  // Try to get params beyond range
  int beyond = param->get_param_int(100);
  double beyond_d = param->get_param_double(100);
  char const *beyond_s = param->get_param_string(100);
  bool beyond_b = param->get_param_boolean(100);
  void *beyond_data = param->get_param_data(100);

  // Try negative index
  int neg = param->get_param_int(-1);

  // Try table operations on non-table
  int table_on_int = param->get_param_table_int(0, "key");

  // Try array operations on non-array (assuming first param is int)
  int array_num_on_int = param->get_param_array_num(0);

  param->push_result_int(beyond);
  param->push_result_double(beyond_d);
  param->push_result_string(beyond_s);
  param->push_result_boolean(beyond_b);
  param->push_result_int(neg);
  param->push_result_int(table_on_int);
  param->push_result_int(array_num_on_int);

  (void)beyond_data;
}

/**
 * @brief Function with a very long name for testing heap allocation
 */
static void func_with_very_long_name_that_exceeds_sixty_four_characters_limit_for_testing_heap_allocation(
    struct aviutl2_script_module_param *param) {
  param->push_result_string("long_name_function_called");
}

/**
 * @brief Function that does complex parameter/result operations
 */
static void func_complex_operation(struct aviutl2_script_module_param *param) {
  int count = param->get_param_num();
  if (count < 2) {
    param->set_error("requires at least 2 parameters");
    return;
  }

  // Get operation type from first param
  char const *op = param->get_param_string(0);
  if (!op) {
    param->set_error("first parameter must be operation string");
    return;
  }

  if (strcmp(op, "add") == 0) {
    double sum = 0.0;
    for (int i = 1; i < count; i++) {
      sum += param->get_param_double(i);
    }
    param->push_result_double(sum);
  } else if (strcmp(op, "concat") == 0) {
    // Just return first string for simplicity
    char const *s = param->get_param_string(1);
    param->push_result_string(s ? s : "");
  } else if (strcmp(op, "table_sum") == 0) {
    // Sum values from table keys "a", "b", "c"
    double sum = 0.0;
    sum += param->get_param_table_double(1, "a");
    sum += param->get_param_table_double(1, "b");
    sum += param->get_param_table_double(1, "c");
    param->push_result_double(sum);
  } else {
    param->set_error("unknown operation");
  }
}

// ============================================================================
// Script Module Table Definitions
// ============================================================================

static struct aviutl2_script_module_function g_test_functions[] = {
    {L"get_param_count", func_get_param_count},
    {L"sum_integers", func_sum_integers},
    {L"sum_doubles", func_sum_doubles},
    {L"string_param", func_string_param},
    {L"boolean_param", func_boolean_param},
    {L"data_param", func_data_param},
    {L"table_param", func_table_param},
    {L"array_param", func_array_param},
    {L"array_double_param", func_array_double_param},
    {L"array_string_param", func_array_string_param},
    {L"multi_return", func_multi_return},
    {L"return_table_int", func_return_table_int},
    {L"return_table_double", func_return_table_double},
    {L"return_table_string", func_return_table_string},
    {L"return_array_int", func_return_array_int},
    {L"return_array_double", func_return_array_double},
    {L"return_array_string", func_return_array_string},
    {L"set_error", func_set_error},
    {L"edge_cases", func_edge_cases},
    {L"func_with_very_long_name_that_exceeds_sixty_four_characters_limit_for_testing_heap_allocation",
     func_with_very_long_name_that_exceeds_sixty_four_characters_limit_for_testing_heap_allocation},
    {L"complex_operation", func_complex_operation},
    {NULL, NULL} // Terminator
};

static struct aviutl2_script_module_table g_test_module = {
    .information = L"Test Script Module",
    .functions = g_test_functions,
};

// Second module for testing multiple modules
static struct aviutl2_script_module_function g_test_functions2[] = {
    {L"get_param_count", func_get_param_count},
    {NULL, NULL},
};

static struct aviutl2_script_module_table g_test_module2 = {
    .information = L"Test Script Module 2",
    .functions = g_test_functions2,
};

// ============================================================================
// Helper to run Lua code and check results
// ============================================================================

static bool run_lua_code(lua_State *L, char const *code, char const *test_name) {
  if (luaL_dostring(L, code) != 0) {
    char const *err = lua_tostring(L, -1);
    TEST_MSG("%s failed: %s", test_name, err ? err : "unknown error");
    lua_pop(L, 1);
    return false;
  }
  return true;
}

// Helper to register test module lookup function
static void register_module_lookup(lua_State *L) {
  // Register a simple get_script_module function that looks up from registry
  char const *code = "function get_test_module(name)\n"
                     "  local key = '" // Will be set below
      ;
  (void)code;

  // We'll access the module directly from the registry using the key
  lua_getfield(L, LUA_REGISTRYINDEX, gcmz_lua_get_script_modules_key());
  if (!lua_isnil(L, -1)) {
    lua_setglobal(L, "_script_modules");
  } else {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setglobal(L, "_script_modules");
  }

  // Register helper function
  luaL_dostring(L,
                "function get_test_module(name)\n"
                "  return _script_modules and _script_modules[name]\n"
                "end\n");
}

// ============================================================================
// Test Cases
// ============================================================================

static void test_register_module(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }

  // Register the test module
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  // Verify module is in registry
  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L, "assert(get_test_module('testmod') ~= nil, 'module should exist')", "module lookup"));

  gcmz_lua_destroy(&ctx);
}

static void test_register_duplicate_module(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }

  // Register the test module
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  // Try to register again - should fail
  bool result = gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err);
  TEST_CHECK(!result);
  OV_ERROR_DESTROY(&err);

  gcmz_lua_destroy(&ctx);
}

static void test_register_multiple_modules(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }

  // Register first module
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "mod1", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  // Register second module
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module2, "mod2", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L, "assert(get_test_module('mod1') ~= nil)", "mod1 exists"));
  TEST_CHECK(run_lua_code(L, "assert(get_test_module('mod2') ~= nil)", "mod2 exists"));
  TEST_CHECK(run_lua_code(L, "assert(get_test_module('mod1') ~= get_test_module('mod2'))", "modules are different"));

  gcmz_lua_destroy(&ctx);
}

static void test_invalid_registration(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }

  // NULL context
  TEST_CHECK(!gcmz_lua_register_script_module(NULL, &g_test_module, "test", &err));
  OV_ERROR_DESTROY(&err);

  // NULL table
  TEST_CHECK(!gcmz_lua_register_script_module(ctx, NULL, "test", &err));
  OV_ERROR_DESTROY(&err);

  // NULL module name
  TEST_CHECK(!gcmz_lua_register_script_module(ctx, &g_test_module, NULL, &err));
  OV_ERROR_DESTROY(&err);

  // Empty module name
  TEST_CHECK(!gcmz_lua_register_script_module(ctx, &g_test_module, "", &err));
  OV_ERROR_DESTROY(&err);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_no_params(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L, "local m = get_test_module('testmod'); assert(m.get_param_count() == 0)", "no params"));

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_integers(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L, "local m = get_test_module('testmod'); assert(m.sum_integers(1, 2, 3) == 6)", "sum ints"));
  TEST_CHECK(g_captured.num_params == 3);
  TEST_CHECK(g_captured.int_values[0] == 1);
  TEST_CHECK(g_captured.int_values[1] == 2);
  TEST_CHECK(g_captured.int_values[2] == 3);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_doubles(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(
      run_lua_code(L,
                   "local m = get_test_module('testmod'); assert(math.abs(m.sum_doubles(1.5, 2.5, 3.0) - 7.0) < 0.001)",
                   "sum doubles"));
  TEST_CHECK(g_captured.num_params == 3);
  TEST_CHECK(fabs(g_captured.double_values[0] - 1.5) < 0.001);
  TEST_CHECK(fabs(g_captured.double_values[1] - 2.5) < 0.001);
  TEST_CHECK(fabs(g_captured.double_values[2] - 3.0) < 0.001);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_string(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(
      run_lua_code(L,
                   "local m = get_test_module('testmod'); assert(m.string_param('hello world') == 'hello world')",
                   "string param"));
  TEST_CHECK(strcmp(g_captured.string_values[0], "hello world") == 0);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_boolean(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(
      run_lua_code(L, "local m = get_test_module('testmod'); assert(m.boolean_param(true) == true)", "bool true"));
  TEST_CHECK(g_captured.bool_values[0] == true);

  clear_captured();
  TEST_CHECK(
      run_lua_code(L, "local m = get_test_module('testmod'); assert(m.boolean_param(false) == false)", "bool false"));
  TEST_CHECK(g_captured.bool_values[0] == false);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_table(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "m.table_param({int_field=42, double_field=3.14, string_field='test', bool_field=true})",
                          "table param"));

  TEST_CHECK(g_captured.table_int_values[0] == 42);
  TEST_CHECK(fabs(g_captured.table_double_values[0] - 3.14) < 0.001);
  TEST_CHECK(strcmp(g_captured.table_string_values[0], "test") == 0);
  TEST_CHECK(g_captured.table_bool_values[0] == true);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_array(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(
      L, "local m = get_test_module('testmod'); assert(m.array_param({10, 20, 30, 40}) == 4)", "array int"));

  TEST_CHECK(g_captured.array_num == 4);
  TEST_CHECK(g_captured.array_int_values[0] == 10);
  TEST_CHECK(g_captured.array_int_values[1] == 20);
  TEST_CHECK(g_captured.array_int_values[2] == 30);
  TEST_CHECK(g_captured.array_int_values[3] == 40);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_array_doubles(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(
      L, "local m = get_test_module('testmod'); assert(m.array_double_param({1.1, 2.2, 3.3}) == 3)", "array double"));

  TEST_CHECK(g_captured.array_num == 3);
  TEST_CHECK(fabs(g_captured.array_double_values[0] - 1.1) < 0.001);
  TEST_CHECK(fabs(g_captured.array_double_values[1] - 2.2) < 0.001);
  TEST_CHECK(fabs(g_captured.array_double_values[2] - 3.3) < 0.001);

  gcmz_lua_destroy(&ctx);
}

static void test_call_function_with_array_strings(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod'); assert(m.array_string_param({'a', 'bb', 'ccc'}) == 3)",
                          "array string"));

  TEST_CHECK(g_captured.array_num == 3);
  TEST_CHECK(strcmp(g_captured.array_string_values[0], "a") == 0);
  TEST_CHECK(strcmp(g_captured.array_string_values[1], "bb") == 0);
  TEST_CHECK(strcmp(g_captured.array_string_values[2], "ccc") == 0);

  gcmz_lua_destroy(&ctx);
}

static void test_multi_return(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local a, b, c, d = m.multi_return()\n"
                          "assert(a == 42, 'first return')\n"
                          "assert(math.abs(b - 3.14) < 0.001, 'second return')\n"
                          "assert(c == 'hello', 'third return')\n"
                          "assert(d == true, 'fourth return')\n",
                          "multi return"));

  gcmz_lua_destroy(&ctx);
}

static void test_return_table_int(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local t = m.return_table_int()\n"
                          "assert(t.a == 10)\n"
                          "assert(t.b == 20)\n"
                          "assert(t.c == 30)\n",
                          "return table int"));

  gcmz_lua_destroy(&ctx);
}

static void test_return_table_double(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local t = m.return_table_double()\n"
                          "assert(math.abs(t.x - 1.1) < 0.001)\n"
                          "assert(math.abs(t.y - 2.2) < 0.001)\n"
                          "assert(math.abs(t.z - 3.3) < 0.001)\n",
                          "return table double"));

  gcmz_lua_destroy(&ctx);
}

static void test_return_table_string(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local t = m.return_table_string()\n"
                          "assert(t.name == 'test')\n"
                          "assert(t.type == 'module')\n",
                          "return table string"));

  gcmz_lua_destroy(&ctx);
}

static void test_return_array_int(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local arr = m.return_array_int()\n"
                          "assert(#arr == 5)\n"
                          "assert(arr[1] == 1)\n"
                          "assert(arr[2] == 2)\n"
                          "assert(arr[3] == 3)\n"
                          "assert(arr[4] == 4)\n"
                          "assert(arr[5] == 5)\n",
                          "return array int"));

  gcmz_lua_destroy(&ctx);
}

static void test_return_array_double(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local arr = m.return_array_double()\n"
                          "assert(#arr == 3)\n"
                          "assert(math.abs(arr[1] - 1.5) < 0.001)\n"
                          "assert(math.abs(arr[2] - 2.5) < 0.001)\n"
                          "assert(math.abs(arr[3] - 3.5) < 0.001)\n",
                          "return array double"));

  gcmz_lua_destroy(&ctx);
}

static void test_return_array_string(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local arr = m.return_array_string()\n"
                          "assert(#arr == 3)\n"
                          "assert(arr[1] == 'one')\n"
                          "assert(arr[2] == 'two')\n"
                          "assert(arr[3] == 'three')\n",
                          "return array string"));

  gcmz_lua_destroy(&ctx);
}

static void test_error_handling(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  // Error should be raised
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local ok, errmsg = pcall(function() m.set_error('custom error') end)\n"
                          "assert(not ok, 'should have failed')\n"
                          "assert(string.find(errmsg, 'custom error'), 'error message should contain custom error')\n",
                          "error handling"));

  gcmz_lua_destroy(&ctx);
}

static void test_module_protection(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  // Trying to modify module table should fail
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local ok = pcall(function() m.new_field = 123 end)\n"
                          "assert(not ok, 'modification should fail')\n",
                          "module protection"));

  gcmz_lua_destroy(&ctx);
}

static void test_edge_cases(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  // Call edge_cases with an integer (to test table/array ops on non-table)
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local r1, r2, r3, r4, r5, r6, r7 = m.edge_cases(42)\n"
                          "assert(r1 == 0, 'beyond int should be 0')\n"
                          "assert(r2 == 0, 'beyond double should be 0')\n"
                          "assert(r3 == nil, 'beyond string should be nil')\n"
                          "assert(r4 == false, 'beyond bool should be false')\n"
                          "assert(r5 == 0, 'negative index int should be 0')\n"
                          "assert(r6 == 0, 'table_int on non-table should be 0')\n"
                          "assert(r7 == 0, 'array_num on non-table should be 0')\n",
                          "edge cases"));

  gcmz_lua_destroy(&ctx);
}

static void test_long_function_name(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(
      L,
      "local m = get_test_module('testmod')\n"
      "assert(m.func_with_very_long_name_that_exceeds_sixty_four_characters_limit_for_testing_heap_allocation() == "
      "'long_name_function_called')\n",
      "long function name"));

  gcmz_lua_destroy(&ctx);
}

static void test_complex_operation(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  // Test add operation
  TEST_CHECK(run_lua_code(
      L,
      "local m = get_test_module('testmod'); assert(math.abs(m.complex_operation('add', 1, 2, 3) - 6) < 0.001)",
      "add op"));

  // Test concat operation
  TEST_CHECK(
      run_lua_code(L,
                   "local m = get_test_module('testmod'); assert(m.complex_operation('concat', 'hello') == 'hello')",
                   "concat op"));

  // Test table_sum operation
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "assert(math.abs(m.complex_operation('table_sum', {a=1, b=2, c=3}) - 6) < 0.001)",
                          "table_sum op"));

  // Test error on unknown operation
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local ok = pcall(function() m.complex_operation('unknown') end)\n"
                          "assert(not ok)",
                          "unknown op error"));

  // Test error on too few params
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local ok = pcall(function() m.complex_operation() end)\n"
                          "assert(not ok)",
                          "too few params error"));

  gcmz_lua_destroy(&ctx);
}

static void test_nonexistent_module(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  // Don't register any module

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);

  TEST_CHECK(run_lua_code(L, "assert(get_test_module('nonexistent') == nil)", "nonexistent module"));

  gcmz_lua_destroy(&ctx);
}

static void test_utf8_string_handling(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  // Test UTF-8 strings (Japanese text)
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "local s = m.string_param('こんにちは世界')\n"
                          "assert(s == 'こんにちは世界')\n",
                          "utf8 string"));

  TEST_CHECK(strcmp(g_captured.string_values[0], "こんにちは世界") == 0);

  gcmz_lua_destroy(&ctx);
}

static void test_empty_string(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L, "local m = get_test_module('testmod'); assert(m.string_param('') == '')", "empty string"));

  TEST_CHECK(strcmp(g_captured.string_values[0], "") == 0);

  gcmz_lua_destroy(&ctx);
}

static void test_nil_parameters(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  // String param with nil
  TEST_CHECK(run_lua_code(L, "local m = get_test_module('testmod'); assert(m.string_param(nil) == nil)", "nil string"));

  gcmz_lua_destroy(&ctx);
}

static void test_mixed_type_parameters(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  // get_param_count should count all types
  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "assert(m.get_param_count(1, 'str', true, 3.14, {}, nil) == 6)\n",
                          "mixed types"));

  gcmz_lua_destroy(&ctx);
}

static void test_large_numbers(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "assert(m.sum_integers(2147483647, -2147483648) == -1)\n",
                          "large integers"));

  TEST_CHECK(run_lua_code(L,
                          "local m = get_test_module('testmod')\n"
                          "assert(math.abs(m.sum_doubles(1e308, -1e308)) < 1e-10)\n",
                          "large doubles"));

  gcmz_lua_destroy(&ctx);
}

static void test_empty_array(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L, "local m = get_test_module('testmod'); assert(m.array_param({}) == 0)", "empty array"));
  TEST_CHECK(g_captured.array_num == 0);

  gcmz_lua_destroy(&ctx);
}

static void test_empty_table(void) {
  struct gcmz_lua_context *ctx = NULL;
  struct ov_error err = {0};

  if (!TEST_SUCCEEDED(gcmz_lua_create(&ctx, &err), &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(gcmz_lua_register_script_module(ctx, &g_test_module, "testmod", &err), &err)) {
    gcmz_lua_destroy(&ctx);
    return;
  }

  lua_State *L = gcmz_lua_get_state(ctx);
  register_module_lookup(L);
  clear_captured();

  TEST_CHECK(run_lua_code(L, "local m = get_test_module('testmod'); m.table_param({})", "empty table"));

  // All values should be defaults
  TEST_CHECK(g_captured.table_int_values[0] == 0);
  TEST_CHECK(g_captured.table_double_values[0] == 0.0);
  TEST_CHECK(strlen(g_captured.table_string_values[0]) == 0);
  TEST_CHECK(g_captured.table_bool_values[0] == false);

  gcmz_lua_destroy(&ctx);
}

// ============================================================================
// Test List
// ============================================================================

TEST_LIST = {
    // Registration tests
    {"register_module", test_register_module},
    {"register_duplicate_module", test_register_duplicate_module},
    {"register_multiple_modules", test_register_multiple_modules},
    {"invalid_registration", test_invalid_registration},

    // Basic parameter tests
    {"call_function_no_params", test_call_function_no_params},
    {"call_function_with_integers", test_call_function_with_integers},
    {"call_function_with_doubles", test_call_function_with_doubles},
    {"call_function_with_string", test_call_function_with_string},
    {"call_function_with_boolean", test_call_function_with_boolean},

    // Table parameter tests
    {"call_function_with_table", test_call_function_with_table},

    // Array parameter tests
    {"call_function_with_array", test_call_function_with_array},
    {"call_function_with_array_doubles", test_call_function_with_array_doubles},
    {"call_function_with_array_strings", test_call_function_with_array_strings},

    // Return value tests
    {"multi_return", test_multi_return},
    {"return_table_int", test_return_table_int},
    {"return_table_double", test_return_table_double},
    {"return_table_string", test_return_table_string},
    {"return_array_int", test_return_array_int},
    {"return_array_double", test_return_array_double},
    {"return_array_string", test_return_array_string},

    // Error handling tests
    {"error_handling", test_error_handling},
    {"module_protection", test_module_protection},

    // Edge case tests
    {"edge_cases", test_edge_cases},
    {"long_function_name", test_long_function_name},
    {"complex_operation", test_complex_operation},
    {"nonexistent_module", test_nonexistent_module},

    // String handling tests
    {"utf8_string_handling", test_utf8_string_handling},
    {"empty_string", test_empty_string},
    {"nil_parameters", test_nil_parameters},
    {"mixed_type_parameters", test_mixed_type_parameters},

    // Numeric edge cases
    {"large_numbers", test_large_numbers},

    // Empty collection tests
    {"empty_array", test_empty_array},
    {"empty_table", test_empty_table},

    {NULL, NULL},
};
