#include "do_sub.h"

#include <ovthreads.h>

enum thread_state {
  thread_state_stopped,
  thread_state_mtx_created,
  thread_state_cnd_created,
  thread_state_running,
  thread_state_stopping,
};

enum task_state {
  task_state_idle,
  task_state_pending,
  task_state_running,
};

/**
 * Internal context structure for worker thread system
 */
struct gcmz_do_sub {
  thrd_t thread;
  enum thread_state thread_state;

  mtx_t task_mutex;
  cnd_t task_available;
  cnd_t task_completed;

  enum task_state task_state;
  gcmz_do_sub_func current_func;
  void *current_data;
};

static int worker_thread_proc(void *arg) {
  struct gcmz_do_sub *const ctx = (struct gcmz_do_sub *)arg;
  if (!ctx) {
    return -1;
  }

  if (mtx_lock(&ctx->task_mutex) != thrd_success) {
    return -1;
  }

  while (ctx->thread_state == thread_state_running) {
    while (ctx->task_state == task_state_idle && ctx->thread_state == thread_state_running) {
      cnd_wait(&ctx->task_available, &ctx->task_mutex);
    }

    if (ctx->thread_state != thread_state_running) {
      break;
    }

    gcmz_do_sub_func func = ctx->current_func;
    void *data = ctx->current_data;
    ctx->task_state = task_state_running;

    mtx_unlock(&ctx->task_mutex);
    func(data);
    mtx_lock(&ctx->task_mutex);

    ctx->task_state = task_state_idle;
    ctx->current_func = NULL;
    ctx->current_data = NULL;

    cnd_broadcast(&ctx->task_completed);
  }

  mtx_unlock(&ctx->task_mutex);
  return 0;
}

void gcmz_do_sub_destroy(struct gcmz_do_sub **const ctxpp) {
  if (!ctxpp || !*ctxpp) {
    return;
  }
  struct gcmz_do_sub *const ctx = *ctxpp;

  bool has_running_thread = false;
  if (ctx->thread_state >= thread_state_mtx_created) {
    if (mtx_lock(&ctx->task_mutex) == thrd_success) {
      while (ctx->task_state != task_state_idle) {
        cnd_wait(&ctx->task_completed, &ctx->task_mutex);
      }
      has_running_thread = ctx->thread_state == thread_state_running;
      if (has_running_thread) {
        ctx->thread_state = thread_state_stopping;
        cnd_signal(&ctx->task_available);
      }
      mtx_unlock(&ctx->task_mutex);
    }
  }

  if (has_running_thread) {
    thrd_join(ctx->thread, NULL);
  }

  if (ctx->thread_state >= thread_state_cnd_created) {
    cnd_destroy(&ctx->task_completed);
    cnd_destroy(&ctx->task_available);
  }

  if (ctx->thread_state >= thread_state_mtx_created) {
    mtx_destroy(&ctx->task_mutex);
  }

  OV_FREE(ctxpp);
}

NODISCARD struct gcmz_do_sub *gcmz_do_sub_create(struct ov_error *const err) {
  struct gcmz_do_sub *ctx = NULL;
  struct gcmz_do_sub *result = NULL;
  int r = 0;

  {
    if (!OV_REALLOC(&ctx, 1, sizeof(struct gcmz_do_sub))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    *ctx = (struct gcmz_do_sub){0};

    if (mtx_init(&ctx->task_mutex, mtx_plain) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    ctx->thread_state = thread_state_mtx_created;

    if (cnd_init(&ctx->task_available) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    if (cnd_init(&ctx->task_completed) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    ctx->thread_state = thread_state_cnd_created;

    if (mtx_lock(&ctx->task_mutex) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    r = thrd_create(&ctx->thread, worker_thread_proc, ctx);
    if (r == thrd_success) {
      ctx->thread_state = thread_state_running;
      cnd_signal(&ctx->task_available);
    }
    mtx_unlock(&ctx->task_mutex);

    if (r != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
  }

  result = ctx;
  ctx = NULL;

cleanup:
  if (ctx) {
    gcmz_do_sub_destroy(&ctx);
  }
  return result;
}

void gcmz_do_sub_do(struct gcmz_do_sub *const ctx, gcmz_do_sub_func func, void *data) {
  if (!ctx || !func) {
    return;
  }

  if (mtx_lock(&ctx->task_mutex) != thrd_success) {
    return;
  }

  while (ctx->task_state != task_state_idle) {
    cnd_wait(&ctx->task_completed, &ctx->task_mutex);
  }

  ctx->current_func = func;
  ctx->current_data = data;
  ctx->task_state = task_state_pending;
  cnd_signal(&ctx->task_available);

  mtx_unlock(&ctx->task_mutex);
}

void gcmz_do_sub_do_blocking(struct gcmz_do_sub *const ctx, gcmz_do_sub_func func, void *data) {
  if (!ctx || !func) {
    return;
  }

  if (mtx_lock(&ctx->task_mutex) != thrd_success) {
    return;
  }

  while (ctx->task_state != task_state_idle) {
    cnd_wait(&ctx->task_completed, &ctx->task_mutex);
  }

  ctx->current_func = func;
  ctx->current_data = data;
  ctx->task_state = task_state_pending;
  cnd_signal(&ctx->task_available);

  while (ctx->task_state != task_state_idle) {
    cnd_wait(&ctx->task_completed, &ctx->task_mutex);
  }

  mtx_unlock(&ctx->task_mutex);
}
