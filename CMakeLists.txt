cmake_minimum_required(VERSION 3.10)
project(gcmzdrops C)
enable_testing()

set(EXPORT_DIR_NAME "bin" CACHE STRING "export directory")
if ("${EXPORT_DIR_NAME}" STREQUAL "")
  set(EXPORT_DIR_NAME "bin" CACHE STRING "export directory" FORCE)
endif()
set(EXPORT_DIR "${PROJECT_BINARY_DIR}/${EXPORT_DIR_NAME}")

add_subdirectory(src/c)
