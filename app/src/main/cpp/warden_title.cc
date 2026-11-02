#include "warden_title.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace warden {

namespace {

// XEX2 is big-endian throughout - it is an Xbox 360 format and the Xenon was a
// big-endian PowerPC. Assembled explicitly rather than byte-swapped, so the code reads
// the same on either host.
uint32_t ReadBigU32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

constexpr uint32_t kExecutionIdKey = 0x00040006;
// The execution header is 24 bytes: media id, version, base version, title id, then
// platform, exe type, disc number and disc count as single bytes.
constexpr uint32_t kExecutionIdSize = 24;
// Header table starts at 0x18; each entry is a key and a value.
constexpr uint32_t kOptionalHeaderTableOffset = 0x18;

// Enough of the file to hold the header table and everything it points at nearby.
// default.xex is 19 MB and almost all of it is the compressed image; reading the whole
// thing to find four integers would be wasteful on a handheld.
constexpr size_t kHeaderReadSize = 64 * 1024;

}  // namespace

std::string TitleInfo::Describe() const {
  if (!valid) {
    return "not a recognisable XEX2 executable";
  }
  char buffer[160];
  const char* name = is_origins()     ? "Dragon Age: Origins"
                     : is_awakening() ? "Dragon Age: Origins - Awakening"
                     : is_dragon_age() ? "Dragon Age (unknown disc)"
                                       : "some other title";
  std::snprintf(buffer, sizeof(buffer),
                "%s  title %08X  media %08X  v%u.%u.%u.%u  disc %u of %u", name,
                title_id, media_id, version >> 28, (version >> 24) & 0xF,
                (version >> 8) & 0xFFFF, version & 0xFF, disc_number, disc_count);
  return buffer;
}

TitleInfo ParseXex(const uint8_t* data, size_t size) {
  TitleInfo info;
  if (size < kOptionalHeaderTableOffset || std::memcmp(data, "XEX2", 4) != 0) {
    return info;
  }
  const uint32_t header_count = ReadBigU32(data + 0x14);
  // A plausibility bound. The count is read from the file, and without one a corrupt
  // value would walk the table far past the buffer - the offsets are checked below,
  // but there is no reason to iterate four billion times to discover that.
  if (header_count > 256) {
    return info;
  }
  for (uint32_t i = 0; i < header_count; ++i) {
    const size_t entry = kOptionalHeaderTableOffset + size_t(i) * 8;
    if (entry + 8 > size) {
      break;
    }
    const uint32_t key = ReadBigU32(data + entry);
    const uint32_t value = ReadBigU32(data + entry + 4);
    if (key != kExecutionIdKey) {
      continue;
    }
    if (size_t(value) + kExecutionIdSize > size) {
      // The header points outside what was read. Not corrupt necessarily - just
      // further into the file than the window above - so say nothing rather than
      // reporting a wrong answer.
      return info;
    }
    const uint8_t* execution = data + value;
    info.media_id = ReadBigU32(execution);
    info.version = ReadBigU32(execution + 4);
    info.title_id = ReadBigU32(execution + 12);
    info.disc_number = execution[18];
    info.disc_count = execution[19];
    info.valid = true;
    return info;
  }
  return info;
}

TitleInfo ReadTitleInfo(Xdvdfs& iso) {
  TitleInfo info;
  Xdvdfs::Entry entry;
  if (!iso.Resolve("default.xex", entry) || entry.is_directory()) {
    return info;
  }
  const size_t want = size_t(entry.size < kHeaderReadSize ? entry.size
                                                          : kHeaderReadSize);
  std::vector<uint8_t> header(want);
  if (!iso.Read(entry, 0, header.data(), want)) {
    return info;
  }
  return ParseXex(header.data(), header.size());
}

}  // namespace warden
