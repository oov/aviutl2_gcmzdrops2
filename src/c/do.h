#pragma once

#include <ovbase.h>

/**
 * @brief Function pointer type for window thread execution callbacks
 */
typedef void (*gcmz_do_func)(void *data);

/**
 * @brief Options for initializing window thread execution system
 */
struct gcmz_do_init_option {
  void *window;                    ///< Window handle to subclass for message handling
  gcmz_do_func on_change_activate; ///< Activate change callback (called for WM_ACTIVATE/WM_ACTIVATEAPP, can be NULL)
  void *userdata;                  ///< User data passed to callbacks
};

/**
 * @brief Initialize window thread execution system
 *
 * Subclasses the window to enable execution of functions
 * on the window's thread from other threads via custom window messages.
 *
 * @param option Options for initialization
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_do_init(struct gcmz_do_init_option const *const option, struct ov_error *const err);

/**
 * @brief Terminate the window thread execution system
 */
void gcmz_do_exit(void);

/**
 * @brief Execute function on the window thread asynchronously
 *
 * This function can be called from any thread. The specified function
 * will be executed asynchronously on the window's thread.
 *
 * @param func Function pointer to execute
 * @param data Data to pass to the function
 */
void gcmz_do(gcmz_do_func func, void *data);

/**
 * @brief Execute function on the window thread and wait for completion
 *
 * This function can be called from any thread. The caller will
 * block until the specified function has been executed
 * on the window's thread and completed.
 *
 * @param func Function pointer to execute
 * @param data Data to pass to the function
 */
void gcmz_do_blocking(gcmz_do_func func, void *data);
