#pragma once

#include <stdbool.h>
#include <wchar.h>

bool gcmz_is_virtual_file_skip_target(wchar_t const *const filename);
bool gcmz_is_copy_target_file(wchar_t const *const filename);
