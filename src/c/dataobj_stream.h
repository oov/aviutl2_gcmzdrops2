#pragma once

#include <ovbase.h>

struct ovl_source;

/**
 * @brief Create ovl_source from Windows data object
 *
 * @param dataobj IDataObject pointer
 * @param formatetc FORMATETC structure pointer
 * @param sp [out] Pointer to store the created source
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmz_dataobj_source_create(void *const dataobj,
                                          void const *const formatetc,
                                          struct ovl_source **const sp,
                                          struct ov_error *const err);
