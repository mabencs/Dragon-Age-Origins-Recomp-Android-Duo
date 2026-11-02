// A short, honest statement of what the port can do right now, plus guest-image facts
// counted at runtime. See warden_guest_info.cc.
#ifndef WARDEN_GUEST_INFO_H_
#define WARDEN_GUEST_INFO_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace warden {

struct GuestImage {
  size_t function_count = 0;
  uint32_t lowest_address = 0;
  uint32_t highest_address = 0;
};

struct Status {
  bool presenting = false;
  bool game_data_ready = false;
  size_t imports_declared = 0;
  size_t imports_implemented = 0;
  bool image_loaded = false;
  bool image_matches_build = false;
};

// Walks the recompiled function mapping table the guest uses for indirect calls.
GuestImage DescribeGuestImage();

// The block at the top of the overlay.
std::string StatusReport(const Status& status);

}  // namespace warden

#endif  // WARDEN_GUEST_INFO_H_
