// Host harness for the native XDVDFS reader. Built and run by
// tools/check_xdvdfs.py, which diffs its output against tools/dao_iso.py.
//
// The reader that ships in the APK cannot be tested against the real disc on the
// device without first putting a 7 GiB image there, and even then a disagreement with
// the Python would be hard to see. So it is tested here instead, against the actual
// ISO, on the machine that already holds it - the same differential approach Phase 2
// used for the recompiled code.
//
// This file is deliberately the only host-specific part: warden_xdvdfs.cc reads
// through a callable, so the Android build hands it a file descriptor from the Storage
// Access Framework and this hands it a FILE*.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "warden_title.h"
#include "warden_xdvdfs.h"

namespace {

void PrintTree(warden::Xdvdfs& iso, const std::string& path) {
  std::vector<warden::Xdvdfs::Entry> entries;
  if (!iso.List(path, entries)) {
    std::fprintf(stderr, "error: cannot list '%s': %s\n", path.c_str(),
                 iso.error().c_str());
    return;
  }
  for (const warden::Xdvdfs::Entry& entry : entries) {
    const std::string child = path.empty() ? entry.name : path + "/" + entry.name;
    if (entry.is_directory()) {
      std::printf("DIR\t%s\n", child.c_str());
      PrintTree(iso, child);
    } else {
      std::printf("%u\t%s\n", entry.size, child.c_str());
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: xdvdfs_host <image.iso>\n");
    return 2;
  }
  std::FILE* file = std::fopen(argv[1], "rb");
  if (!file) {
    std::fprintf(stderr, "error: cannot open %s\n", argv[1]);
    return 1;
  }

  warden::Xdvdfs iso([file](uint64_t offset, void* out, size_t size) {
    // _fseeki64 on MSVC, fseeko elsewhere: a 7 GiB image is well past what a long
    // offset holds on Windows, and a silently truncated seek would read the wrong
    // sector rather than fail.
#if defined(_WIN32)
    if (_fseeki64(file, int64_t(offset), SEEK_SET) != 0) {
      return false;
    }
#else
    if (fseeko(file, off_t(offset), SEEK_SET) != 0) {
      return false;
    }
#endif
    return std::fread(out, 1, size, file) == size;
  });

  if (!iso.Open()) {
    std::fprintf(stderr, "error: %s\n", iso.error().c_str());
    std::fclose(file);
    return 1;
  }
  std::printf("BASE\t0x%llX\n", (unsigned long long)iso.base_offset());
  // Checked against dao_iso.py's parse_xex by the same differential run, so the
  // identification the app relies on to pick the right disc is verified too.
  const warden::TitleInfo title = warden::ReadTitleInfo(iso);
  if (title.valid) {
    std::printf("TITLE\t%08X\t%08X\t%08X\t%u\t%u\n", title.title_id,
                title.media_id, title.version, title.disc_number, title.disc_count);
  } else {
    std::printf("TITLE\tnone\n");
  }
  PrintTree(iso, "");
  std::fclose(file);
  return 0;
}
