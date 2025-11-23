#pragma once

#include <ovbase.h>

struct config_dialog_combo_tooltip;

/**
 * @brief Callback function for getting combobox item tooltip text
 *
 * @param item_index Index of the combobox item (0-based)
 * @param userdata User-defined data passed to create function
 * @return UTF-8 encoded tooltip text, or NULL/empty string if no tooltip
 */
typedef char const *(*config_dialog_combo_tooltip_callback)(int item_index, void *userdata);

/**
 * @brief Create tooltip manager for combobox dropdown items
 *
 * @param parent Parent window handle (HWND)
 * @param combobox Combobox control handle (HWND)
 * @param callback Callback function to get tooltip text for each item
 * @param userdata User-defined data to pass to callback
 * @param err [out] Error information on failure
 * @return Tooltip manager instance on success, NULL on failure
 */
struct config_dialog_combo_tooltip *config_dialog_combo_tooltip_create(void *parent,
                                                                       void *combobox,
                                                                       config_dialog_combo_tooltip_callback callback,
                                                                       void *userdata,
                                                                       struct ov_error *const err);

/**
 * @brief Destroy tooltip manager and free resources
 *
 * @param ttpp Pointer to tooltip manager instance
 */
void config_dialog_combo_tooltip_destroy(struct config_dialog_combo_tooltip **ttpp);
