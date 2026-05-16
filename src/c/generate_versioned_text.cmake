if(NOT DEFINED version_env OR NOT DEFINED input_file OR NOT DEFINED output_file)
  message(FATAL_ERROR "Required variables not defined: version_env, input_file, output_file")
endif()

if(NOT EXISTS "${version_env}")
  message(FATAL_ERROR "version.env not found: ${version_env}")
endif()

file(STRINGS "${version_env}" version_lines)
foreach(line IN LISTS version_lines)
  if(line MATCHES "^GCMZ_VERSION=(.+)$")
    set(GCMZ_VERSION "${CMAKE_MATCH_1}")
  endif()
endforeach()

if(NOT DEFINED GCMZ_VERSION OR "${GCMZ_VERSION}" STREQUAL "")
  message(FATAL_ERROR "GCMZ_VERSION not found in version.env: ${version_env}")
endif()

configure_file("${input_file}" "${output_file}" @ONLY NEWLINE_STYLE CRLF)
