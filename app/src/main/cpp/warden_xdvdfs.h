// Read-only XDVDFS - the Xbox 360 disc filesystem. Task 4.4.
//
// A port of tools/dao_iso.py's Xdvdfs class, which is the reference implementation
// and is checked against this one by tools/check_xdvdfs.py. If the two disagree, this
// is wrong: the Python has read the real disc correctly since Phase 0.
//
// Reads go through a ReadAt callable rather than owning a file handle. Three reasons:
// on Android the image arrives as a file descriptor from the Storage Access Framework
// and never as a path; pread has no shared seek position, so reads are thread-safe
// without locking; and a host build can supply a FILE*-backed reader, which is what
// makes the differential check against the Python possible at all.
#ifndef WARDEN_XDVDFS_H_
#define WARDEN_XDVDFS_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace warden {

class Xdvdfs {
 public:
  static constexpr uint32_t kSectorSize = 2048;
  static constexpr uint8_t kAttributeDirectory = 0x10;

  // Reads exactly `size` bytes at `offset`, or returns false. A short read is a
  // failure, not a partial success: every caller here needs the whole structure it
  // asked for.
  using ReadAt = std::function<bool(uint64_t offset, void* out, size_t size)>;

  struct Entry {
    std::string name;
    uint32_t sector = 0;
    uint32_t size = 0;
    uint8_t attributes = 0;

    bool is_directory() const { return (attributes & kAttributeDirectory) != 0; }
  };

  explicit Xdvdfs(ReadAt read_at) : read_at_(std::move(read_at)) {}

  // Finds the volume descriptor. Must succeed before anything else is called.
  bool Open();

  const std::string& error() const { return error_; }
  uint64_t base_offset() const { return base_; }

  // Lists one directory. An empty path lists the root. Returns false if the path does
  // not exist or is not a directory.
  bool List(const std::string& path, std::vector<Entry>& out);
  // Resolves a '/'-separated path. Case-insensitive, as the filesystem is.
  bool Resolve(const std::string& path, Entry& out);

  // Byte offset of a file's contents in the image.
  uint64_t FileOffset(const Entry& entry) const {
    return base_ + uint64_t(entry.sector) * kSectorSize;
  }

  bool Read(const Entry& entry, uint64_t offset, void* out, size_t size) const {
    return read_at_(FileOffset(entry) + offset, out, size);
  }

 private:
  bool ReadDirectory(uint32_t sector, uint32_t size, std::vector<Entry>& out);

  ReadAt read_at_;
  uint64_t base_ = 0;
  uint32_t root_sector_ = 0;
  uint32_t root_size_ = 0;
  bool opened_ = false;
  std::string error_;
};

}  // namespace warden

#endif  // WARDEN_XDVDFS_H_
