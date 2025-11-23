#pragma once

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wreserved-macro-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#  endif
#endif // __GNUC__
#include <lualib.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#include <ovbase.h>

// Implementation function, use gcmz_luafn_err macro instead
int gcmz_luafn_err_(lua_State *const L, struct ov_error *const e, char const *const funcname);

/**
 * @brief Report error to Lua and clean up
 *
 * Converts and reports an ov_error to Lua's error system,
 * then cleans up the error structure. Automatically captures
 * the function name for error context.
 *
 * @param L Lua state
 * @param err Error to report
 * @return Result of lua_error (does not return)
 */
#define gcmz_luafn_err(L, err) gcmz_luafn_err_((L), (err), (__func__))

/**
 * @brief Call Lua function with error handling
 *
 * Wrapper around lua_pcall that converts Lua errors to struct ov_error.
 * On error, the error message from Lua is captured and set in the error structure,
 * and the error is popped from the stack.
 *
 * @param L Lua state (function and arguments on stack)
 * @param nargs Number of arguments
 * @param nresults Number of return values
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_lua_pcall(lua_State *const L, int nargs, int nresults, struct ov_error *const err);
