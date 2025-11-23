#pragma once

// This file based on plugin2.h from AviUtl ExEdit2 Plugin SDK
// LICENSE: The MIT License / Copyright (c) 2025 Kenkun

// Plugins must export the following functions for the host to call:
//
// Required plugin registration function
//   void RegisterPlugin(struct aviutl2_host_app_table *host)
//
// Optional plugin DLL initialization function (version is the host application version)
//   bool InitializePlugin(uint32_t version)
//
// Optional plugin DLL uninitialization function
//   void UninitializePlugin(void)
//
// Optional logger initialization function (see aviutl2_logger2.h). Called before InitializePlugin()
//   void InitializeLogger(struct aviutl2_log_handle *logger)

#include <stdbool.h>
#include <stdint.h>

// Forward declarations
struct aviutl2_input_plugin_table;
struct aviutl2_output_plugin_table;
struct aviutl2_filter_plugin_table;
struct aviutl2_script_module_table;

/**
 * Object handle
 */
typedef void *aviutl2_object_handle;

/**
 * Object layer and frame information
 * In object frames, frame numbers and layer numbers start from 0 (different from UI display)
 */
struct aviutl2_object_layer_frame {
  int layer; /**< Layer number */
  int start; /**< Start frame number */
  int end;   /**< End frame number */
};

//--------------------------------

/**
 * Edit information
 * In edit info, frame numbers and layer numbers start from 0 (different from UI display)
 */
struct aviutl2_edit_info {
  int width, height; /**< Scene resolution */
  int rate, scale;   /**< Scene frame rate */
  int sample_rate;   /**< Scene sampling rate */
  int frame;         /**< Current cursor frame number */
  int layer;         /**< Current selected layer number */
  int frame_max;     /**< Maximum frame number where objects exist */
  int layer_max;     /**< Maximum layer number where objects exist */
};

/**
 * Edit section
 * In edit section, frame numbers and layer numbers start from 0 (different from UI display)
 */
struct aviutl2_edit_section {
  /**
   * Edit information
   */
  struct aviutl2_edit_info *info;

  /**
   * Create object alias at specified position
   * @param alias Pointer to object alias data (UTF-8). Same format as object alias file
   * @param layer Layer number to create
   * @param frame Frame number to create
   * @param length Frame length of object. Used when frame length is not specified in alias data
   * @return Handle of created object (returns NULL on failure). Fails if overlapping with existing object or if alias
   * data is invalid
   */
  aviutl2_object_handle (*create_object_from_alias)(char const *alias, int layer, int frame, int length);

  /**
   * Find object at specified frame number or later
   * @param layer Target layer number
   * @param frame Frame number to start search from
   * @return Handle of found object (returns NULL if not found)
   */
  aviutl2_object_handle (*find_object)(int layer, int frame);

  /**
   * Get number of target effects in object
   * @param object Object handle
   * @param effect Target effect name (effect.name value in alias file)
   * @return Number of target effects (0 if not found)
   */
  int (*count_object_effect)(aviutl2_object_handle object, wchar_t const *effect);

  /**
   * Get object layer and frame information
   * @param object Object handle
   * @return Object layer and frame information
   */
  struct aviutl2_object_layer_frame (*get_object_layer_frame)(aviutl2_object_handle object);

  /**
   * Get object alias data
   * @param object Object handle
   * @return Pointer to object alias data (UTF-8) (returns NULL if cannot be obtained).
   *         Same format as object alias file.
   *         Returned string is valid until the end of callback function that uses this function
   */
  char const *(*get_object_alias)(aviutl2_object_handle object);

  /**
   * Get object configuration item value as string
   * @param object Object handle
   * @param effect Target effect name (effect.name value in alias file)
   * @param item Target configuration item name (key name in alias file)
   * @return Pointer to obtained configuration value (UTF-8) (returns NULL if cannot be obtained).
   *         Same format as configuration value in alias file.
   *         Returned string is valid until the end of callback function that uses this function
   */
  char const *(*get_object_item_value)(aviutl2_object_handle object, wchar_t const *effect, wchar_t const *item);

  /**
   * Set object configuration item value as string
   * @param object Object handle
   * @param effect Target effect name (effect.name value in alias file)
   * @param item Target configuration item name (key name in alias file)
   * @param value Configuration value (UTF-8). Same format as configuration value in alias file
   * @return true if configuration succeeded (fails if target does not exist)
   */
  bool (*set_object_item_value)(aviutl2_object_handle object,
                                wchar_t const *effect,
                                wchar_t const *item,
                                char const *value);

  /**
   * Move object
   * @param object Object handle
   * @param layer Destination layer number
   * @param frame Destination frame number
   * @return true if move succeeded (fails if object exists at destination)
   */
  bool (*move_object)(aviutl2_object_handle object, int layer, int frame);

  /**
   * Delete object
   * @param object Object handle
   */
  void (*delete_object)(aviutl2_object_handle object);

  /**
   * Get handle of currently selected object in object configuration window
   * @return Object handle (returns NULL if no selection)
   */
  aviutl2_object_handle (*get_focus_object)(void);

  /**
   * Set object to be selected in object configuration window (set at end of callback)
   * @param object Object handle
   */
  void (*set_focus_object)(aviutl2_object_handle object);

  /**
   * Deprecated logging helper kept for compatibility
   * @param message Message to output (UTF-16)
   */
  void (*deprecated_output_log)(wchar_t const *message);

  /**
   * Get handle of selected object in layer editor by index
   * @param index Selection index (0 based)
   * @return Object handle (NULL if index is out of range)
   */
  aviutl2_object_handle (*get_selected_object)(int index);

  /**
   * Get number of selected objects in layer editor
   * @return Number of selected objects
   */
  int (*get_selected_object_num)(void);
};

/**
 * Edit handle
 */
struct aviutl2_edit_handle {
  /**
   * Call callback function (func_proc_edit) to edit project data
   * Callback function is called every time edit is repeated
   * Objects edited in callback function are automatically registered to Undo
   * Callback function is called from main thread
   * @param func_proc_edit Callback function for editing
   * @return true on success. Fails if edit is not available (during output)
   */
  bool (*call_edit_section)(void (*func_proc_edit)(struct aviutl2_edit_section *edit));

  /**
   * Call callback function (func_proc_edit) to edit project data with parameter
   * @param param Parameter to pass to callback function
   * @param func_proc_edit Callback function for editing
   * @return true on success. Fails if edit is not available (during output)
   */
  bool (*call_edit_section_param)(void *param, void (*func_proc_edit)(void *param, struct aviutl2_edit_section *edit));
};

//--------------------------------

/**
 * Project file
 * Used in callbacks for loading and saving project files
 * Project save data is part of plugin data
 */
struct aviutl2_project_file {
  /**
   * Get string (UTF-8) saved in project
   * @param key Key name (UTF-8)
   * @return Pointer to obtained string (NULL if not set)
   */
  char const *(*get_param_string)(char const *key);

  /**
   * Save string (UTF-8) to project
   * @param key Key name (UTF-8)
   * @param value String to save (UTF-8)
   */
  void (*set_param_string)(char const *key, char const *value);

  /**
   * Get binary data saved in project
   * @param key Key name (UTF-8)
   * @param data Pointer to storage for obtained data
   * @param size Size of data to obtain (fails if different from saved size)
   * @return true if data obtained correctly
   */
  bool (*get_param_binary)(char const *key, void *data, int size);

  /**
   * Save binary data to project
   * @param key Key name (UTF-8)
   * @param data Pointer to data to save
   * @param size Size of data to save (4096 bytes or less)
   */
  void (*set_param_binary)(char const *key, void *data, int size);

  /**
   * Delete all data saved in project
   */
  void (*clear_params)(void);
};

//--------------------------------

/**
 * Host application table
 */
struct aviutl2_host_app_table {
  /**
   * Set plugin information
   * @param information Plugin information
   */
  void (*set_plugin_information)(wchar_t const *information);

  /**
   * Register input plugin
   * @param input_plugin_table Input plugin table
   */
  void (*register_input_plugin)(struct aviutl2_input_plugin_table *input_plugin_table);

  /**
   * Register output plugin
   * @param output_plugin_table Output plugin table
   */
  void (*register_output_plugin)(struct aviutl2_output_plugin_table *output_plugin_table);

  /**
   * Register filter plugin
   * @param filter_plugin_table Filter plugin table
   */
  void (*register_filter_plugin)(struct aviutl2_filter_plugin_table *filter_plugin_table);

  /**
   * Register script module
   * @param script_module_table Script module table
   */
  void (*register_script_module)(struct aviutl2_script_module_table *script_module_table);

  /**
   * Register import menu
   * @param name Import menu name
   * @param func_proc_import Callback function for import menu selection
   */
  void (*register_import_menu)(wchar_t const *name, void (*func_proc_import)(struct aviutl2_edit_section *edit));

  /**
   * Register export menu
   * @param name Export menu name
   * @param func_proc_export Callback function for export menu selection
   */
  void (*register_export_menu)(wchar_t const *name, void (*func_proc_export)(struct aviutl2_edit_section *edit));

  /**
   * Register window client
   * @param name Window name
   * @param hwnd Window handle. WS_CHILD will be added to window and parent window will be set (WS_POPUP will be
   * removed)
   */
  void (*register_window_client)(wchar_t const *name, HWND hwnd);

  /**
   * Get handle for editing project data
   * @return Edit handle
   */
  struct aviutl2_edit_handle *(*create_edit_handle)(void);

  /**
   * Register function to be called when project file is loaded (called during project initialization)
   * @param func_project_load Callback function for project file load
   */
  void (*register_project_load_handler)(void (*func_project_load)(struct aviutl2_project_file *project));

  /**
   * Register function to be called just before project file is saved
   * @param func_project_save Callback function for project file save
   */
  void (*register_project_save_handler)(void (*func_project_save)(struct aviutl2_project_file *project));

  /**
   * Register layer context menu item (layer editor, no selection state)
   * @param name Menu item name
   * @param func_proc_layer_menu Callback invoked on menu selection
   */
  void (*register_layer_menu)(wchar_t const *name, void (*func_proc_layer_menu)(struct aviutl2_edit_section *edit));

  /**
   * Register object context menu item (layer editor, object selected state)
   * @param name Menu item name
   * @param func_proc_object_menu Callback invoked on menu selection
   */
  void (*register_object_menu)(wchar_t const *name, void (*func_proc_object_menu)(struct aviutl2_edit_section *edit));
};
