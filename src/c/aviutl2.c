#include "aviutl2.h"

#include "aviutl2_sdk_c/aviutl2_logger2.h"
#include "aviutl2_sdk_c/aviutl2_plugin2.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#include <ovarray.h>
#include <ovcyrb64.h>
#include <ovmo.h>
#include <ovsort.h>
#include <ovutf.h>

#include <ovl/crypto.h>
#include <ovl/os.h>
#include <ovl/path.h>

#include "do.h"
#include "gcmz_types.h"
#include "ini_reader.h"
#include "ini_sign.h"
#include "ini_sign_key.h"
#include "logf.h"

static_assert(sizeof(g_ini_sign_public_key) == gcmz_sign_public_key_size,
              "Embedded public key size does not match expected size");

#define GCMZ_DEBUG 1

#if GCMZ_DEBUG
#  include <ovprintf.h>
#endif // GCMZ_DEBUG

struct aviutl2_main_context;

static struct version_info {
  char section_name[32];
  uint32_t version;
  size_t version_string;
  uint64_t version_string_hash;

  // Absolute addresses in target program
  size_t layer_window_context;
  size_t log_verbose_func;
  size_t log_info_func;
  size_t log_warn_func;
  size_t log_error_func;
  size_t set_frame_cursor_func;
  size_t set_display_layer_func;
  size_t set_display_zoom_func;

  // Offsets relative to layer_window_context
  size_t project_context_offset;

  // Offsets relative to main_context
  size_t project_data_offset;
  size_t project_path_offset;

  // Offsets relative to project_data structure
  size_t video_rate_offset;
  size_t video_scale_offset;
  size_t width_offset;
  size_t height_offset;
  size_t sample_rate_offset;
  size_t cursor_frame_offset;
  size_t display_frame_offset;
  size_t display_layer_offset;
  size_t display_zoom_offset;
} g_version_info = {0};

static HWND g_aviutl2_window[8] = {0};
static HMODULE g_aviutl2_module = NULL;

static inline bool is_valid_version_info(void) { return g_version_info.section_name[0] != '\0'; }

static void *calc_offset(void const *const addr, size_t const offset) {
  return (void *)((uintptr_t)addr + (uintptr_t)offset);
}

static bool parse_hex_u64(char const *const str, size_t const len, uint64_t *const result, struct ov_error *const err) {
  if (!str || !len || !result) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  uint64_t value = 0;
  char const *const p = str;
  size_t start = 0;

  // Skip "0x" prefix if present
  if (len >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    start = 2;
  }

  for (size_t i = start; i < len; ++i) {
    char const c = p[i];
    uint64_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = (uint64_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = (uint64_t)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = (uint64_t)(c - 'A' + 10);
    } else {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      return false;
    }

    // Check for overflow before shifting
    if (value > (UINT64_MAX >> 4)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      return false;
    }
    value = (value << 4) | digit;
  }

  *result = value;
  return true;
}

static bool parse_hex_zu(char const *const str, size_t const len, size_t *const result, struct ov_error *const err) {
  uint64_t tmp = 0;
  if (!parse_hex_u64(str, len, &tmp, err)) {
    return false;
  }
  if (sizeof(size_t) < sizeof(uint64_t)) {
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  if __has_warning("-Wtautological-type-limit-compare")
#    pragma GCC diagnostic ignored "-Wtautological-type-limit-compare"
#  endif
#endif // __GNUC__
    if (tmp > (uint64_t)SIZE_MAX) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      return false;
    }
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__
  }
  *result = (size_t)tmp;
  return true;
}

static bool parse_dec_u32(char const *const str, size_t const len, uint32_t *const result, struct ov_error *const err) {
  if (!str || !len || !result) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  uint64_t value = 0;
  for (size_t i = 0; i < len; ++i) {
    char const c = str[i];
    if (c < '0' || c > '9') {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      return false;
    }
    uint64_t const digit = (uint64_t)(c - '0');
    if (value > (UINT32_MAX - digit) / 10) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      return false;
    }
    value = value * 10 + digit;
  }

  *result = (uint32_t)value;
  return true;
}

struct section_info {
  char name[32];
  size_t line_number;
};

static int section_info_compare(void const *const a, void const *const b, void *userdata) {
  (void)userdata; // Unused parameter
  struct section_info const *const s0 = (struct section_info const *)a;
  struct section_info const *const s1 = (struct section_info const *)b;
  size_t const line0 = s0->line_number;
  size_t const line1 = s1->line_number;
  if (line0 < line1) {
    return -1;
  }
  if (line0 > line1) {
    return 1;
  }
  return 0;
}

static size_t get_module_size(HMODULE module, struct ov_error *const err) {
  if (!module) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return 0;
  }

  size_t result = 0;

  {
    IMAGE_DOS_HEADER const *dos_header = (IMAGE_DOS_HEADER const *)module;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    IMAGE_NT_HEADERS const *nt_headers =
        (IMAGE_NT_HEADERS const *)(void *)((size_t)module + (size_t)dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
      goto cleanup;
    }

    result = nt_headers->OptionalHeader.SizeOfImage;
  }

cleanup:
  return result;
}

static struct version_info const *
check_version_info(HMODULE module, struct version_info const *const info, size_t const module_size) {
  if (!info || !module) {
    return NULL;
  }

  // Check if version_string offset is within module bounds
  if ((size_t)info->version_string >= module_size) {
    return NULL;
  }

  wchar_t const *const version_str = (wchar_t const *)calc_offset(module, info->version_string);
  size_t len = 0;
  size_t const max_chars = (module_size - (size_t)info->version_string) / sizeof(wchar_t);
  size_t const safe_limit = max_chars < 64 ? max_chars : 64;
  while (len < safe_limit && version_str[len] != L'\0') {
    ++len;
  }
  if (len == 0) {
    return NULL;
  }
  if (len % 2 == 1) {
    len++; // include null terminator to make it even
  }
  struct ov_cyrb64 ctx;
  ov_cyrb64_init(&ctx, 0);
  ov_cyrb64_update(&ctx, (uint32_t const *)(void const *)version_str, len / 2);
  uint64_t const hash = ov_cyrb64_final(&ctx);
#if GCMZ_DEBUG
  {
    wchar_t buf[256];
    ov_snprintf_wchar(buf, 256, NULL, L"Detected version: %hs (hash: 0x%016llx)\n", info->section_name, hash);
    OutputDebugStringW(buf);
  }
#endif
  if (hash == info->version_string_hash) {
    return info;
  }
#if GCMZ_DEBUG
  {
    wchar_t buf[256];
    ov_snprintf_wchar(buf,
                      256,
                      NULL,
                      L"Version hash mismatch: calculated=0x%016llx, expected=0x%016llx\n",
                      hash,
                      info->version_string_hash);
    OutputDebugStringW(buf);
  }
#endif
  return NULL;
}

static void const *load_aviutl2_addr_ini(size_t *const size, struct ov_error *const err) {
  if (!size) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  void *dll_module = NULL;
  HRSRC hrsrc = NULL;
  HGLOBAL hglobal = NULL;
  void const *result = NULL;

  {
    if (!ovl_os_get_hinstance_from_fnptr((void *)load_aviutl2_addr_ini, &dll_module, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    hrsrc = FindResourceW((HMODULE)dll_module, L"ADDR", MAKEINTRESOURCEW(10));
    if (!hrsrc) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    hglobal = LoadResource((HMODULE)dll_module, hrsrc);
    if (!hglobal) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    *size = (size_t)SizeofResource((HMODULE)dll_module, hrsrc);
    if (!*size) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    result = LockResource(hglobal);
    if (!result) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
      goto cleanup;
    }
  }

cleanup:
  return result;
}

/**
 * Detect AviUtl2 version and verify INI signature
 *
 * Attempts to detect the AviUtl2 version by reading the embedded address information
 * and verifying the signature of the aviutl2_addr.ini file.
 *
 * @param aviutl2_module [in] Handle to the AviUtl2 module
 * @param dest [out] Pointer to version_info structure to be filled with detected information
 * @param err [in/out] Error information, set on failure
 * @return gcmz_aviutl2_status_success if version detected and signature verified
 *         gcmz_aviutl2_status_signature_failed if version detected but signature verification failed
 *         gcmz_aviutl2_status_unknown_binary if version not found in INI
 *         gcmz_aviutl2_status_error if fatal error occurred (err will be set)
 */
static enum gcmz_aviutl2_status
detect_version(HMODULE aviutl2_module, struct version_info *const dest, struct ov_error *const err) {
  struct gcmz_ini_reader *reader = NULL;
  struct section_info *sections = NULL;
  size_t section_count = 0;
  struct gcmz_ini_iter iter = {0};
  size_t module_size = 0;
  void const *resource_data = NULL;
  size_t resource_size = 0;
  enum gcmz_aviutl2_status result = gcmz_aviutl2_status_error;
  bool signature_verified = true;

  module_size = get_module_size(aviutl2_module, err);
  if (!module_size) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  resource_data = load_aviutl2_addr_ini(&resource_size, err);
  if (!resource_data) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_ini_reader_create(&reader, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_ini_reader_load_memory(reader, resource_data, resource_size, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_sign_verify(reader, g_ini_sign_public_key, err)) {
    if (!ov_error_is(err, ov_error_type_generic, ov_error_generic_fail)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
    // continue execution even if signature verification fails
    OV_ERROR_DESTROY(err);
    signature_verified = false;
  }

  section_count = gcmz_ini_reader_get_section_count(reader);
  if (section_count == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_fail);
    goto cleanup;
  }
  if (!OV_ARRAY_GROW(&sections, section_count)) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  {
    size_t i = 0;
    while (gcmz_ini_reader_iter_sections(reader, &iter) && i < section_count) {
      if (!iter.name || iter.name_len == 0) {
        continue;
      }
      if (iter.name_len >= sizeof(sections[i].name)) {
        // Section name too long - truncate and warn
        memcpy(sections[i].name, iter.name, sizeof(sections[i].name) - 1);
        sections[i].name[sizeof(sections[i].name) - 1] = '\0';
        {
          wchar_t buf[256];
          ov_snprintf_wchar(buf,
                            256,
                            NULL,
                            L"[WARN] Section name truncated (length: %zu, max: %zu): %.31hs\n",
                            iter.name_len,
                            sizeof(sections[i].name) - 1,
                            iter.name);
          OutputDebugStringW(buf);
        }
      } else {
        memcpy(sections[i].name, iter.name, iter.name_len);
        sections[i].name[iter.name_len] = '\0';
      }
      sections[i].line_number = iter.line_number;
      ++i;
    }
    section_count = i;
  }

  ov_qsort(sections, section_count, sizeof(struct section_info), section_info_compare, NULL);

  for (size_t i = 0; i < section_count; ++i) {
    struct version_info temp_info = {0};
    char const *const section_name = sections[i].name;
    strcpy(temp_info.section_name, section_name);

#define LOAD_FIELD(field, parse_func)                                                                                  \
  do {                                                                                                                 \
    static char const field_name[] = #field;                                                                           \
    struct gcmz_ini_value const value = gcmz_ini_reader_get_value(reader, section_name, field_name);                   \
    if (value.ptr && value.size > 0) {                                                                                 \
      if (!parse_func(value.ptr, value.size, &temp_info.field, err)) {                                                 \
        OV_ERROR_ADD_TRACE(err);                                                                                       \
        goto cleanup;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)

    LOAD_FIELD(version_string, parse_hex_zu);
    LOAD_FIELD(version_string_hash, parse_hex_u64);
    if (check_version_info(aviutl2_module, &temp_info, module_size)) {
      LOAD_FIELD(version, parse_dec_u32);
      LOAD_FIELD(layer_window_context, parse_hex_zu);
      LOAD_FIELD(log_verbose_func, parse_hex_zu);
      LOAD_FIELD(log_info_func, parse_hex_zu);
      LOAD_FIELD(log_warn_func, parse_hex_zu);
      LOAD_FIELD(log_error_func, parse_hex_zu);
      LOAD_FIELD(set_frame_cursor_func, parse_hex_zu);
      LOAD_FIELD(set_display_layer_func, parse_hex_zu);
      LOAD_FIELD(set_display_zoom_func, parse_hex_zu);
      LOAD_FIELD(project_context_offset, parse_hex_zu);
      LOAD_FIELD(project_data_offset, parse_hex_zu);
      LOAD_FIELD(project_path_offset, parse_hex_zu);
      LOAD_FIELD(video_rate_offset, parse_hex_zu);
      LOAD_FIELD(video_scale_offset, parse_hex_zu);
      LOAD_FIELD(width_offset, parse_hex_zu);
      LOAD_FIELD(height_offset, parse_hex_zu);
      LOAD_FIELD(sample_rate_offset, parse_hex_zu);
      LOAD_FIELD(cursor_frame_offset, parse_hex_zu);
      LOAD_FIELD(display_frame_offset, parse_hex_zu);
      LOAD_FIELD(display_layer_offset, parse_hex_zu);
      LOAD_FIELD(display_zoom_offset, parse_hex_zu);
      *dest = temp_info;
      result = signature_verified ? gcmz_aviutl2_status_success : gcmz_aviutl2_status_signature_failed;
      goto cleanup;
    }

#undef LOAD_FIELD
  }

  result = gcmz_aviutl2_status_unknown_binary;

cleanup:
  if (sections) {
    OV_ARRAY_DESTROY(&sections);
  }
  if (reader) {
    gcmz_ini_reader_destroy(&reader);
  }
  return result;
}

static struct aviutl2_main_context *get_main_context(void) {
  void *layer_ctx = *(void **)calc_offset(g_aviutl2_module, g_version_info.layer_window_context);
  return *(struct aviutl2_main_context **)calc_offset(layer_ctx, g_version_info.project_context_offset);
}

static void *get_internal_object_ptr(void) {
  struct aviutl2_main_context *ctx = get_main_context();
  return *(void **)calc_offset(ctx, g_version_info.project_data_offset);
}

static int get_project_data_int(size_t const offset) {
  void *internal_obj = get_internal_object_ptr();
  if (!internal_obj) {
    return 0;
  }
  return *(int *)calc_offset(internal_obj, offset);
}

static void set_project_data_int(size_t const offset, int const value) {
  void *internal_obj = get_internal_object_ptr();
  if (!internal_obj) {
    return;
  }
  *(int *)calc_offset(internal_obj, offset) = value;
}

static wchar_t const *get_project_path_internal(void) {
  struct aviutl2_main_context *ctx = get_main_context();
  return *(wchar_t const **)calc_offset(ctx, g_version_info.project_path_offset);
}

static void call_set_cursor_frame(void *userdata) {
  int const frame = *(int const *)userdata;
  typedef HRESULT(set_cursor_frame)(struct aviutl2_main_context * ctx, int frame);
  set_cursor_frame *f = (set_cursor_frame *)calc_offset(g_aviutl2_module, g_version_info.set_frame_cursor_func);
  struct aviutl2_main_context *ctx = get_main_context();
  f(ctx, frame);
}

static void call_set_display_layer(void *userdata) {
  int const layer = *(int const *)userdata;
  typedef HRESULT(set_display_layer)(void *this, int display_layer, char x);
  set_display_layer *f = (set_display_layer *)calc_offset(g_aviutl2_module, g_version_info.set_display_layer_func);
  void *ctx = *(void **)calc_offset(g_aviutl2_module, g_version_info.layer_window_context);
  f(ctx, layer, 1);
}

static void call_set_display_zoom(void *userdata) {
  int const zoom = *(int const *)userdata;
  typedef HRESULT(set_display_zoom)(void *this, int *display_zoom);
  set_display_zoom *f = (set_display_zoom *)calc_offset(g_aviutl2_module, g_version_info.set_display_zoom_func);
  set_project_data_int(g_version_info.display_zoom_offset, zoom);
  void *ctx = *(void **)calc_offset(g_aviutl2_module, g_version_info.layer_window_context);
  int *display_zoom_ptr = (int *)calc_offset(get_internal_object_ptr(), g_version_info.display_zoom_offset);
  f(ctx, display_zoom_ptr);
}

size_t gcmz_aviutl2_find_manager_windows(void **window, size_t const window_len, struct ov_error *const err) {
  if (!window || window_len == 0) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return SIZE_MAX;
  }

  DWORD const pid = GetCurrentProcessId();
  wchar_t const class_name[] = L"aviutl2Manager";

  size_t count = 0;
  HWND h = NULL;
  DWORD wpid;
  while ((h = FindWindowExW(NULL, h, class_name, NULL)) != NULL) {
    GetWindowThreadProcessId(h, &wpid);
    if (wpid != pid) {
      continue;
    }
    if (count >= window_len) {
      OV_ERROR_SET(
          err, ov_error_type_generic, ov_error_generic_fail, gettext("too many AviUtl2 manager windows found"));
      return SIZE_MAX;
    }
    window[count++] = h;
  }
  if (count < window_len) {
    window[count] = NULL;
  }
  return count;
}

enum gcmz_aviutl2_status gcmz_aviutl2_init(struct ov_error *const err) {
  enum gcmz_aviutl2_status result = gcmz_aviutl2_status_error;

  {
    HMODULE aviutl2_module = GetModuleHandleW(NULL);
    if (!aviutl2_module) {
      OV_ERROR_SET_HRESULT(err, HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }

    size_t const window_count = gcmz_aviutl2_find_manager_windows(
        (void **)g_aviutl2_window, sizeof(g_aviutl2_window) / sizeof(g_aviutl2_window[0]), err);
    if (window_count == SIZE_MAX) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }

    g_aviutl2_module = aviutl2_module;

    result = detect_version(aviutl2_module, &g_version_info, err);
    if (result == gcmz_aviutl2_status_error) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

cleanup:
  if (result == gcmz_aviutl2_status_error) {
    gcmz_aviutl2_cleanup();
  }
  return result;
}

void gcmz_aviutl2_cleanup(void) {
  g_version_info = (struct version_info){0};
  memset(g_aviutl2_window, 0, sizeof(g_aviutl2_window));
  g_aviutl2_module = NULL;
}

HWND gcmz_aviutl2_get_main_window(void) { return g_aviutl2_window[0]; }

wchar_t const *gcmz_aviutl2_get_project_path(void) {
  if (!is_valid_version_info()) {
    return NULL;
  }
  if (!g_version_info.project_path_offset) {
    return NULL;
  }
  return get_project_path_internal();
}

struct extended_project_info_context {
  int *display_frame;
  int *display_layer;
  int *display_zoom;
  wchar_t const **project_path;
};

static void get_extended_project_info_internal(void *data) {
  struct extended_project_info_context *ctx = (struct extended_project_info_context *)data;
  if (!ctx || !is_valid_version_info()) {
    return;
  }

  if (ctx->display_frame && g_version_info.display_frame_offset) {
    *ctx->display_frame = get_project_data_int(g_version_info.display_frame_offset);
  }
  if (ctx->display_layer && g_version_info.display_layer_offset) {
    *ctx->display_layer = get_project_data_int(g_version_info.display_layer_offset);
  }
  if (ctx->display_zoom && g_version_info.display_zoom_offset) {
    *ctx->display_zoom = get_project_data_int(g_version_info.display_zoom_offset);
  }
  if (ctx->project_path && g_version_info.project_path_offset) {
    *ctx->project_path = get_project_path_internal();
  }
}

bool gcmz_aviutl2_get_extended_project_info(int *display_frame,
                                            int *display_layer,
                                            int *display_zoom,
                                            wchar_t const **project_path,
                                            struct ov_error *const err) {
  if (!is_valid_version_info()) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_unexpected);
    return false;
  }

  struct extended_project_info_context ctx = {
      .display_frame = display_frame,
      .display_layer = display_layer,
      .display_zoom = display_zoom,
      .project_path = project_path,
  };

  gcmz_do_blocking(get_extended_project_info_internal, &ctx);

  return true;
}

void gcmz_aviutl2_set_cursor_frame(int frame) {
  if (!is_valid_version_info()) {
    return;
  }
  gcmz_do_blocking(call_set_cursor_frame, (void *)&frame);
}

void gcmz_aviutl2_set_display_layer(int layer) {
  if (!is_valid_version_info()) {
    return;
  }
  gcmz_do_blocking(call_set_display_layer, (void *)&layer);
}

void gcmz_aviutl2_set_display_zoom(int zoom) {
  if (!is_valid_version_info()) {
    return;
  }
  gcmz_do_blocking(call_set_display_zoom, (void *)&zoom);
}

static void get_simulated_edit_info_internal(void *data) {
  struct aviutl2_edit_info *info = (struct aviutl2_edit_info *)data;
  if (!info || !is_valid_version_info()) {
    return;
  }

  info->width = get_project_data_int(g_version_info.width_offset);
  info->height = get_project_data_int(g_version_info.height_offset);
  info->rate = get_project_data_int(g_version_info.video_rate_offset);
  info->scale = get_project_data_int(g_version_info.video_scale_offset);
  info->sample_rate = get_project_data_int(g_version_info.sample_rate_offset);
  info->frame = get_project_data_int(g_version_info.cursor_frame_offset);
  info->layer = get_project_data_int(g_version_info.display_layer_offset);
  info->frame_max = -1;
  info->layer_max = -1;
}

static bool simulated_call_edit_section(void (*func_proc_edit)(struct aviutl2_edit_section *edit)) {
  if (!func_proc_edit) {
    return false;
  }
  if (!is_valid_version_info()) {
    return false;
  }

  struct aviutl2_edit_info info = {0};
  gcmz_do_blocking(get_simulated_edit_info_internal, &info);
  func_proc_edit(&(struct aviutl2_edit_section){
      .info = &info,
  });
  return true;
}

struct aviutl2_edit_handle *gcmz_aviutl2_create_simulated_edit_handle(void) {
  if (!is_valid_version_info()) {
    return NULL;
  }
  static struct aviutl2_edit_handle h = {
      .call_edit_section = simulated_call_edit_section,
  };
  return &h;
}

typedef void (*log_func)(char const *category, wchar_t const *format, ...);
struct aviutl2_log_context {
  struct aviutl2_log_handle base;
  log_func verbose;
  log_func info;
  log_func warn;
  log_func error;
  char category[64];
};

#define DEFINE_WRAPPED_LOG_FUNC(func_name)                                                                             \
  static void wrapped_log_##func_name(struct aviutl2_log_handle *handle, wchar_t const *message) {                     \
    struct aviutl2_log_context *ctx = (struct aviutl2_log_context *)(void *)handle;                                    \
    ctx->func_name(ctx->category, L"%s", message);                                                                     \
  }

DEFINE_WRAPPED_LOG_FUNC(verbose)
DEFINE_WRAPPED_LOG_FUNC(info)
DEFINE_WRAPPED_LOG_FUNC(warn)
DEFINE_WRAPPED_LOG_FUNC(error)

static void write_category(char category[64]) {
  HMODULE module = NULL;
  NATIVE_CHAR *module_path = NULL;
  struct ov_error err = {0};
  bool success = false;

  {
    if (!ovl_os_get_hinstance_from_fnptr((void *)write_category, (void **)&module, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!ovl_path_get_module_name(&module_path, (void *)module, &err)) {
      OV_ERROR_ADD_TRACE(&err);
      goto cleanup;
    }
    if (!module_path || *module_path == '\0') {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_unexpected);
      goto cleanup;
    }
    NATIVE_CHAR const *filename = ovl_path_extract_file_name(module_path);
    if (!filename || *filename == '\0') {
      filename = module_path;
    }
    static char const prefix[] = "Plugin::";
    size_t const prefix_len = sizeof(prefix) - 1;
    enum { category_buf_size = 64 };
    size_t const space = category_buf_size - prefix_len - 1;
    size_t const filename_len = wcslen(filename);
    size_t const filename_utf8_len = ov_wchar_to_utf8_len(filename, filename_len);
    if (!filename_utf8_len || filename_utf8_len > space) {
      OV_ERROR_SET_GENERIC(&err, ov_error_generic_fail);
      goto cleanup;
    }
    strcpy(category, prefix);
    ov_wchar_to_utf8(filename, filename_len, category + prefix_len, space + 1, NULL);
    success = true;
  }

cleanup:
  if (!success) {
    OV_ERROR_REPORT(&err, NULL);
    strcpy(category, "Plugin::Unknown");
  }
  if (module_path) {
    OV_ARRAY_DESTROY(&module_path);
  }
}

struct aviutl2_log_handle *gcmz_aviutl2_create_simulated_log_handle(void) {
  static struct aviutl2_log_context ctx;
  if (!ctx.error && is_valid_version_info()) {
    ctx.verbose = (log_func)calc_offset(g_aviutl2_module, g_version_info.log_verbose_func);
    ctx.info = (log_func)calc_offset(g_aviutl2_module, g_version_info.log_info_func);
    ctx.warn = (log_func)calc_offset(g_aviutl2_module, g_version_info.log_warn_func);
    ctx.error = (log_func)calc_offset(g_aviutl2_module, g_version_info.log_error_func);
    ctx.base.log = NULL;
    ctx.base.verbose = wrapped_log_verbose;
    ctx.base.info = wrapped_log_info;
    ctx.base.warn = wrapped_log_warn;
    ctx.base.error = wrapped_log_error;
    write_category(ctx.category);
  }
  if (!ctx.base.verbose || !ctx.base.info || !ctx.base.warn || !ctx.base.error) {
    return NULL;
  }
  return (struct aviutl2_log_handle *)(void *)&ctx;
}

char const *gcmz_aviutl2_get_detected_version(void) {
  if (!is_valid_version_info()) {
    return NULL;
  }
  return g_version_info.section_name;
}

uint32_t gcmz_aviutl2_get_detected_version_uint32(void) {
  if (!is_valid_version_info()) {
    return 0;
  }
  return g_version_info.version;
}
