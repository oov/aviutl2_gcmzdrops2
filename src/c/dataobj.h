#pragma once

#include <ovbase.h>

struct gcmz_file_list;

/**
 * @brief Extract files and data from IDataObject
 *
 * Extracts files and data from an IDataObject in multiple formats.
 * Attempts to extract in priority order: Data URI, PNG, JPEG, and other formats,
 * with plain text fallback. Returns on first successful extraction.
 *
 * @param pDataObj Pointer to IDataObject to extract from. Must not be NULL.
 * @param file_list [out] File list to add extracted files to. Must not be NULL.
 *                        Extracted files are appended to existing entries.
 * @param err [out] Pointer to error structure for error information. Can be NULL.
 * @return true on success (files extracted and added to list), false on failure
 *         (check err for error details)
 */
NODISCARD bool gcmz_dataobj_extract_from_dataobj(void *const pDataObj,
                                                 struct gcmz_file_list *const file_list,
                                                 struct ov_error *const err);
