#include "dataobj.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GCMZ_DEBUG 0

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

#include <ovarray.h>

#include <ovl/path.h>
#include <ovl/source.h>

#include "dataobj_stream.h"
#include "datauri.h"
#include "file.h"
#include "sniffer.h"
#include "temp.h"

static size_t extract_file_name(wchar_t const *path) {
  if (!path) {
    return 0;
  }
  wchar_t const *bslash = wcsrchr(path, L'\\');
  wchar_t const *slash = wcsrchr(path, L'/');
  if (bslash == NULL && slash == NULL) {
    return 0;
  }
  wchar_t const *separator = NULL;
  if (bslash != NULL && slash != NULL) {
    separator = bslash > slash ? bslash : slash;
  } else {
    separator = bslash != NULL ? bslash : slash;
  }
  return (size_t)(separator + 1 - path);
}

static size_t extract_file_extension(wchar_t const *filename) {
  if (!filename) {
    return 0;
  }
  size_t len = wcslen(filename);
  size_t filename_start = extract_file_name(filename);
  for (size_t i = len; i > filename_start; --i) {
    if (filename[i - 1] == L'.') {
      // Don't treat dot at start of filename as extension (e.g., ".gitignore")
      if (i - 1 > filename_start) {
        return i - 1;
      }
    }
  }
  return len;
}

static NODISCARD bool get_data(IDataObject *const dataobj,
                               CLIPFORMAT const format,
                               LONG const index,
                               void **const data,
                               size_t *const len,
                               struct ov_error *const err) {
  if (!dataobj || !data || !len) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  struct ovl_source *source = NULL;
  char *buf = NULL;
  bool result = false;

  {
    if (!gcmz_dataobj_source_create(dataobj,
                                    &(FORMATETC){
                                        .cfFormat = format,
                                        .ptd = NULL,
                                        .dwAspect = DVASPECT_CONTENT,
                                        .lindex = index,
                                        .tymed = TYMED_HGLOBAL,
                                    },
                                    &source,
                                    err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    uint64_t const size = ovl_source_size(source);
    if (size == UINT64_MAX || size > SIZE_MAX) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
      goto cleanup;
    }
    if (!OV_ARRAY_GROW(&buf, (size_t)size)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    size_t const bytes_read = ovl_source_read(source, buf, 0, (size_t)size);
    if (bytes_read == SIZE_MAX || bytes_read != (size_t)size) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    OV_ARRAY_SET_LENGTH(buf, bytes_read);
    *data = buf;
    *len = bytes_read;
    buf = NULL;
  }
  result = true;

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  if (buf) {
    OV_ARRAY_DESTROY(&buf);
  }
  return result;
}

static NODISCARD bool create_temp_file_from_data(void const *data,
                                                 size_t data_len,
                                                 wchar_t const *filename,
                                                 wchar_t const *mime_type,
                                                 struct gcmz_file_list *files,
                                                 struct ov_error *const err) {
  if (!data || !data_len || !filename || !mime_type || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *temp_file = NULL;
  HANDLE hFile = INVALID_HANDLE_VALUE;
  bool result = false;

  if (!gcmz_temp_create_unique_file(filename, &temp_file, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  hFile = CreateFileW(temp_file, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  {
    DWORD bytes_written;
    BOOL write_result = WriteFile(hFile, data, (DWORD)data_len, &bytes_written, NULL);
    if (!write_result || bytes_written != data_len) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!gcmz_file_list_add_temporary(files, temp_file, mime_type, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }
  result = true;

cleanup:
  if (hFile != INVALID_HANDLE_VALUE) {
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;
  }
  if (temp_file) {
    if (!result) {
      DeleteFileW(temp_file);
    }
    OV_ARRAY_DESTROY(&temp_file);
  }
  return result;
}

static wchar_t const *detect_mime_type_from_extension(wchar_t const *filename) {
  static wchar_t const default_mime[] = L"application/octet-stream";

  if (!filename) {
    return default_mime;
  }

  size_t const ext_pos = extract_file_extension(filename);
  if (ext_pos >= wcslen(filename)) {
    return default_mime;
  }

  wchar_t const *ext = filename + ext_pos;
  if (ovl_path_is_same_ext(ext, L".txt")) {
    return L"text/plain";
  } else if (ovl_path_is_same_ext(ext, L".html") || ovl_path_is_same_ext(ext, L".htm")) {
    return L"text/html";
  } else if (ovl_path_is_same_ext(ext, L".css")) {
    return L"text/css";
  } else if (ovl_path_is_same_ext(ext, L".js")) {
    return L"application/javascript";
  } else if (ovl_path_is_same_ext(ext, L".json")) {
    return L"application/json";
  } else if (ovl_path_is_same_ext(ext, L".xml")) {
    return L"application/xml";
  } else if (ovl_path_is_same_ext(ext, L".pdf")) {
    return L"application/pdf";
  } else if (ovl_path_is_same_ext(ext, L".zip")) {
    return L"application/zip";
  } else if (ovl_path_is_same_ext(ext, L".rar")) {
    return L"application/x-rar-compressed";
  } else if (ovl_path_is_same_ext(ext, L".7z")) {
    return L"application/x-7z-compressed";
  } else if (ovl_path_is_same_ext(ext, L".png")) {
    return L"image/png";
  } else if (ovl_path_is_same_ext(ext, L".jpg") || ovl_path_is_same_ext(ext, L".jpeg")) {
    return L"image/jpeg";
  } else if (ovl_path_is_same_ext(ext, L".gif")) {
    return L"image/gif";
  } else if (ovl_path_is_same_ext(ext, L".bmp")) {
    return L"image/bmp";
  } else if (ovl_path_is_same_ext(ext, L".svg")) {
    return L"image/svg+xml";
  } else if (ovl_path_is_same_ext(ext, L".ico")) {
    return L"image/x-icon";
  } else if (ovl_path_is_same_ext(ext, L".mp3")) {
    return L"audio/mpeg";
  } else if (ovl_path_is_same_ext(ext, L".wav")) {
    return L"audio/wav";
  } else if (ovl_path_is_same_ext(ext, L".mp4")) {
    return L"video/mp4";
  } else if (ovl_path_is_same_ext(ext, L".avi")) {
    return L"video/x-msvideo";
  } else if (ovl_path_is_same_ext(ext, L".doc")) {
    return L"application/msword";
  } else if (ovl_path_is_same_ext(ext, L".docx")) {
    return L"application/vnd.openxmlformats-officedocument.wordprocessingml.document";
  } else if (ovl_path_is_same_ext(ext, L".xls")) {
    return L"application/vnd.ms-excel";
  } else if (ovl_path_is_same_ext(ext, L".xlsx")) {
    return L"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
  } else if (ovl_path_is_same_ext(ext, L".ppt")) {
    return L"application/vnd.ms-powerpoint";
  } else if (ovl_path_is_same_ext(ext, L".pptx")) {
    return L"application/vnd.openxmlformats-officedocument.presentationml.presentation";
  } else {
    return default_mime;
  }
}

static wchar_t const *detect_mime_type_with_sniffing(void const *data,
                                                     size_t data_len,
                                                     wchar_t const *filename,
                                                     wchar_t const **suggested_extension) {
  static wchar_t const default_mime[] = L"application/octet-stream";
  wchar_t const *sniffed_mime = NULL;
  wchar_t const *sniffed_ext = NULL;
  if (data && data_len > 0) {
    if (gcmz_sniff(data, data_len, &sniffed_mime, &sniffed_ext)) {
      if (suggested_extension) {
        *suggested_extension = sniffed_ext;
      }
      return sniffed_mime;
    }
  }
  // Fall back to extension-based detection if filename is provided
  if (filename) {
    // For extension-based detection, use the original extension
    if (suggested_extension) {
      size_t const ext_pos = extract_file_extension(filename);
      if (ext_pos < wcslen(filename)) {
        *suggested_extension = filename + ext_pos;
      } else {
        *suggested_extension = L"";
      }
    }
    return detect_mime_type_from_extension(filename);
  }
  if (suggested_extension) {
    *suggested_extension = L".bin";
  }
  return default_mime;
}

static void sanitize_filename(NATIVE_CHAR *filename) {
  if (!filename) {
    return;
  }
  for (NATIVE_CHAR *p = filename; *p; ++p) {
    NATIVE_CHAR const c = *p;
    if (c <= 0x1f || c == 0x22 || c == 0x2a || c == 0x2b || c == 0x2f || c == 0x3a || c == 0x3c || c == 0x3e ||
        c == 0x3f || c == 0x7c || c == 0x7f) {
      *p = L'-';
    }
  }
  NATIVE_CHAR const *reserved_names[] = {
      L"CON",  L"PRN",  L"AUX",  L"NUL",  L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8",
      L"COM9", L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9", NULL,
  };

  for (int i = 0; reserved_names[i]; ++i) {
    // extension_equals actually wcsicmp() == 0 for ASCIIs, so it works here.
    if (ovl_path_is_same_ext(filename, reserved_names[i])) {
      filename[0] = L'-';
      break;
    }
  }
  size_t const len = wcslen(filename);
  if (len > 255) {
    filename[255] = L'\0';
  }
}

static NODISCARD bool try_extract_custom_image_format(IDataObject *const dataobj,
                                                      wchar_t const *format_name,
                                                      wchar_t const *fallback_extension,
                                                      wchar_t const *fallback_mime_type,
                                                      struct gcmz_file_list *const files,
                                                      struct ov_error *const err) {
  if (!dataobj || !files || !format_name || !fallback_extension || !fallback_mime_type) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  void *data = NULL;
  bool result = false;

  {
    CLIPFORMAT const custom_format = (CLIPFORMAT)RegisterClipboardFormatW(format_name);
    if (custom_format == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    size_t data_len = 0;
    if (!get_data(dataobj, custom_format, -1, &data, &data_len, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    wchar_t const *suggested_ext = NULL;
    wchar_t fallback_filename[64];
    wcscpy(fallback_filename, L"image");
    wcscat(fallback_filename, fallback_extension);
    wchar_t const *mime_type = detect_mime_type_with_sniffing(data, data_len, fallback_filename, &suggested_ext);
    if (!suggested_ext) {
      suggested_ext = fallback_extension;
    }

    wchar_t combined_filename[64];
    wcscpy(combined_filename, L"image");
    wcscat(combined_filename, suggested_ext);
    if (!create_temp_file_from_data(data, data_len, combined_filename, mime_type, files, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (data) {
    OV_ARRAY_DESTROY(&data);
  }
  return result;
}

static NODISCARD bool process_single_file(IDataObject *const dataobj,
                                          CLIPFORMAT fmt,
                                          LONG index,
                                          FILEDESCRIPTORW const *const fd,
                                          struct gcmz_file_list *const files,
                                          struct ov_error *const err) {
  void *data = NULL;
  bool result = false;

  {
    size_t len = 0;
    if (!get_data(dataobj, fmt, index, &data, &len, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    wchar_t filename[MAX_PATH];
    wchar_t extension[MAX_PATH];
    size_t const name_pos = extract_file_name(fd->cFileName);
    wcsncpy(filename, fd->cFileName + name_pos, MAX_PATH - 1);
    filename[MAX_PATH - 1] = L'\0';
    sanitize_filename(filename);
    size_t const ext_pos = extract_file_extension(filename);
    if (ext_pos < wcslen(filename)) {
      wcscpy(extension, filename + ext_pos);
      filename[ext_pos] = L'\0';
    }

    wchar_t const *suggested_ext = NULL;
    wchar_t const *mime_type = detect_mime_type_with_sniffing(data, len, filename, &suggested_ext);
    if (suggested_ext && wcslen(suggested_ext) > 0 && wcslen(extension) == 0) {
      wcsncpy(extension, suggested_ext, MAX_PATH - 1);
      extension[MAX_PATH - 1] = L'\0';
    }

    wchar_t combined_filename[MAX_PATH * 2];
    wcscpy(combined_filename, filename);
    wcscat(combined_filename, extension);
    if (!create_temp_file_from_data(data, len, combined_filename, mime_type, files, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (data) {
    OV_ARRAY_DESTROY(&data);
  }
  return result;
}

static NODISCARD bool
try_extract_file_contents(IDataObject *const dataobj, struct gcmz_file_list *const files, struct ov_error *const err) {
  if (!dataobj || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  FILEGROUPDESCRIPTORW *desc = NULL;
  size_t const initial_count = gcmz_file_list_count(files);
  bool result = false;

  {
    CLIPFORMAT const fmt = (CLIPFORMAT)RegisterClipboardFormatW(L"FileGroupDescriptorW");
    if (fmt == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    size_t desc_len = 0;
    if (!get_data(dataobj, fmt, -1, (void **)&desc, &desc_len, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (desc_len < sizeof(UINT)) {
      OV_ERROR_SET(
          err, ov_error_type_generic, ov_error_generic_invalid_argument, "FileGroupDescriptorW data too small");
      goto cleanup;
    }
    size_t const expected_size = sizeof(UINT) + (desc->cItems * sizeof(FILEDESCRIPTORW));
    if (desc_len < expected_size) {
      OV_ERROR_SET(
          err, ov_error_type_generic, ov_error_generic_invalid_argument, "FileGroupDescriptorW data incomplete");
      goto cleanup;
    }
    CLIPFORMAT const fc_fmt = (CLIPFORMAT)RegisterClipboardFormatW(L"FileContents");
    if (fc_fmt == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    for (UINT i = 0; i < desc->cItems; ++i) {
      FILEDESCRIPTORW const *const fd = &desc->fgd[i];
      if (fd->dwFlags & FD_ATTRIBUTES && fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        continue;
      }
      if (!process_single_file(dataobj, fc_fmt, (LONG)i, fd, files, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
    }
  }

  result = true;

cleanup:
  if (desc) {
    OV_ARRAY_DESTROY(&desc);
  }
  if (!result) {
    // Rollback: Remove any files added during this failed operation
    size_t const current_count = gcmz_file_list_count(files);
    for (size_t i = current_count; i > initial_count; --i) {
      struct gcmz_file const *const file = gcmz_file_list_get_mutable(files, i - 1);
      if (file && file->path && file->temporary) {
        DeleteFileW(file->path); // Remove temporary file
      }
    }
  }
  return result;
}

static NODISCARD bool
try_extract_dib_format(IDataObject *const dataobj, struct gcmz_file_list *const files, struct ov_error *const err) {
  if (!dataobj || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  uint8_t *bmp = NULL;
  BITMAPINFOHEADER *bih = NULL;
  size_t data_len;
  bool result = false;

  {
    if (!get_data(dataobj, CF_DIB, -1, (void **)&bih, &data_len, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    if (data_len < sizeof(BITMAPINFOHEADER)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "DIB data too small");
      goto cleanup;
    }
    if (bih->biSize < sizeof(BITMAPINFOHEADER)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "invalid BITMAPINFOHEADER size");
      goto cleanup;
    }
    DWORD bfOffBits = sizeof(BITMAPFILEHEADER) + bih->biSize;
    switch (bih->biBitCount) {
    case 1:
    case 4:
    case 8:
      bfOffBits += (bih->biClrUsed ? bih->biClrUsed : 1 << bih->biBitCount) * sizeof(RGBQUAD);
      break;
    case 16:
    case 24:
    case 32:
      bfOffBits += bih->biClrUsed * sizeof(RGBQUAD);
      if (bih->biCompression == BI_BITFIELDS) {
        bfOffBits += sizeof(DWORD) * 3;
      }
      break;
    default:
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "unsupported bit depth");
      goto cleanup;
    }

    // Create BMP file by adding BITMAPFILEHEADER to DIB data
    size_t const bmp_len = sizeof(BITMAPFILEHEADER) + data_len;
    if (!OV_ARRAY_GROW(&bmp, bmp_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    BITMAPFILEHEADER *const bfh = (BITMAPFILEHEADER *)(void *)bmp;
    *bfh = (BITMAPFILEHEADER){
        .bfType = 0x4d42,
        .bfSize = (DWORD)bmp_len,
        .bfReserved1 = 0,
        .bfReserved2 = 0,
        .bfOffBits = bfOffBits,
    };
    memcpy((char *)bmp + sizeof(BITMAPFILEHEADER), bih, data_len);

    wchar_t const *suggested_ext = NULL;
    wchar_t const *mime_type = detect_mime_type_with_sniffing(bmp, bmp_len, L"image.bmp", &suggested_ext);
    if (!suggested_ext) {
      suggested_ext = L".bmp";
    }
    wchar_t combined_filename[64];
    wcscpy(combined_filename, L"image");
    wcscat(combined_filename, suggested_ext);
    if (!create_temp_file_from_data(bmp, bmp_len, combined_filename, mime_type, files, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }
  result = true;

cleanup:
  if (bmp) {
    OV_ARRAY_DESTROY(&bmp);
  }
  if (bih) {
    OV_ARRAY_DESTROY(&bih);
  }
  return result;
}

static NODISCARD bool
try_extract_hdrop_format(IDataObject *const dataobj, struct gcmz_file_list *const files, struct ov_error *const err) {
  if (!dataobj || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_source *source = NULL;
  wchar_t *filename = NULL;
  wchar_t *filenames_data = NULL;
  bool result = false;

  {
    if (!gcmz_dataobj_source_create(dataobj,
                                    &(FORMATETC){
                                        .cfFormat = CF_HDROP,
                                        .ptd = NULL,
                                        .dwAspect = DVASPECT_CONTENT,
                                        .lindex = -1,
                                        .tymed = TYMED_HGLOBAL,
                                    },
                                    &source,
                                    err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const size = ovl_source_size(source);
    if (size == UINT64_MAX || size < sizeof(DROPFILES)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "invalid HDROP data size");
      goto cleanup;
    }

    DROPFILES dropfiles = {0};
    size_t const read_size = ovl_source_read(source, &dropfiles, 0, sizeof(dropfiles));
    if (read_size != sizeof(dropfiles)) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read DROPFILES header");
      goto cleanup;
    }

    if (dropfiles.pFiles >= size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "invalid DROPFILES offset");
      goto cleanup;
    }

    uint64_t const filenames_size = size - dropfiles.pFiles;
    if (!OV_ARRAY_GROW(&filenames_data, (size_t)filenames_size / sizeof(wchar_t))) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const filenames_read = ovl_source_read(source, filenames_data, dropfiles.pFiles, (size_t)filenames_size);
    if (filenames_read != (size_t)filenames_size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read filenames data");
      goto cleanup;
    }

    wchar_t const *current = filenames_data;
    wchar_t const *end = filenames_data + (filenames_size / sizeof(wchar_t));

    while (current < end && *current != L'\0') {
      size_t const len = wcsnlen(current, (size_t)(end - current));
      if (len == 0) {
        break;
      }

      if (!OV_ARRAY_GROW(&filename, len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }

      wcsncpy(filename, current, len);
      filename[len] = L'\0';

      if (!gcmz_file_list_add(files, filename, NULL, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }

      current += len + 1;
    }
  }

  result = true;

cleanup:
  if (filenames_data) {
    OV_ARRAY_DESTROY(&filenames_data);
  }
  if (filename) {
    OV_ARRAY_DESTROY(&filename);
  }
  if (source) {
    ovl_source_destroy(&source);
  }
  return result;
}

static NODISCARD bool
try_extract_data_uri(IDataObject *const dataobj, struct gcmz_file_list *const files, struct ov_error *const err) {
  if (!dataobj || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_source *source = NULL;
  wchar_t *text_data = NULL;
  struct gcmz_data_uri data_uri = {0};
  wchar_t *suggested_filename = NULL;
  wchar_t *mime_type = NULL;
  bool result = false;

  {
    if (!gcmz_dataobj_source_create(dataobj,
                                    &(FORMATETC){
                                        .cfFormat = CF_UNICODETEXT,
                                        .ptd = NULL,
                                        .dwAspect = DVASPECT_CONTENT,
                                        .lindex = -1,
                                        .tymed = TYMED_HGLOBAL,
                                    },
                                    &source,
                                    err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const size = ovl_source_size(source);
    if (size == UINT64_MAX || size == 0 || size % sizeof(wchar_t) != 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "invalid Unicode text data size");
      goto cleanup;
    }

    size_t const text_char_count = (size_t)size / sizeof(wchar_t);
    if (!OV_ARRAY_GROW(&text_data, text_char_count + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const read_size = ovl_source_read(source, text_data, 0, (size_t)size);
    if (read_size != (size_t)size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read text data");
      goto cleanup;
    }

    text_data[text_char_count] = L'\0';
    size_t const text_len = wcsnlen(text_data, text_char_count);

    if (!gcmz_data_uri_parse(text_data, text_len, &data_uri, err)) {
      // Not a data URI - this is expected, not an error for this function
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!gcmz_data_uri_decode(&data_uri, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!gcmz_data_uri_suggest_filename(&data_uri, &suggested_filename, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    if (!gcmz_data_uri_get_mime(&data_uri, &mime_type, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    wchar_t const *final_mime_type = mime_type ? mime_type : L"application/octet-stream";
    if (!create_temp_file_from_data(
            data_uri.decoded, data_uri.decoded_len, suggested_filename, final_mime_type, files, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (mime_type) {
    OV_ARRAY_DESTROY(&mime_type);
  }
  if (suggested_filename) {
    OV_ARRAY_DESTROY(&suggested_filename);
  }
  if (text_data) {
    OV_ARRAY_DESTROY(&text_data);
  }
  gcmz_data_uri_destroy(&data_uri);
  if (source) {
    ovl_source_destroy(&source);
  }
  return result;
}

static NODISCARD bool
try_extract_plain_text(IDataObject *const dataobj, struct gcmz_file_list *const files, struct ov_error *const err) {
  if (!dataobj || !files) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_source *source = NULL;
  wchar_t *text_data = NULL;
  char *utf8_data = NULL;
  bool result = false;

  {
    if (!gcmz_dataobj_source_create(dataobj,
                                    &(FORMATETC){
                                        .cfFormat = CF_UNICODETEXT,
                                        .ptd = NULL,
                                        .dwAspect = DVASPECT_CONTENT,
                                        .lindex = -1,
                                        .tymed = TYMED_HGLOBAL,
                                    },
                                    &source,
                                    err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    uint64_t const size = ovl_source_size(source);
    if (size == UINT64_MAX || size == 0 || size % sizeof(wchar_t) != 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_invalid_argument, "invalid Unicode text data size");
      goto cleanup;
    }

    size_t const text_char_count = (size_t)size / sizeof(wchar_t);
    if (!OV_ARRAY_GROW(&text_data, text_char_count + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const read_size = ovl_source_read(source, text_data, 0, (size_t)size);
    if (read_size != (size_t)size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read text data");
      goto cleanup;
    }

    // Ensure null termination
    text_data[text_char_count] = L'\0';

    // Find actual text length (may be shorter than allocated)
    size_t text_len = wcsnlen(text_data, text_char_count);
    if (text_len == 0) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "empty text data");
      goto cleanup;
    }

    // Convert Unicode text to UTF-8 for file storage
    // First call to get required buffer size
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, text_data, (int)text_len, NULL, 0, NULL, NULL);
    if (utf8_len == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    if (!OV_ARRAY_GROW(&utf8_data, utf8_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    // Second call to perform actual conversion
    int converted_len = WideCharToMultiByte(CP_UTF8, 0, text_data, (int)text_len, utf8_data, utf8_len, NULL, NULL);
    if (converted_len == 0) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    // Create temporary text file
    if (!create_temp_file_from_data(utf8_data, (size_t)converted_len, L"text.txt", L"text/plain", files, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (utf8_data) {
    OV_ARRAY_DESTROY(&utf8_data);
  }
  if (text_data) {
    OV_ARRAY_DESTROY(&text_data);
  }
  if (source) {
    ovl_source_destroy(&source);
  }
  return result;
}

NODISCARD bool gcmz_dataobj_extract_from_dataobj(void *const dataobj,
                                                 struct gcmz_file_list *const file_list,
                                                 struct ov_error *const err) {
  if (!dataobj || !file_list) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Starting custom format extraction\n");
#endif

  size_t initial_count = gcmz_file_list_count(file_list);
  IDataObject *const obj = (IDataObject *)dataobj;

  // 1. Data URI (highest priority - no false positives)
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying Data URI format\n");
#endif
  bool data_uri_ret = try_extract_data_uri(obj, file_list, err);
  if (data_uri_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Data URI format extraction succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!data_uri_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Data URI format not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  // 2. PNG format
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying PNG format\n");
#endif
  bool png_ret = try_extract_custom_image_format(obj, L"PNG", L".png", L"image/png", file_list, err);
  if (png_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: PNG format extraction succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!png_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: PNG format not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  // 3. JPEG format
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying JPEG format\n");
#endif
  bool jpeg_ret = try_extract_custom_image_format(obj, L"JPEG", L".jpg", L"image/jpeg", file_list, err);
  if (jpeg_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: JPEG format extraction succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!jpeg_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: JPEG format not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  // 4. File contents (7-zip, browser files)
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying FileContents format\n");
#endif
  bool file_contents_ret = try_extract_file_contents(obj, file_list, err);
  if (file_contents_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: FileContents format extraction succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!file_contents_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: FileContents format not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  // 5. HDROP format (standard file drop)
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying HDROP format\n");
#endif
  bool hdrop_ret = try_extract_hdrop_format(obj, file_list, err);
  if (hdrop_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: HDROP format extraction succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!hdrop_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: HDROP format not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  // 6. DIB bitmap data (high false positive rate, so moved to end)
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying DIB format\n");
#endif
  bool dib_ret = try_extract_dib_format(obj, file_list, err);
  if (dib_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: DIB format extraction succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!dib_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: DIB format not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  // 7. Plain text fallback (final resort)
#if GCMZ_DEBUG
  OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Trying plain text fallback\n");
#endif
  bool text_ret = try_extract_plain_text(obj, file_list, err);
  if (text_ret && gcmz_file_list_count(file_list) > initial_count) {
#if GCMZ_DEBUG
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Plain text fallback succeeded\n");
#endif
    return true;
  }
#if GCMZ_DEBUG
  if (!text_ret) {
    OutputDebugStringW(L"gcmz_dataobj_extract_from_dataobj: Plain text fallback not available\n");
  }
#endif
  OV_ERROR_DESTROY(err);

  OV_ERROR_SET_GENERIC(err, ov_error_generic_not_found);
  return false;
}
