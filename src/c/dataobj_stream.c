#include "dataobj_stream.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#include <ole2.h>
#include <string.h>

#include <ovl/source.h>
#include <ovl/source/file.h>

struct source_hglobal {
  struct ovl_source_vtable const *vtable;
  STGMEDIUM stgmedium;
  void *locked_ptr;
  uint64_t size;
};

static void source_hglobal_destroy(struct ovl_source **const sp) {
  struct source_hglobal **const shgp = (struct source_hglobal **)sp;
  if (!shgp || !*shgp) {
    return;
  }
  struct source_hglobal *const shg = *shgp;
  if (shg->locked_ptr && shg->stgmedium.hGlobal) {
    GlobalUnlock(shg->stgmedium.hGlobal);
    shg->locked_ptr = NULL;
  }
  ReleaseStgMedium(&shg->stgmedium);
  OV_FREE(sp);
}

static size_t source_hglobal_read(struct ovl_source *const s, void *const p, uint64_t const offset, size_t const len) {
  struct source_hglobal *const shg = (struct source_hglobal *)s;
  if (!shg || !shg->locked_ptr || offset > shg->size || len == SIZE_MAX) {
    return SIZE_MAX;
  }
  size_t const real_len = offset + len > shg->size ? (size_t)(shg->size - offset) : len;
  if (real_len == 0) {
    return 0;
  }
  memcpy(p, (const char *)shg->locked_ptr + offset, real_len);
  return real_len;
}

static uint64_t source_hglobal_size(struct ovl_source *const s) {
  struct source_hglobal *const shg = (struct source_hglobal *)s;
  if (!shg || !shg->locked_ptr) {
    return UINT64_MAX;
  }
  return shg->size;
}

static NODISCARD bool
source_hglobal_create(STGMEDIUM const *sm, struct ovl_source **const sp, struct ov_error *const err) {
  if (!sm || sm->tymed != TYMED_HGLOBAL || !sm->hGlobal || !sp) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct source_hglobal *shg = NULL;
  void *locked_ptr = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&shg, 1, sizeof(*shg))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    locked_ptr = GlobalLock(sm->hGlobal);
    if (!locked_ptr) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    SIZE_T const sz = GlobalSize(sm->hGlobal);
    if (sz == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    static struct ovl_source_vtable const vtable = {
        .destroy = source_hglobal_destroy,
        .read = source_hglobal_read,
        .size = source_hglobal_size,
    };

    *shg = (struct source_hglobal){
        .vtable = &vtable,
        .stgmedium = *sm,
        .locked_ptr = locked_ptr,
        .size = sz,
    };

    *sp = (struct ovl_source *)shg;
    shg = NULL;
    locked_ptr = NULL;
  }

  result = true;

cleanup:
  if (locked_ptr) {
    GlobalUnlock(sm->hGlobal);
  }
  if (shg) {
    source_hglobal_destroy((struct ovl_source **)&shg);
  }
  return result;
}

struct source_file {
  struct ovl_source_vtable const *vtable;
  STGMEDIUM stgmedium;
  struct ovl_source *file_source;
};

static void source_file_destroy(struct ovl_source **const sp) {
  struct source_file **const sfp = (struct source_file **)sp;
  if (!sfp || !*sfp) {
    return;
  }
  struct source_file *const sf = *sfp;
  if (sf->file_source) {
    ovl_source_destroy(&sf->file_source);
  }
  ReleaseStgMedium(&sf->stgmedium);
  OV_FREE(sp);
}

static size_t source_file_read(struct ovl_source *const s, void *const p, uint64_t const offset, size_t const len) {
  struct source_file *const sf = (struct source_file *)s;
  if (!sf || !sf->file_source) {
    return SIZE_MAX;
  }
  return ovl_source_read(sf->file_source, p, offset, len);
}

static uint64_t source_file_size(struct ovl_source *const s) {
  struct source_file *const sf = (struct source_file *)s;
  if (!sf || !sf->file_source) {
    return UINT64_MAX;
  }
  return ovl_source_size(sf->file_source);
}

static NODISCARD bool
source_file_create(STGMEDIUM const *sm, struct ovl_source **const sp, struct ov_error *const err) {
  if (!sm || sm->tymed != TYMED_FILE || !sm->lpszFileName || !sp) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct source_file *sf = NULL;
  bool result = false;

  {
    if (!OV_REALLOC(&sf, 1, sizeof(*sf))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    if (!ovl_source_file_create(sm->lpszFileName, &sf->file_source, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    static struct ovl_source_vtable const vtable = {
        .destroy = source_file_destroy,
        .read = source_file_read,
        .size = source_file_size,
    };

    *sf = (struct source_file){
        .vtable = &vtable,
        .stgmedium = *sm,
        .file_source = sf->file_source,
    };

    *sp = (struct ovl_source *)sf;
    sf = NULL;
  }

  result = true;

cleanup:
  if (sf) {
    source_file_destroy((struct ovl_source **)&sf);
  }
  return result;
}

struct source_istream {
  struct ovl_source_vtable const *vtable;
  STGMEDIUM stgmedium;
  uint64_t size;
};

static void source_istream_destroy(struct ovl_source **const sp) {
  struct source_istream **const sisp = (struct source_istream **)sp;
  if (!sisp || !*sisp) {
    return;
  }
  struct source_istream *const sis = *sisp;
  ReleaseStgMedium(&sis->stgmedium);
  OV_FREE(sp);
}

static size_t source_istream_read(struct ovl_source *const s, void *const p, uint64_t const offset, size_t const len) {
  struct source_istream *const sis = (struct source_istream *)s;
  if (!sis || !sis->stgmedium.pstm || offset > sis->size || len == SIZE_MAX) {
    return SIZE_MAX;
  }

  size_t const real_len = offset + len > sis->size ? (size_t)(sis->size - offset) : len;
  if (real_len == 0) {
    return 0;
  }

  LARGE_INTEGER li = {0};
  li.QuadPart = (LONGLONG)offset;
  HRESULT hr = IStream_Seek(sis->stgmedium.pstm, li, STREAM_SEEK_SET, NULL);
  if (FAILED(hr)) {
    return SIZE_MAX;
  }

  ULONG bytes_read = 0;
  hr = IStream_Read(sis->stgmedium.pstm, p, (ULONG)real_len, &bytes_read);
  if (FAILED(hr)) {
    return SIZE_MAX;
  }

  return bytes_read;
}

static uint64_t source_istream_size(struct ovl_source *const s) {
  struct source_istream *const sis = (struct source_istream *)s;
  if (!sis || !sis->stgmedium.pstm) {
    return UINT64_MAX;
  }
  return sis->size;
}

static NODISCARD bool
source_istream_create(STGMEDIUM const *sm, struct ovl_source **const sp, struct ov_error *const err) {
  if (!sm || sm->tymed != TYMED_ISTREAM || !sm->pstm || !sp) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct source_istream *sis = NULL;
  STATSTG statstg = {0};
  bool result = false;

  {
    if (!OV_REALLOC(&sis, 1, sizeof(*sis))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    HRESULT hr = IStream_Stat(sm->pstm, &statstg, STATFLAG_NONAME);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }

    static struct ovl_source_vtable const vtable = {
        .destroy = source_istream_destroy,
        .read = source_istream_read,
        .size = source_istream_size,
    };

    *sis = (struct source_istream){
        .vtable = &vtable,
        .stgmedium = *sm,
        .size = statstg.cbSize.QuadPart,
    };

    *sp = (struct ovl_source *)sis;
    sis = NULL;
  }

  result = true;

cleanup:
  if (sis) {
    source_istream_destroy((struct ovl_source **)&sis);
  }
  return result;
}

NODISCARD bool gcmz_dataobj_source_create(void *const dataobj,
                                          void const *const formatetc,
                                          struct ovl_source **const sp,
                                          struct ov_error *const err) {
  if (!dataobj || !formatetc || !sp) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  STGMEDIUM sm = {0};
  bool sm_initialized = false;
  bool result = false;

  {
    FORMATETC fmt = *(FORMATETC const *)formatetc;
    HRESULT const hr = IDataObject_GetData((IDataObject *)dataobj, &fmt, &sm);
    if (FAILED(hr)) {
      OV_ERROR_SET_HRESULT(err, hr);
      goto cleanup;
    }
    sm_initialized = true;

    typedef NODISCARD bool (*source_create_func)(
        STGMEDIUM const *sm, struct ovl_source **const sp, struct ov_error *const err);
    source_create_func create_func = NULL;
    switch (sm.tymed) {
    case TYMED_HGLOBAL:
      create_func = source_hglobal_create;
      break;
    case TYMED_FILE:
      create_func = source_file_create;
      break;
    case TYMED_ISTREAM:
      create_func = source_istream_create;
      break;
    default:
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    if (!create_func(&sm, sp, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    sm_initialized = false; // ownership transferred
  }

  result = true;

cleanup:
  if (sm_initialized) {
    ReleaseStgMedium(&sm);
  }
  return result;
}
