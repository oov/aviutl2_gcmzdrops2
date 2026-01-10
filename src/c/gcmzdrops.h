#pragma once

#include <ovbase.h>

struct gcmzdrops;
struct gcmz_lua_context;
struct aviutl2_host_app_table;
struct aviutl2_edit_section;
struct aviutl2_project_file;

/**
 * @brief Create and initialize gcmzdrops context
 *
 * @param ctx [out] Pointer to store the created context
 * @param lua_ctx Lua context created by gcmz_lua_create (ownership NOT transferred)
 * @param version AviUtl ExEdit2 version number
 * @param err [out] Error information on failure
 * @return true on success, false on failure
 */
NODISCARD bool gcmzdrops_create(struct gcmzdrops **const ctx,
                                struct gcmz_lua_context *const lua_ctx,
                                uint32_t const version,
                                struct ov_error *const err);

/**
 * @brief Destroy gcmzdrops context
 *
 * @param ctx [in,out] Pointer to context to destroy, will be set to NULL
 */
void gcmzdrops_destroy(struct gcmzdrops **const ctx);

/**
 * @brief Register plugin with AviUtl2 host
 */
void gcmzdrops_register(struct gcmzdrops *const ctx, struct aviutl2_host_app_table *const host);

/**
 * @brief Show configuration dialog
 *
 * @param ctx Plugin context
 * @param hwnd Parent window handle
 * @param dll_hinst DLL instance handle
 */
void gcmzdrops_show_config_dialog(struct gcmzdrops *const ctx, void *const hwnd, void *const dll_hinst);

/**
 * @brief Handle project load event
 *
 * @param ctx Plugin context
 * @param project Project file interface
 */
void gcmzdrops_on_project_load(struct gcmzdrops *const ctx, struct aviutl2_project_file *const project);

/**
 * @brief Handle project save event
 *
 * @param ctx Plugin context
 * @param project Project file interface
 */
void gcmzdrops_on_project_save(struct gcmzdrops *const ctx, struct aviutl2_project_file *const project);

/**
 * @brief Paste from clipboard
 *
 * @param ctx Plugin context
 * @param edit Edit section (must be valid, called from within edit section callback)
 */
void gcmzdrops_paste_from_clipboard(struct gcmzdrops *const ctx, struct aviutl2_edit_section *const edit);
