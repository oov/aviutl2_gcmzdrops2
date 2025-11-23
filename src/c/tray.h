#pragma once

#include <ovbase.h>

struct gcmz_tray;

/**
 * @brief Menu callback event data
 */
struct gcmz_tray_callback_event {
  enum gcmz_tray_callback_type {
    gcmz_tray_callback_clicked,    ///< Menu item was clicked
    gcmz_tray_callback_query_info, ///< Query item info: set label/enabled (label==NULL hides)
  } type;                          ///< Event type

  union {
    struct gcmz_tray_query_info_result {
      wchar_t const *label; ///< Label (NULL = hide). Must remain valid until next query.
      bool enabled;         ///< true = enabled; false = disabled (grayed)
    } query_info;
  } result;
};

/**
 * @brief Menu item callback function
 *
 * Called on the tray thread for events:
 *  - gcmz_tray_callback_query_info: set `query_info.label` and `enabled` (label==NULL hides)
 *  - gcmz_tray_callback_clicked: handle a click (do not block)
 *
 * @param userdata Menu item specific user data
 * @param event Event data (input/output)
 */
typedef void (*gcmz_tray_callback)(void *const userdata, struct gcmz_tray_callback_event *const event);

/**
 * @brief Create and start system tray icon with context menu
 *
 * Creates a system tray icon that runs in a separate thread with its own message loop.
 * Menu items can be added dynamically via gcmz_tray_add_menu_item().
 *
 * @param icon Icon to display in the system tray (can be NULL for default icon)
 * @param err [out] Error information
 * @return Tray instance pointer on success, NULL on failure
 * @see gcmz_tray_add_menu_item
 * @see gcmz_tray_destroy
 */
NODISCARD struct gcmz_tray *gcmz_tray_create(void *const icon, struct ov_error *const err);

/**
 * @brief Add a menu item to the tray context menu
 *
 * This function is thread-safe and can be called from any thread.
 *
 * @param tray Tray instance pointer
 * @param callback Callback function for the menu item
 * @param userdata User data passed to callback
 * @param err [out] Error information
 * @return true on success, false on failure
 */
bool gcmz_tray_add_menu_item(struct gcmz_tray *const tray,
                             gcmz_tray_callback const callback,
                             void *const userdata,
                             struct ov_error *const err);

/**
 * @brief Remove a menu item from the tray context menu
 *
 * This function is thread-safe and can be called from any thread.
 * Removes the first menu item with the matching callback function.
 *
 * @param tray Tray instance pointer
 * @param callback Callback function of the menu item to remove
 */
void gcmz_tray_remove_menu_item(struct gcmz_tray *const tray, gcmz_tray_callback const callback);

/**
 * @brief Destroy tray icon instance and cleanup resources
 *
 * This function sends a close message to the tray window,
 * waits for the tray thread to terminate, and frees all resources.
 *
 * @param traypp Tray instance pointer
 */
void gcmz_tray_destroy(struct gcmz_tray **const traypp);
