// Host harness for the XEX loader. Built and run by tools/check_xex.py.
//
// Loads default.xex with the same warden_xex.cc that ships in the APK and writes the
// assembled image out, so the check can compare it against the recompiled source -
// which is the only reference that matters, since the recompiled code was written
// against this exact image.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "warden_xex.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: xex_host <default.xex> <image-out.bin>\n");
    return 2;
  }
  std::vector<uint8_t> image;
  const warden::XexImage info = warden::LoadXexFile(argv[1], image);
  if (!info.loaded) {
    std::fprintf(stderr, "error: %s\n", info.error.c_str());
    return 1;
  }

  // Machine-readable, so check_xex.py does not have to parse prose.
  std::printf("base %08X\n", info.base);
  std::printf("size %u\n", info.size);
  std::printf("entry %08X\n", info.entry_point);
  std::printf("stack %u\n", info.stack_size);
  std::printf("encryption %u\n", info.encryption_type);
  std::printf("compression %u\n", info.compression_type);
  std::printf("blocks %u\n", info.block_count);
  std::printf("data %u\n", info.data_bytes);
  std::printf("zero %u\n", info.zero_bytes);

  std::FILE* out = std::fopen(argv[2], "wb");
  if (!out) {
    std::fprintf(stderr, "error: cannot write %s\n", argv[2]);
    return 1;
  }
  const size_t written = std::fwrite(image.data(), 1, image.size(), out);
  std::fclose(out);
  if (written != image.size()) {
    std::fprintf(stderr, "error: short write\n");
    return 1;
  }
  return 0;
}
