#include "window_list.h"

#include <ovsort.h>

#include <string.h>

enum { max_windows = 16 };

struct gcmz_window_list {
  void *items[max_windows];
  size_t num_items;
};

struct gcmz_window_list *gcmz_window_list_create(struct ov_error *const err) {
  struct gcmz_window_list *wl = NULL;

  if (!OV_REALLOC(&wl, 1, sizeof(struct gcmz_window_list))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    return NULL;
  }

  *wl = (struct gcmz_window_list){
      .items = {0},
      .num_items = 0,
  };

  return wl;
}

void gcmz_window_list_destroy(struct gcmz_window_list **const wl) {
  if (!wl || !*wl) {
    return;
  }
  OV_FREE(wl);
}

static int compare_windows(void const *const a, void const *const b, void *const userdata) {
  (void)userdata;
  void *const *const wa = (void *const *)a;
  void *const *const wb = (void *const *)b;
  if (*wa < *wb) {
    return -1;
  }
  if (*wa > *wb) {
    return 1;
  }
  return 0;
}

ov_tribool gcmz_window_list_update(struct gcmz_window_list *const wl,
                                   void *const *const windows,
                                   size_t const num_windows,
                                   struct ov_error *const err) {
  if (!wl || !windows) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return ov_indeterminate;
  }
  if (num_windows > max_windows) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return ov_indeterminate;
  }

  // Create sorted copy of input windows
  void *sorted[max_windows];
  memcpy(sorted, windows, num_windows * sizeof(void *));
  ov_qsort(sorted, num_windows, sizeof(void *), compare_windows, NULL);

  bool changed = (wl->num_items != num_windows);
  if (!changed) {
    for (size_t i = 0; i < num_windows; ++i) {
      if (wl->items[i] != sorted[i]) {
        changed = true;
        break;
      }
    }
  }

  // Update list contents
  memcpy(wl->items, sorted, num_windows * sizeof(void *));
  wl->num_items = num_windows;
  return changed ? ov_true : ov_false;
}
