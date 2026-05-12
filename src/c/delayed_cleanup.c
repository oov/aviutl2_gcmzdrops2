#include "delayed_cleanup.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ovarray.h>
#include <ovnum.h>
#include <ovthreads.h>

#include "file.h"

enum {
  delay_seconds = 30,
  worker_sleep_interval_seconds = 10,
};

enum thread_state {
  thread_state_stopped,
  thread_state_mtx_created,
  thread_state_cnd_created,
  thread_state_running,
  thread_state_stopping,
};

struct entry {
  wchar_t *file_path;
  uint64_t schedule_time_seconds;
};

struct context {
  struct entry *queue;
  mtx_t queue_mutex;
  cnd_t wake_condition;
  enum thread_state thread_state;
  thrd_t thread;
};

static uint64_t get_current_time_seconds(void) {
  struct timespec ts;
  if (timespec_get(&ts, TIME_UTC) == 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec;
}

static void entry_destroy(struct entry *const entry) {
  if (!entry) {
    return;
  }
  if (entry->file_path) {
    OV_ARRAY_DESTROY(&entry->file_path);
  }
}

static bool is_hex_digit(wchar_t const c) {
  return (L'0' <= c && c <= L'9') || (L'a' <= c && c <= L'f') || (L'A' <= c && c <= L'F');
}

static wchar_t const *find_parent_dir_name(wchar_t const *const path, wchar_t const **const parent_end) {
  if (!path || !parent_end) {
    return NULL;
  }

  wchar_t const *last_sep = NULL;
  for (wchar_t const *p = path; *p; ++p) {
    if (*p == L'\\' || *p == L'/') {
      last_sep = p;
    }
  }
  if (!last_sep || last_sep == path) {
    return NULL;
  }

  wchar_t const *parent_start = path;
  for (wchar_t const *p = path; p < last_sep; ++p) {
    if (*p == L'\\' || *p == L'/') {
      parent_start = p + 1;
    }
  }

  *parent_end = last_sep;
  return parent_start;
}

static bool parent_dir_name_is_unique_id(wchar_t const *const path, wchar_t const **const parent_end) {
  wchar_t const *end = NULL;
  wchar_t const *const name = find_parent_dir_name(path, &end);
  if (!name) {
    return false;
  }
  if ((size_t)(end - name) != 16) {
    return false;
  }
  for (wchar_t const *p = name; p < end; ++p) {
    if (!is_hex_digit(*p)) {
      return false;
    }
  }
  if (parent_end) {
    *parent_end = end;
  }
  return true;
}

static bool is_current_temp_unique_parent_directory(wchar_t const *const parent_path) {
  if (!parent_path) {
    return false;
  }

  wchar_t temp_path[MAX_PATH];
  DWORD const temp_len = GetTempPathW(sizeof(temp_path) / sizeof(temp_path[0]), temp_path);
  if (temp_len == 0 || temp_len >= sizeof(temp_path) / sizeof(temp_path[0])) {
    return false;
  }

  wchar_t prefix[MAX_PATH + 64];
  wcscpy(prefix, temp_path);
  wcscat(prefix, L"gcmzdrops");
  wchar_t pid_buf[32];
  wcscat(prefix, ov_utoa_wchar(GetCurrentProcessId(), pid_buf));
  wcscat(prefix, L"\\");

  size_t const prefix_len = wcslen(prefix);
  if (wcsncmp(parent_path, prefix, prefix_len) != 0) {
    return false;
  }
  wchar_t const *const dirname = parent_path + prefix_len;
  if (wcslen(dirname) != 16) {
    return false;
  }
  for (wchar_t const *p = dirname; *p; ++p) {
    if (!is_hex_digit(*p)) {
      return false;
    }
  }
  return true;
}

static void remove_empty_unique_parent_directory(wchar_t const *const file_path) {
  wchar_t const *parent_end = NULL;
  if (!parent_dir_name_is_unique_id(file_path, &parent_end)) {
    return;
  }

  wchar_t *parent_path = NULL;
  {
    size_t const parent_len = (size_t)(parent_end - file_path);
    if (!OV_ARRAY_GROW(&parent_path, parent_len + 1)) {
      return;
    }
    wcsncpy(parent_path, file_path, parent_len);
    parent_path[parent_len] = L'\0';
    if (is_current_temp_unique_parent_directory(parent_path)) {
      RemoveDirectoryW(parent_path);
    }
  }

  if (parent_path) {
    OV_ARRAY_DESTROY(&parent_path);
  }
}

static bool delete_scheduled_file(wchar_t const *const file_path) {
  if (!file_path) {
    return true;
  }

  if (DeleteFileW(file_path)) {
    remove_empty_unique_parent_directory(file_path);
    return true;
  }

  HRESULT const hr = HRESULT_FROM_WIN32(GetLastError());
  if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
    remove_empty_unique_parent_directory(file_path);
    return true;
  }

  return false;
}

static NODISCARD bool
entry_create(struct entry *const entry, wchar_t const *const file_path, struct ov_error *const err) {
  if (!entry || !file_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  if (!OV_ARRAY_GROW(&entry->file_path, wcslen(file_path) + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  wcscpy(entry->file_path, file_path);
  entry->schedule_time_seconds = get_current_time_seconds();

  result = true;

cleanup:
  if (!result) {
    entry_destroy(entry);
  }
  return result;
}

static void process_queue(struct context *const ctx, uint64_t const delete_threshold_seconds) {
  if (!ctx) {
    return;
  }
  size_t const ln = OV_ARRAY_LENGTH(ctx->queue);
  size_t wi = 0;
  for (size_t ri = 0; ri < ln; ++ri) {
    struct entry *const entry = &ctx->queue[ri];

    if (entry->schedule_time_seconds <= delete_threshold_seconds) {
      if (delete_scheduled_file(entry->file_path)) {
        entry_destroy(entry);
      } else {
        if (wi != ri) {
          ctx->queue[wi] = *entry;
        }
        ++wi;
      }
    } else {
      if (wi != ri) {
        ctx->queue[wi] = *entry;
      }
      ++wi;
    }
  }

  OV_ARRAY_SET_LENGTH(ctx->queue, wi);
}

static int worker_thread_proc(void *arg) {
  struct context *const ctx = (struct context *)arg;
  if (!ctx) {
    return -1;
  }
  if (mtx_lock(&ctx->queue_mutex) != thrd_success) {
    return -1;
  }
  while (ctx->thread_state == thread_state_running) {
    process_queue(ctx, get_current_time_seconds() - delay_seconds);
    struct timespec timeout_ts;
    if (timespec_get(&timeout_ts, TIME_UTC) == 0) {
      break;
    }
    timeout_ts.tv_sec += worker_sleep_interval_seconds;
    cnd_timedwait(&ctx->wake_condition, &ctx->queue_mutex, &timeout_ts);
  }

  process_queue(ctx, get_current_time_seconds() + 1); // delete all remaining files
  mtx_unlock(&ctx->queue_mutex);

  return 0;
}

static void context_destroy(struct context **const ctxpp) {
  if (!ctxpp || !*ctxpp) {
    return;
  }
  struct context *const ctx = *ctxpp;

  bool has_running_thread = false;
  if (mtx_lock(&ctx->queue_mutex) == thrd_success) {
    has_running_thread = ctx->thread_state == thread_state_running;
    if (has_running_thread) {
      ctx->thread_state = thread_state_stopping;
      cnd_signal(&ctx->wake_condition);
    }
    mtx_unlock(&ctx->queue_mutex);
  }
  if (has_running_thread) {
    thrd_join(ctx->thread, NULL);
  }
  if (ctx->thread_state >= thread_state_cnd_created) {
    cnd_destroy(&ctx->wake_condition);
  }
  if (ctx->thread_state >= thread_state_mtx_created) {
    mtx_destroy(&ctx->queue_mutex);
  }
  if (ctx->queue) {
    size_t const ln = OV_ARRAY_LENGTH(ctx->queue);
    for (size_t i = 0; i < ln; ++i) {
      if (ctx->queue[i].file_path) {
        delete_scheduled_file(ctx->queue[i].file_path);
      }
      entry_destroy(&ctx->queue[i]);
    }
    OV_ARRAY_DESTROY(&ctx->queue);
  }
  OV_FREE(ctxpp);
}

static NODISCARD struct context *context_create(struct ov_error *const err) {
  struct context *ctx = NULL;
  struct context *result = NULL;
  int r = 0;

  if (!OV_REALLOC(&ctx, 1, sizeof(struct context))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  *ctx = (struct context){0};

  if (mtx_init(&ctx->queue_mutex, mtx_plain) != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  ctx->thread_state = thread_state_mtx_created;

  if (cnd_init(&ctx->wake_condition) != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  ctx->thread_state = thread_state_cnd_created;

  if (mtx_lock(&ctx->queue_mutex) != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }

  r = thrd_create(&ctx->thread, worker_thread_proc, ctx);
  if (r == thrd_success) {
    ctx->thread_state = thread_state_running;
    cnd_signal(&ctx->wake_condition);
  }
  mtx_unlock(&ctx->queue_mutex);

  if (r != thrd_success) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }

  result = ctx;
  ctx = NULL;

cleanup:
  if (ctx) {
    context_destroy(&ctx);
  }
  return result;
}

static NODISCARD bool
context_schedule_file(struct context *const ctx, wchar_t const *const file_path, struct ov_error *const err) {
  if (!ctx || !file_path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (ctx->thread_state != thread_state_running) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }

  struct entry entry = {0};
  bool result = false;

  {
    if (!entry_create(&entry, file_path, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (mtx_lock(&ctx->queue_mutex) != thrd_success) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    size_t const queue_length = OV_ARRAY_LENGTH(ctx->queue);
    if (!OV_ARRAY_GROW(&ctx->queue, queue_length + 1)) {
      mtx_unlock(&ctx->queue_mutex);
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    OV_ARRAY_SET_LENGTH(ctx->queue, queue_length + 1);
    ctx->queue[queue_length] = entry;
    entry = (struct entry){0};
    cnd_signal(&ctx->wake_condition);
    mtx_unlock(&ctx->queue_mutex);
  }

  result = true;

cleanup:
  entry_destroy(&entry);
  return result;
}

static NODISCARD bool
context_schedule_files(struct context *const ctx, struct gcmz_file_list *const files, struct ov_error *const err) {
  if (!ctx || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (ctx->thread_state != thread_state_running) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }

  bool result = false;

  {
    size_t const count = gcmz_file_list_count(files);
    for (size_t i = 0; i < count; ++i) {
      struct gcmz_file *file = gcmz_file_list_get_mutable(files, i);
      if (file && file->temporary && file->path) {
        if (!context_schedule_file(ctx, file->path, err)) {
          OV_ERROR_ADD_TRACE(err);
          goto cleanup;
        }
        file->temporary = false;
      }
    }
  }

  result = true;

cleanup:
  return result;
}

static struct context *g_singleton_context = NULL;

NODISCARD bool gcmz_delayed_cleanup_init(struct ov_error *const err) {
  if (g_singleton_context) {
    return true;
  }
  g_singleton_context = context_create(err);
  return g_singleton_context != NULL;
}

void gcmz_delayed_cleanup_exit(void) { context_destroy(&g_singleton_context); }

NODISCARD bool gcmz_delayed_cleanup_schedule_file(wchar_t const *const file_path, struct ov_error *const err) {
  if (!g_singleton_context) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }
  return context_schedule_file(g_singleton_context, file_path, err);
}

NODISCARD bool gcmz_delayed_cleanup_schedule_temporary_files(struct gcmz_file_list *const files,
                                                             struct ov_error *const err) {
  if (!g_singleton_context) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }
  return context_schedule_files(g_singleton_context, files, err);
}
