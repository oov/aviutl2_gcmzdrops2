#pragma once

#include <ovbase.h>

#include "gcmz_types.h"

struct gcmz_config;

/**
 * @brief Project path retrieval function
 *
 * @param userdata User data passed to the function
 * @return Project path, or NULL on failure
 */
typedef NATIVE_CHAR const *(*gcmz_project_path_provider_fn)(void *userdata);

/**
 * @brief Configuration creation options
 */
struct gcmz_config_options {
  gcmz_project_path_provider_fn project_path_provider;
  void *project_path_provider_userdata;
};

/**
 * @brief Create and initialize configuration with default values
 *
 * @param options Configuration options (can be NULL for defaults)
 * @param err Error information
 * @return Configuration structure pointer on success, NULL on failure
 */
struct gcmz_config *gcmz_config_create(struct gcmz_config_options const *const options, struct ov_error *const err);

/**
 * @brief Destroy configuration and free memory
 *
 * @param config Configuration structure pointer
 */
void gcmz_config_destroy(struct gcmz_config **const config);

/**
 * @brief Load configuration from JSON file
 *
 * @param config Configuration structure
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_load(struct gcmz_config *const config, struct ov_error *const err);

/**
 * @brief Save configuration to JSON file
 *
 * @param config Configuration structure
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_save(struct gcmz_config const *const config, struct ov_error *const err);

/**
 * @brief Get file save path based on current configuration
 *
 * @param config Configuration structure
 * @param filename File name to save
 * @param err Error information
 * @return Save path on success (caller must OV_ARRAY_DESTROY), NULL on failure
 */
NATIVE_CHAR *gcmz_config_get_save_path(struct gcmz_config const *const config,
                                       NATIVE_CHAR const *const filename,
                                       struct ov_error *const err);

/**
 * @brief Get file processing mode
 *
 * @param config Configuration structure
 * @param mode Processing mode output
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_get_processing_mode(struct gcmz_config const *const config,
                                     enum gcmz_processing_mode *const mode,
                                     struct ov_error *const err);

/**
 * @brief Set file processing mode
 *
 * @param config Configuration structure
 * @param mode Processing mode
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_set_processing_mode(struct gcmz_config *const config,
                                     enum gcmz_processing_mode const mode,
                                     struct ov_error *const err);

/**
 * @brief Get directory creation setting
 *
 * @param config Configuration structure
 * @param allow_create_directories Output setting value
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_get_allow_create_directories(struct gcmz_config const *const config,
                                              bool *const allow_create_directories,
                                              struct ov_error *const err);

/**
 * @brief Set directory creation setting
 *
 * @param config Configuration structure
 * @param allow_create_directories Whether to create directories automatically
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_set_allow_create_directories(struct gcmz_config *const config,
                                              bool const allow_create_directories,
                                              struct ov_error *const err);

/**
 * @brief Get external API setting
 *
 * @param config Configuration structure
 * @param external_api Output setting value
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_get_external_api(struct gcmz_config const *const config,
                                  bool *const external_api,
                                  struct ov_error *const err);

/**
 * @brief Set external API setting
 *
 * @param config Configuration structure
 * @param external_api Whether to enable external API
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_set_external_api(struct gcmz_config *const config,
                                  bool const external_api,
                                  struct ov_error *const err);

/**
 * @brief Get show debug menu setting
 *
 * @param config Configuration structure
 * @param show_debug_menu Output setting value
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_get_show_debug_menu(struct gcmz_config const *const config,
                                     bool *const show_debug_menu,
                                     struct ov_error *const err);

/**
 * @brief Set show debug menu setting
 *
 * @param config Configuration structure
 * @param show_debug_menu Whether to show debug menu
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_set_show_debug_menu(struct gcmz_config *const config,
                                     bool const show_debug_menu,
                                     struct ov_error *const err);

/**
 * @brief Get fallback save path used when no save paths are configured or all fail
 *
 * @return Read-only fallback path string (never NULL)
 */
NATIVE_CHAR const *gcmz_config_get_fallback_save_path(void);

/**
 * @brief Get all save paths
 *
 * @param config Configuration structure
 * @return Read-only array of paths, NULL if config is invalid
 *         Use OV_ARRAY_LENGTH() to get the number of paths
 */
NATIVE_CHAR const *const *gcmz_config_get_save_paths(struct gcmz_config const *const config);

/**
 * @brief Set all save paths (replaces existing paths)
 *
 * @param config Configuration structure
 * @param paths Array of paths to set
 * @param num_paths Number of paths in array
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_set_save_paths(struct gcmz_config *const config,
                                NATIVE_CHAR const *const *const paths,
                                size_t const num_paths,
                                struct ov_error *const err);

/**
 * @brief Expand placeholders in a path string
 *
 * @param config Configuration structure (for accessing project path provider)
 * @param path Path string containing placeholders like %PROJECTDIR%
 * @param expanded_path Output expanded path (caller must OV_ARRAY_DESTROY)
 * @param err Error information
 * @return true on success, false on failure
 */
bool gcmz_config_expand_placeholders(struct gcmz_config const *const config,
                                     NATIVE_CHAR const *const path,
                                     NATIVE_CHAR **const expanded_path,
                                     struct ov_error *const err);
