#pragma once

#include <ovbase.h>

struct gcmz_api;
struct gcmz_file_list;
struct gcmz_project_data;

/**
 * @brief Request parameters structure passed to callback
 */
struct gcmz_api_request_params {
  struct gcmz_file_list *files;
  int layer;
  int frame_advance;
  bool use_exo_converter;
  struct ov_error *err;
  void *userdata;
};

/**
 * @brief Callback function for completion of file drop request processing
 */
typedef void (*gcmz_api_request_complete_func)(struct gcmz_api_request_params *const params);

/**
 * @brief Callback function for handling file drop requests
 *
 * This callback is invoked when files are dropped and need to be processed.
 * The callback must call the completion function when processing is finished.
 *
 * @param params Request parameters including file list, layer, and frame advance
 * @param complete Completion function that must be called when processing is done
 */
typedef void (*gcmz_api_request_func)(struct gcmz_api_request_params *const params,
                                      gcmz_api_request_complete_func const complete);

/**
 * @brief Callback function for project data update requests
 *
 * This callback is invoked periodically as a reminder to update project data.
 * When called, the callback should retrieve current AviUtl2 project information
 * and call gcmz_api_set_project_data() to provide the updated data.
 * This ensures the API has current project state for file drop operations.
 *
 * @param api API instance that needs updated project data
 * @param userdata User data provided during API creation
 */
typedef void (*gcmz_api_update_request_func)(struct gcmz_api *const api, void *const userdata);

/**
 * @brief Options structure for API initialization
 *
 * This structure contains optional callback functions and associated userdata
 * that can be set during API instance creation.
 */
struct gcmz_api_options {
  gcmz_api_request_func request_callback;       ///< File drop request handler callback
  gcmz_api_update_request_func update_callback; ///< Project data update request callback
  void *userdata;                               ///< Shared user data for both callbacks
  uint32_t aviutl2_ver;                         ///< AviUtl2 version (from InitializePlugin or detected)
  uint32_t gcmz_ver;                            ///< GCMZDrops version (from version.h)
};

/**
 * @brief Create a new GCMZDrops API instance
 *
 * Creates and initializes a new API instance for handling file drop operations.
 * The API creates a dedicated thread for message window handling and sets up
 * shared memory for inter-process communication.
 *
 * @param options Pointer to options structure containing callback functions and userdata.
 *                Can be NULL to create an instance without callbacks.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return Pointer to new API instance on success, NULL on failure
 *         (check err for error details)
 *
 * @note The returned instance must be destroyed with gcmz_api_destroy()
 *       to prevent memory leaks and resource cleanup.
 * @note Callbacks are set during initialization and cannot be changed later.
 */
NODISCARD struct gcmz_api *gcmz_api_create(struct gcmz_api_options const *const options, struct ov_error *const err);

/**
 * @brief Destroy API instance and free all associated resources
 *
 * Safely destroys the API instance, stopping the dedicated thread, cleaning up
 * shared memory, and freeing all associated resources. The API pointer is set
 * to NULL after destruction.
 *
 * @param api Pointer to API instance pointer. Must not be NULL.
 *            After successful destruction, *api will be set to NULL.
 */
void gcmz_api_destroy(struct gcmz_api **const api);

/**
 * @brief Set current AviUtl2 project data
 *
 * Updates the current project data for the API instance. This function should
 * be called in response to the notify_update callback to provide the latest
 * AviUtl2 project information.
 *
 * @param api Pointer to API instance. Must not be NULL.
 * @param proj Pointer to project data structure. Must not be NULL.
 * @param err Pointer to error structure for error information. Can be NULL.
 * @return true on success, false on failure (check err for error details)
 *
 * @note This function should be called from the notify_update callback or the
 *       main thread. It is not thread-safe for concurrent access.
 */
NODISCARD bool gcmz_api_set_project_data(struct gcmz_api *const api,
                                         struct gcmz_project_data const *const proj,
                                         struct ov_error *const err);
