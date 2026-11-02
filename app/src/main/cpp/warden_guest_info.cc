// What the linked guest image actually contains, measured at runtime.
//
// Added because the bring-up overlay was a wall of numbers with no statement of what
// the app can and cannot do yet - which is a fair thing to be confused by. This
// produces the top block: a short, honest status, with the guest figures counted rather
// than quoted from a build log.
//
// Counting the function table also proves something worth proving: that 53,107
// recompiled functions are not merely in the library but reachable through the mapping
// the guest uses for indirect calls. A linker can resolve symbols that are then
// unreachable; walking the table cannot.

#include "warden_guest_info.h"

#include <cstdio>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"

namespace warden {

GuestImage DescribeGuestImage() {
  GuestImage image;
  // Nullptr-terminated, as the generator emits it. Bounded anyway: a corrupt or
  // unterminated table would otherwise walk off the end of the library.
  constexpr size_t kSanityLimit = 1u << 20;
  for (size_t i = 0; i < kSanityLimit; ++i) {
    const PPCFuncMapping& entry = warden::game::Mappings()[i];
    if (!entry.host && !entry.guest) {
      break;
    }
    const uint32_t address = uint32_t(entry.guest);
    if (!image.function_count) {
      image.lowest_address = address;
      image.highest_address = address;
    } else {
      if (address < image.lowest_address) {
        image.lowest_address = address;
      }
      if (address > image.highest_address) {
        image.highest_address = address;
      }
    }
    ++image.function_count;
  }
  return image;
}

std::string StatusReport(const Status& status) {
  const GuestImage image = DescribeGuestImage();
  char line[256];
  std::string out = "WARDEN'S PORT - phase 5, not yet playable\n";

  auto row = [&](const char* label, const char* state, const char* detail) {
    std::snprintf(line, sizeof(line), "  %-14s %-10s %s\n", label, state, detail);
    out += line;
  };

  row("renderer", status.presenting ? "ready" : "no",
      status.presenting ? "Vulkan device up, presenting" : "not presenting");

  char detail[160];
  if (status.game_data_ready) {
    std::snprintf(detail, sizeof(detail), "extracted to app storage");
  } else {
    std::snprintf(detail, sizeof(detail), "not extracted yet");
  }
  row("game data", status.game_data_ready ? "ready" : "no", detail);

  std::snprintf(detail, sizeof(detail), "%zu functions, %08X-%08X",
                image.function_count, image.lowest_address, image.highest_address);
  row("guest code", image.function_count ? "linked" : "MISSING", detail);

  std::snprintf(detail, sizeof(detail), "%zu declared, %zu implemented",
                status.imports_declared, status.imports_implemented);
  row("kernel", "stubs", detail);

  // The honest part, and the reason this block exists. There is no button to press
  // because there is nothing behind it yet. Booting needs three things, none of them
  // written:
  //
  //   1. the XEX image loaded into guest memory at 0x82000000. Measured from the disc
  //      (docs/ASSETS.md): AES-128-CBC encrypted, but compression type 1 - a raw block
  //      list, four blocks, 19,005,440 bytes of data plus 753,664 of zero-fill. So
  //      decryption and block assembly, and *no* LZX decompression.
  //   2. a guest thread context and a stack. The header asks for 256 KB.
  //   3. a call to the entry point, 0x829E8698.
  if (status.image_loaded && status.image_matches_build) {
    row("image", "loaded", "decrypted and verified against the build");
  } else if (status.image_loaded) {
    row("image", "WRONG", "loaded, but not this build's image - see below");
  } else {
    row("image", "no", "default.xex not loaded");
  }
  row("boot", "NOT YET", "no guest thread - entry point never called");
  out += "\n";
  out += "  Nothing to start yet: the game's code is linked but is never called.\n";
  out += "  What follows is what the port can measure about this device.\n";
  return out;
}

}  // namespace warden
