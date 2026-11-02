#include "warden_stfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <utility>

namespace warden {

namespace {

// Data blocks covered by one hash table at each level. The blocks under one level-0
// table sit next to each other in the file, which is what makes contiguous reads of up
// to 170 blocks possible.
constexpr uint32_t kLevel0 = 170;
constexpr uint32_t kLevel1 = 170 * 170;
constexpr uint32_t kLevel2 = 170 * 170 * 170;

// A file table of 1024 blocks is 65,536 entries - far past anything real, and a bound
// is needed because the count comes out of the package.
constexpr uint32_t kMaxTableBlocks = 1024;
constexpr uint32_t kMaxDepth = 32;
constexpr uint32_t kEntrySize = 0x40;

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}
uint16_t Be16(const uint8_t* p) {
  return uint16_t((uint32_t(p[0]) << 8) | uint32_t(p[1]));
}
// The 24-bit block numbers are little-endian, unlike everything else in the header.
uint32_t Le24(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}

// Names the destination filesystem cannot hold, or that would escape the folder.
// Checked even though this writes on Android, because a package is shared between
// platforms and a name that is legal here would still be a trap on a desktop - and
// because '/' and '..' must be rejected everywhere.
bool NameIsSafe(const std::string& name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 32 || std::strchr("<>:\"/\\|?*", c)) {
      return false;
    }
  }
  if (name.back() == '.' || name.back() == ' ') {
    return false;
  }
  static const char* kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM", "LPT"};
  std::string stem = name.substr(0, name.find('.'));
  for (char& c : stem) {
    c = char(std::toupper(static_cast<unsigned char>(c)));
  }
  for (const char* reserved : kReserved) {
    const size_t length = std::strlen(reserved);
    if (stem.compare(0, length, reserved) != 0) {
      continue;
    }
    // CON / PRN / AUX / NUL exactly; COM and LPT with one trailing digit.
    if (stem.size() == length && length == 3 &&
        std::strcmp(reserved, "COM") != 0 && std::strcmp(reserved, "LPT") != 0) {
      return false;
    }
    if (stem.size() == length + 1 && stem[length] >= '0' && stem[length] <= '9') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool Stfs::Fail(const std::string& message) {
  error_ = message;
  return false;
}

uint64_t Stfs::BlockOffset(uint32_t index) const {
  // Each level's hash tables sit in front of the blocks they cover, so the file
  // position of a data block is its index plus one table per level it has passed.
  uint64_t block = index;
  const uint32_t levels[3] = {kLevel0, kLevel1, kLevel2};
  for (uint32_t level : levels) {
    block += (uint64_t(index) + level) / level;
    if (index < level) {
      break;
    }
  }
  return base_ + (block << 12);
}

bool Stfs::ReadBlocks(uint32_t index, uint32_t count, void* out, size_t length) {
  // A run must stay inside one level-0 table, because only then are the blocks
  // adjacent in the file.
  if (count == 0 || index + count > total_blocks_ ||
      index / kLevel0 != (index + count - 1) / kLevel0) {
    return Fail("damaged (block out of range)");
  }
  const uint64_t offset = BlockOffset(index);
  if (offset + length > size_) {
    return Fail("damaged (truncated)");
  }
  if (!read_at_(offset, out, length)) {
    return Fail("read failed");
  }
  return true;
}

bool Stfs::NextBlock(uint32_t index, uint32_t& next) {
  uint32_t table;
  if (index < kLevel0) {
    table = 0;
  } else {
    table = (index / kLevel0) * (kLevel0 + 1) + index / kLevel1 + 1;
    if (index >= kLevel1) {
      ++table;
    }
  }
  const uint64_t at = base_ + (uint64_t(table) << 12) + uint64_t(index % kLevel0) * 0x18;
  if (at + 0x18 > size_) {
    return Fail("damaged (truncated hash table)");
  }
  uint8_t entry[4];
  if (!read_at_(at + 0x14, entry, sizeof(entry))) {
    return Fail("read failed");
  }
  next = Be32(entry) & 0xFFFFFF;
  return true;
}

bool Stfs::ReadHeader() {
  std::vector<uint8_t> header(0x1800);
  if (size_ < header.size() || !read_at_(0, header.data(), header.size())) {
    return Fail("not an Xbox 360 package (too short)");
  }
  std::memcpy(magic_, header.data(), 4);
  if (std::memcmp(magic_, "CON ", 4) != 0 && std::memcmp(magic_, "LIVE", 4) != 0 &&
      std::memcmp(magic_, "PIRS", 4) != 0) {
    return Fail("not an Xbox 360 package");
  }

  header_size_ = Be32(header.data() + 0x340);
  content_type_ = Be32(header.data() + 0x344);
  media_id_ = Be32(header.data() + 0x354);
  title_id_ = Be32(header.data() + 0x360);
  static constexpr char kHex[] = "0123456789ABCDEF";
  content_id_.clear();
  for (size_t i = 0; i < 20; ++i) {
    content_id_ += kHex[header[0x32C + i] >> 4];
    content_id_ += kHex[header[0x32C + i] & 0xF];
  }
  display_name_.clear();
  for (size_t i = 0; i < 0x40; ++i) {
    const char16_t c = char16_t((header[0x411 + 2 * i] << 8) | header[0x412 + 2 * i]);
    if (!c) {
      break;
    }
    display_name_ += c;
  }

  const uint8_t* vd = header.data() + 0x379;
  const bool read_only = (vd[2] & 1) != 0;
  table_blocks_ = uint32_t(vd[3]) | (uint32_t(vd[4]) << 8);
  table_block_ = Le24(vd + 5);
  total_blocks_ = Be32(vd + 0x1C);

  const uint32_t volume_type = Be32(header.data() + 0x3A9);
  if (volume_type != 0) {
    return Fail("not an STFS package (an SVOD one, as games on demand are)");
  }
  if (!read_only) {
    return Fail("a package in the writable format (as saves are), not downloadable content");
  }

  base_ = (uint64_t(header_size_) + kBlockSize - 1) / kBlockSize * kBlockSize;
  if (header_size_ < 0x1000 || header_size_ > 0x10000 || base_ > size_) {
    return Fail("damaged (bad header size)");
  }
  if (table_blocks_ == 0 || table_blocks_ > kMaxTableBlocks ||
      total_blocks_ >= kLevel2) {
    return Fail("damaged (bad volume descriptor)");
  }
  return true;
}

bool Stfs::ReadTable() {
  // parent index -> (folder path, depth). 0xFFFF is the root.
  struct Folder {
    std::string path;
    uint32_t depth;
  };
  std::vector<std::pair<uint32_t, Folder>> folders;
  folders.push_back({0xFFFF, Folder{"", 0}});
  auto find_folder = [&](uint32_t key, Folder& out) {
    for (const auto& entry : folders) {
      if (entry.first == key) {
        out = entry.second;
        return true;
      }
    }
    return false;
  };

  std::set<std::string> seen;
  uint64_t total = 0;
  const uint64_t capacity =
      std::min<uint64_t>(uint64_t(total_blocks_) << 12, size_ - base_);

  uint32_t index = 0;
  uint32_t block = table_block_;
  std::vector<uint8_t> data(kBlockSize);
  for (uint32_t i = 0; i < table_blocks_; ++i) {
    if (!ReadBlocks(block, 1, data.data(), data.size())) {
      return false;
    }
    for (uint32_t j = 0; j < kBlockSize / kEntrySize; ++j) {
      const uint8_t* e = data.data() + j * kEntrySize;
      if (e[0] == 0) {
        break;
      }
      const uint8_t flags = e[40];
      const size_t name_length = std::min<size_t>(flags & 0x3F, 40);
      std::string name(reinterpret_cast<const char*>(e), name_length);
      const uint32_t start = Le24(e + 47);
      const uint16_t parent = Be16(e + 50);
      const uint32_t length = Be32(e + 52);

      if (!NameIsSafe(name)) {
        return Fail("contains an unusable file name");
      }
      Folder folder;
      if (!find_folder(parent, folder)) {
        return Fail("damaged (entry before its folder)");
      }
      if (folder.depth >= kMaxDepth) {
        return Fail("damaged (folders too deep)");
      }
      const std::string path = folder.path + name;
      std::string lowered = path;
      for (char& c : lowered) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
      }
      if (!seen.insert(lowered).second) {
        return Fail("damaged (a file listed twice)");
      }

      if (flags & 0x80) {
        folders.push_back({index, Folder{path + "/", folder.depth + 1}});
      } else {
        total += length;
        // A chain that loops on one block would otherwise be read for ever; holding
        // the sizes together to what the package can hold stops that.
        if (total > capacity) {
          return Fail("damaged (bad file size)");
        }
        File file;
        file.path = path;
        file.size = length;
        file.start_block = start;
        file.contiguous = (flags & 0x40) != 0;
        files_.push_back(std::move(file));
      }
      ++index;
    }
    if (!NextBlock(block, block)) {
      return false;
    }
  }
  return true;
}

bool Stfs::Open() {
  files_.clear();
  error_.clear();
  return ReadHeader() && ReadTable();
}

bool Stfs::ReadFile(const File& file, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(size_t(file.size));
  uint32_t block = file.start_block;
  uint64_t left = file.size;
  std::vector<uint8_t> buffer;
  while (left > 0) {
    // Contiguous files can be read a whole level-0 run at a time; fragmented ones
    // have to follow the hash table one block at a time.
    uint32_t count = 1;
    if (file.contiguous) {
      const uint32_t blocks_left = uint32_t((left + kBlockSize - 1) / kBlockSize);
      count = std::min(blocks_left, kLevel0 - block % kLevel0);
    }
    const size_t want = size_t(std::min<uint64_t>(uint64_t(count) * kBlockSize, left));
    buffer.resize(want);
    if (!ReadBlocks(block, count, buffer.data(), want)) {
      return false;
    }
    out.insert(out.end(), buffer.begin(), buffer.end());
    left -= want;
    if (left > 0) {
      if (file.contiguous) {
        block += count;
      } else if (!NextBlock(block, block)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace warden
