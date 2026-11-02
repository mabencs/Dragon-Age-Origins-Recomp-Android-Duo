// Host harness for the STFS reader. Built and run by tools/check_stfs.py, which diffs
// its output against DAORecomp's scripts/stfs.py - the reference both of their
// implementations track, and the one that produced the known-good extraction.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "warden_stfs.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: stfs_host <package> [extract-dir]\n");
    return 2;
  }
  std::FILE* file = std::fopen(argv[1], "rb");
  if (!file) {
    std::fprintf(stderr, "error: cannot open %s\n", argv[1]);
    return 1;
  }
#if defined(_WIN32)
  _fseeki64(file, 0, SEEK_END);
  const uint64_t size = uint64_t(_ftelli64(file));
#else
  fseeko(file, 0, SEEK_END);
  const uint64_t size = uint64_t(ftello(file));
#endif

  warden::Stfs package(
      [file](uint64_t offset, void* out, size_t length) {
#if defined(_WIN32)
        if (_fseeki64(file, int64_t(offset), SEEK_SET) != 0) return false;
#else
        if (fseeko(file, off_t(offset), SEEK_SET) != 0) return false;
#endif
        return std::fread(out, 1, length, file) == length;
      },
      size);

  if (!package.Open()) {
    std::fprintf(stderr, "error: %s\n", package.error().c_str());
    std::fclose(file);
    return 1;
  }
  std::printf("MAGIC\t%s\n", package.magic());
  std::printf("TITLE\t%08X\n", package.title_id());
  std::printf("MEDIA\t%08X\n", package.media_id());
  std::printf("TYPE\t%08X\n", package.content_type());
  for (const warden::Stfs::File& entry : package.files()) {
    std::printf("FILE\t%llu\t%s\n", (unsigned long long)entry.size,
                entry.path.c_str());
  }

  // With a destination, also write the contents out so the check can compare bytes.
  if (argc >= 3) {
    for (const warden::Stfs::File& entry : package.files()) {
      std::vector<uint8_t> data;
      if (!package.ReadFile(entry, data)) {
        std::fprintf(stderr, "error reading %s: %s\n", entry.path.c_str(),
                     package.error().c_str());
        std::fclose(file);
        return 1;
      }
      // Flat names: the check compares contents by path, and creating directories
      // portably here is beside the point.
      std::string out_path = std::string(argv[2]) + "/";
      for (char c : entry.path) {
        out_path.push_back(c == '/' ? '_' : c);
      }
      std::FILE* out = std::fopen(out_path.c_str(), "wb");
      if (!out) {
        std::fprintf(stderr, "error: cannot write %s\n", out_path.c_str());
        std::fclose(file);
        return 1;
      }
      std::fwrite(data.data(), 1, data.size(), out);
      std::fclose(out);
    }
  }
  std::fclose(file);
  return 0;
}
