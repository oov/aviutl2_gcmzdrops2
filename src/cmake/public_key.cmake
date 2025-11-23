if(NOT DEFINED input_file OR NOT DEFINED output_file)
  message(FATAL_ERROR "Required variables not defined: input_file, output_file")
endif()

# Get public key from environment variable
set(GCMZ_PUBLIC_KEY_ENV "$ENV{GCMZ_PUBLIC_KEY}")

if("${GCMZ_PUBLIC_KEY_ENV}" STREQUAL "")
  message(FATAL_ERROR "GCMZ_PUBLIC_KEY environment variable not set. Please set it in .env file or export it.")
endif()

# Validate hex string format
string(LENGTH "${GCMZ_PUBLIC_KEY_ENV}" key_length)
if(NOT key_length EQUAL 64)
  message(FATAL_ERROR "GCMZ_PUBLIC_KEY must be exactly 64 hex characters (32 bytes), got ${key_length} characters")
endif()

# Check if it's a valid hex string
string(REGEX MATCH "^[0-9a-fA-F]+$" is_hex "${GCMZ_PUBLIC_KEY_ENV}")
if(NOT is_hex)
  message(FATAL_ERROR "GCMZ_PUBLIC_KEY must contain only hexadecimal characters")
endif()

# Convert hex string to C array format
set(PUBLIC_KEY_BYTES "")
string(LENGTH "${GCMZ_PUBLIC_KEY_ENV}" hex_len)
math(EXPR byte_count "${hex_len} / 2")

set(bytes_list "")
math(EXPR last_index "${byte_count} - 1")

foreach(i RANGE 0 ${last_index})
  math(EXPR pos "${i} * 2")
  string(SUBSTRING "${GCMZ_PUBLIC_KEY_ENV}" ${pos} 2 hex_byte)
  list(APPEND bytes_list "0x${hex_byte}")
endforeach()

string(REPLACE ";" ", " PUBLIC_KEY_BYTES "${bytes_list}")

# Generate header from template
configure_file("${input_file}" "${output_file}" @ONLY NEWLINE_STYLE LF)

# Output information for debugging
message(STATUS "Generated ini_sign_key.h with public key from GCMZ_PUBLIC_KEY")
