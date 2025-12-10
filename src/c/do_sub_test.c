#include <ovtest.h>

#include <stdatomic.h>

#include <ovthreads.h>

#include "do_sub.h"

static void test_create_with_null_error(void) {
  struct gcmz_do_sub *ctx = gcmz_do_sub_create(NULL);
  TEST_CHECK(ctx != NULL);

  gcmz_do_sub_destroy(&ctx);
  TEST_CHECK(ctx == NULL);
}

static void test_create_success(void) {
  struct ov_error err = {0};

  struct gcmz_do_sub *ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx != NULL, &err)) {
    return;
  }

  gcmz_do_sub_destroy(&ctx);
  TEST_CHECK(ctx == NULL);
}

static void test_double_create(void) {
  struct ov_error err = {0};

  struct gcmz_do_sub *ctx1 = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx1 != NULL, &err)) {
    return;
  }

  struct gcmz_do_sub *ctx2 = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx2 != NULL, &err)) {
    gcmz_do_sub_destroy(&ctx1);
    return;
  }

  TEST_CHECK(ctx1 != ctx2);

  gcmz_do_sub_destroy(&ctx1);
  gcmz_do_sub_destroy(&ctx2);
}

static mtx_t g_test_mutex;
static int g_counter = 0;

static void increment_counter(void *data) {
  (void)data;
  mtx_lock(&g_test_mutex);
  g_counter++;
  mtx_unlock(&g_test_mutex);
}

static void test_async_task(void) {
  struct ov_error err = {0};

  mtx_init(&g_test_mutex, mtx_plain);
  g_counter = 0;

  struct gcmz_do_sub *ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx != NULL, &err)) {
    mtx_destroy(&g_test_mutex);
    return;
  }

  gcmz_do_sub_do(ctx, increment_counter, NULL);

  thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 100000000}, NULL);

  mtx_lock(&g_test_mutex);
  int count = g_counter;
  mtx_unlock(&g_test_mutex);

  TEST_CHECK(count == 1);

  gcmz_do_sub_destroy(&ctx);
  mtx_destroy(&g_test_mutex);
}

static void test_blocking_task(void) {
  struct ov_error err = {0};

  mtx_init(&g_test_mutex, mtx_plain);
  g_counter = 0;

  struct gcmz_do_sub *ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx != NULL, &err)) {
    mtx_destroy(&g_test_mutex);
    return;
  }

  gcmz_do_sub_do_blocking(ctx, increment_counter, NULL);

  mtx_lock(&g_test_mutex);
  int count = g_counter;
  mtx_unlock(&g_test_mutex);

  TEST_CHECK(count == 1);

  gcmz_do_sub_destroy(&ctx);
  mtx_destroy(&g_test_mutex);
}

static void test_sequential_tasks(void) {
  struct ov_error err = {0};

  mtx_init(&g_test_mutex, mtx_plain);
  g_counter = 0;

  struct gcmz_do_sub *ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx != NULL, &err)) {
    mtx_destroy(&g_test_mutex);
    return;
  }

  gcmz_do_sub_do_blocking(ctx, increment_counter, NULL);
  gcmz_do_sub_do_blocking(ctx, increment_counter, NULL);
  gcmz_do_sub_do_blocking(ctx, increment_counter, NULL);

  mtx_lock(&g_test_mutex);
  int count = g_counter;
  mtx_unlock(&g_test_mutex);

  TEST_CHECK(count == 3);

  gcmz_do_sub_destroy(&ctx);
  mtx_destroy(&g_test_mutex);
}

static void sleep_task(void *data) {
  int *flag = (int *)data;
  thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 200000000}, NULL);
  *flag = 1;
}

static void test_shutdown_while_running(void) {
  struct ov_error err = {0};

  int task_completed = 0;

  struct gcmz_do_sub *ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx != NULL, &err)) {
    return;
  }

  gcmz_do_sub_do(ctx, sleep_task, &task_completed);

  thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 50000000}, NULL);

  gcmz_do_sub_destroy(&ctx);

  TEST_CHECK(task_completed == 1);
}

static void test_shutdown_while_idle(void) {
  struct ov_error err = {0};

  struct gcmz_do_sub *ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(ctx != NULL, &err)) {
    return;
  }

  gcmz_do_sub_destroy(&ctx);

  TEST_CHECK(1);
}

static struct gcmz_do_sub *g_parallel_ctx = NULL;
static _Atomic int g_atomic_counter = 0;

static void atomic_increment_task(void *data) {
  int const iterations = *(int *)data;
  for (int i = 0; i < iterations; ++i) {
    atomic_fetch_add_explicit(&g_atomic_counter, 1, memory_order_relaxed);
  }
}

struct thread_context {
  int iterations;
  int thread_id;
};

static int parallel_caller_thread(void *arg) {
  struct thread_context *ctx = (struct thread_context *)arg;
  gcmz_do_sub_do_blocking(g_parallel_ctx, atomic_increment_task, &ctx->iterations);
  return 0;
}

static void test_parallel_execution(void) {
  struct ov_error err = {0};

  int const num_threads = 10;
  int const iterations_per_task = 100;
  int const expected_total = num_threads * iterations_per_task;

  atomic_store(&g_atomic_counter, 0);

  g_parallel_ctx = gcmz_do_sub_create(&err);
  if (!TEST_SUCCEEDED(g_parallel_ctx != NULL, &err)) {
    return;
  }

  thrd_t threads[10];
  struct thread_context contexts[10];

  for (int i = 0; i < num_threads; ++i) {
    contexts[i].iterations = iterations_per_task;
    contexts[i].thread_id = i;
    thrd_create(&threads[i], parallel_caller_thread, &contexts[i]);
  }

  for (int i = 0; i < num_threads; ++i) {
    thrd_join(threads[i], NULL);
  }

  int const final_count = atomic_load(&g_atomic_counter);

  TEST_CHECK(final_count == expected_total);
  TEST_MSG("Expected %d, got %d", expected_total, final_count);

  gcmz_do_sub_destroy(&g_parallel_ctx);
}

TEST_LIST = {
    {"test_create_with_null_error", test_create_with_null_error},
    {"test_create_success", test_create_success},
    {"test_double_create", test_double_create},
    {"test_async_task", test_async_task},
    {"test_blocking_task", test_blocking_task},
    {"test_sequential_tasks", test_sequential_tasks},
    {"test_shutdown_while_running", test_shutdown_while_running},
    {"test_shutdown_while_idle", test_shutdown_while_idle},
    {"test_parallel_execution", test_parallel_execution},
    {NULL, NULL},
};
