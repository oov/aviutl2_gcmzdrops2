#include "gcmz_file_types.h"

#include <ovl/path.h>

static bool filename_has_suffix(wchar_t const *const filename, wchar_t const *const suffix) {
  if (!filename || !suffix) {
    return false;
  }

  size_t const filename_len = wcslen(filename);
  size_t const suffix_len = wcslen(suffix);
  if (filename_len < suffix_len) {
    return false;
  }
  return ovl_path_is_same_ext(filename + filename_len - suffix_len, suffix);
}

static bool filename_has_any_suffix(wchar_t const *const filename, wchar_t const *const suffixes[]) {
  if (!suffixes) {
    return false;
  }
  for (size_t i = 0; suffixes[i]; ++i) {
    if (filename_has_suffix(filename, suffixes[i])) {
      return true;
    }
  }
  return false;
}

bool gcmz_is_virtual_file_skip_target(wchar_t const *const filename) {
  static wchar_t const *const suffixes[] = {
      // Script file types
      L".anm",
      L".anm2",
      L".obj",
      L".obj2",
      L".cam",
      L".cam2",
      L".scn",
      L".scn2",
      L".tra",
      L".tra2",
      // Plugins and packages
      L".aui2",
      L".aul2",
      L".auo2",
      L".aup2",
      L".aux2",
      NULL,
  };
  return filename_has_any_suffix(filename, suffixes);
}

bool gcmz_is_copy_target_file(wchar_t const *const filename) {
  static wchar_t const *const suffixes[] = {
      L".txt",
      L".object",
      L".exo",
      L".au2pkg.zip",
      NULL,
  };
  return !filename_has_any_suffix(filename, suffixes) && !gcmz_is_virtual_file_skip_target(filename);
}
