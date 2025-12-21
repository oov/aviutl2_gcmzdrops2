#pragma once

#include <ovbase.h>

struct lua_State;
struct aviutl2_script_module_param;

/**
 * @brief Call a script module function with parameter marshalling
 *
 * Sets up the parameter context, builds the aviutl2_script_module_param
 * interface, calls the function, and handles the result/error.
 *
 * This is a pure marshalling layer between Lua stack values and the
 * aviutl2_script_module_param interface. It handles:
 * - Getting parameters from Lua stack (get_param_*)
 * - Pushing results to Lua stack (push_result_*)
 * - Error handling (set_error)
 *
 * @param L Lua state
 * @param func The script module function to call
 * @return Number of return values, or raises Lua error on failure
 */
int script_module_param_call(struct lua_State *const L, void (*func)(struct aviutl2_script_module_param *));
