// Xbox 360 STFS packages - LIVE/PIRS, the read-only format title updates arrive in.
// Task 5.0f.
//
// Ported from DAORecomp's scripts/stfs.py, which is the reference both of their
// implementations claim to track and is the one that produced the known-good
// extraction this is checked against. Their C++ reader exists too but is built on
// Win32 HANDLEs and the launcher's own file and text helpers, so porting the Python is
// less work and less entanglement than adapting it.
//
// The package is the player's file and is treated as untrusted: every block index,
// name, size and parent reference is checked before use. A malformed package should
// produce an error, never a read outside the file or a path outside the destination.
//
// Reads go through a callable, as the XDVDFS reader does, so the same code serves the
// Android build and a host harness that diffs it against the Python.
#ifndef WARDEN_STFS_H_
#define WARDEN_STFS_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace warden {

class Stfs {
 public:
  static constexpr uint32_t kBlockSize = 0x1000;

  using ReadAt = std::function<bool(uint64_t offset, void* out, size_t size)>;

  struct File {
    std::string path;  // '/'-separated
    uint64_t size = 0;
    uint32_t start_block = 0;
    bool contiguous = false;
  };

  Stfs(ReadAt read_at, uint64_t file_size)
      : read_at_(std::move(read_at)), size_(file_size) {}

  // Reads the header and the file table.
  bool Open();

  const std::string& error() const { return error_; }
  const std::vector<File>& files() const { return files_; }
  uint32_t title_id() const { return title_id_; }
  uint32_t media_id() const { return media_id_; }
  uint32_t content_type() const { return content_type_; }
  // PLAN 10.10: the header's SHA-1 in hex - the console's own name for the package - and
  // its display name (the first, English one), as DAORecomp's reader takes them.
  const std::string& content_id() const { return content_id_; }
  const std::u16string& display_name() const { return display_name_; }
  const char* magic() const { return magic_; }

  // Appends a file's contents to `out`.
  bool ReadFile(const File& file, std::vector<uint8_t>& out);

 private:
  bool ReadHeader();
  bool ReadTable();
  uint64_t BlockOffset(uint32_t index) const;
  bool ReadBlocks(uint32_t index, uint32_t count, void* out, size_t length);
  bool NextBlock(uint32_t index, uint32_t& next);
  bool Fail(const std::string& message);

  ReadAt read_at_;
  uint64_t size_ = 0;
  uint64_t base_ = 0;
  char magic_[5] = {};
  uint32_t header_size_ = 0;
  uint32_t content_type_ = 0;
  uint32_t media_id_ = 0;
  uint32_t title_id_ = 0;
  std::string content_id_;
  std::u16string display_name_;
  uint32_t table_blocks_ = 0;
  uint32_t table_block_ = 0;
  uint32_t total_blocks_ = 0;
  std::vector<File> files_;
  std::string error_;
};

}  // namespace warden

#endif  // WARDEN_STFS_H_
