#include "file.h"

#include <ovarray.h>
#include <wchar.h>

struct gcmz_file_list {
  struct gcmz_file *files;
};

struct gcmz_file_list *gcmz_file_list_create(struct ov_error *const err) {
  struct gcmz_file_list *new_list = NULL;
  struct gcmz_file_list *result = NULL;

  {
    if (!OV_REALLOC(&new_list, 1, sizeof(*new_list))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    new_list->files = NULL;
    result = new_list;
    new_list = NULL;
  }

cleanup:
  if (new_list) {
    OV_FREE(&new_list);
  }
  return result;
}

void gcmz_file_list_destroy(struct gcmz_file_list **const list) {
  if (!list || !*list) {
    return;
  }

  struct gcmz_file_list *l = *list;
  if (l->files) {
    size_t const count = OV_ARRAY_LENGTH(l->files);
    for (size_t i = 0; i < count; i++) {
      if (l->files[i].path) {
        OV_ARRAY_DESTROY(&l->files[i].path);
      }
      if (l->files[i].mime_type) {
        OV_ARRAY_DESTROY(&l->files[i].mime_type);
      }
    }
    OV_ARRAY_DESTROY(&l->files);
  }

  OV_FREE((void **)list);
}

static NODISCARD bool file_list_add(struct gcmz_file_list *const list,
                                    wchar_t const *const path,
                                    wchar_t const *const mime_type,
                                    bool const temporary,
                                    struct ov_error *const err) {
  if (!list || !path) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct gcmz_file file = {
      .temporary = temporary,
  };
  bool result = false;

  {
    size_t const path_len = wcslen(path);
    if (!OV_ARRAY_GROW(&file.path, path_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(file.path, path);

    if (mime_type) {
      size_t const mime_len = wcslen(mime_type);
      if (!OV_ARRAY_GROW(&file.mime_type, mime_len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(file.mime_type, mime_type);
    }

    size_t const index = OV_ARRAY_LENGTH(list->files);
    if (!OV_ARRAY_GROW(&list->files, index + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    OV_ARRAY_SET_LENGTH(list->files, index + 1);

    list->files[index] = file;
    file = (struct gcmz_file){0};
  }

  result = true;

cleanup:
  if (file.path) {
    OV_ARRAY_DESTROY(&file.path);
  }
  if (file.mime_type) {
    OV_ARRAY_DESTROY(&file.mime_type);
  }
  return result;
}

NODISCARD bool gcmz_file_list_add(struct gcmz_file_list *const list,
                                  wchar_t const *const path,
                                  wchar_t const *const mime_type,
                                  struct ov_error *const err) {
  return file_list_add(list, path, mime_type, false, err);
}

NODISCARD bool gcmz_file_list_add_temporary(struct gcmz_file_list *const list,
                                            wchar_t const *const path,
                                            wchar_t const *const mime_type,
                                            struct ov_error *const err) {
  return file_list_add(list, path, mime_type, true, err);
}

NODISCARD bool
gcmz_file_list_remove(struct gcmz_file_list *const list, size_t const index, struct ov_error *const err) {
  if (!list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (!list->files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  size_t const count = OV_ARRAY_LENGTH(list->files);
  if (index >= count) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (list->files[index].path) {
    OV_ARRAY_DESTROY(&list->files[index].path);
  }
  if (list->files[index].mime_type) {
    OV_ARRAY_DESTROY(&list->files[index].mime_type);
  }

  for (size_t j = index; j < count - 1; j++) {
    list->files[j] = list->files[j + 1];
  }
  OV_ARRAY_SET_LENGTH(list->files, count - 1);

  return true;
}

size_t gcmz_file_list_count(struct gcmz_file_list const *const list) {
  if (!list || !list->files) {
    return 0;
  }
  return OV_ARRAY_LENGTH(list->files);
}

struct gcmz_file const *gcmz_file_list_get(struct gcmz_file_list const *const list, size_t const index) {
  if (!list || !list->files || index >= OV_ARRAY_LENGTH(list->files)) {
    return NULL;
  }
  return &list->files[index];
}

struct gcmz_file *gcmz_file_list_get_mutable(struct gcmz_file_list *const list, size_t const index) {
  if (!list || !list->files || index >= OV_ARRAY_LENGTH(list->files)) {
    return NULL;
  }
  return &list->files[index];
}

void gcmz_file_list_clear(struct gcmz_file_list *const list) {
  if (!list || !list->files) {
    return;
  }

  size_t const count = OV_ARRAY_LENGTH(list->files);
  for (size_t i = 0; i < count; i++) {
    if (list->files[i].path) {
      OV_ARRAY_DESTROY(&list->files[i].path);
    }
    if (list->files[i].mime_type) {
      OV_ARRAY_DESTROY(&list->files[i].mime_type);
    }
  }
  OV_ARRAY_SET_LENGTH(list->files, 0);
}
