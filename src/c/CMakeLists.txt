option(USE_COMPILER_RT "use compiler-rt runtime" OFF)
option(USE_ADDRESS_SANITIZER "use address sanitizer" OFF)
cmake_minimum_required(VERSION 3.10)

find_program(CLANG_FORMAT_EXE clang-format)
file(GLOB_RECURSE C_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "*.c" "*.h")
list(FILTER C_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/3rd")
list(FILTER C_SOURCES EXCLUDE REGEX "test_🌙_module\\.(c|h)") # clang-format does not support emoji filenames

add_custom_target(${PROJECT_NAME}-format-c
  COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${C_SOURCES}
)

set(STYLUA_VERSION "v2.3.1")
if(CMAKE_HOST_WIN32)
  set(STYLUA_ARCHIVE "stylua-windows-x86_64.zip")
  set(STYLUA_EXE_NAME "stylua.exe")
else()
  set(STYLUA_ARCHIVE "stylua-linux-x86_64-musl.zip")
  set(STYLUA_EXE_NAME "stylua")
endif()
set(STYLUA_URL "https://github.com/JohnnyMorganz/StyLua/releases/download/${STYLUA_VERSION}/${STYLUA_ARCHIVE}")
set(STYLUA_ARCHIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${STYLUA_ARCHIVE}")
set(STYLUA_DIR "${CMAKE_CURRENT_BINARY_DIR}/stylua")
set(STYLUA_EXE "${STYLUA_DIR}/${STYLUA_EXE_NAME}")
if(NOT EXISTS "${STYLUA_EXE}")
  if(NOT EXISTS "${STYLUA_ARCHIVE_PATH}")
    file(DOWNLOAD "${STYLUA_URL}" "${STYLUA_ARCHIVE_PATH}")
  endif()
  file(MAKE_DIRECTORY "${STYLUA_DIR}")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${STYLUA_ARCHIVE_PATH}"
    WORKING_DIRECTORY "${STYLUA_DIR}"
  )
  if(NOT CMAKE_HOST_WIN32)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E chmod +x "${STYLUA_EXE}"
    )
  endif()
endif()

set(STYLUA_CONFIG_PATH "${CMAKE_SOURCE_DIR}/.stylua.toml")
set(STYLUA_CONFIG_ARGS)
if(EXISTS "${STYLUA_CONFIG_PATH}")
  set(STYLUA_CONFIG_ARGS "--config-path" "${STYLUA_CONFIG_PATH}")
endif()

set(LUA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lua")
file(GLOB_RECURSE LUA_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${LUA_SOURCE_DIR}/*.lua")
list(FILTER LUA_SOURCES EXCLUDE REGEX "json\\.lua$")

add_custom_target(${PROJECT_NAME}-format-lua
  COMMAND "${STYLUA_EXE}" ${STYLUA_CONFIG_ARGS} ${LUA_SOURCES}
)

add_custom_target(${PROJECT_NAME}-format ALL
  DEPENDS ${PROJECT_NAME}-format-c ${PROJECT_NAME}-format-lua
)

add_subdirectory(3rd/ovbase)
add_subdirectory(3rd/ovl)
add_subdirectory(3rd/yyjson)

set(LOCAL_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/local)
file(MAKE_DIRECTORY ${LOCAL_INSTALL_PREFIX}/include)
include(ExternalProject)

set(LANGCSV "${CMAKE_CURRENT_SOURCE_DIR}/../i18n/langs.csv")
file(READ "${LANGCSV}" langs)
string(STRIP ${langs} langs)
string(REPLACE "\n" ";" langs "${langs}")
foreach(line IN LISTS langs)
  if (line MATCHES "^#.*$|^([^,]+),$")
    continue()
  endif()
  if (line MATCHES "^([^,]+),([^,]+)$")
    list(APPEND polist "${CMAKE_CURRENT_SOURCE_DIR}/../i18n/${CMAKE_MATCH_1}.po.DO_NOT_EDIT")
  else()
    message(FATAL_ERROR "invalid language difinition: ${line}")
  endif()
endforeach()
add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/i18n.rc"
  COMMAND
    ${CMAKE_COMMAND}
    -Doutput_dir="${CMAKE_CURRENT_BINARY_DIR}"
    -Drctmpl="${CMAKE_CURRENT_SOURCE_DIR}/i18n.rc.tmpl"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/i18n_rc.cmake"
  WORKING_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../i18n"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n.rc.tmpl"
    ${polist}
)
add_custom_target(${PROJECT_NAME}_generate_i18n_rc DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/i18n.rc")

# Version generation - runs every build to check if git state changed
add_custom_target(${PROJECT_NAME}_generate_version_h
  COMMAND ${CMAKE_COMMAND}
    -Dlocal_dir="${CMAKE_CURRENT_SOURCE_DIR}"
    -Dinput_file="${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    -Doutput_file="${CMAKE_CURRENT_BINARY_DIR}/version.h"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/version.cmake"
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.h
  COMMENT "Checking version information..."
)

set(LUAJIT_PLATFORM x86_64)
set(LUAJIT_URL "https://github.com/oov/luajit/releases/download/v2.1.20250708-a2/luajit_v2.1.20250708-a2_${LUAJIT_PLATFORM}.zip")
string(REGEX MATCH "[^/]+$" LUAJIT_ARCHIVE_NAME "${LUAJIT_URL}")
set(LUAJIT_ARCHIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${LUAJIT_ARCHIVE_NAME}")
set(LUAJIT_DIR "${CMAKE_CURRENT_BINARY_DIR}/luajit")
set(LUAJIT_DLL "${LUAJIT_DIR}/bin/luaJIT.dll")
set(LUAJIT_INCLUDE "${LUAJIT_DIR}/include")
if(NOT EXISTS "${LUAJIT_DIR}")
  if(NOT EXISTS "${LUAJIT_ARCHIVE_PATH}")
    file(DOWNLOAD "${LUAJIT_URL}" "${LUAJIT_ARCHIVE_PATH}")
  endif()
  string(REGEX REPLACE "\\.[^.]+$" "" LUAJIT_ARCHIVE_NOEXT "${LUAJIT_ARCHIVE_NAME}")
  file(MAKE_DIRECTORY "${LUAJIT_DIR}")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf ${LUAJIT_ARCHIVE_PATH}
    WORKING_DIRECTORY "${LUAJIT_DIR}"
  )
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy "${LUAJIT_DIR}/bin/luaJIT.dll" "${CMAKE_CURRENT_BINARY_DIR}/luaJIT.dll"
  )
endif()

set(is_clang "$<C_COMPILER_ID:Clang>")
set(v16_or_later "$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,16>")
set(v18_or_later "$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,18>")
set(v19_or_later "$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,19>")
set(v21_or_later "$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,21>")
add_library(gcmzdrops_intf INTERFACE)
target_include_directories(gcmzdrops_intf INTERFACE
  "${CMAKE_CURRENT_BINARY_DIR}" # for version.h
  "${LUAJIT_INCLUDE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rd/aviutl2_plugin_sdk_for_c/include"
)
set(GCMZ_SCRIPT_SUBDIR "GCMZScript")
target_compile_definitions(gcmzdrops_intf INTERFACE
  $<$<COMPILE_LANGUAGE:C>:SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}>
  GCMZ_SCRIPT_SUBDIR="${GCMZ_SCRIPT_SUBDIR}"
  $<$<BOOL:${WIN32}>:_WIN32_WINNT=0x0601>
  _WINDOWS
  $<$<CONFIG:Release>:NDEBUG>
  # WW_DEBUG
)
target_compile_options(gcmzdrops_intf INTERFACE
  $<$<AND:$<BOOL:${WIN32}>,$<BOOL:${USE_COMPILER_RT}>>:--rtlib=compiler-rt>
  -mstackrealign
  -Wall
  -Wextra
  -Werror
  -Weverything
  -Wshadow
  -Werror=return-type
  -pedantic-errors
  -Wno-declaration-after-statement
  -Wno-padded
  $<$<AND:${is_clang},${v21_or_later}>:-Wno-c++-keyword>
  $<$<AND:${is_clang},${v16_or_later}>:-Wno-unsafe-buffer-usage>
  $<$<AND:${is_clang},${v18_or_later}>:-Wno-switch-default>
  $<$<AND:${is_clang},${v19_or_later}>:-Wno-pre-c11-compat>
  -ffunction-sections
  -fdata-sections
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Release>:-O2>
  $<$<BOOL:${USE_ADDRESS_SANITIZER}>:-fsanitize=address>
  $<$<NOT:$<BOOL:${USE_ADDRESS_SANITIZER}>>:-flto>
)
target_link_options(gcmzdrops_intf INTERFACE
  -fuse-ld=lld
  -Wl,--gc-sections
  # -Wl,--print-gc-sections
  -Wl,--kill-at
  $<$<BOOL:${USE_ADDRESS_SANITIZER}>:-fsanitize=address>
  $<$<CONFIG:Release>:-s>
)
add_dependencies(gcmzdrops_intf ${PROJECT_NAME}-format)

add_library(gcmzdrops SHARED
  api.c
  config.c
  config_dialog.c
  config_dialog.rc
  config_dialog_combo_tooltip.c
  config_dialog_listview_tooltip.c
  config_dialog_tooltip.c
  dataobj.c
  dataobj_stream.c
  datauri.c
  delayed_cleanup.c
  dllmain.c
  do.c
  do_sub.c
  drop.c
  copy.c
  error.c
  file.c
  gcmzdrops.c
  gcmzdrops.rc
  i18n.rc
  ini_reader.c
  json.c
  logf.c
  lua.c
  lua_api.c
  lua_script_module_param.c
  luautil.c
  sniffer.c
  temp.c
  tray.c
  window_list.c
)
set_property(SOURCE gcmzdrops.rc APPEND PROPERTY OBJECT_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/gcmzdrops.manifest
)
set(GCMZ_PLUGIN_DIR "${EXPORT_DIR}/Plugin/GCMZDrops")
set_target_properties(gcmzdrops PROPERTIES
  OUTPUT_NAME "GCMZDrops.aux2"
  PREFIX ""
  SUFFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${GCMZ_PLUGIN_DIR}"
)
add_dependencies(gcmzdrops ${PROJECT_NAME}_generate_i18n_rc ${PROJECT_NAME}_generate_version_h)
target_link_libraries(gcmzdrops PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  -Wl,-delayload,luaJIT.dll
  ovbase
  ovl
  yyjson
  comctl32
  shlwapi
  uxtheme
)

set(GCMZ_SCRIPT_DIR "${GCMZ_PLUGIN_DIR}/${GCMZ_SCRIPT_SUBDIR}")
set(LUA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lua")
add_custom_command(TARGET gcmzdrops POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${GCMZ_SCRIPT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy "${LUA_SRC_DIR}/exo.lua" "${GCMZ_SCRIPT_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${LUA_SRC_DIR}/entrypoint.lua" "${GCMZ_SCRIPT_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${LUA_SRC_DIR}/ini.lua" "${GCMZ_SCRIPT_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${LUA_SRC_DIR}/json.lua" "${GCMZ_SCRIPT_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/README.md" "${GCMZ_PLUGIN_DIR}/GCMZDrops.txt"
)

add_executable(test_ini_reader ini_reader_test.c ini_reader.c)
target_link_libraries(test_ini_reader PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  yyjson
  shlwapi
)
add_test(NAME test_ini_reader COMMAND test_ini_reader)

# Test module for Lua C module cleanup verification
add_library(test_cleanup SHARED test_data/test_cleanup_module.c)
target_link_libraries(test_cleanup PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
)
set_target_properties(test_cleanup PROPERTIES
  OUTPUT_NAME "test_cleanup"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_modules"
)

# Test module with Unicode (emoji) name for UTF-8 path handling verification
add_library(test_unicode SHARED test_data/test_🌙_module.c test_data/test_🌙_module.def)
target_link_libraries(test_unicode PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
)
set_target_properties(test_unicode PROPERTIES
  OUTPUT_NAME "test_🌙"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_modules"
)

# Copy Unicode test Lua script to build directory (with proper dependencies)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_data/test_🌙.lua"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/test_🌙.lua"
    "${CMAKE_CURRENT_BINARY_DIR}/test_data/test_🌙.lua"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_data/test_🌙.lua"
  COMMENT "Copying Unicode test Lua script"
)
add_custom_target(unicode_test_script ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_data/test_🌙.lua")

# Test plugin module (single .dll file style) for setup_plugin_loading verification
add_library(test_plugin_cmodule SHARED test_data/lua_plugin/testmod.c)
target_link_libraries(test_plugin_cmodule PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
)
set_target_properties(test_plugin_cmodule PROPERTIES
  OUTPUT_NAME "testmod"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin"
)

# Copy Lua plugin test scripts to build directory (with proper dependencies)
set(LUA_PLUGIN_TEST_OUTPUTS
  "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/testpkg.lua"
  "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/testpkg2/init.lua"
  "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/test_handler.lua"
  "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/entrypoint.lua"
)
add_custom_command(
  OUTPUT ${LUA_PLUGIN_TEST_OUTPUTS}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/lua_plugin/testpkg.lua"
    "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/testpkg.lua"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/testpkg2"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/lua_plugin/testpkg2/init.lua"
    "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/testpkg2/init.lua"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/lua_plugin/test_handler.lua"
    "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/test_handler.lua"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LUA_SOURCE_DIR}/entrypoint.lua"
    "${CMAKE_CURRENT_BINARY_DIR}/test_data/lua_plugin/entrypoint.lua"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/lua_plugin/testpkg.lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/lua_plugin/testpkg2/init.lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_data/lua_plugin/test_handler.lua"
    "${LUA_SOURCE_DIR}/entrypoint.lua"
  COMMENT "Copying Lua plugin test scripts and entrypoint"
)
add_custom_target(lua_plugin_test_scripts ALL DEPENDS ${LUA_PLUGIN_TEST_OUTPUTS})

add_executable(test_lua lua_test.c file.c lua.c luautil.c lua_script_module_param.c)
target_link_libraries(test_lua PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
  yyjson
  comctl32
  shlwapi
)
target_compile_definitions(test_lua PRIVATE
  BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
)
add_test(NAME test_lua COMMAND test_lua)
add_dependencies(test_lua test_cleanup test_unicode test_plugin_cmodule lua_plugin_test_scripts)

add_executable(test_lua_script_module lua_script_module_test.c file.c lua.c luautil.c lua_script_module_param.c)
target_link_libraries(test_lua_script_module PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
  yyjson
  comctl32
  shlwapi
)
add_test(NAME test_lua_script_module COMMAND test_lua_script_module)

add_executable(test_luautil luautil_test.c luautil.c)
target_link_libraries(test_luautil PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
)
add_test(NAME test_luautil COMMAND test_luautil)

add_executable(test_lua_api lua_api_test.c lua_api.c luautil.c)
target_link_libraries(test_lua_api PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
)
add_test(NAME test_lua_api COMMAND test_lua_api)

add_executable(test_exo_lua exo_lua_test.c logf.c lua_api.c luautil.c lua.c file.c ini_reader.c lua_script_module_param.c)
target_link_libraries(test_exo_lua PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
  shlwapi
)
add_test(NAME test_exo_lua COMMAND test_exo_lua)

add_executable(test_sniffer sniffer_test.c sniffer.c)
target_link_libraries(test_sniffer PRIVATE
  gcmzdrops_intf
  ovbase
)
add_test(NAME test_sniffer COMMAND test_sniffer)

add_executable(test_datauri datauri_test.c datauri.c sniffer.c)
target_link_libraries(test_datauri PRIVATE
  gcmzdrops_intf
  ovbase
)
add_test(NAME test_datauri COMMAND test_datauri)

add_executable(test_drop drop_test.c drop.c file.c temp.c ini_reader.c logf.c)
target_link_libraries(test_drop PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
)
add_test(NAME test_drop COMMAND test_drop)

add_executable(test_dataobj dataobj_test.c dataobj_stream.c datauri.c file.c sniffer.c temp.c)
target_link_libraries(test_dataobj PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  yyjson
  ole32
  shlwapi
)
add_test(NAME test_dataobj COMMAND test_dataobj)

add_executable(test_temp temp_test.c temp.c)
target_link_libraries(test_temp PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
)
add_test(NAME test_temp COMMAND test_temp)

add_executable(test_config config_test.c json.c)
target_link_libraries(test_config PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  yyjson
)
add_test(NAME test_config COMMAND test_config)

add_executable(test_config_dialog config_dialog_test.c config_dialog.c config_dialog_combo_tooltip.c config_dialog_listview_tooltip.c config_dialog_tooltip.c config.c json.c)
target_link_libraries(test_config_dialog PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  yyjson
  comctl32
  uxtheme
)
add_test(NAME test_config_dialog COMMAND test_config_dialog)

add_executable(test_gcmz_file file_test.c file.c)
target_link_libraries(test_gcmz_file PRIVATE
  gcmzdrops_intf
  ovbase
  comctl32
  shlwapi
)
add_test(NAME test_gcmz_file COMMAND test_gcmz_file)

add_executable(test_dataobj_stream dataobj_stream_test.c dataobj_stream.c)
target_link_libraries(test_dataobj_stream PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  ole32
)
add_test(NAME test_dataobj_stream COMMAND test_dataobj_stream)

add_executable(test_do do_test.c do.c)
target_link_libraries(test_do PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  comctl32
)
add_test(NAME test_do COMMAND test_do)

add_executable(test_do_sub do_sub_test.c do_sub.c)
target_link_libraries(test_do_sub PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
)
add_test(NAME test_do_sub COMMAND test_do_sub)

add_executable(test_api api_test.c api.c file.c json.c)
target_link_libraries(test_api PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
  yyjson
  comctl32
  shlwapi
)
add_test(NAME test_api COMMAND test_api)

add_executable(test_copy copy_test.c json.c do.c api.c drop.c file.c ini_reader.c lua.c lua_api.c luautil.c lua_script_module_param.c dataobj.c dataobj_stream.c datauri.c sniffer.c temp.c logf.c)
target_link_libraries(test_copy PRIVATE
  gcmzdrops_intf
  "${LUAJIT_DLL}"
  ovbase
  ovl
  yyjson
  comctl32
  shlwapi
  gdiplus
)
add_test(NAME test_copy COMMAND test_copy)

add_executable(test_delayed_cleanup delayed_cleanup_test.c delayed_cleanup.c file.c temp.c)
target_link_libraries(test_delayed_cleanup PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
)
add_test(NAME test_delayed_cleanup COMMAND test_delayed_cleanup)

add_executable(test_tray tray_test.c tray.c)
target_link_libraries(test_tray PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
)
add_test(NAME test_tray COMMAND test_tray)

add_executable(test_window_list window_list_test.c window_list.c)
target_link_libraries(test_window_list PRIVATE
  gcmzdrops_intf
  ovbase
  ovl
)
add_test(NAME test_window_list COMMAND test_window_list)
