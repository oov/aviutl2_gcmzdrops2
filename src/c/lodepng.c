#include "lodepng.h"

#include <ovbase.h>

static void *lodepng_malloc(size_t size) {
  if (!size) {
    return NULL;
  }
  void *ptr = NULL;
  if (!OV_REALLOC(&ptr, 1, size)) {
    return NULL;
  }
  return ptr;
}

/* NOTE: when realloc returns NULL, it leaves the original memory untouched */
static void *lodepng_realloc(void *ptr, size_t new_size) {
  if (!new_size) {
    OV_FREE(&ptr);
    return NULL;
  }
  if (!OV_REALLOC(&ptr, 1, new_size)) {
    return NULL;
  }
  return ptr;
}

static void lodepng_free(void *ptr) {
  if (ptr) {
    OV_FREE(&ptr);
  }
}

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wextra-semi-stmt")
#    pragma GCC diagnostic ignored "-Wextra-semi-stmt"
#  endif
#  if __has_warning("-Wimplicit-int-conversion")
#    pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#  endif
#  if __has_warning("-Wcovered-switch-default")
#    pragma GCC diagnostic ignored "-Wcovered-switch-default"
#  endif
#  if __has_warning("-Wmissing-prototypes")
#    pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  endif
#endif // __GNUC__
#include "3rd/lodepng/lodepng.cpp"
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__
