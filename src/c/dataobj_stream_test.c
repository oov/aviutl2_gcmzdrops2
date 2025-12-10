static void test_init(void);
static void test_cleanup(void);

#define TEST_MY_INIT test_init()
#define TEST_MY_FINI test_cleanup()

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#include <ole2.h>

#include <ovtest.h>

#include <ovarray.h>
#include <ovbase.h>
#include <ovl/file.h>
#include <ovl/source.h>

#include "dataobj_stream.h"

static void test_init(void) {
  HRESULT hr = CoInitialize(NULL);
  if (FAILED(hr)) {
    fprintf(stderr, "Failed to initialize COM: 0x%08lx\n", (unsigned long)hr);
    abort();
  }
}

static void test_cleanup(void) { CoUninitialize(); }

static NATIVE_CHAR *create_test_file(NATIVE_CHAR const *filename, char const *data, struct ov_error *const err) {
  if (!filename || !data) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  struct ovl_file *f = NULL;
  NATIVE_CHAR *path = NULL;
  size_t const data_len = strlen(data);
  NATIVE_CHAR *result = NULL;

  if (!ovl_file_create_temp(filename, &f, &path, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (data && data_len > 0) {
    size_t written = 0;
    if (!ovl_file_write(f, data, data_len, &written, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (written != data_len) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to write all data to temp file");
      goto cleanup;
    }
  }

  result = path;
  path = NULL;

cleanup:
  if (f) {
    ovl_file_close(f);
    f = NULL;
  }
  if (path) {
    DeleteFileW(path);
    OV_ARRAY_DESTROY(&path);
  }
  return result;
}

struct mock_data_object {
  IDataObject iface;
  LONG ref_count;
  CLIPFORMAT format;
  DWORD tymed;
  HRESULT get_data_result; ///< Control what GetData returns
  bool ignore_tymed_check; ///< Skip TYMED compatibility check (simulate bad IDataObjects)
  union {
    struct {
      void *data;
      size_t data_len;
    }; ///< For TYMED_HGLOBAL and TYMED_ISTREAM
    wchar_t *file_path; ///< For TYMED_FILE
  };
};

static HRESULT STDMETHODCALLTYPE mock_data_object_query_interface(IDataObject *iface, REFIID riid, void **ppv) {
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
    *ppv = iface;
    IDataObject_AddRef(iface);
    return S_OK;
  }
  *ppv = NULL;
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mock_data_object_add_ref(IDataObject *iface) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;
  return (ULONG)InterlockedIncrement(&mock->ref_count);
}

static ULONG STDMETHODCALLTYPE mock_data_object_release(IDataObject *iface) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;
  ULONG ref = (ULONG)InterlockedDecrement(&mock->ref_count);
  if (ref == 0) {
    switch (mock->tymed) {
    case TYMED_HGLOBAL:
    case TYMED_ISTREAM:
      OV_FREE(&mock->data);
      break;
    case TYMED_FILE:
      if (mock->file_path) {
        DeleteFileW(mock->file_path);
        OV_ARRAY_DESTROY(&mock->file_path);
      }
      break;
    }
    OV_FREE((void **)&mock);
  }
  return ref;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_get_data(IDataObject *iface,
                                                           FORMATETC *pformatetc,
                                                           STGMEDIUM *pmedium) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;
  if (FAILED(mock->get_data_result)) {
    return mock->get_data_result;
  }
  if (pformatetc->cfFormat != mock->format) {
    return DV_E_FORMATETC;
  }
  if (!mock->ignore_tymed_check && !(pformatetc->tymed & mock->tymed)) {
    return DV_E_TYMED;
  }
  ZeroMemory(pmedium, sizeof(*pmedium));
  pmedium->tymed = mock->tymed;
  pmedium->pUnkForRelease = NULL;
  switch (mock->tymed) {
  case TYMED_HGLOBAL: {
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, mock->data_len);
    if (!hGlobal) {
      return E_OUTOFMEMORY;
    }
    void *p = GlobalLock(hGlobal);
    if (!p) {
      GlobalFree(hGlobal);
      return E_OUTOFMEMORY;
    }
    if (mock->data && mock->data_len > 0) {
      memcpy(p, mock->data, mock->data_len);
    }
    GlobalUnlock(hGlobal);
    pmedium->hGlobal = hGlobal;
    break;
  }
  case TYMED_FILE: {
    if (!mock->file_path) {
      return E_FAIL;
    }
    size_t len = wcslen(mock->file_path) + 1;
    wchar_t *file_copy = (wchar_t *)CoTaskMemAlloc(len * sizeof(wchar_t));
    if (!file_copy) {
      return E_OUTOFMEMORY;
    }
    wcscpy(file_copy, mock->file_path);
    pmedium->lpszFileName = file_copy;
    break;
  }
  case TYMED_ISTREAM: {
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, mock->data_len);
    if (!hGlobal) {
      return E_OUTOFMEMORY;
    }
    void *p = GlobalLock(hGlobal);
    if (!p) {
      GlobalFree(hGlobal);
      return E_OUTOFMEMORY;
    }
    if (mock->data && mock->data_len > 0) {
      memcpy(p, mock->data, mock->data_len);
    }
    GlobalUnlock(hGlobal);
    IStream *stream = NULL;
    HRESULT hr = CreateStreamOnHGlobal(hGlobal, TRUE, &stream);
    if (FAILED(hr)) {
      GlobalFree(hGlobal);
      return hr;
    }
    pmedium->pstm = stream;
    break;
  }
  default:
    return DV_E_TYMED;
  }

  return S_OK;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_get_data_here(IDataObject *iface,
                                                                FORMATETC *pformatetc,
                                                                STGMEDIUM *pmedium) {
  (void)iface;
  (void)pformatetc;
  (void)pmedium;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_query_get_data(IDataObject *iface, FORMATETC *pformatetc) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;
  if (pformatetc->cfFormat == mock->format && (pformatetc->tymed & mock->tymed)) {
    return S_OK;
  }
  return DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_get_canonical_format_etc(IDataObject *iface,
                                                                           FORMATETC *pformatectIn,
                                                                           FORMATETC *pformatetcOut) {
  (void)iface;
  (void)pformatectIn;
  (void)pformatetcOut;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_set_data(IDataObject *iface,
                                                           FORMATETC *pformatetc,
                                                           STGMEDIUM *pmedium,
                                                           BOOL fRelease) {
  (void)iface;
  (void)pformatetc;
  (void)pmedium;
  (void)fRelease;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_enum_format_etc(IDataObject *iface,
                                                                  DWORD dwDirection,
                                                                  IEnumFORMATETC **ppenumFormatEtc) {
  (void)iface;
  (void)dwDirection;
  (void)ppenumFormatEtc;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_d_advise(
    IDataObject *iface, FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) {
  (void)iface;
  (void)pformatetc;
  (void)advf;
  (void)pAdvSink;
  (void)pdwConnection;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_d_unadvise(IDataObject *iface, DWORD dwConnection) {
  (void)iface;
  (void)dwConnection;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mock_data_object_enum_d_advise(IDataObject *iface, IEnumSTATDATA **ppenumAdvise) {
  (void)iface;
  (void)ppenumAdvise;
  return E_NOTIMPL;
}

static const IDataObjectVtbl mock_data_object_vtbl = {
    mock_data_object_query_interface,
    mock_data_object_add_ref,
    mock_data_object_release,
    mock_data_object_get_data,
    mock_data_object_get_data_here,
    mock_data_object_query_get_data,
    mock_data_object_get_canonical_format_etc,
    mock_data_object_set_data,
    mock_data_object_enum_format_etc,
    mock_data_object_d_advise,
    mock_data_object_d_unadvise,
    mock_data_object_enum_d_advise,
};

static struct mock_data_object *create_mock_dataobject_core(CLIPFORMAT format, DWORD tymed, HRESULT get_data_result) {
  struct mock_data_object *mock = NULL;
  if (!OV_REALLOC(&mock, 1, sizeof(*mock))) {
    return NULL;
  }
  *mock = (struct mock_data_object){
      .iface =
          {
              .lpVtbl = &mock_data_object_vtbl,
          },
      .ref_count = 1,
      .format = format,
      .tymed = tymed,
      .get_data_result = get_data_result,
  };
  return mock;
}
static struct mock_data_object *create_mock_dataobject(CLIPFORMAT format, DWORD tymed, char const *data) {
  struct mock_data_object *mock = create_mock_dataobject_core(format, tymed, S_OK);
  if (!mock) {
    return NULL;
  }
  if (!data) {
    return mock;
  }
  if (tymed == TYMED_FILE) {
    struct ov_error err = {0};
    NATIVE_CHAR *path = create_test_file(L"test_mock_file.txt", data, &err);
    if (!path) {
      OV_ERROR_DESTROY(&err);
      IDataObject_Release(&mock->iface);
      return NULL;
    }
    mock->file_path = path;
  } else if (tymed == TYMED_HGLOBAL || tymed == TYMED_ISTREAM) {
    size_t const data_len = strlen(data);
    if (data_len > 0) {
      if (!OV_REALLOC(&mock->data, 1, data_len)) {
        IDataObject_Release(&mock->iface);
        return NULL;
      }
      memcpy(mock->data, data, data_len);
    }
    mock->data_len = data_len;
  }

  return mock;
}

static struct mock_data_object *create_ignoring_tymed_mock(CLIPFORMAT format, DWORD tymed, char const *const data) {
  struct mock_data_object *mock = create_mock_dataobject(format, tymed, data);
  if (mock) {
    mock->ignore_tymed_check = true;
  }
  return mock;
}

static void test_dataobj_source_create_null_params(void) {
  struct ovl_source *source = NULL;
  struct ov_error err = {0};
  void *dataobj = (void *)0x1; // dummy non-NULL pointer
  FORMATETC formatetc = {0};

  TEST_FAILED_WITH(gcmz_dataobj_source_create(NULL, &formatetc, &source, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_dataobj_source_create(dataobj, NULL, &source, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  TEST_FAILED_WITH(gcmz_dataobj_source_create(dataobj, &formatetc, NULL, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
}

static void test_dataobj_source_create_getdata_fail(void) {
  struct ovl_source *source = NULL;
  struct ov_error err = {0};
  struct mock_data_object *mock = create_mock_dataobject_core(CF_TEXT, TYMED_HGLOBAL, E_FAIL);
  if (!TEST_CHECK(mock != NULL)) {
    return;
  }
  TEST_FAILED_WITH(gcmz_dataobj_source_create(&mock->iface,
                                              &(FORMATETC){
                                                  .cfFormat = CF_TEXT,
                                                  .dwAspect = DVASPECT_CONTENT,
                                                  .lindex = -1,
                                                  .tymed = TYMED_HGLOBAL,
                                              },
                                              &source,
                                              &err),
                   &err,
                   ov_error_type_hresult,
                   E_FAIL);
  TEST_CHECK(source == NULL);
  IDataObject_Release(&mock->iface);
}

static void verify_source_creation(CLIPFORMAT format, DWORD tymed, char const *test_data) {
  size_t const data_len = strlen(test_data);
  char buffer[64] = {0};
  struct mock_data_object *mock = NULL;
  struct ovl_source *source = NULL;
  struct ov_error err = {0};

  mock = create_mock_dataobject(format, tymed, test_data);
  if (!TEST_CHECK(mock != NULL)) {
    goto cleanup;
  }

  if (!TEST_SUCCEEDED(gcmz_dataobj_source_create(&mock->iface,
                                                 &(FORMATETC){
                                                     .cfFormat = format,
                                                     .dwAspect = DVASPECT_CONTENT,
                                                     .lindex = -1,
                                                     .tymed = tymed,
                                                 },
                                                 &source,
                                                 &err),
                      &err)) {
    goto cleanup;
  }
  if (!TEST_CHECK(source != NULL)) {
    goto cleanup;
  }

  TEST_CHECK(ovl_source_read(source, buffer, 0, sizeof(buffer) - 1) == data_len);
  TEST_CHECK(memcmp(buffer, test_data, data_len) == 0);
  TEST_CHECK(ovl_source_size(source) == data_len);

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  if (mock) {
    IDataObject_Release(&mock->iface);
  }
}

static void test_dataobj_source_create_with_tymed(void) {
  static char const test_data[] = "Test data content";
  TEST_CASE("TYMED_HGLOBAL");
  verify_source_creation(CF_TEXT, TYMED_HGLOBAL, test_data);
  TEST_CASE("TYMED_FILE");
  verify_source_creation(CF_HDROP, TYMED_FILE, test_data);
  TEST_CASE("TYMED_ISTREAM");
  verify_source_creation(CF_TEXT, TYMED_ISTREAM, test_data);
}

static void verify_source_with_mismatched_tymed(DWORD actual_tymed, CLIPFORMAT format, DWORD requested_tymed) {
  static char const test_data[] = "TYMED mismatch test";
  size_t const data_len = sizeof(test_data) - 1;

  char buffer[64] = {0};
  struct mock_data_object *mock = NULL;
  struct ovl_source *source = NULL;
  struct ov_error err = {0};

  mock = create_ignoring_tymed_mock(format, actual_tymed, test_data);
  if (!TEST_CHECK(mock != NULL)) {
    goto cleanup;
  }
  if (!TEST_SUCCEEDED(gcmz_dataobj_source_create(&mock->iface,
                                                 &(FORMATETC){
                                                     .cfFormat = format,
                                                     .dwAspect = DVASPECT_CONTENT,
                                                     .lindex = -1,
                                                     .tymed = requested_tymed,
                                                 },
                                                 &source,
                                                 &err),
                      &err)) {
    goto cleanup;
  }
  if (!TEST_CHECK(source != NULL)) {
    goto cleanup;
  }

  TEST_CHECK(ovl_source_read(source, buffer, 0, sizeof(buffer) - 1) == data_len);
  TEST_CHECK(memcmp(buffer, test_data, data_len) == 0);

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  if (mock) {
    IDataObject_Release(&mock->iface);
  }
}

static void test_dataobj_source_create_tymed_mismatch(void) {
  TEST_CASE("TYMED_FILE");
  verify_source_with_mismatched_tymed(TYMED_FILE, CF_HDROP, TYMED_HGLOBAL | TYMED_ISTREAM);
  TEST_CASE("TYMED_HGLOBAL");
  verify_source_with_mismatched_tymed(TYMED_HGLOBAL, CF_TEXT, TYMED_FILE | TYMED_ISTREAM);
  TEST_CASE("TYMED_ISTREAM");
  verify_source_with_mismatched_tymed(TYMED_ISTREAM, CF_TEXT, TYMED_HGLOBAL | TYMED_FILE);
}

TEST_LIST = {
    {"dataobj_source_create_null_params", test_dataobj_source_create_null_params},
    {"dataobj_source_create_getdata_fail", test_dataobj_source_create_getdata_fail},
    {"dataobj_source_create_with_tymed", test_dataobj_source_create_with_tymed},
    {"dataobj_source_create_tymed_mismatch", test_dataobj_source_create_tymed_mismatch},
    {NULL, NULL} // Terminator
};
