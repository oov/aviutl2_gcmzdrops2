#pragma once

#include <ovbase.h>

struct gcmz_config;

/**
 * @brief Show configuration dialog
 *
 * Displays a modal dialog for editing application configuration.
 * Changes are saved to GCMZDrops.json when the user clicks OK.
 *
 * @param config Configuration object to use for the dialog
 * @param parent_window Parent window handle for dialog positioning (can be NULL)
 * @param external_api_running Whether the external API is currently running
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
bool gcmz_config_dialog_show(struct gcmz_config *config,
                             void *parent_window,
                             bool const external_api_running,
                             struct ov_error *const err);
