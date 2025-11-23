#include "window_list.h"

#include "gcmz_types.h"

#include <ovsort.h>

#include <string.h>

enum { max_windows = 8 };

struct gcmz_window_list {
  struct gcmz_window_info items[max_windows];
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
  struct gcmz_window_info const *const wa = (struct gcmz_window_info const *)a;
  struct gcmz_window_info const *const wb = (struct gcmz_window_info const *)b;
  if (wa->window < wb->window) {
    return -1;
  }
  if (wa->window > wb->window) {
    return 1;
  }
  return 0;
}

ov_tribool gcmz_window_list_update(struct gcmz_window_list *const wl,
                                   struct gcmz_window_info const *const windows,
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
  struct gcmz_window_info sorted[max_windows];
  memcpy(sorted, windows, num_windows * sizeof(struct gcmz_window_info));
  ov_qsort(sorted, num_windows, sizeof(struct gcmz_window_info), compare_windows, NULL);

  bool changed = (wl->num_items != num_windows);
  if (!changed) {
    for (size_t i = 0; i < num_windows; ++i) {
      if (wl->items[i].window != sorted[i].window) {
        changed = true;
        break;
      }
    }
  }

  // Update list contents
  // Even if not changed, update sizes as they may have changed
  memcpy(wl->items, sorted, num_windows * sizeof(struct gcmz_window_info));
  wl->num_items = num_windows;
  return changed ? ov_true : ov_false;
}

struct gcmz_window_info const *gcmz_window_list_get(struct gcmz_window_list const *const wl,
                                                    size_t *const num_windows) {
  if (!wl || !num_windows) {
    return NULL;
  }
  *num_windows = wl->num_items;
  return wl->items;
}
