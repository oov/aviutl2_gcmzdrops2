#pragma once

#include <ovbase.h>

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wdocumentation")
#    pragma GCC diagnostic ignored "-Wdocumentation"
#  endif
#  if __has_warning("-Wdocumentation-unknown-command")
#    pragma GCC diagnostic ignored "-Wdocumentation-unknown-command"
#  endif
#endif // __GNUC__
#include <yyjson.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

/**
 * @brief Get JSON allocator for yyjson operations
 *
 * Returns a shared allocator instance that uses ovbase memory management
 * functions (OV_REALLOC/OV_FREE) for JSON operations. This ensures
 * consistent memory management across the application.
 *
 * @return Pointer to yyjson allocator structure. Never returns NULL.
 *
 * @note The returned allocator is statically allocated and does not
 *       need to be freed.
 */
NODISCARD struct yyjson_alc const *gcmz_json_get_alc(void);
