#pragma once

#include <ovbase.h>

/**
 * @brief Create IDataObject wrapper with GCMZ-specific properties
 *
 * Wraps an existing IDataObject to attach GCMZ configuration properties.
 *
 * @param dataobj IDataObject to wrap (will be AddRef'd)
 * @param use_exo_converter Whether to enable EXO conversion
 * @param from_external_api Whether this drop originated from external API
 * @param err [out] Error information on failure
 * @return IDataObject wrapper on success, NULL on failure
 */
NODISCARD void *gcmz_dataobj_create(void *const dataobj,
                                    bool const use_exo_converter,
                                    bool const from_external_api,
                                    struct ov_error *const err);

/**
 * @brief Check if EXO conversion is enabled for IDataObject
 *
 * @param dataobj IDataObject to query
 * @return true if EXO conversion is enabled, false if disabled or not a GCMZ wrapper
 */
bool gcmz_dataobj_is_exo_convert_enabled(void *const dataobj);

/**
 * @brief Check if drop originated from external API
 *
 * @param dataobj IDataObject to query
 * @return true if from external API, false if from normal drag-and-drop or not a GCMZ wrapper
 */
bool gcmz_dataobj_is_from_external_api(void *const dataobj);
