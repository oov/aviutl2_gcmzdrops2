#include "json.h"

static void *yj_malloc(void *ctx, size_t size) {
  (void)ctx;
  void *ptr = NULL;
  if (!OV_REALLOC(&ptr, size, sizeof(char))) {
    return NULL;
  }
  return ptr;
}

static void *yj_realloc(void *ctx, void *ptr, size_t old_size, size_t size) {
  (void)ctx;
  (void)old_size;
  if (!OV_REALLOC(&ptr, size, sizeof(char))) {
    return NULL;
  }
  return ptr;
}

static void yj_free(void *ctx, void *ptr) {
  (void)ctx;
  OV_FREE(&ptr);
}

struct yyjson_alc const *gcmz_json_get_alc(void) {
  static struct yyjson_alc const alc = {
      .malloc = yj_malloc,
      .realloc = yj_realloc,
      .free = yj_free,
      .ctx = NULL,
  };
  return &alc;
}
