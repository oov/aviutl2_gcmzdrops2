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
 * @brief Report error to Lua and clean up (raises error - does not return)
 *
 * Converts and reports an ov_error to Lua's error system,
 * then cleans up the error structure. Automatically captures
 * the function name for error context.
 *
 * Use this for programming errors (invalid arguments, API not configured, etc.)
 * where the script should not continue execution.
 *
 * @param L Lua state
 * @param err Error to report
 * @return Result of lua_error (does not return)
 */
#define gcmz_luafn_err(L, err) gcmz_luafn_err_((L), (err), (__func__))

// Implementation function, use gcmz_luafn_result_err macro instead
int gcmz_luafn_result_err_(lua_State *const L, struct ov_error *const e, char const *const funcname);

/**
 * @brief Return error as (nil, errmsg) to Lua and clean up
 *
 * Converts an ov_error to an error message and returns (nil, errmsg) to Lua.
 * Cleans up the error structure after use.
 *
 * Use this for operational errors (file not found, conversion failed, etc.)
 * where the script can reasonably handle the error with pcall or checking nil.
 *
 * @param L Lua state
 * @param err Error to convert and clean up
 * @return 2 (number of return values: nil and error message)
 */
#define gcmz_luafn_result_err(L, err) gcmz_luafn_result_err_((L), (err), (__func__))

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

/**
 * @brief Convert UTF-8 string to wchar_t
 *
 * Allocates memory using OV_ARRAY_GROW. Caller is responsible for calling OV_ARRAY_DESTROY.
 * The buffer can be reused by passing a non-NULL pointer; it will be grown if needed.
 * If src is NULL or empty, the destination will contain an empty string.
 *
 * @param src Source UTF-8 string (can be NULL)
 * @param dest [in/out] Pointer to destination buffer (can be reused)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_utf8_to_wchar(char const *src, wchar_t **dest, struct ov_error *err);

/**
 * @brief Convert wchar_t string to UTF-8
 *
 * Allocates memory using OV_ARRAY_GROW. Caller is responsible for calling OV_ARRAY_DESTROY.
 * The buffer can be reused by passing a non-NULL pointer; it will be grown if needed.
 * If src is NULL or empty, the destination will contain an empty string.
 *
 * @param src Source wide string (can be NULL)
 * @param dest [in/out] Pointer to destination buffer (can be reused)
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_wchar_to_utf8(wchar_t const *src, char **dest, struct ov_error *err);

/**
 * @brief Setup UTF-8 aware file loading functions for Lua
 *
 * Replaces standard Lua file loading functions with UTF-8 aware versions
 * for proper Unicode path support on Windows. This function should be called
 * after luaL_openlibs() to override the standard implementations.
 *
 * Functions replaced:
 * - loadfile: Uses wchar_t file APIs instead of fopen
 * - dofile: Reimplemented using the new loadfile
 * - package.loaders[2]: Lua file searcher with wchar_t support
 * - package.loaders[3]: C library searcher with LoadLibraryW
 * - package.loaders[4]: All-in-one C library searcher with LoadLibraryW
 * - io.open: Uses CreateFileW for Unicode path support
 * - io.input: UTF-8 path support when filename is given
 * - io.output: UTF-8 path support when filename is given
 * - io.lines: UTF-8 path support when filename is given
 * - io.close, io.flush, io.read, io.write: Work with new file handles
 * - io.type: Recognizes new file handles
 * - io.popen: Uses CreateProcessW with Unicode command support
 * - io.tmpfile: Uses GetTempFileNameW for Unicode path support
 * - os.execute: Uses CreateProcessW with Unicode command support
 * - os.remove: Uses DeleteFileW/RemoveDirectoryW for Unicode path support
 * - os.rename: Uses MoveFileW for Unicode path support
 * - os.tmpname: Uses GetTempFileNameW and returns UTF-8 path
 * - os.getenv: Uses GetEnvironmentVariableW and returns UTF-8 value
 * - os.setlocale: Disabled (always raises error to preserve UTF-8 environment)
 *
 * @param L Lua state
 */
void gcmz_lua_setup_utf8_funcs(lua_State *const L);
