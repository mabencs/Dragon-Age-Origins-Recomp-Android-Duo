// Host harness: apply a title update's patch to a base XEX.
//
// Uses XenonRecomp's own XexPatcher, deliberately - it is what XenonRecomp used to
// produce recomp/generated/ppc_tu6, so a patched image produced any other way could
// disagree with the code that was translated from it.
//
// Verified by tools/check_xex.py --build ppc_tu6: the patched image's instruction
// encodings must agree with what the TU7 recompilation quotes.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "xex_patcher.h"

namespace {

bool ReadAll(const char* path, std::vector<uint8_t>& out) {
  std::FILE* file = std::fopen(path, "rb");
  if (!file) {
    std::fprintf(stderr, "error: cannot open %s\n", path);
    return false;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(file);
    std::fprintf(stderr, "error: empty %s\n", path);
    return false;
  }
  out.assign(static_cast<size_t>(size), 0);
  const size_t read = std::fread(out.data(), 1, out.size(), file);
  std::fclose(file);
  return read == out.size();
}

const char* ResultName(XexPatcher::Result result) {
  switch (result) {
    case XexPatcher::Result::Success: return "Success";
    case XexPatcher::Result::FileOpenFailed: return "FileOpenFailed";
    case XexPatcher::Result::FileWriteFailed: return "FileWriteFailed";
    case XexPatcher::Result::XexFileUnsupported: return "XexFileUnsupported";
    case XexPatcher::Result::XexFileInvalid: return "XexFileInvalid";
    case XexPatcher::Result::PatchFileInvalid: return "PatchFileInvalid";
    case XexPatcher::Result::PatchIncompatible: return "PatchIncompatible";
    case XexPatcher::Result::PatchFailed: return "PatchFailed";
    case XexPatcher::Result::PatchUnsupported: return "PatchUnsupported";
    default: return "?";
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: xexpatch_host <base.xex> <patch.xexp> <out.xex>\n");
    return 2;
  }
  std::vector<uint8_t> base;
  std::vector<uint8_t> patch;
  if (!ReadAll(argv[1], base) || !ReadAll(argv[2], patch)) {
    return 1;
  }

  std::vector<uint8_t> out;
  const XexPatcher::Result result =
      XexPatcher::apply(base.data(), base.size(), patch.data(), patch.size(), out,
                        false);
  if (result != XexPatcher::Result::Success) {
    std::fprintf(stderr, "error: patch failed: %s\n", ResultName(result));
    return 1;
  }

  std::FILE* file = std::fopen(argv[3], "wb");
  if (!file) {
    std::fprintf(stderr, "error: cannot write %s\n", argv[3]);
    return 1;
  }
  const size_t written = std::fwrite(out.data(), 1, out.size(), file);
  std::fclose(file);
  if (written != out.size()) {
    std::fprintf(stderr, "error: short write\n");
    return 1;
  }
  std::printf("patched %zu -> %zu bytes\n", base.size(), out.size());
  return 0;
}
