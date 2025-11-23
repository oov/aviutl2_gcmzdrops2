#include "datauri.h"

#include "sniffer.h"

#include <ovarray.h>
#include <ovmo.h>
#include <ovutf.h>

static const uint8_t base64_table[128] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62,
    255, 255, 255, 63,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  255, 255, 255, 255, 255, 255, 255, 0,
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,
    23,  24,  25,  255, 255, 255, 255, 255, 255, 26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,
    39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  255, 255, 255, 255, 255,
};

static inline uint_least8_t hex2int(wchar_t const c) {
  if (L'0' <= c && c <= L'9') {
    return (c & 0xff) - L'0';
  }
  if ((L'A' <= c && c <= L'F') || (L'a' <= c && c <= L'f')) {
    return (c & 0x5f) - L'A' + 10;
  }
  return 255;
}

NODISCARD static bool base64_decoded_len(wchar_t const *const ws, size_t const wslen, size_t *const len) {
  size_t const omitted_equals = (4 - wslen % 4) & 0x3;
  size_t pad = omitted_equals;
  for (size_t pos = wslen - 1; pos > 0 && ws[pos] == L'=' && pad < 3; --pos) {
    ++pad;
  }
  if (pad > 2) {
    // broken input
    return false;
  }
  *len = ((wslen + omitted_equals) / 4) * 3 - pad;
  return true;
}

NODISCARD static bool
base64_decode(wchar_t const *const ws, size_t const wslen, void *const data, size_t const datalen) {
  size_t len = 0;
  if (!base64_decoded_len(ws, wslen, &len)) {
    return false;
  }
  if (len > datalen) {
    return false;
  }
  uint8_t *d = (uint8_t *)data;
  size_t const end = (len * 4 + 2) / 3;
  size_t const remain = end % 4;
  size_t const last = end - remain;
  for (size_t i = 0; i < last; i += 4) {
    wchar_t const c0 = ws[i + 0], c1 = ws[i + 1], c2 = ws[i + 2], c3 = ws[i + 3];
    if (c0 > 127 || c1 > 127 || c2 > 127 || c3 > 127) {
      return false;
    }
    uint_least8_t const p0 = base64_table[c0], p1 = base64_table[c1], p2 = base64_table[c2], p3 = base64_table[c3];
    if (p0 == 255 || p1 == 255 || p2 == 255 || p3 == 255) {
      return false;
    }
    uint_least32_t const v = (uint_least32_t)((p0 << 18) | (p1 << 12) | (p2 << 6) | (p3 << 0));
    *d++ = (v >> 16) & 0xff;
    *d++ = (v >> 8) & 0xff;
    *d++ = (v >> 0) & 0xff;
  }
  if (remain > 0) {
    wchar_t const c0 = ws[last], c1 = ws[last + 1], c2 = remain == 3 ? ws[last + 2] : L'A';
    if (c0 > 127 || c1 > 127 || c2 > 127) {
      return false;
    }
    uint_least8_t const p0 = base64_table[c0], p1 = base64_table[c1], p2 = base64_table[c2];
    if (p0 == 255 || p1 == 255 || p2 == 255) {
      return false;
    }
    uint_least32_t const v = (uint_least32_t)((p0 << 18) | (p1 << 12) | (p2 << 6));
    *d++ = (v >> 16) & 0xff;
    if (remain == 3) {
      *d++ = (v >> 8) & 0xff;
    }
  }
  return true;
}

NODISCARD static bool percent_decoded_len(wchar_t const *const ws, size_t const wslen, size_t *const len) {
  size_t n = 0;
  for (size_t i = 0; i < wslen; ++i) {
    wchar_t const c = ws[i];
    if (c > 127) {
      return false;
    }
    if (c != L'%') {
      ++n;
      continue;
    }
    if (i + 2 >= wslen) {
      return false;
    }
    wchar_t const c1 = ws[i + 1], c2 = ws[i + 2];
    if (c1 > 127 || c2 > 127) {
      return false;
    }
    uint_least8_t const v1 = hex2int(c1), v2 = hex2int(c2);
    if (v1 == 255 || v2 == 255) {
      return false;
    }
    ++n;
    i += 2;
  }
  *len = n;
  return true;
}

NODISCARD static bool
percent_decode(wchar_t const *const ws, size_t const wslen, void *const data, size_t const datalen) {
  uint8_t *d = (uint8_t *)data;
  size_t n = 0;
  for (size_t i = 0; i < wslen; ++i) {
    wchar_t const c = ws[i];
    if (c > 127) {
      return false;
    }
    if (c != L'%') {
      if (n == datalen) {
        return false;
      }
      *d++ = c & 0xff;
      ++n;
      continue;
    }
    if (i + 2 >= wslen) {
      return false;
    }
    wchar_t const c1 = ws[i + 1], c2 = ws[i + 2];
    if (c1 > 127 || c2 > 127) {
      return false;
    }
    uint_least8_t const v1 = hex2int(c1), v2 = hex2int(c2);
    if (v1 == 255 || v2 == 255) {
      return false;
    }
    if (n == datalen) {
      return false;
    }
    *d++ = (uint8_t)((v1 << 4) | (v2 << 0));
    ++n;
    i += 2;
  }
  return true;
}

static size_t extract_file_name_pos(wchar_t const *const path) {
  if (!path) {
    return 0;
  }
  wchar_t const *const bslash = wcsrchr(path, L'\\');
  wchar_t const *const slash = wcsrchr(path, L'/');
  if (bslash == NULL && slash == NULL) {
    return 0;
  }
  if (bslash != NULL && slash != NULL) {
    return (size_t)((bslash > slash ? bslash : slash) + 1 - path);
  }
  return (size_t)((bslash != NULL ? bslash : slash) + 1 - path);
}

static void sanitize_string(wchar_t *const s, size_t const len) {
  if (!s) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    wchar_t const c = s[i];
    if (c == L'\0') {
      break;
    }
    if (c <= 0x1f || c == 0x22 || c == 0x2a || c == 0x2b || c == 0x2f || c == 0x3a || c == 0x3c || c == 0x3e ||
        c == 0x3f || c == 0x7c || c == 0x7f) {
      s[i] = L'-';
    }
  }
}

bool gcmz_data_uri_parse(wchar_t const *ws,
                         size_t const wslen,
                         struct gcmz_data_uri *const d,
                         struct ov_error *const err) {
  if (!ws) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!d) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  if (wslen < 5 || wcsncmp(ws, L"data:", 5) != 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }
  wchar_t const *const body = ws + 5;
  size_t const bodylen = wslen - 5;
  wchar_t const *const comma = wcsstr(body, L",");
  if (!comma) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    return false;
  }

  struct gcmz_data_uri dd = {0};
  char tmp[128] = {0};
  wchar_t *tmp2 = NULL;
  bool success = false;

  size_t const headerlen = (size_t)(comma - body);
  if (headerlen > 0) {
    wchar_t const *cur = body;
    while (cur < comma) {
      wchar_t const *const sep = wcspbrk(cur, L";,");
      size_t const len = (size_t)(sep - cur);
      if (len == 6 && wcsncmp(cur, L"base64", 6) == 0) {
        dd.encoding = data_uri_encoding_base64;
        cur += len + 1;
        continue;
      }
      if (len > 8 && wcsncmp(cur, L"charset=", 8) == 0) {
        if (len - 8 >= 128) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        wcsncpy(dd.charset, cur + 8, len - 8);
        dd.charset[len - 8] = L'\0';
        cur += len + 1;
        continue;
      }
      // Non-standard exntension for filename
      if (len > 9 && wcsncmp(cur, L"filename=", 9) == 0) {
        size_t sz = 0;
        if (!percent_decoded_len(cur + 9, len - 9, &sz)) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        if (sz >= 128) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        if (!percent_decode(cur + 9, len - 9, tmp, sz)) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        tmp[sz] = L'\0';
        size_t const wchar_len = ov_utf8_to_wchar_len(tmp, sz);
        if (wchar_len == 0) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        if (!OV_ARRAY_GROW(&tmp2, wchar_len + 1)) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
          goto cleanup;
        }
        size_t const converted_len = ov_utf8_to_wchar(tmp, sz, tmp2, wchar_len + 1, NULL);
        if (converted_len == 0) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        if (converted_len >= 128) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
          goto cleanup;
        }
        wcscpy(dd.ext_filename, tmp2);
        cur += len + 1;
        continue;
      }
      if (dd.mime[0] != L'\0') {
        // unknown block
        cur += len + 1;
        continue;
      }
      if (len >= 256) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }
      wcsncpy(dd.mime, cur, len);
      dd.mime[len] = L'\0';
      cur += len + 1;
    }
  }
  if (dd.mime[0] == L'\0' && dd.charset[0] == L'\0') {
    wcscpy(dd.mime, L"text/plain");
    wcscpy(dd.charset, L"US-ASCII");
  }
  // RFC 2046: Default charset for text/* MIME types is US-ASCII
  if (dd.charset[0] == L'\0' && wcsncmp(dd.mime, L"text/", 5) == 0) {
    wcscpy(dd.charset, L"US-ASCII");
  }
  dd.encoded = (wchar_t const *)comma + 1;
  dd.encoded_len = bodylen - (size_t)(dd.encoded - body);
  *d = dd;
  success = true;

cleanup:
  if (tmp2) {
    OV_ARRAY_DESTROY(&tmp2);
  }
  return success;
}

bool gcmz_data_uri_decode(struct gcmz_data_uri *const d, struct ov_error *const err) {
  if (!d) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool (*decoded_len_func)(wchar_t const *const, size_t const, size_t *const) = NULL;
  bool (*decode_func)(wchar_t const *const, size_t const, void *, size_t const) = NULL;
  switch (d->encoding) {
  case data_uri_encoding_percent:
    decoded_len_func = percent_decoded_len;
    decode_func = percent_decode;
    break;
  case data_uri_encoding_base64:
    decoded_len_func = base64_decoded_len;
    decode_func = base64_decode;
    break;
  default:
    OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
    return false;
  }

  uint8_t *decoded = NULL;
  size_t decoded_len = 0;
  bool success = false;

  if (d->encoded_len) {
    if (!decoded_len_func(d->encoded, d->encoded_len, &decoded_len)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }
    if (decoded_len) {
      if (!OV_ARRAY_GROW(&decoded, decoded_len)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      if (!decode_func(d->encoded, d->encoded_len, decoded, decoded_len)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }
    }
  }
  d->decoded = decoded;
  decoded = NULL;
  d->decoded_len = decoded_len;
  success = true;

cleanup:
  if (decoded) {
    OV_ARRAY_DESTROY(&decoded);
  }
  return success;
}

void gcmz_data_uri_destroy(struct gcmz_data_uri *const d) {
  if (!d) {
    return;
  }
  if (d->decoded) {
    OV_ARRAY_DESTROY(&d->decoded);
  }
  d->decoded_len = 0;
}

static wchar_t const *mime_to_extension(wchar_t const *const mime) {
  if (wcscmp(mime, L"image/x-icon") == 0) {
    return L".ico";
  }
  if (wcscmp(mime, L"image/vnd.microsoft.icon") == 0) {
    return L".ico";
  }
  if (wcscmp(mime, L"image/bmp") == 0) {
    return L".bmp";
  }
  if (wcscmp(mime, L"image/gif") == 0) {
    return L".gif";
  }
  if (wcscmp(mime, L"image/webp") == 0) {
    return L".webp";
  }
  if (wcscmp(mime, L"image/png") == 0) {
    return L".png";
  }
  if (wcscmp(mime, L"image/jpeg") == 0) {
    return L".jpg";
  }
  if (wcscmp(mime, L"audio/basic") == 0) {
    return L".snd";
  }
  if (wcscmp(mime, L"audio/aiff") == 0) {
    return L".aiff";
  }
  if (wcscmp(mime, L"audio/mpeg") == 0) {
    return L".mp3";
  }
  if (wcscmp(mime, L"application/ogg") == 0) {
    return L".ogg";
  }
  if (wcscmp(mime, L"audio/midi") == 0) {
    return L".mid";
  }
  if (wcscmp(mime, L"video/avi") == 0) {
    return L".avi";
  }
  if (wcscmp(mime, L"audio/wave") == 0) {
    return L".wav";
  }
  if (wcscmp(mime, L"video/mp4") == 0) {
    return L".mp4";
  }
  if (wcscmp(mime, L"video/webm") == 0) {
    return L".webm";
  }
  if (wcscmp(mime, L"application/pdf") == 0) {
    return L".pdf";
  }
  if (wcscmp(mime, L"text/plain") == 0) {
    return L".txt";
  }
  return NULL;
}

bool gcmz_data_uri_suggest_filename(struct gcmz_data_uri const *const d,
                                    wchar_t **const dest,
                                    struct ov_error *const err) {
  if (!d) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *tmp = NULL;
  wchar_t *filename = NULL;
  bool success = false;

  // If filename is stored, use it.
  if (d->ext_filename[0] != L'\0') {
    size_t const pos = extract_file_name_pos(d->ext_filename);
    size_t const len = wcslen(d->ext_filename + pos);
    if (len > 0) {
      if (!OV_ARRAY_GROW(dest, len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(*dest, d->ext_filename + pos);
      OV_ARRAY_SET_LENGTH(*dest, len);
      success = true;
      goto cleanup;
    }
  }

  // Use encoded strings in a way similar to a browser.
  if (d->encoded_len >= 24) {
    if (!OV_ARRAY_GROW(&tmp, 25)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcsncpy(tmp, d->encoded + d->encoded_len - 24, 24);
    tmp[24] = L'\0';
    sanitize_string(tmp, 24);

    size_t const pos = extract_file_name_pos(tmp);
    size_t const len = wcslen(tmp + pos);
    if (len > 0) {
      if (!OV_ARRAY_GROW(&filename, len + 1)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
        goto cleanup;
      }
      wcscpy(filename, tmp + pos);
    }
  }
  // Last resort.
  if (!filename) {
    size_t const noname_len = 6;
    if (!OV_ARRAY_GROW(&filename, noname_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(filename, L"noname");
  }

  {
    // Guessing file extension by MIME.
    wchar_t const *ext = mime_to_extension(d->mime);
    if (!ext && d->decoded && d->decoded_len >= 16) {
      // Guessing file extension by content.
      if (!gcmz_sniff(d->decoded, d->decoded_len, NULL, &ext)) {
        OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
        goto cleanup;
      }
    }
    // Last resort.
    if (!ext) {
      ext = L".bin";
    }

    size_t const filename_len = wcslen(filename);
    size_t const ext_len = wcslen(ext);
    if (!OV_ARRAY_GROW(&filename, filename_len + ext_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscat(filename, ext);

    size_t const final_len = filename_len + ext_len;
    if (!OV_ARRAY_GROW(dest, final_len + 1)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }
    wcscpy(*dest, filename);
    OV_ARRAY_SET_LENGTH(*dest, final_len);
  }
  success = true;

cleanup:
  if (filename) {
    OV_ARRAY_DESTROY(&filename);
  }
  if (tmp) {
    OV_ARRAY_DESTROY(&tmp);
  }
  return success;
}

bool gcmz_data_uri_get_mime(struct gcmz_data_uri const *const d, wchar_t **const dest, struct ov_error *const err) {
  if (!d) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }
  if (!dest) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  wchar_t *tmp = NULL;
  bool success = false;

  size_t const mime_len = wcslen(d->mime);
  bool const has_charset = d->charset[0] != L'\0';
  size_t const charset_len = has_charset ? wcslen(d->charset) : 0;
  size_t const total_len = mime_len + (has_charset ? 10 + charset_len : 0); // 10 for "; charset="

  if (!OV_ARRAY_GROW(&tmp, total_len + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  wcscpy(tmp, d->mime);
  if (has_charset) {
    wcscat(tmp, L"; charset=");
    wcscat(tmp, d->charset);
  }

  if (!OV_ARRAY_GROW(dest, total_len + 1)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  wcscpy(*dest, tmp);
  OV_ARRAY_SET_LENGTH(*dest, total_len);
  success = true;

cleanup:
  if (tmp) {
    OV_ARRAY_DESTROY(&tmp);
  }
  return success;
}
