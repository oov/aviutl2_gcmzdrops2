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
#include <stdio.h>

#include "dataobj.c" // Include implementation to test internal functions
#include "datauri.h"
#include "temp.h"

#include <ovl/file.h>

static void test_init(void) {
  struct ov_error err = {0};
  if (!gcmz_temp_create_directory(&err)) {
    char *msg = NULL;
    ov_error_to_string(&err, &msg, true, NULL);
    fprintf(stderr, "test_init failed: %s\n", msg);
    OV_ARRAY_DESTROY(&msg);
    OV_ERROR_DESTROY(&err);
    abort();
  }
}

static void test_cleanup(void) { gcmz_temp_remove_directory(); }

static void cleanup_temporary_files(struct gcmz_file_list *file_list) {
  if (!file_list) {
    return;
  }

  size_t count = gcmz_file_list_count(file_list);
  for (size_t i = 0; i < count; ++i) {
    struct gcmz_file *file = gcmz_file_list_get_mutable(file_list, i);
    if (file && file->temporary && file->path) {
      DeleteFileW(file->path);
      file->temporary = false;
    }
  }
}

// Helper function to test sanitize_filename with given input and expected output
static void assert_sanitize(char const *const title, NATIVE_CHAR const *input, NATIVE_CHAR const *expected) {
  TEST_CASE(title);
  NATIVE_CHAR filename[MAX_PATH];
  wcsncpy(filename, input, MAX_PATH - 1);
  filename[MAX_PATH - 1] = L'\0';
  sanitize_filename(filename);
  TEST_CHECK(wcscmp(filename, expected) == 0);
  TEST_MSG("want %ls, got %ls", expected, filename);
}

static void test_sanitize_filename(void) {
  assert_sanitize("Test invalid characters replacement", L"test<>:\"/\\|?*file.txt", L"test-----\\---file.txt");
  assert_sanitize("Test control characters",
                  L"test\x01\x1f\x7f"
                  L"file.txt",
                  L"test---file.txt");
  assert_sanitize("Test reserved name", L"CON", L"-ON");
  assert_sanitize("Test case insensitive reserved name", L"con", L"-on");
  assert_sanitize("Test Unicode filename support", L"テスト<ファイル>.txt", L"テスト-ファイル-.txt");
  assert_sanitize("Test mixed ASCII and Unicode with invalid characters",
                  L"test_テスト:file|名前.txt",
                  L"test_テスト-file-名前.txt");

  // Test long filename truncation
  TEST_CASE("Long filename truncation");
  wchar_t long_filename[300];
  for (int i = 0; i < 260; ++i) {
    long_filename[i] = L'a';
  }
  long_filename[260] = L'\0';
  sanitize_filename(long_filename);
  size_t truncated_len = wcslen(long_filename);
  size_t const expected_len = 255;
  if (!TEST_CHECK(truncated_len == expected_len)) {
    TEST_MSG("want %zu, got %zu", expected_len, truncated_len);
  }
}

// Helper function to test extract_file_extension with given path and expected position
static void assert_extension_pos(char const *const title, wchar_t const *path, size_t expected_pos) {
  TEST_CASE(title);
  size_t ext_pos = extract_file_extension(path);
  TEST_CHECK(ext_pos == expected_pos);
  TEST_MSG("want %zu, got %zu", expected_pos, ext_pos);
}

static void test_extract_file_extension(void) {
  assert_extension_pos("Test normal file with extension", L"test.txt", 4);
  assert_extension_pos("Test file without extension", L"testfile", 8);
  assert_extension_pos("Test file with path and extension", L"C:\\path\\to\\file.png", 15);
  assert_extension_pos("Test hidden file", L".gitignore", 10);
  assert_extension_pos("Test hidden file with extension", L".config.json", 7);
  assert_extension_pos("Test file with multiple dots", L"archive.tar.gz", 11);
  assert_extension_pos("Test path with dot in directory name", L"C:\\path.with.dots\\filename", 26);
  assert_extension_pos("Test path with dot in directory and file extension", L"C:\\path.with.dots\\file.txt", 22);
}

static void test_create_temp_file_from_data(void) {
  char const test_data[] = "Test file content for temporary file creation";
  size_t const test_data_len = strlen(test_data);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }
  if (!TEST_SUCCEEDED(
          create_temp_file_from_data(test_data, test_data_len, L"test_base.txt", L"text/plain", file_list, &err),
          &err)) {
    return;
  }
  TEST_CHECK(gcmz_file_list_count(file_list) == 1);

  struct gcmz_file const *file = gcmz_file_list_get(file_list, 0);
  TEST_ASSERT(file != NULL);
  TEST_CHECK(file->temporary);
  TEST_ASSERT(file->mime_type != NULL);
  TEST_CHECK(wcscmp(file->mime_type, L"text/plain") == 0);

  struct ovl_file *ovl_f = NULL;
  struct ov_error file_err = {0};
  TEST_ASSERT(ovl_file_open(file->path, &ovl_f, &file_err));

  char read_buffer[256];
  size_t bytes_read = 0;
  bool read_result = ovl_file_read(ovl_f, read_buffer, sizeof(read_buffer), &bytes_read, &file_err);
  ovl_file_close(ovl_f);

  TEST_CHECK(read_result);
  TEST_CHECK(bytes_read == test_data_len);
  TEST_CHECK(memcmp(read_buffer, test_data, test_data_len) == 0);

  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
}

static void test_temp_file_uniqueness(void) {
  char const test_data[] = "Uniqueness test data";
  size_t const test_data_len = strlen(test_data);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Create multiple temporary files with same base name
  for (int i = 0; i < 5; ++i) {
    if (!TEST_SUCCEEDED(create_temp_file_from_data(
                            test_data, test_data_len, L"unique_test.dat", L"application/octet-stream", file_list, &err),
                        &err)) {
      return;
    }
  }

  TEST_CHECK(gcmz_file_list_count(file_list) == 5);

  // Verify all paths are different
  for (size_t i = 0; i < 5; ++i) {
    struct gcmz_file const *file1 = gcmz_file_list_get(file_list, i);
    for (size_t j = i + 1; j < 5; ++j) {
      struct gcmz_file const *file2 = gcmz_file_list_get(file_list, j);
      TEST_CHECK(wcscmp(file1->path, file2->path) != 0);
    }
  }

  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
}

static void test_cleanup_temporary_files(void) {
  char const test_data[] = "Cleanup test data";
  size_t const test_data_len = strlen(test_data);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  for (int i = 0; i < 3; ++i) {
    if (!TEST_SUCCEEDED(
            create_temp_file_from_data(
                test_data, test_data_len, L"cleanup_test.tmp", L"application/octet-stream", file_list, &err),
            &err)) {
      return;
    }
  }

  wchar_t *file_paths[3] = {NULL};
  for (size_t i = 0; i < 3; ++i) {
    struct gcmz_file const *file = gcmz_file_list_get(file_list, i);
    size_t path_len = wcslen(file->path);
    if (!OV_REALLOC(&file_paths[i], path_len + 1, sizeof(wchar_t))) {
      for (size_t j = 0; j < i; ++j) {
        OV_FREE(&file_paths[j]);
      }
      gcmz_file_list_destroy(&file_list);
      return;
    }
    wcsncpy(file_paths[i], file->path, path_len);
    file_paths[i][path_len] = L'\0';
  }

  for (int i = 0; i < 3; ++i) {
    struct ovl_file *ovl_f = NULL;
    struct ov_error file_err = {0};
    TEST_ASSERT(ovl_file_open(file_paths[i], &ovl_f, &file_err));
    ovl_file_close(ovl_f);
  }

  cleanup_temporary_files(file_list);

  for (int i = 0; i < 3; ++i) {
    struct ovl_file *ovl_f = NULL;
    struct ov_error file_err = {0};
    if (!TEST_FAILED_WITH(ovl_file_open(file_paths[i], &ovl_f, &file_err),
                          &file_err,
                          ov_error_type_hresult,
                          HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) {
      ovl_file_close(ovl_f);
    }
  }
  for (size_t i = 0; i < 3; ++i) {
    struct gcmz_file *file = gcmz_file_list_get_mutable(file_list, i);
    TEST_CHECK(!file->temporary);
  }
  for (int i = 0; i < 3; ++i) {
    OV_FREE(&file_paths[i]);
  }
  gcmz_file_list_destroy(&file_list);
}

static void test_filename_utilities_error_handling(void) {
  sanitize_filename(NULL); // Should safely return without crashing
  TEST_CHECK(extract_file_extension(NULL) == 0);
}

static void test_temp_file_error_handling(void) {
  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test with NULL data
  TEST_FAILED_WITH(create_temp_file_from_data(NULL, 10, L"test.txt", L"text/plain", file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  // Test with zero length
  char const test_data[] = "test";
  TEST_FAILED_WITH(create_temp_file_from_data(test_data, 0, L"test.txt", L"text/plain", file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  // Test with NULL filename
  TEST_FAILED_WITH(create_temp_file_from_data(test_data, 4, NULL, L"text/plain", file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  gcmz_file_list_destroy(&file_list);
}

enum mock_format_type {
  MOCK_FORMAT_NONE = 0,
  MOCK_FORMAT_PNG,
  MOCK_FORMAT_DIB,
  MOCK_FORMAT_TEXT,
};

struct mock_data_object {
  IDataObject iface;
  LONG ref_count;
  enum mock_format_type format_type;
  void *data;
  size_t data_len;
};

static HRESULT STDMETHODCALLTYPE MockDataObject_QueryInterface(IDataObject *iface, REFIID riid, void **ppv) {
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
    *ppv = iface;
    IDataObject_AddRef(iface);
    return S_OK;
  }
  *ppv = NULL;
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE MockDataObject_AddRef(IDataObject *iface) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;
  return (ULONG)InterlockedIncrement(&mock->ref_count);
}

static ULONG STDMETHODCALLTYPE MockDataObject_Release(IDataObject *iface) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;
  ULONG ref = (ULONG)InterlockedDecrement(&mock->ref_count);
  if (ref == 0) {
    if (mock->data) {
      OV_FREE(&mock->data);
    }
    OV_FREE(&mock);
  }
  return ref;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_GetData(IDataObject *iface, FORMATETC *pformatetc, STGMEDIUM *pmedium) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;

  if (!(pformatetc->tymed & TYMED_HGLOBAL)) {
    return DV_E_TYMED;
  }

  CLIPFORMAT png_format = (CLIPFORMAT)RegisterClipboardFormatW(L"PNG");

  // Check format compatibility
  if (pformatetc->cfFormat == png_format && mock->format_type != MOCK_FORMAT_PNG) {
    return DV_E_FORMATETC;
  }
  if (pformatetc->cfFormat == CF_DIB && mock->format_type != MOCK_FORMAT_DIB) {
    return DV_E_FORMATETC;
  }
  if (pformatetc->cfFormat == CF_UNICODETEXT && mock->format_type != MOCK_FORMAT_TEXT) {
    return DV_E_FORMATETC;
  }

  // Calculate data size (text format needs null terminator space)
  size_t data_size = mock->data_len;
  if (mock->format_type == MOCK_FORMAT_TEXT) {
    data_size = (mock->data_len + 1) * sizeof(wchar_t);
  }

  HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, data_size);
  if (!hGlobal) {
    return E_OUTOFMEMORY;
  }

  void *p = GlobalLock(hGlobal);
  if (!p) {
    GlobalFree(hGlobal);
    return E_OUTOFMEMORY;
  }

  if (mock->format_type == MOCK_FORMAT_TEXT) {
    wchar_t *wp = (wchar_t *)p;
    wcsncpy(wp, (wchar_t *)mock->data, mock->data_len);
    wp[mock->data_len] = L'\0';
  } else {
    memcpy(p, mock->data, mock->data_len);
  }

  GlobalUnlock(hGlobal);

  pmedium->tymed = TYMED_HGLOBAL;
  pmedium->hGlobal = hGlobal;
  pmedium->pUnkForRelease = NULL;

  return S_OK;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_GetDataHere(IDataObject *iface,
                                                            FORMATETC *pformatetc,
                                                            STGMEDIUM *pmedium) {
  (void)iface;
  (void)pformatetc;
  (void)pmedium;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_QueryGetData(IDataObject *iface, FORMATETC *pformatetc) {
  struct mock_data_object *mock = (struct mock_data_object *)iface;

  if (!(pformatetc->tymed & TYMED_HGLOBAL)) {
    return DV_E_FORMATETC;
  }

  CLIPFORMAT png_format = (CLIPFORMAT)RegisterClipboardFormatW(L"PNG");

  if (pformatetc->cfFormat == png_format && mock->format_type == MOCK_FORMAT_PNG) {
    return S_OK;
  }
  if (pformatetc->cfFormat == CF_DIB && mock->format_type == MOCK_FORMAT_DIB) {
    return S_OK;
  }
  if (pformatetc->cfFormat == CF_UNICODETEXT && mock->format_type == MOCK_FORMAT_TEXT) {
    return S_OK;
  }

  return DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_GetCanonicalFormatEtc(IDataObject *iface,
                                                                      FORMATETC *pformatectIn,
                                                                      FORMATETC *pformatetcOut) {
  (void)iface;
  (void)pformatectIn;
  (void)pformatetcOut;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_SetData(IDataObject *iface,
                                                        FORMATETC *pformatetc,
                                                        STGMEDIUM *pmedium,
                                                        BOOL fRelease) {
  (void)iface;
  (void)pformatetc;
  (void)pmedium;
  (void)fRelease;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_EnumFormatEtc(IDataObject *iface,
                                                              DWORD dwDirection,
                                                              IEnumFORMATETC **ppenumFormatEtc) {
  (void)iface;
  (void)dwDirection;
  (void)ppenumFormatEtc;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_DAdvise(
    IDataObject *iface, FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) {
  (void)iface;
  (void)pformatetc;
  (void)advf;
  (void)pAdvSink;
  (void)pdwConnection;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_DUnadvise(IDataObject *iface, DWORD dwConnection) {
  (void)iface;
  (void)dwConnection;
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MockDataObject_EnumDAdvise(IDataObject *iface, IEnumSTATDATA **ppenumAdvise) {
  (void)iface;
  (void)ppenumAdvise;
  return E_NOTIMPL;
}

static const IDataObjectVtbl MockDataObjectVtbl = {MockDataObject_QueryInterface,
                                                   MockDataObject_AddRef,
                                                   MockDataObject_Release,
                                                   MockDataObject_GetData,
                                                   MockDataObject_GetDataHere,
                                                   MockDataObject_QueryGetData,
                                                   MockDataObject_GetCanonicalFormatEtc,
                                                   MockDataObject_SetData,
                                                   MockDataObject_EnumFormatEtc,
                                                   MockDataObject_DAdvise,
                                                   MockDataObject_DUnadvise,
                                                   MockDataObject_EnumDAdvise};

static struct mock_data_object *
create_mock_dataobject(enum mock_format_type format_type, void const *data, size_t data_len) {
  struct mock_data_object *mock = NULL;
  if (!OV_REALLOC(&mock, 1, sizeof(*mock))) {
    return NULL;
  }

  *mock = (struct mock_data_object){
      .iface.lpVtbl = &MockDataObjectVtbl,
      .ref_count = 1,
      .format_type = format_type,
      .data_len = data_len,
  };
  if (!data || data_len == 0) {
    return mock;
  }

  size_t alloc_size = data_len;
  if (format_type == MOCK_FORMAT_TEXT) {
    alloc_size = (data_len + 1) * sizeof(wchar_t);
  }
  if (!OV_REALLOC(&mock->data, 1, alloc_size)) {
    OV_FREE(&mock);
    return NULL;
  }
  if (format_type == MOCK_FORMAT_TEXT) {
    wchar_t *wdata = (wchar_t *)mock->data;
    wcsncpy(wdata, (wchar_t const *)data, data_len);
    wdata[data_len] = L'\0';
  } else {
    memcpy(mock->data, data, data_len);
  }
  return mock;
}

static void create_test_dib_data(unsigned char **dib_data, size_t *dib_len, int bit_depth) {
  BITMAPINFOHEADER bih = {0};
  bih.biSize = sizeof(BITMAPINFOHEADER);
  bih.biWidth = 2; // 2x2 pixel image
  bih.biHeight = 2;
  bih.biPlanes = 1;
  bih.biBitCount = (WORD)bit_depth;
  bih.biCompression = BI_RGB;
  bih.biSizeImage = 0; // Can be 0 for BI_RGB
  bih.biXPelsPerMeter = 0;
  bih.biYPelsPerMeter = 0;
  bih.biClrUsed = 0;
  bih.biClrImportant = 0;

  // Calculate color table size
  DWORD color_table_size = 0;
  switch (bit_depth) {
  case 1:
    color_table_size = 2 * sizeof(RGBQUAD);
    break;
  case 4:
    color_table_size = 16 * sizeof(RGBQUAD);
    break;
  case 8:
    color_table_size = 256 * sizeof(RGBQUAD);
    break;
  case 24:
  case 32:
    color_table_size = 0; // No color table for 24/32-bit
    break;
  }

  // Calculate image data size (with padding)
  size_t bytes_per_pixel = (size_t)(bit_depth / 8);
  if (bit_depth < 8) {
    bytes_per_pixel = 1; // For 1-bit and 4-bit, we still need at least 1 byte per row
  }
  size_t row_size = (((size_t)bih.biWidth * bytes_per_pixel + 3) / 4) * 4; // DWORD aligned
  size_t image_data_size = row_size * (size_t)bih.biHeight;

  *dib_len = sizeof(BITMAPINFOHEADER) + color_table_size + image_data_size;
  if (!OV_REALLOC(dib_data, 1, *dib_len)) {
    return;
  }

  // Copy header
  memcpy(*dib_data, &bih, sizeof(BITMAPINFOHEADER));

  // Add color table if needed
  if (color_table_size > 0) {
    RGBQUAD *color_table = (RGBQUAD *)(*dib_data + sizeof(BITMAPINFOHEADER));
    // Create simple grayscale palette
    for (DWORD i = 0; i < color_table_size / sizeof(RGBQUAD); ++i) {
      BYTE gray = (BYTE)(i * 255 / (color_table_size / sizeof(RGBQUAD) - 1));
      color_table[i].rgbRed = gray;
      color_table[i].rgbGreen = gray;
      color_table[i].rgbBlue = gray;
      color_table[i].rgbReserved = 0;
    }
  }

  // Add simple image data (all zeros for simplicity)
  memset(*dib_data + sizeof(BITMAPINFOHEADER) + color_table_size, 0, image_data_size);
}

static void test_try_extract_png_format_success(void) {
  // Create mock PNG data (simplified PNG header)
  unsigned char png_data[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG signature
                              0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
                              0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // 1x1 pixel
                              0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, // RGB, no compression
                              0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, // IDAT chunk
                              0x54, 0x08, 0x99, 0x01, 0x01, 0x00, 0x00, 0xFF, // minimal image data
                              0xFF, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
                              0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, // IEND chunk
                              0x42, 0x60, 0x82};

  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_PNG, png_data, sizeof(png_data));
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test PNG extraction
  if (!TEST_SUCCEEDED(try_extract_custom_image_format(&mock->iface, L"PNG", L".png", L"image/png", file_list, &err),
                      &err)) {
    return;
  }

  // Verify file was created
  TEST_CHECK(gcmz_file_list_count(file_list) == 1);

  struct gcmz_file const *file = gcmz_file_list_get(file_list, 0);
  TEST_ASSERT(file != NULL);

  // Verify file properties
  TEST_CHECK(file->temporary);
  TEST_ASSERT(file->mime_type != NULL);
  TEST_CHECK(wcscmp(file->mime_type, L"image/png") == 0);

  // Verify file extension
  TEST_ASSERT(file->path != NULL);
  TEST_CHECK(wcsstr(file->path, L".png") != NULL);

  // Verify file content
  struct ovl_file *ovl_f = NULL;
  struct ov_error file_err = {0};
  TEST_ASSERT(ovl_file_open(file->path, &ovl_f, &file_err));

  uint64_t file_size = 0;
  TEST_CHECK(ovl_file_size(ovl_f, &file_size, &file_err));
  TEST_CHECK(file_size == sizeof(png_data));

  unsigned char read_buffer[256];
  size_t bytes_read = 0;
  bool read_result = ovl_file_read(ovl_f, read_buffer, sizeof(read_buffer), &bytes_read, &file_err);
  ovl_file_close(ovl_f);

  TEST_CHECK(read_result);
  TEST_CHECK(bytes_read == sizeof(png_data));
  TEST_CHECK(memcmp(read_buffer, png_data, sizeof(png_data)) == 0);

  // Clean up
  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_try_extract_png_format_no_png_data(void) {
  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_NONE, NULL, 0);
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test PNG extraction when no PNG data is available
  TEST_FAILED_WITH(try_extract_custom_image_format(&mock->iface, L"PNG", L".png", L"image/png", file_list, &err),
                   &err,
                   ov_error_type_hresult,
                   DV_E_FORMATETC);

  // Verify no files were created
  TEST_CHECK(gcmz_file_list_count(file_list) == 0);

  // Clean up
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_try_extract_png_format_error_handling(void) {
  unsigned char png_data[] = {0x89, 0x50, 0x4E, 0x47}; // Minimal data
  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_PNG, png_data, sizeof(png_data));
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test with NULL dataobj
  TEST_FAILED_WITH(try_extract_custom_image_format(NULL, L"PNG", L".png", L"image/png", file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  // Test with NULL files
  TEST_FAILED_WITH(try_extract_custom_image_format(&mock->iface, L"PNG", L".png", L"image/png", NULL, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  // Clean up
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_try_extract_dib_format_success_24bit(void) {
  // Create mock 24-bit DIB data
  unsigned char *dib_data = NULL;
  size_t dib_len = 0;
  create_test_dib_data(&dib_data, &dib_len, 24);

  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_DIB, dib_data, dib_len);
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test DIB extraction
  if (!TEST_SUCCEEDED(try_extract_dib_format(&mock->iface, file_list, &err), &err)) {
    return;
  }

  // Verify file was created
  TEST_CHECK(gcmz_file_list_count(file_list) == 1);

  struct gcmz_file const *file = gcmz_file_list_get(file_list, 0);
  TEST_ASSERT(file != NULL);

  // Verify file properties
  TEST_CHECK(file->temporary);
  TEST_ASSERT(file->mime_type != NULL);
  TEST_CHECK(wcscmp(file->mime_type, L"image/bmp") == 0);

  // Verify file extension
  TEST_ASSERT(file->path != NULL);
  TEST_CHECK(wcsstr(file->path, L".bmp") != NULL);

  // Verify file content (should be DIB data + BITMAPFILEHEADER)
  struct ovl_file *ovl_f = NULL;
  struct ov_error file_err = {0};
  TEST_ASSERT(ovl_file_open(file->path, &ovl_f, &file_err));

  uint64_t file_size = 0;
  TEST_CHECK(ovl_file_size(ovl_f, &file_size, &file_err));
  TEST_CHECK(file_size == dib_len + sizeof(BITMAPFILEHEADER));

  // Read and verify BMP file header
  BITMAPFILEHEADER bfh;
  size_t bytes_read = 0;
  bool read_result = ovl_file_read(ovl_f, &bfh, sizeof(BITMAPFILEHEADER), &bytes_read, &file_err);
  TEST_CHECK(read_result);
  TEST_CHECK(bytes_read == sizeof(BITMAPFILEHEADER));
  TEST_CHECK(bfh.bfType == 0x4D42); // 'BM' signature
  TEST_CHECK(bfh.bfSize == dib_len + sizeof(BITMAPFILEHEADER));

  ovl_file_close(ovl_f);

  // Clean up
  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
  OV_FREE(&dib_data);
}

static void test_try_extract_dib_format_no_dib_data(void) {
  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_NONE, NULL, 0);
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test DIB extraction when no DIB data is available
  TEST_FAILED_WITH(try_extract_dib_format(&mock->iface, file_list, &err), &err, ov_error_type_hresult, DV_E_FORMATETC);

  // Verify no files were created
  TEST_CHECK(gcmz_file_list_count(file_list) == 0);

  // Clean up
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_try_extract_dib_format_error_handling(void) {
  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test with NULL dataobj
  TEST_FAILED_WITH(
      try_extract_dib_format(NULL, file_list, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);

  // Test with invalid data (too small)
  unsigned char invalid_dib[] = {0x01, 0x02, 0x03, 0x04};
  struct mock_data_object *mock_invalid = create_mock_dataobject(MOCK_FORMAT_DIB, invalid_dib, sizeof(invalid_dib));
  TEST_ASSERT(mock_invalid != NULL);
  TEST_FAILED_WITH(try_extract_dib_format(&mock_invalid->iface, file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  IDataObject_Release(&mock_invalid->iface);

  // Test with invalid header size
  BITMAPINFOHEADER invalid_bih = {0};
  invalid_bih.biSize = 10;
  invalid_bih.biWidth = 1;
  invalid_bih.biHeight = 1;
  invalid_bih.biPlanes = 1;
  invalid_bih.biBitCount = 24;
  struct mock_data_object *mock_header = create_mock_dataobject(MOCK_FORMAT_DIB, &invalid_bih, sizeof(invalid_bih));
  TEST_ASSERT(mock_header != NULL);
  TEST_FAILED_WITH(try_extract_dib_format(&mock_header->iface, file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  IDataObject_Release(&mock_header->iface);

  // Test with unsupported bit depth
  BITMAPINFOHEADER unsupported_bih = {0};
  unsupported_bih.biSize = sizeof(BITMAPINFOHEADER);
  unsupported_bih.biWidth = 1;
  unsupported_bih.biHeight = 1;
  unsupported_bih.biPlanes = 1;
  unsupported_bih.biBitCount = 12;
  unsupported_bih.biCompression = BI_RGB;
  struct mock_data_object *mock_bit =
      create_mock_dataobject(MOCK_FORMAT_DIB, &unsupported_bih, sizeof(unsupported_bih));
  TEST_ASSERT(mock_bit != NULL);
  TEST_FAILED_WITH(try_extract_dib_format(&mock_bit->iface, file_list, &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);
  IDataObject_Release(&mock_bit->iface);

  // Verify no files were created
  TEST_CHECK(gcmz_file_list_count(file_list) == 0);

  gcmz_file_list_destroy(&file_list);
}

static void test_try_extract_data_uri_only_success(void) {
  // Test with a simple data URI
  wchar_t const *data_uri_text = L"data:text/plain;base64,SGVsbG8gV29ybGQ="; // "Hello World" in base64

  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_TEXT, data_uri_text, wcslen(data_uri_text));
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test text data URI extraction
  if (!TEST_SUCCEEDED(try_extract_data_uri(&mock->iface, file_list, &err), &err)) {
    return;
  }

  // Verify file was created
  TEST_CHECK(gcmz_file_list_count(file_list) == 1);

  struct gcmz_file const *file = gcmz_file_list_get(file_list, 0);
  TEST_ASSERT(file != NULL);

  // Verify file properties
  TEST_CHECK(file->temporary);
  TEST_ASSERT(file->mime_type != NULL);
  TEST_CHECK(wcsstr(file->mime_type, L"text/plain") != NULL);

  // Verify file extension
  TEST_ASSERT(file->path != NULL);
  TEST_CHECK(wcsstr(file->path, L".txt") != NULL);

  // Verify file content
  struct ovl_file *ovl_f = NULL;
  struct ov_error file_err = {0};
  TEST_ASSERT(ovl_file_open(file->path, &ovl_f, &file_err));

  char read_buffer[256];
  size_t bytes_read = 0;
  bool read_result = ovl_file_read(ovl_f, read_buffer, sizeof(read_buffer), &bytes_read, &file_err);
  ovl_file_close(ovl_f);

  TEST_CHECK(read_result);
  TEST_CHECK(bytes_read == 11); // "Hello World" length
  TEST_CHECK(memcmp(read_buffer, "Hello World", 11) == 0);

  // Clean up
  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_try_extract_data_uri_only_plain_text_fallback(void) {
  // Test with plain text (not a data URI)
  wchar_t const *plain_text = L"This is just plain text, not a data URI";

  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_TEXT, plain_text, wcslen(plain_text));
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test plain text fallback processing
  if (!TEST_SUCCEEDED(try_extract_plain_text(&mock->iface, file_list, &err), &err)) {
    return;
  }

  // Verify file was created
  TEST_CHECK(gcmz_file_list_count(file_list) == 1);

  struct gcmz_file const *file = gcmz_file_list_get(file_list, 0);
  TEST_ASSERT(file != NULL);

  // Verify file properties
  TEST_CHECK(file->temporary);
  TEST_ASSERT(file->mime_type != NULL);
  TEST_CHECK(wcscmp(file->mime_type, L"text/plain") == 0);

  // Verify file extension
  TEST_ASSERT(file->path != NULL);
  TEST_CHECK(wcsstr(file->path, L".txt") != NULL);

  // Verify file content (should be UTF-8 encoded)
  struct ovl_file *ovl_f = NULL;
  struct ov_error file_err = {0};
  TEST_ASSERT(ovl_file_open(file->path, &ovl_f, &file_err));

  char read_buffer[256];
  size_t bytes_read = 0;
  bool read_result = ovl_file_read(ovl_f, read_buffer, sizeof(read_buffer), &bytes_read, &file_err);
  ovl_file_close(ovl_f);

  TEST_CHECK(read_result);

  // Convert expected text to UTF-8 for comparison
  char expected_utf8[256];
  int expected_len_int =
      WideCharToMultiByte(CP_UTF8, 0, plain_text, -1, expected_utf8, sizeof(expected_utf8), NULL, NULL);
  TEST_CHECK(expected_len_int > 0);
  expected_len_int--; // Remove null terminator from count
  size_t expected_len = (size_t)expected_len_int;

  TEST_CHECK(bytes_read == expected_len);
  TEST_CHECK(memcmp(read_buffer, expected_utf8, expected_len) == 0);

  // Clean up
  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_try_extract_data_uri_only_error_handling(void) {
  wchar_t const *test_text = L"test data";
  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_TEXT, test_text, wcslen(test_text));
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test with NULL dataobj
  TEST_FAILED_WITH(
      try_extract_data_uri(NULL, file_list, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);

  // Test with NULL files
  TEST_FAILED_WITH(
      try_extract_data_uri(&mock->iface, NULL, &err), &err, ov_error_type_generic, ov_error_generic_invalid_argument);

  // Clean up
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_extract_from_dataobj_with_data_uri(void) {
  // Test that gcmz_dataobj_extract_from_dataobj includes text data URI extraction
  wchar_t const *data_uri_text = L"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/"
                                 L"5+hHgAHggJ/PchI7wAAAABJRU5ErkJggg=="; // 1x1 transparent PNG

  struct mock_data_object *mock = create_mock_dataobject(MOCK_FORMAT_TEXT, data_uri_text, wcslen(data_uri_text));
  TEST_ASSERT(mock != NULL);

  struct gcmz_file_list *file_list = NULL;
  struct ov_error err = {0};
  file_list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(file_list != NULL, &err)) {
    return;
  }

  // Test extraction through the main function
  if (!TEST_SUCCEEDED(gcmz_dataobj_extract_from_dataobj(&mock->iface, file_list, &err), &err)) {
    return;
  }

  // Verify file was created
  TEST_CHECK(gcmz_file_list_count(file_list) == 1);

  struct gcmz_file const *file = gcmz_file_list_get(file_list, 0);
  TEST_ASSERT(file != NULL);

  // Verify file properties
  TEST_CHECK(file->temporary);
  TEST_ASSERT(file->mime_type != NULL);
  TEST_CHECK(wcsstr(file->mime_type, L"image/png") != NULL);

  // Verify file extension
  TEST_ASSERT(file->path != NULL);
  TEST_CHECK(wcsstr(file->path, L".png") != NULL);

  // Clean up
  cleanup_temporary_files(file_list);
  gcmz_file_list_destroy(&file_list);
  IDataObject_Release(&mock->iface);
}

static void test_detect_mime_type_from_extension(void) {
  wchar_t const *mime_type = NULL;

  // Test image extensions
  mime_type = detect_mime_type_from_extension(L"test.png");
  TEST_CHECK(wcscmp(mime_type, L"image/png") == 0);

  mime_type = detect_mime_type_from_extension(L"image.jpg");
  TEST_CHECK(wcscmp(mime_type, L"image/jpeg") == 0);

  mime_type = detect_mime_type_from_extension(L"photo.jpeg");
  TEST_CHECK(wcscmp(mime_type, L"image/jpeg") == 0);

  mime_type = detect_mime_type_from_extension(L"icon.gif");
  TEST_CHECK(wcscmp(mime_type, L"image/gif") == 0);

  // Test document extensions
  mime_type = detect_mime_type_from_extension(L"document.pdf");
  TEST_CHECK(wcscmp(mime_type, L"application/pdf") == 0);

  mime_type = detect_mime_type_from_extension(L"data.json");
  TEST_CHECK(wcscmp(mime_type, L"application/json") == 0);

  mime_type = detect_mime_type_from_extension(L"page.html");
  TEST_CHECK(wcscmp(mime_type, L"text/html") == 0);

  // Test case insensitive
  mime_type = detect_mime_type_from_extension(L"FILE.PNG");
  TEST_CHECK(wcscmp(mime_type, L"image/png") == 0);

  // Test unknown extension
  mime_type = detect_mime_type_from_extension(L"file.xyz");
  TEST_CHECK(wcscmp(mime_type, L"application/octet-stream") == 0);

  // Test no extension
  mime_type = detect_mime_type_from_extension(L"filename");
  TEST_CHECK(wcscmp(mime_type, L"application/octet-stream") == 0);
}

static void test_detect_mime_type_with_sniffing(void) {
  wchar_t const *mime_type = NULL;
  wchar_t const *suggested_ext = NULL;

  // Test PNG data sniffing
  uint8_t png_data[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d};
  mime_type = detect_mime_type_with_sniffing(png_data, sizeof(png_data), NULL, &suggested_ext);
  TEST_CHECK(wcscmp(mime_type, L"image/png") == 0);
  TEST_CHECK(wcscmp(suggested_ext, L".png") == 0);

  // Test JPEG data sniffing
  uint8_t jpeg_data[] = {0xff, 0xd8, 0xff, 0xe0};
  mime_type = detect_mime_type_with_sniffing(jpeg_data, sizeof(jpeg_data), NULL, &suggested_ext);
  TEST_CHECK(wcscmp(mime_type, L"image/jpeg") == 0);
  TEST_CHECK(wcscmp(suggested_ext, L".jpg") == 0);

  // Test GIF data sniffing
  uint8_t gif_data[] = {'G', 'I', 'F', '8', '9', 'a'};
  mime_type = detect_mime_type_with_sniffing(gif_data, sizeof(gif_data), NULL, &suggested_ext);
  TEST_CHECK(wcscmp(mime_type, L"image/gif") == 0);
  TEST_CHECK(wcscmp(suggested_ext, L".gif") == 0);

  // Test fallback to extension when sniffing returns unknown
  // Note: gcmz_sniff always returns true, so we need to check if it returned application/octet-stream
  uint8_t unknown_data[] = {0x00, 0x01, 0x02, 0x03};
  mime_type = detect_mime_type_with_sniffing(unknown_data, sizeof(unknown_data), L"test.txt", &suggested_ext);
  // Since sniffing will return application/octet-stream for unknown data, we expect that result
  TEST_CHECK(wcscmp(mime_type, L"application/octet-stream") == 0);
  TEST_CHECK(wcscmp(suggested_ext, L".bin") == 0);

  // Test default fallback when both sniffing and extension fail
  mime_type = detect_mime_type_with_sniffing(unknown_data, sizeof(unknown_data), NULL, &suggested_ext);
  TEST_CHECK(wcscmp(mime_type, L"application/octet-stream") == 0);
  TEST_CHECK(wcscmp(suggested_ext, L".bin") == 0);

  // Test extension override with sniffing
  mime_type = detect_mime_type_with_sniffing(png_data, sizeof(png_data), L"test.txt", &suggested_ext);
  TEST_CHECK(wcscmp(mime_type, L"image/png") == 0); // Sniffing overrides extension
  TEST_CHECK(wcscmp(suggested_ext, L".png") == 0);

  // Test true extension fallback by passing NULL data (no content sniffing possible)
  mime_type = detect_mime_type_with_sniffing(NULL, 0, L"document.txt", &suggested_ext);
  TEST_CHECK(wcscmp(mime_type, L"text/plain") == 0);
  TEST_CHECK(wcscmp(suggested_ext, L".txt") == 0);
}

TEST_LIST = {
    {"sanitize_filename", test_sanitize_filename},
    {"extract_file_extension", test_extract_file_extension},
    {"filename_utilities_error_handling", test_filename_utilities_error_handling},
    {"create_temp_file_from_data", test_create_temp_file_from_data},
    {"temp_file_uniqueness", test_temp_file_uniqueness},
    {"cleanup_temporary_files", test_cleanup_temporary_files},
    {"temp_file_error_handling", test_temp_file_error_handling},
    {"try_extract_png_format_success", test_try_extract_png_format_success},
    {"try_extract_png_format_no_png_data", test_try_extract_png_format_no_png_data},
    {"try_extract_png_format_error_handling", test_try_extract_png_format_error_handling},
    {"try_extract_dib_format_success_24bit", test_try_extract_dib_format_success_24bit},
    {"try_extract_dib_format_no_dib_data", test_try_extract_dib_format_no_dib_data},
    {"try_extract_dib_format_error_handling", test_try_extract_dib_format_error_handling},
    {"try_extract_data_uri_only_success", test_try_extract_data_uri_only_success},
    {"try_extract_data_uri_only_plain_text_fallback", test_try_extract_data_uri_only_plain_text_fallback},
    {"try_extract_data_uri_only_error_handling", test_try_extract_data_uri_only_error_handling},
    {"extract_from_dataobj_with_data_uri", test_extract_from_dataobj_with_data_uri},
    {"detect_mime_type_from_extension", test_detect_mime_type_from_extension},
    {"detect_mime_type_with_sniffing", test_detect_mime_type_with_sniffing},
    {NULL, NULL},
};
