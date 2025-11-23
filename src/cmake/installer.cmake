if(NOT DEFINED local_dir OR NOT DEFINED input_file OR NOT DEFINED output_file)
  message(FATAL_ERROR "Required variables not defined: local_dir, input_file, output_file")
endif()

if(NOT DEFINED build_output_dir)
  message(FATAL_ERROR "Required variable not defined: build_output_dir")
endif()

if(NOT DEFINED work_dir)
  message(FATAL_ERROR "Required variable not defined: work_dir")
endif()

if(NOT DEFINED output_dir)
  message(FATAL_ERROR "Required variable not defined: output_dir")
endif()

if(NOT DEFINED version)
  message(FATAL_ERROR "Required variable not defined: version")
endif()
set(GCMZ_INSTALLER_VERSION "${version}")

# Download Chinese Simplified language file for Inno Setup
set(CHINESE_SIMPLIFIED_ISL_URL "https://raw.githubusercontent.com/jrsoftware/issrc/main/Files/Languages/Unofficial/ChineseSimplified.isl")
set(CHINESE_SIMPLIFIED_ISL_FILE "${work_dir}/ChineseSimplified.isl")
if(NOT EXISTS "${CHINESE_SIMPLIFIED_ISL_FILE}")
  message(STATUS "Downloading Chinese Simplified language file...")
  file(DOWNLOAD "${CHINESE_SIMPLIFIED_ISL_URL}" "${CHINESE_SIMPLIFIED_ISL_FILE}" STATUS download_status)
  list(GET download_status 0 status_code)
  if(NOT status_code EQUAL 0)
    message(WARNING "Failed to download Chinese Simplified language file")
    set(CHINESE_SIMPLIFIED_ISL_FILE "")
  endif()
endif()

# Set paths (convert to Windows paths with backslashes for Inno Setup)
string(REPLACE "/" "\\" GCMZ_BUILD_OUTPUT_DIR "${build_output_dir}")
string(REPLACE "/" "\\" GCMZ_README_FILE "${local_dir}/README.md")
string(REPLACE "/" "\\" GCMZ_INSTALLER_OUTPUT_DIR "${output_dir}")
if(CHINESE_SIMPLIFIED_ISL_FILE)
  string(REPLACE "/" "\\" GCMZ_CHINESE_SIMPLIFIED_ISL "${CHINESE_SIMPLIFIED_ISL_FILE}")
else()
  set(GCMZ_CHINESE_SIMPLIFIED_ISL "")
endif()

# Generate installer script from template
configure_file("${input_file}" "${output_file}" @ONLY NEWLINE_STYLE CRLF)

message(STATUS "Generated installer script: ${output_file}")
message(STATUS "  Version: ${GCMZ_INSTALLER_VERSION}")
message(STATUS "  Build output: ${GCMZ_BUILD_OUTPUT_DIR}")
message(STATUS "  Installer output: ${GCMZ_INSTALLER_OUTPUT_DIR}")
