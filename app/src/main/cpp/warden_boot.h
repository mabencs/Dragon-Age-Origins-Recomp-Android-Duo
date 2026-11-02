// Loading the title image into guest memory. See warden_boot.cc.
#ifndef WARDEN_BOOT_H_
#define WARDEN_BOOT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "warden_xex.h"

namespace xe {
class Memory;
}

namespace warden {

struct BootImage {
  bool loaded = false;
  // Whether the image belongs to the recompilation linked beside it, measured rather
  // than assumed - see warden_boot.cc.
  bool matches_build = false;
  uint32_t base = 0;
  uint32_t size = 0;
  uint32_t entry_point = 0;
  uint32_t stack_size = 0;
  // Thread-local storage, from the XEX header (warden_xex.h).
  uint32_t tls_slot_count = 0;
  uint32_t tls_raw_data_address = 0;
  uint32_t tls_data_size = 0;
  uint32_t tls_raw_data_size = 0;
  // Every import record, for the variable patch (warden_kernel_variables.cc).
  std::vector<XexImport> imports;
  size_t entry_points_checked = 0;
  size_t entry_points_matching = 0;
  double match_ratio = 0.0;
  // Which title update was applied, if any, and where its patch came from.
  bool patched = false;
  std::string patch_source;
  // The title update's data files, which take precedence over the base ERFs.
  size_t overlay_written = 0;
  size_t overlay_present = 0;
  std::string detail;

  std::string Describe() const;
};

// Reads default.xex from the extracted game data, applies a title update's patch if
// one can be found, decrypts and assembles the result, and writes it into guest memory
// at its image base.
//
// `search_dirs` are looked through for a title update package (a LIVE/PIRS file). The
// app passes the game data directory and the app's own storage, which is where adb or
// a file manager can put one.
BootImage LoadTitleImage(const std::string& game_data_dir,
                         const std::vector<std::string>& search_dirs,
                         xe::Memory& memory);

// PLAN 12.5: a title update package's default.xexp written to `out_path`, for the making
// of the game's code on the device. False if it is not a package holding one.
bool ExtractTitleUpdatePatch(const std::string& package_path, const std::string& out_path);

}  // namespace warden

#endif  // WARDEN_BOOT_H_
