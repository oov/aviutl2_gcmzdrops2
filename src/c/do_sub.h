#pragma once

#include <ovbase.h>

/**
 * @brief Opaque structure for worker thread execution context
 */
struct gcmz_do_sub;

/**
 * @brief Function pointer type for worker thread execution callbacks
 */
typedef void (*gcmz_do_sub_func)(void *data);

/**
 * @brief Create a new worker thread execution context
 *
 * Creates a single dedicated worker thread for executing tasks.
 * Only one task can execute at a time. If a task is already running,
 * gcmz_do_sub_do() will block until the current task completes.
 *
 * @param err [out] Error information on failure
 * @return Pointer to new context on success, NULL on failure
 */
NODISCARD struct gcmz_do_sub *gcmz_do_sub_create(struct ov_error *const err);

/**
 * @brief Destroy the worker thread execution context
 *
 * Waits for any currently running task to complete before shutting down.
 * After this call, the worker thread will be destroyed and the context freed.
 *
 * @param ctxpp [in/out] Pointer to context pointer, will be set to NULL
 */
void gcmz_do_sub_destroy(struct gcmz_do_sub **const ctxpp);

/**
 * @brief Execute function on the worker thread asynchronously
 *
 * Posts a task to the worker thread. If a task is already running,
 * this function will BLOCK until the current task completes.
 * This ensures only one task runs at a time without queuing.
 *
 * @param ctx Context pointer (must not be NULL)
 * @param func Function pointer to execute (must not be NULL)
 * @param data Data to pass to the function (can be NULL)
 */
void gcmz_do_sub_do(struct gcmz_do_sub *const ctx, gcmz_do_sub_func func, void *data);

/**
 * @brief Execute function on the worker thread and wait for completion
 *
 * Posts a task to the worker thread and blocks the caller until
 * the task completes execution. If a task is already running,
 * this function will wait for both the current task and the posted task.
 *
 * @param ctx Context pointer (must not be NULL)
 * @param func Function pointer to execute (must not be NULL)
 * @param data Data to pass to the function (can be NULL)
 */
void gcmz_do_sub_do_blocking(struct gcmz_do_sub *const ctx, gcmz_do_sub_func func, void *data);
