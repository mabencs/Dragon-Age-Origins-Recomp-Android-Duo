// Identifying a disc image from its XEX2 execution header.
//
// Added in Phase 4.4 after the handheld's Download folder turned out to hold three
// disc images, two of them other games. Picking "the largest .iso in the folder"
// happened to choose correctly, which is not the same as choosing correctly: the right
// answer is to read the image's own identity.
//
// Measured from the discs themselves (`python tools/dao_iso.py info <image>`):
//
//   Origins      title 0x454108C0   media 0x186A1115   version 0.0.0.1
//   Awakening    title 0x454108C0   media 0x2E6F7560   version 0.0.0.9
//
// So the title id says "this is Dragon Age" and the media id says which disc. Both are
// needed: the title id alone cannot tell Origins from Awakening, and the media id
// alone would have to be listed exhaustively to reject anything else.
//
// The title id is also what xe::dao_host::TitleId() has to return, so this serves
// Phase 5 as well as the folder scan.
#ifndef WARDEN_TITLE_H_
#define WARDEN_TITLE_H_

#include <cstdint>
#include <string>

#include "warden_xdvdfs.h"

namespace warden {

// Dragon Age: Origins, both discs.
constexpr uint32_t kDragonAgeTitleId = 0x454108C0;
constexpr uint32_t kOriginsMediaId = 0x186A1115;
constexpr uint32_t kAwakeningMediaId = 0x2E6F7560;

struct TitleInfo {
  bool valid = false;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
  uint8_t disc_number = 0;
  uint8_t disc_count = 0;

  bool is_dragon_age() const { return title_id == kDragonAgeTitleId; }
  bool is_origins() const { return is_dragon_age() && media_id == kOriginsMediaId; }
  bool is_awakening() const { return is_dragon_age() && media_id == kAwakeningMediaId; }

  // "Dragon Age: Origins", "Dragon Age: Origins - Awakening", or the raw ids.
  std::string Describe() const;
};

// Reads default.xex from an opened image and parses its execution header.
TitleInfo ReadTitleInfo(Xdvdfs& iso);

// Parses an XEX2 blob directly. Exposed so a host test can feed it bytes.
TitleInfo ParseXex(const uint8_t* data, size_t size);

}  // namespace warden

#endif  // WARDEN_TITLE_H_
