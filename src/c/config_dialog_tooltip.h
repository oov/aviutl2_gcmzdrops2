#pragma once

#include <ovbase.h>

struct gcmz_config;
struct config_dialog_tooltip;

/**
 * @brief Create tooltip manager for configuration dialog controls
 *
 * @param config Configuration structure for placeholder expansion
 * @param parent Parent window handle (HWND)
 * @param listbox Listbox control handle (HWND) to attach tooltip, can be NULL
 * @param edit_control Edit control handle (HWND) to attach tooltip, can be NULL
 * @param err [out] Error information on failure
 * @return Tooltip manager instance on success, NULL on failure
 */
struct config_dialog_tooltip *config_dialog_tooltip_create(
    struct gcmz_config *config, void *parent, void *listbox, void *edit_control, struct ov_error *const err);

/**
 * @brief Destroy tooltip manager and free resources
 *
 * @param ttpp Pointer to tooltip manager instance
 */
void config_dialog_tooltip_destroy(struct config_dialog_tooltip **ttpp);
