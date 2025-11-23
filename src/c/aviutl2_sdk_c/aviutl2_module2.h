#pragma once

// This file based on module2.h from AviUtl ExEdit2 Plugin SDK
// LICENSE: The MIT License / Copyright (c) 2025 Kenkun

// Script modules must export the following functions for the host to call:
//
// Required entry point returning the script module definition
//   struct aviutl2_script_module_table *GetScriptModuleTable(void)
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

/**
 * Script module parameter interface
 * Used for passing parameters to and receiving results from script module functions
 */
struct aviutl2_script_module_param {
  /**
   * Get parameter count
   * @return Number of parameters passed to the function
   */
  int (*get_param_num)(void);

  /**
   * Get parameter as integer
   * @param index Parameter position (0 based)
   * @return Parameter value (0 if not available)
   */
  int (*get_param_int)(int index);

  /**
   * Get parameter as floating point number
   * @param index Parameter position (0 based)
   * @return Parameter value (0 if not available)
   */
  double (*get_param_double)(int index);

  /**
   * Get parameter as string (UTF-8)
   * @param index Parameter position (0 based)
   * @return Pointer to parameter string (NULL if not available). Returned string is valid until the end of callback
   * function that uses this function
   */
  char const *(*get_param_string)(int index);

  /**
   * Get parameter as data pointer
   * @param index Parameter position (0 based)
   * @return Parameter value (NULL if not available)
   */
  void *(*get_param_data)(int index);

  //--------------------------------

  /**
   * Get associative array element as integer
   * @param index Parameter position (0 based)
   * @param key Key string (UTF-8)
   * @return Element value (0 if not available)
   */
  int (*get_param_table_int)(int index, char const *key);

  /**
   * Get associative array element as floating point number
   * @param index Parameter position (0 based)
   * @param key Key string (UTF-8)
   * @return Element value (0 if not available)
   */
  double (*get_param_table_double)(int index, char const *key);

  /**
   * Get associative array element as string (UTF-8)
   * @param index Parameter position (0 based)
   * @param key Key string (UTF-8)
   * @return Pointer to element string (NULL if not available). Returned string is valid until the end of callback
   * function that uses this function
   */
  char const *(*get_param_table_string)(int index, char const *key);

  //--------------------------------

  /**
   * Get array element count
   * @param index Parameter position (0 based)
   * @return Number of array elements
   */
  int (*get_param_array_num)(int index);

  /**
   * Get array element as integer
   * @param index Parameter position (0 based)
   * @param key Array index (0 based)
   * @return Element value (0 if not available)
   */
  int (*get_param_array_int)(int index, int key);

  /**
   * Get array element as floating point number
   * @param index Parameter position (0 based)
   * @param key Array index (0 based)
   * @return Element value (0 if not available)
   */
  double (*get_param_array_double)(int index, int key);

  /**
   * Get array element as string (UTF-8)
   * @param index Parameter position (0 based)
   * @param key Array index (0 based)
   * @return Pointer to element string (NULL if not available). Returned string is valid until the end of callback
   * function that uses this function
   */
  char const *(*get_param_array_string)(int index, int key);

  //--------------------------------

  /**
   * Add integer return value
   * @param value Return value to add
   */
  void (*push_result_int)(int value);

  /**
   * Add floating point return value
   * @param value Return value to add
   */
  void (*push_result_double)(double value);

  /**
   * Add string (UTF-8) return value
   * @param value Return value to add
   */
  void (*push_result_string)(char const *value);

  /**
   * Add data pointer return value
   * @param value Return value to add
   */
  void (*push_result_data)(void *value);

  //--------------------------------

  /**
   * Add integer associative array return value
   * @param key Array of key strings (UTF-8)
   * @param value Array of integer values
   * @param num Number of array elements
   */
  void (*push_result_table_int)(char const **key, int *value, int num);

  /**
   * Add floating point associative array return value
   * @param key Array of key strings (UTF-8)
   * @param value Array of floating point values
   * @param num Number of array elements
   */
  void (*push_result_table_double)(char const **key, double *value, int num);

  /**
   * Add string (UTF-8) associative array return value
   * @param key Array of key strings (UTF-8)
   * @param value Array of string values (UTF-8)
   * @param num Number of array elements
   */
  void (*push_result_table_string)(char const **key, char const **value, int num);

  //--------------------------------

  /**
   * Add integer array return value
   * @param value Array of integer values
   * @param num Number of array elements
   */
  void (*push_result_array_int)(int *value, int num);

  /**
   * Add floating point array return value
   * @param value Array of floating point values
   * @param num Number of array elements
   */
  void (*push_result_array_double)(double *value, int num);

  /**
   * Add string (UTF-8) array return value
   * @param value Array of string values (UTF-8)
   * @param num Number of array elements
   */
  void (*push_result_array_string)(char const **value, int num);

  //--------------------------------

  /**
   * Set error message
   * Call this when the invoked function encounters an error
   * @param message Error message (UTF-8)
   */
  void (*set_error)(char const *message);

  //--------------------------------

  /**
   * Get parameter as boolean
   * @param index Parameter position (0 based)
   * @return Parameter value (false if not available)
   */
  bool (*get_param_boolean)(int index);

  /**
   * Add boolean return value
   * @param value Return value to add
   */
  void (*push_result_boolean)(bool value);

  /**
   * Get associative array element as boolean
   * @param index Parameter position (0 based)
   * @param key Key string (UTF-8)
   * @return Element value (false if not available)
   */
  bool (*get_param_table_boolean)(int index, char const *key);
};

//--------------------------------

/**
 * Script module function definition
 */
struct aviutl2_script_module_function {
  /**
   * Function name (used for script invocation)
   */
  wchar_t const *name;

  /**
   * Function pointer to the implementation
   * @param param Pointer to parameter interface
   */
  void (*func)(struct aviutl2_script_module_param *param);
};

/**
 * Script module table
 * Defines metadata and function list for a script module
 */
struct aviutl2_script_module_table {
  /**
   * Module information string
   * Contains module name and description
   */
  wchar_t const *information;

  /**
   * Array of script module functions
   * Null-terminated array (last element has NULL name field)
   */
  struct aviutl2_script_module_function *functions;
};
