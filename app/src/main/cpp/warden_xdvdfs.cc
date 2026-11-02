#include "warden_xdvdfs.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace warden {

namespace {

constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicLength = sizeof(kMagic) - 1;
// The volume descriptor sits one sector-aligned 64 KiB into the partition.
constexpr uint64_t kDescriptorOffset = 0x10000;

// Known game-partition bases: raw, ISO9660-ish, XGD2, XGD3 and a variant. Same list
// as tools/dao_iso.py, in the same order, so both find the same partition on a disc
// that happens to contain more than one.
constexpr uint64_t kPartitionOffsets[] = {
    0x0, 0x10000, 0xFD90000, 0x18300000, 0x2080000,
};

uint16_t ReadU16(const uint8_t* p) {
  // Explicit little-endian assembly rather than a cast: the host may be either
  // endianness, and the on-disc format is fixed.
  return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8);
}

uint32_t ReadU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

char LowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (LowerAscii(a[i]) != LowerAscii(b[i])) {
      return false;
    }
  }
  return true;
}

bool LessIgnoreCase(const Xdvdfs::Entry& a, const Xdvdfs::Entry& b) {
  const size_t shortest = std::min(a.name.size(), b.name.size());
  for (size_t i = 0; i < shortest; ++i) {
    const char left = LowerAscii(a.name[i]);
    const char right = LowerAscii(b.name[i]);
    if (left != right) {
      return left < right;
    }
  }
  return a.name.size() < b.name.size();
}

}  // namespace

bool Xdvdfs::Open() {
  uint8_t header[kMagicLength + 8];
  for (uint64_t offset : kPartitionOffsets) {
    if (!read_at_(offset + kDescriptorOffset, header, sizeof(header))) {
      // Not an error: a short image simply has no partition at this offset.
      continue;
    }
    if (std::memcmp(header, kMagic, kMagicLength) != 0) {
      continue;
    }
    base_ = offset;
    root_sector_ = ReadU32(header + kMagicLength);
    root_size_ = ReadU32(header + kMagicLength + 4);
    opened_ = true;
    return true;
  }
  error_ = "no XDVDFS volume descriptor found at any known partition offset";
  return false;
}

bool Xdvdfs::ReadDirectory(uint32_t sector, uint32_t size,
                           std::vector<Entry>& out) {
  out.clear();
  if (!size) {
    // An empty directory is legal and has no table at all.
    return true;
  }
  // A directory table is a handful of sectors at most. Anything claiming more than
  // this is a corrupt or hostile image, and reading it would mean trusting a
  // length field with a multi-gigabyte allocation behind it.
  constexpr uint32_t kMaxDirectoryBytes = 4u * 1024 * 1024;
  if (size > kMaxDirectoryBytes) {
    error_ = "directory table implausibly large (" + std::to_string(size) + " bytes)";
    return false;
  }

  std::vector<uint8_t> table(size);
  if (!read_at_(base_ + uint64_t(sector) * kSectorSize, table.data(), size)) {
    error_ = "could not read directory table at sector " + std::to_string(sector);
    return false;
  }

  // Entries form a binary tree; left and right are offsets in 4-byte words. Walked
  // iteratively with a visited set, so a malformed image with a cycle in the tree
  // terminates instead of hanging - which a recursive walk would not.
  std::vector<uint32_t> stack{0};
  std::unordered_set<uint32_t> seen;
  constexpr uint32_t kEntryHeaderSize = 14;
  while (!stack.empty()) {
    const uint32_t position = stack.back();
    stack.pop_back();
    if (position + kEntryHeaderSize > size || !seen.insert(position).second) {
      continue;
    }
    const uint8_t* entry = table.data() + position;
    const uint16_t left = ReadU16(entry);
    const uint16_t right = ReadU16(entry + 2);
    if (left == 0xFFFF && right == 0xFFFF) {
      // The terminator the format uses for an unused slot.
      continue;
    }
    const uint32_t start_sector = ReadU32(entry + 4);
    const uint32_t file_size = ReadU32(entry + 8);
    const uint8_t attributes = entry[12];
    const uint8_t name_length = entry[13];

    if (position + kEntryHeaderSize + name_length <= size && name_length) {
      Entry parsed;
      parsed.name.assign(reinterpret_cast<const char*>(entry + kEntryHeaderSize),
                         name_length);
      parsed.sector = start_sector;
      parsed.size = file_size;
      parsed.attributes = attributes;
      out.push_back(std::move(parsed));
    }
    if (left) {
      stack.push_back(uint32_t(left) * 4);
    }
    if (right) {
      stack.push_back(uint32_t(right) * 4);
    }
  }

  // Sorted case-insensitively, matching the Python reference, so that both produce
  // the same listing and the differential check compares like with like.
  std::sort(out.begin(), out.end(), LessIgnoreCase);
  return true;
}

bool Xdvdfs::Resolve(const std::string& path, Entry& out) {
  if (!opened_) {
    error_ = "Resolve before a successful Open";
    return false;
  }

  Entry current;
  current.sector = root_sector_;
  current.size = root_size_;
  current.attributes = kAttributeDirectory;

  // Split on either separator, skipping empty components, so that "a//b", "a\\b" and
  // "/a/b" all behave.
  std::vector<std::string> parts;
  std::string component;
  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!component.empty()) {
        parts.push_back(component);
        component.clear();
      }
    } else {
      component.push_back(c);
    }
  }
  if (!component.empty()) {
    parts.push_back(component);
  }

  std::vector<Entry> entries;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!current.is_directory()) {
      // A path component after a file: "default.xex/something".
      return false;
    }
    if (!ReadDirectory(current.sector, current.size, entries)) {
      return false;
    }
    bool found = false;
    for (const Entry& entry : entries) {
      if (EqualsIgnoreCase(entry.name, parts[i])) {
        current = entry;
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  out = current;
  return true;
}

bool Xdvdfs::List(const std::string& path, std::vector<Entry>& out) {
  Entry entry;
  if (path.empty()) {
    entry.sector = root_sector_;
    entry.size = root_size_;
    entry.attributes = kAttributeDirectory;
    if (!opened_) {
      error_ = "List before a successful Open";
      return false;
    }
  } else if (!Resolve(path, entry)) {
    return false;
  }
  if (!entry.is_directory()) {
    return false;
  }
  return ReadDirectory(entry.sector, entry.size, out);
}

}  // namespace warden
