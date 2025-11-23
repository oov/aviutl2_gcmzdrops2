#pragma once

#include <ovbase.h>

/**
 * @brief File entry structure for GCMZDrops file management
 *
 * Represents a single file entry with path, MIME type, and temporary flag.
 * Used within gcmz_file_list to manage collections of files.
 *
 * @note This structure only manages file metadata. The actual files on disk
 *       are NOT deleted by any gcmz_file_list functions. Temporary file cleanup
 *       must be handled separately by the caller based on the temporary flag.
 */
struct gcmz_file {
  wchar_t *path;      ///< Wide character file path (null-terminated), owned by this structure
  wchar_t *mime_type; ///< Wide character MIME type string (null-terminated), owned by this structure, can be NULL
  bool temporary;     ///< Metadata flag indicating this file is temporary. Does NOT trigger automatic file deletion.
};

/**
 * @brief Opaque structure for managing a list of files
 *
 * Internal structure that maintains a dynamic array of gcmz_file entries.
 * All memory management is handled automatically by the API functions.
 */
struct gcmz_file_list;

/**
 * @brief Create a new empty file list
 *
 * Creates and initializes a new file list structure. The returned list must be
 * destroyed with gcmz_file_list_destroy() to prevent memory leaks.
 *
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return Pointer to new file list on success, NULL on failure (check err for details)
 */
struct gcmz_file_list *gcmz_file_list_create(struct ov_error *const err);

/**
 * @brief Destroy file list and free all associated memory
 *
 * Safely destroys the file list, freeing all file entries and their associated
 * path and MIME type strings. The list pointer is set to NULL after destruction.
 * This function is safe to call with NULL or already destroyed lists.
 *
 * @note This function only frees in-memory data structures. Actual files on disk
 *       are NOT deleted. Caller must handle file cleanup separately if needed.
 *
 * @param list Pointer to file list pointer. Must not be NULL.
 *             After successful destruction, *list will be set to NULL.
 */
void gcmz_file_list_destroy(struct gcmz_file_list **const list);

/**
 * @brief Add a regular file to the file list
 *
 * Adds a new file entry to the list with the specified path and MIME type.
 * The file is marked as non-temporary. Both path and MIME type strings are
 * copied internally, so the caller retains ownership of the input strings.
 *
 * @param list Pointer to file list. Must not be NULL.
 * @param path Wide character file path string. Must not be NULL and must be null-terminated.
 * @param mime_type Wide character MIME type string. Can be NULL. If provided, must be null-terminated.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for details, typically out of memory)
 */
NODISCARD bool gcmz_file_list_add(struct gcmz_file_list *const list,
                                  wchar_t const *const path,
                                  wchar_t const *const mime_type,
                                  struct ov_error *const err);

/**
 * @brief Add a temporary file to the file list
 *
 * Adds a new file entry to the list with the specified path and MIME type.
 * The file is marked as temporary, indicating it should be cleaned up when
 * no longer needed. Both path and MIME type strings are copied internally.
 *
 * @param list Pointer to file list. Must not be NULL.
 * @param path Wide character file path string. Must not be NULL and must be null-terminated.
 * @param mime_type Wide character MIME type string. Can be NULL. If provided, must be null-terminated.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for details, typically out of memory)
 */
NODISCARD bool gcmz_file_list_add_temporary(struct gcmz_file_list *const list,
                                            wchar_t const *const path,
                                            wchar_t const *const mime_type,
                                            struct ov_error *const err);

/**
 * @brief Remove file entry from the list by index
 *
 * Removes the file entry at the specified index from the list. All file entries
 * after the removed entry are shifted forward to fill the gap. The removed entry's
 * memory (path and MIME type strings) is automatically freed.
 *
 * @note This function only removes the entry from the list. The actual file on disk
 *       is NOT deleted. Caller must handle file cleanup separately if needed.
 *
 * @param list Pointer to file list. Must not be NULL.
 * @param index Zero-based index of the file entry to remove. Must be less than the list count.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for details, typically invalid index)
 */
NODISCARD bool gcmz_file_list_remove(struct gcmz_file_list *const list, size_t const index, struct ov_error *const err);

/**
 * @brief Get the number of files in the list
 *
 * Returns the current number of file entries in the list. This function
 * is safe to call with NULL lists and will return 0 in such cases.
 *
 * @param list Pointer to file list. Can be NULL (returns 0).
 * @return Number of file entries in the list, or 0 if list is NULL or empty
 */
size_t gcmz_file_list_count(struct gcmz_file_list const *const list);

/**
 * @brief Get read-only access to file entry at specified index
 *
 * Returns a read-only pointer to the file entry at the specified index.
 * The returned pointer is valid until the list is modified (entries added/removed)
 * or the list is destroyed. Do not attempt to modify the returned structure.
 *
 * @param list Pointer to file list. Must not be NULL.
 * @param index Zero-based index of the file entry to retrieve. Must be less than the list count.
 * @return Pointer to file entry on success, NULL if list is NULL or index is out of bounds
 */
struct gcmz_file const *gcmz_file_list_get(struct gcmz_file_list const *const list, size_t const index);

/**
 * @brief Get mutable access to file entry at specified index
 *
 * Returns a mutable pointer to the file entry at the specified index.
 * The returned pointer is valid until the list is modified (entries added/removed)
 * or the list is destroyed. Use this function when you need to modify file properties.
 *
 * @warning Do not directly modify the path or mime_type pointers. Use appropriate
 *          functions to update these fields to prevent memory leaks.
 *
 * @param list Pointer to file list. Must not be NULL.
 * @param index Zero-based index of the file entry to retrieve. Must be less than the list count.
 * @return Pointer to file entry on success, NULL if list is NULL or index is out of bounds
 */
struct gcmz_file *gcmz_file_list_get_mutable(struct gcmz_file_list *const list, size_t const index);

/**
 * @brief Clear all entries from the file list
 *
 * Removes all file entries from the list and frees their associated memory.
 * The list structure itself is preserved and can be reused to add new entries.
 *
 * @note This function only clears in-memory entries. Actual files on disk
 *       are NOT deleted. Caller must handle file cleanup separately if needed.
 *
 * @param list Pointer to file list. Must not be NULL.
 */
void gcmz_file_list_clear(struct gcmz_file_list *const list);
