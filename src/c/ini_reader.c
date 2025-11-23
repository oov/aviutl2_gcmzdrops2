#include "ini_reader.h"

#include <ctype.h>
#include <string.h>

#include <ovarray.h>
#include <ovhashmap.h>
#include <ovl/source.h>
#include <ovl/source/file.h>
#include <ovl/source/memory.h>

static char const g_global_section_internal_name[] = "][";
static char const g_empty_section_internal_name[] = "]]";

struct gcmz_ini_reader {
  struct ov_hashmap *sections;
};

struct section {
  char const *name;
  size_t name_len;
  size_t line_number;
  char *line;
  size_t line_len;
  struct ov_hashmap *entries;
};

struct entry {
  char const *name;
  size_t name_len;
  size_t line_number;
  char *line;
  size_t line_len;
};

static void get_key_from_section(void const *const item, void const **const key, size_t *const key_bytes) {
  struct section const *s = (struct section const *)item;
  *key = s->name;
  *key_bytes = s->name_len;
}

static void get_key_from_entry(void const *const item, void const **const key, size_t *const key_bytes) {
  struct entry const *e = (struct entry const *)item;
  *key = e->name;
  *key_bytes = e->name_len;
}

static void section_to_internal_section_name(char const *const section,
                                             char const **const internal_name,
                                             size_t *const internal_name_len) {
  assert(internal_name != NULL);
  assert(internal_name_len != NULL);
  if (!section) {
    *internal_name = g_global_section_internal_name;
    *internal_name_len = sizeof(g_global_section_internal_name) - 1;
  } else if (section[0] == '\0') {
    *internal_name = g_empty_section_internal_name;
    *internal_name_len = sizeof(g_empty_section_internal_name) - 1;
  } else {
    *internal_name = section;
    *internal_name_len = strlen(section);
  }
}

static void internal_section_name_to_section(char const *const internal_name,
                                             size_t const internal_name_len,
                                             char const **const section,
                                             size_t *const section_len) {
  if (internal_name_len == sizeof(g_global_section_internal_name) - 1 &&
      memcmp(internal_name, g_global_section_internal_name, internal_name_len) == 0) {
    *section = NULL;
    *section_len = 0;
  } else if (internal_name_len == sizeof(g_empty_section_internal_name) - 1 &&
             memcmp(internal_name, g_empty_section_internal_name, internal_name_len) == 0) {
    *section = "";
    *section_len = 0;
  } else {
    *section = internal_name;
    *section_len = internal_name_len;
  }
}

static void cleanup_entry(struct entry *const e) {
  if (!e) {
    return;
  }
  if (e->name) {
    OV_FREE(&e->name);
  }
  if (e->line) {
    OV_FREE(&e->line);
  }
}

static void cleanup_section(struct section *const s) {
  if (!s) {
    return;
  }
  if (s->name) {
    OV_FREE(&s->name);
  }
  if (s->line) {
    OV_FREE(&s->line);
  }
  if (s->entries) {
    struct entry *e = NULL;
    for (size_t i = 0; OV_HASHMAP_ITER(s->entries, &i, &e);) {
      cleanup_entry(e);
    }
    OV_HASHMAP_DESTROY(&s->entries);
  }
}

static struct section const *find_section(struct gcmz_ini_reader const *const reader, char const *const section) {
  if (!reader) {
    return NULL;
  }
  char const *section_name;
  size_t section_len;
  section_to_internal_section_name(section, &section_name, &section_len);
  return (struct section const *)OV_HASHMAP_GET(reader->sections,
                                                &((struct section const){
                                                    .name = section_name,
                                                    .name_len = section_len,
                                                }));
}

void gcmz_ini_reader_destroy(struct gcmz_ini_reader **const rp) {
  if (!rp || !*rp) {
    return;
  }
  struct gcmz_ini_reader *const r = *rp;
  if (r->sections) {
    struct section *s = NULL;
    for (size_t i = 0; OV_HASHMAP_ITER(r->sections, &i, &s);) {
      cleanup_section(s);
    }
    OV_HASHMAP_DESTROY(&r->sections);
  }
  OV_FREE(rp);
}

bool gcmz_ini_reader_create(struct gcmz_ini_reader **const rp, struct ov_error *const err) {
  if (!rp || *rp) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;
  struct gcmz_ini_reader *r = NULL;

  if (!OV_REALLOC(&r, 1, sizeof(struct gcmz_ini_reader))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }
  *r = (struct gcmz_ini_reader){
      .sections = OV_HASHMAP_CREATE_DYNAMIC(sizeof(struct section), 8, get_key_from_section),
  };
  if (!r->sections) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  *rp = r;
  r = NULL;
  result = true;

cleanup:
  if (r) {
    gcmz_ini_reader_destroy(&r);
  }
  return result;
}

static struct ov_hashmap *get_or_create_section_entries(struct gcmz_ini_reader *const r,
                                                        char const *const section,
                                                        size_t section_len,
                                                        size_t line_number,
                                                        char const *const line,
                                                        size_t line_len) {
  if (!r || !section) {
    return NULL;
  }

  struct ov_hashmap *result = NULL;
  struct ov_hashmap *entries = NULL;
  char *name = NULL;
  char *sline = NULL;

  {
    struct section const *found = (struct section const *)OV_HASHMAP_GET(r->sections,
                                                                         &((struct section const){
                                                                             .name = section,
                                                                             .name_len = section_len,
                                                                         }));
    if (found) {
      result = found->entries;
      goto cleanup;
    }
  }

  entries = OV_HASHMAP_CREATE_DYNAMIC(sizeof(struct entry), 8, get_key_from_entry);
  if (!entries) {
    goto cleanup;
  }

  if (section_len > 0) {
    if (!OV_REALLOC(&name, section_len, sizeof(char))) {
      goto cleanup;
    }
    memcpy(name, section, section_len);
  }

  if (line && line_len > 0) {
    if (!OV_REALLOC(&sline, line_len, sizeof(char))) {
      goto cleanup;
    }
    memcpy(sline, line, line_len);
  }

  if (!OV_HASHMAP_SET(r->sections,
                      &((struct section){
                          .name = name,
                          .name_len = section_len,
                          .line_number = line_number,
                          .line = sline,
                          .line_len = line_len,
                          .entries = entries,
                      }))) {
    goto cleanup;
  }
  result = entries;
  name = NULL;
  sline = NULL;
  entries = NULL;

cleanup:
  if (sline) {
    OV_FREE(&sline);
  }
  if (name) {
    OV_FREE(&name);
  }
  if (entries) {
    OV_HASHMAP_DESTROY(&entries);
  }
  return result;
}

static bool add_entry(struct ov_hashmap *const entries,
                      char const *const line,
                      size_t const line_len,
                      size_t const line_number,
                      char const *const key,
                      size_t const key_len) {
  if (!entries || !key) {
    return false;
  }

  bool success = false;
  char *name = NULL;
  char *sline = NULL;

  if (key_len > 0) {
    if (!OV_REALLOC(&name, key_len, sizeof(char))) {
      goto cleanup;
    }
    memcpy(name, key, key_len);
  }

  if (line && line_len > 0) {
    if (!OV_REALLOC(&sline, line_len, sizeof(char))) {
      goto cleanup;
    }
    memcpy(sline, line, line_len);
  }

  if (!OV_HASHMAP_SET(entries,
                      &((struct entry){
                          .name = name,
                          .name_len = key_len,
                          .line = sline,
                          .line_len = line_len,
                          .line_number = line_number,
                      }))) {
    goto cleanup;
  }

  name = NULL;
  sline = NULL;
  success = true;

cleanup:
  if (sline) {
    OV_FREE(&sline);
  }
  if (name) {
    OV_FREE(&name);
  }
  return success;
}

static void trim_whitespace(char const *const str, size_t const str_len, char const **const start, size_t *const len) {
  assert(str != NULL);
  assert(start != NULL);
  assert(len != NULL);

  if (str_len == 0) {
    *start = str;
    *len = 0;
    return;
  }

  // Trim leading whitespace
  char const *trimmed_start = str;
  char const *str_end = str + str_len;
  while (trimmed_start < str_end && isspace((unsigned char)*trimmed_start)) {
    trimmed_start++;
  }

  if (trimmed_start >= str_end) {
    *start = trimmed_start;
    *len = 0;
    return;
  }

  // Trim trailing whitespace
  char const *trimmed_end = str_end - 1;
  while (trimmed_end > trimmed_start && isspace((unsigned char)*trimmed_end)) {
    trimmed_end--;
  }

  *start = trimmed_start;
  *len = (size_t)(trimmed_end - trimmed_start + 1);
}

struct parse_context {
  struct gcmz_ini_reader *r;
  struct ov_hashmap *section_entries;
};

static bool parse_line(struct parse_context *const ctx,
                       char const *const line,
                       size_t const line_len,
                       size_t const line_number,
                       struct ov_error *const err) {
  if (!ctx || !line) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  {
    char const *trimmed;
    size_t trimmed_len;
    trim_whitespace(line, line_len, &trimmed, &trimmed_len);

    // Empty line - skip
    if (trimmed_len == 0) {
      result = true;
      goto cleanup;
    }

    // Comment line - skip
    if (*trimmed == '#' || *trimmed == ';') {
      result = true;
      goto cleanup;
    }

    // Section header [section]
    if (*trimmed == '[') {
      char const *end = strchr(trimmed, ']');
      if (end && end < trimmed + trimmed_len) {
        // Extract section name between [ and ]
        char const *section_content = trimmed + 1;
        size_t section_content_len = (size_t)(end - section_content);

        char const *section_start;
        size_t section_len;
        trim_whitespace(section_content, section_content_len, &section_start, &section_len);
        if (section_len == 0) {
          section_start = g_empty_section_internal_name;
          section_len = sizeof(g_empty_section_internal_name) - 1;
        }

        ctx->section_entries =
            get_or_create_section_entries(ctx->r, section_start, section_len, line_number, line, line_len);
        if (!ctx->section_entries) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
          goto cleanup;
        }
        result = true;
        goto cleanup;
      }
      // Malformed section header - ignore
      result = true;
      goto cleanup;
    }

    // Key-value pair
    char const *equals = strchr(trimmed, '=');
    if (equals && equals < trimmed + trimmed_len) {
      // Extract key part
      size_t key_content_len = (size_t)(equals - trimmed);

      char const *key_start;
      size_t key_len;
      trim_whitespace(trimmed, key_content_len, &key_start, &key_len);
      if (key_start && key_len > 0 && ctx->section_entries) {
        if (!add_entry(ctx->section_entries, line, line_len, line_number, key_start, key_len)) {
          OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
          goto cleanup;
        }
      }
      result = true;
      goto cleanup;
    }
  }

  // Unrecognized line format - ignore
  result = true;

cleanup:
  return result;
}

static bool parse(struct gcmz_ini_reader *const reader,
                  char const *const buffer,
                  size_t const buffer_size,
                  struct ov_error *const err) {
  if (!reader || !buffer) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  bool result = false;

  {
    struct parse_context ctx = {
        .r = reader,
        // create global section
        .section_entries = get_or_create_section_entries(reader,
                                                         g_global_section_internal_name,
                                                         sizeof(g_global_section_internal_name) - 1,
                                                         1, // global section starts at line 1
                                                         NULL,
                                                         0),
    };
    if (!ctx.section_entries) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    char const *line_start = buffer;
    char const *const buffer_end = buffer + buffer_size;
    size_t line_number = 1;
    while (line_start < buffer_end) {
      char const *line_end = line_start;
      while (line_end < buffer_end && *line_end != '\r' && *line_end != '\n') {
        line_end++;
      }
      size_t const line_len = (size_t)(line_end - line_start);
      if (!parse_line(&ctx, line_start, line_len, line_number, err)) {
        OV_ERROR_ADD_TRACE(err);
        goto cleanup;
      }
      line_start = line_end;
      if (line_start < buffer_end && *line_start == '\r') {
        line_start++;
      }
      if (line_start < buffer_end && *line_start == '\n') {
        line_start++;
      }
      line_number++;
    }
  }
  result = true;

cleanup:
  return result;
}

bool gcmz_ini_reader_load(struct gcmz_ini_reader *const reader,
                          struct ovl_source *const source,
                          struct ov_error *const err) {
  if (!reader || !source) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  char *buffer = NULL;
  bool result = false;

  {
    uint64_t const file_size = ovl_source_size(source);
    if (file_size == UINT64_MAX) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to get INI source size");
      goto cleanup;
    }
    if (file_size > SIZE_MAX) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "INI source is too large");
      goto cleanup;
    }
    if (file_size == 0) {
      result = true;
      goto cleanup; // Empty source is valid
    }

    size_t const buffer_size = (size_t)file_size;
    if (!OV_ARRAY_GROW(&buffer, buffer_size)) {
      OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
      goto cleanup;
    }

    size_t const bytes_read = ovl_source_read(source, buffer, 0, buffer_size);
    if (bytes_read == SIZE_MAX) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read INI source");
      goto cleanup;
    }
    if (bytes_read != buffer_size) {
      OV_ERROR_SET(err, ov_error_type_generic, ov_error_generic_fail, "failed to read complete INI source");
      goto cleanup;
    }

    // Handle UTF-8 BOM
    char const *content_start = buffer;
    size_t content_size = bytes_read;
    if (bytes_read >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB &&
        (unsigned char)buffer[2] == 0xBF) {
      content_start = buffer + 3;
      content_size = bytes_read - 3;
    }

    if (!parse(reader, content_start, content_size, err)) {
      OV_ERROR_ADD_TRACE(err);
      goto cleanup;
    }
  }

  result = true;

cleanup:
  if (buffer) {
    OV_ARRAY_DESTROY(&buffer);
  }
  return result;
}

bool gcmz_ini_reader_load_file(struct gcmz_ini_reader *const reader,
                               NATIVE_CHAR const *const filepath,
                               struct ov_error *const err) {
  if (!reader || !filepath) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_source *source = NULL;
  bool result = false;

  if (!ovl_source_file_create(filepath, &source, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_ini_reader_load(reader, source, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  return result;
}

bool gcmz_ini_reader_load_memory(struct gcmz_ini_reader *const r,
                                 void const *const ptr,
                                 size_t const size,
                                 struct ov_error *const err) {
  if (!r || !ptr || !size) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return false;
  }

  struct ovl_source *source = NULL;
  bool result = false;

  if (!ovl_source_memory_create(ptr, size, &source, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  if (!gcmz_ini_reader_load(r, source, err)) {
    OV_ERROR_ADD_TRACE(err);
    goto cleanup;
  }

  result = true;

cleanup:
  if (source) {
    ovl_source_destroy(&source);
  }
  return result;
}

static struct gcmz_ini_value extract_value_from_line(char const *const line, size_t const line_len) {
  struct gcmz_ini_value result = {NULL, 0};

  if (!line) {
    return result;
  }

  // Find the '=' character in the line
  char const *equals = NULL;
  char const *line_end = line + line_len;
  for (char const *p = line; p < line_end; p++) {
    if (*p == '=') {
      equals = p;
      break;
    }
  }
  if (!equals) {
    return result;
  }
  char const *value_start = equals + 1;

  // Look for inline comments (# or ;) within the line bounds
  char const *comment_start = NULL;
  for (char const *p = value_start; p < line_end; p++) {
    if (*p == '#' || *p == ';') {
      comment_start = p;
      break;
    }
  }

  // Calculate value content length (up to comment or end of line)
  size_t value_content_len;
  if (comment_start) {
    value_content_len = (size_t)(comment_start - value_start);
  } else {
    value_content_len = (size_t)(line_end - value_start);
  }

  // Trim the value
  char const *trimmed_start;
  size_t trimmed_len;
  trim_whitespace(value_start, value_content_len, &trimmed_start, &trimmed_len);

  result.ptr = trimmed_start;
  result.size = trimmed_len;
  return result;
}

struct gcmz_ini_value gcmz_ini_reader_get_value(struct gcmz_ini_reader const *const reader,
                                                char const *const section,
                                                char const *const key) {
  struct gcmz_ini_value result = {NULL, 0};
  if (!reader || !key) {
    goto cleanup;
  }

  {
    struct section const *const s = find_section(reader, section);
    if (!s) {
      goto cleanup; // section not found
    }
    struct entry const *const e = (struct entry const *)OV_HASHMAP_GET(s->entries,
                                                                       &((struct entry const){
                                                                           .name = key,
                                                                           .name_len = strlen(key),
                                                                       }));
    if (!e) {
      goto cleanup; // entry not found
    }
    result = extract_value_from_line(e->line, e->line_len);
  }
cleanup:
  return result;
}

bool gcmz_ini_reader_iter_sections(struct gcmz_ini_reader const *const reader, struct gcmz_ini_iter *const iter) {
  if (!reader || !iter) {
    return false;
  }
  struct section *section = NULL;
  if (!OV_HASHMAP_ITER(reader->sections, &iter->index, &section)) {
    return false;
  }
  internal_section_name_to_section(section->name, section->name_len, &iter->name, &iter->name_len);
  iter->line_number = section->line_number;
  return true;
}

bool gcmz_ini_reader_iter_entries(struct gcmz_ini_reader const *const reader,
                                  char const *const section,
                                  struct gcmz_ini_iter *const iter) {
  if (!reader || !iter) {
    return false;
  }

  struct section const *s = (struct section const *)iter->state;

  // First call or section not cached - find the section
  if (!s) {
    s = find_section(reader, section);
    if (!s) {
      return false;
    }
    iter->state = s;
  }

  struct entry *entry = NULL;
  bool const found = OV_HASHMAP_ITER(s->entries, &iter->index, &entry);
  if (!found) {
    return false;
  }

  iter->name = entry->name;
  iter->name_len = entry->name_len;
  iter->line_number = entry->line_number;
  return true;
}

size_t gcmz_ini_reader_get_section_count(struct gcmz_ini_reader const *const reader) {
  if (!reader) {
    return 0;
  }
  return OV_HASHMAP_COUNT(reader->sections);
}

size_t gcmz_ini_reader_get_entry_count(struct gcmz_ini_reader const *const reader, char const *const section) {
  if (!reader) {
    return 0;
  }
  struct section const *s = find_section(reader, section);
  if (!s) {
    return 0;
  }
  return OV_HASHMAP_COUNT(s->entries);
}
