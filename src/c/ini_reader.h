#pragma once

#include <ovbase.h>

struct gcmz_ini_reader;
struct ovl_source;

/**
 * @brief Create and initialize INI reader
 *
 * @param rp [out] Pointer to store the created reader
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_ini_reader_create(struct gcmz_ini_reader **const rp, struct ov_error *const err);

/**
 * @brief Cleanup and destroy reader, freeing all resources
 *
 * @param rp INI reader instance to destroy
 */
void gcmz_ini_reader_destroy(struct gcmz_ini_reader **const rp);

/**
 * @brief Load INI data from ovl_source with UTF-8 support and BOM handling
 *
 * @param r INI reader instance
 * @param source Source to read data from
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool
gcmz_ini_reader_load(struct gcmz_ini_reader *const r, struct ovl_source *const source, struct ov_error *const err);

/**
 * @brief Load INI file from filesystem with UTF-8 support and BOM handling
 *
 * @param r INI reader instance
 * @param filepath Wide character file path
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_ini_reader_load_file(struct gcmz_ini_reader *const r,
                                         NATIVE_CHAR const *const filepath,
                                         struct ov_error *const err);

/**
 * @brief Load INI data from memory buffer with UTF-8 support and BOM handling
 *
 * @param r INI reader instance
 * @param ptr Pointer to memory buffer containing INI data
 * @param size Size of the memory buffer in bytes
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_ini_reader_load_memory(struct gcmz_ini_reader *const r,
                                           void const *const ptr,
                                           size_t const size,
                                           struct ov_error *const err);

/**
 * @brief Result structure for value retrieval
 */
struct gcmz_ini_value {
  char const *ptr; ///< Pointer to the start of the value in the original line
  size_t size;     ///< Size of the value in bytes
};

/**
 * @brief Get value by section and key
 *
 * @param r INI reader instance
 * @param section Section name (NULL for global section)
 * @param key Key to search for
 * @return Result structure with pointer and size (ptr is NULL if not found)
 */
struct gcmz_ini_value
gcmz_ini_reader_get_value(struct gcmz_ini_reader const *const r, char const *const section, char const *const key);

/**
 * @brief Iterator for sections and entries
 */
struct gcmz_ini_iter {
  char const *name;   ///< Section/entry name (NOT null-terminated, use name_len)
  size_t name_len;    ///< Length of name
  size_t line_number; ///< Line number where item was defined
  size_t index;       ///< Iterator index for next call
  void const *state;  ///< Internal state (do not modify)
};

/**
 * @brief Iterate through all sections in unspecified order
 *
 * @param r INI reader instance
 * @param iter [in,out] Iterator information and state
 * @return true if section found, false if iteration complete
 */
bool gcmz_ini_reader_iter_sections(struct gcmz_ini_reader const *const r, struct gcmz_ini_iter *const iter);

/**
 * @brief Iterate through all entries in a section in unspecified order
 *
 * @param r INI reader instance
 * @param section Section name (NULL for global section)
 * @param iter [in,out] Iterator information and state
 * @return true if entry found, false if iteration complete or section not found
 */
bool gcmz_ini_reader_iter_entries(struct gcmz_ini_reader const *const r,
                                  char const *const section,
                                  struct gcmz_ini_iter *const iter);

/**
 * @brief Get the number of sections in the INI file
 *
 * @param r INI reader instance
 * @return Number of sections
 */
size_t gcmz_ini_reader_get_section_count(struct gcmz_ini_reader const *const r);

/**
 * @brief Get the number of entries in a specific section
 *
 * @param r INI reader instance
 * @param section Section name (NULL for global section)
 * @return Number of entries in the section (0 if section not found)
 */
size_t gcmz_ini_reader_get_entry_count(struct gcmz_ini_reader const *const r, char const *const section);
