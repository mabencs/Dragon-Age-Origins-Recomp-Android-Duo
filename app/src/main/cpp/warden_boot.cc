// Loading the title image into guest memory, and checking it is the right one.
// Task 5.0e.
//
// The check matters more than it might look. A wrong AES key, a mis-walked header
// offset or a botched block list does not produce an error - it produces *noise*, and
// noise is indistinguishable from code until something tries to execute it. One such
// mistake happened while writing this (a security-info field offset four bytes early),
// and nothing about the result looked wrong.
//
// So the image is checked against the recompiled build that is linked beside it, using
// data the app already has. XenonRecomp's function mapping table gives the guest
// address of every translated function; on PowerPC a function that saves its return
// address starts with `mflr r12`, encoded 0x7D8802A6, and for this title that is about
// seven in ten of them. Reading the first word at every entry point and counting how
// many are that value is therefore a single number that separates a correct image from
// a wrong one, with no table to bake in and no disassembler to write.
//
// tools/check_xex.py does the same comparison exhaustively on the host, against every
// instruction the recompiled source quotes - 839 groups, all agreeing.

#include "warden_boot.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <thread>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "warden_guest_info.h"
#include "warden_kernel_variables.h"
#include "warden_stfs.h"
#include "warden_xex.h"
#include "xex_patcher.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

namespace warden {

namespace {

// `mflr r12`. Verified on the host: every one of 36,403 sites quoting that instruction
// holds this word (tools/check_xex.py).
constexpr uint32_t kMflrR12 = 0x7D8802A6;

// Below this share of entry points starting with `mflr r12`, the image does not belong
// to the linked build. The measured figure for a correct pairing is about 0.71, and a
// wrong image scores near zero - so the threshold is nowhere near either.
constexpr double kMinimumMatchRatio = 0.5;

uint32_t ReadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

// The title updates the game's code is made from (PLAN 12.3): Title Update 7's executable
// patch, or 6's - the Ultimate Edition's disc 2 carries 6. Title Update 2 is also in
// circulation and is not usable - the title passes over anything that is not TU6's or
// TU7's - so the patch is identified rather than taken on trust: by its size here, by its
// SHA-1 where the game's code is made (GameCode.java, tools/recompile.py).
constexpr uint64_t kTu7XexpSize = 2758656;
constexpr uint64_t kTu6XexpSize = 2684928;

int TitleUpdateOfPatch(uint64_t size) {
  return size == kTu7XexpSize ? 7 : size == kTu6XexpSize ? 6 : 0;
}

// The title update the loaded game's code was translated from, by its image's size
// (TU6's image is 0x13F0000 bytes, TU7's 0x1400000): the image must be patched by the
// same one. 0 when the game's code is not loaded - then 7 is preferred.
int TitleUpdateOfGameCode() {
  if (!warden::game::Loaded()) {
    return 0;
  }
  return warden::game::ImageSize() == 0x13F0000 ? 6 : 7;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    return false;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(file);
    return false;
  }
  out.assign(static_cast<size_t>(size), 0);
  const size_t read = std::fread(out.data(), 1, out.size(), file);
  std::fclose(file);
  return read == out.size();
}

// Looks for default.xexp: either loose in one of the directories, or inside a
// LIVE/PIRS package there. A package is the form the title update actually arrives
// in, so reading it directly means the user drops the file in and nothing else.
//
// `package_path` comes back set when the patch came from a package, so the caller can
// reopen it for the data files. Reopening rather than keeping it open: the package is
// read wholly into memory to be parsed, and holding 3.5 MB for the life of the process
// to save one reread is the wrong trade.
bool FindTitleUpdate(const std::vector<std::string>& search_dirs,
                     std::vector<uint8_t>& xexp, std::string& source,
                     std::string& package_path) {
  // Every candidate looked at, the one the game's code was made from taken (PLAN 12.3:
  // disc 2's Title Update 6 and a chosen 7 can both be there); with no game code
  // loaded, the newest.
  const int wanted = TitleUpdateOfGameCode();
  int found = 0;
  auto consider = [&](int tu, std::vector<uint8_t>& patch, const std::string& from,
                      const std::string& package) {
    if (tu == 0 || (wanted && tu != wanted) || tu <= found) {
      return;
    }
    found = tu;
    xexp.swap(patch);
    source = from;
    package_path = package;
  };
  for (const std::string& directory : search_dirs) {
    if (directory.empty()) {
      continue;
    }
    // Loose first: cheaper, and it is what a host-side extraction leaves.
    for (const char* name : {"/title_update/default.xexp", "/default.xexp"}) {
      const std::string candidate = directory + name;
      std::vector<uint8_t> loose;
      if (ReadWholeFile(candidate, loose)) {
        consider(TitleUpdateOfPatch(loose.size()), loose, candidate, "");
      }
    }

    DIR* dir = opendir(directory.c_str());
    if (!dir) {
      continue;
    }
    while (const dirent* entry = readdir(dir)) {
      if (entry->d_name[0] == '.') {
        continue;
      }
      const std::string candidate = directory + "/" + entry->d_name;
      std::vector<uint8_t> contents;
      if (!ReadWholeFile(candidate, contents) || contents.size() < 0x1800) {
        continue;
      }
      if (std::memcmp(contents.data(), "LIVE", 4) != 0 &&
          std::memcmp(contents.data(), "PIRS", 4) != 0 &&
          std::memcmp(contents.data(), "CON ", 4) != 0) {
        continue;
      }
      Stfs package(
          [&contents](uint64_t offset, void* out, size_t size) {
            if (offset + size > contents.size()) {
              return false;
            }
            std::memcpy(out, contents.data() + offset, size);
            return true;
          },
          contents.size());
      if (!package.Open()) {
        XELOGW("{} is not a readable package: {}", candidate, package.error());
        continue;
      }
      for (const Stfs::File& file : package.files()) {
        const int tu = TitleUpdateOfPatch(file.size);
        if (file.path != "default.xexp" || tu == 0 || (wanted && tu != wanted) ||
            tu <= found) {
          continue;
        }
        std::vector<uint8_t> patch;
        if (package.ReadFile(file, patch)) {
          consider(tu, patch, candidate + " (STFS)", candidate);
        }
      }
    }
    closedir(dir);
  }
  if (found) {
    XELOGI("title update: Title Update {} from {}{}", found, source,
           wanted ? " (the one the game's code was made from)" : "");
  } else if (wanted) {
    XELOGE("title update: none of Title Update {} - the one the game's code was made "
           "from", wanted);
  }
  return found != 0;
}

}  // namespace

// PLAN 12.5: the title update package's executable patch, for the making of the game's
// code on the device (GameCode.java checks its SHA-1).
bool ExtractTitleUpdatePatch(const std::string& package_path, const std::string& out_path) {
  std::vector<uint8_t> contents;
  if (!ReadWholeFile(package_path, contents) || contents.size() < 0x1800) {
    XELOGE("title update patch: cannot read {}", package_path);
    return false;
  }
  Stfs package(
      [&contents](uint64_t offset, void* out, size_t size) {
        if (offset + size > contents.size()) {
          return false;
        }
        std::memcpy(out, contents.data() + offset, size);
        return true;
      },
      contents.size());
  if (!package.Open()) {
    XELOGE("title update patch: {} is not a readable package: {}", package_path,
           package.error());
    return false;
  }
  for (const Stfs::File& file : package.files()) {
    if (file.path != "default.xexp") {
      continue;
    }
    std::vector<uint8_t> patch;
    if (!package.ReadFile(file, patch)) {
      break;
    }
    std::FILE* out = std::fopen(out_path.c_str(), "wb");
    if (!out) {
      return false;
    }
    const bool written = std::fwrite(patch.data(), 1, patch.size(), out) == patch.size();
    return std::fclose(out) == 0 && written;
  }
  XELOGE("title update patch: no default.xexp in {}", package_path);
  return false;
}

namespace {

bool MakeDirectories(const std::string& path) {
  std::string partial;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (!partial.empty() && mkdir(partial.c_str(), 0775) != 0 && errno != EEXIST) {
        return false;
      }
    }
    if (i < path.size()) {
      partial.push_back(path[i]);
    }
  }
  return true;
}

// The title update carries 25 data files under packages/core/patch/ as well as the
// executable patch - GDA tables, .tlk strings, a few plot and item files. They take
// precedence over the base ERFs in the engine's own lookup order, so the game needs
// them next to the disc extraction, not only inside the package.
//
// Written straight from the package rather than as a separate step, because the
// package is already open and verified at this point. Idempotent: a file already
// there at the right size was written by a finished run and is skipped.
void WriteTitleUpdateOverlay(Stfs& package, const std::string& game_data_dir,
                             BootImage& result) {
  std::vector<uint8_t> contents;
  for (const Stfs::File& file : package.files()) {
    // The executable patch is consumed in memory; it is not game data.
    if (file.path == "default.xexp") {
      continue;
    }
    const std::string target = game_data_dir + "/" + file.path;
    struct stat info;
    if (stat(target.c_str(), &info) == 0 && uint64_t(info.st_size) == file.size) {
      ++result.overlay_present;
      continue;
    }
    const size_t slash = target.rfind('/');
    if (slash != std::string::npos && !MakeDirectories(target.substr(0, slash))) {
      XELOGE("cannot create a directory for {}", file.path);
      continue;
    }
    if (!package.ReadFile(file, contents)) {
      XELOGE("cannot read {} from the package: {}", file.path, package.error());
      continue;
    }
    // Written under a temporary name and renamed, so an interrupted write cannot
    // leave a short file that the size check above would then accept.
    const std::string temporary = target + ".part";
    std::FILE* out = std::fopen(temporary.c_str(), "wb");
    if (!out) {
      XELOGE("cannot write {}", temporary);
      continue;
    }
    const size_t written = std::fwrite(contents.data(), 1, contents.size(), out);
    const bool closed = std::fclose(out) == 0;
    if (written != contents.size() || !closed ||
        ::rename(temporary.c_str(), target.c_str()) != 0) {
      XELOGE("failed writing {}", target);
      ::remove(temporary.c_str());
      continue;
    }
    ++result.overlay_written;
  }
  if (result.overlay_written) {
    XELOGI("title update overlay: wrote {} files, {} already present",
           result.overlay_written, result.overlay_present);
  }
}

}  // namespace

BootImage LoadTitleImage(const std::string& game_data_dir,
                         const std::vector<std::string>& search_dirs,
                         xe::Memory& memory) {
  BootImage result;
  const std::string path = game_data_dir + "/default.xex";

  std::vector<uint8_t> base;
  if (!ReadWholeFile(path, base)) {
    result.detail = "cannot read " + path;
    XELOGE("{}", result.detail);
    return result;
  }

  // Apply the title update before loading, if one is there. The recompilation the app
  // links was made from the patched executable, so without this the image is the wrong
  // generation and the check below says so.
  std::vector<uint8_t> xexp;
  std::vector<uint8_t> patched;
  std::string package_path;
  const uint8_t* file = base.data();
  size_t file_size = base.size();
  if (FindTitleUpdate(search_dirs, xexp, result.patch_source, package_path)) {
    const XexPatcher::Result patch_result = XexPatcher::apply(
        base.data(), base.size(), xexp.data(), xexp.size(), patched, false);
    if (patch_result == XexPatcher::Result::Success) {
      file = patched.data();
      file_size = patched.size();
      result.patched = true;
      XELOGI("applied the title update from {}: {} -> {} bytes",
             result.patch_source, base.size(), patched.size());

      // The same package also holds the data files the engine overlays on the base
      // content. Written now, while it is known good.
      if (!package_path.empty()) {
        std::vector<uint8_t> raw;
        if (ReadWholeFile(package_path, raw)) {
          Stfs package(
              [&raw](uint64_t offset, void* out, size_t size) {
                if (offset + size > raw.size()) {
                  return false;
                }
                std::memcpy(out, raw.data() + offset, size);
                return true;
              },
              raw.size());
          if (package.Open()) {
            WriteTitleUpdateOverlay(package, game_data_dir, result);
          }
        }
      }
    } else {
      // Say which failure. "Patch failed" alone cannot distinguish a wrong update
      // from a damaged one.
      XELOGE("title update from {} did not apply (result {})", result.patch_source,
             int(patch_result));
      result.patch_source += " - FAILED";
    }
  }

  std::vector<uint8_t> image;
  const XexImage info = LoadXex(file, file_size, image);
  result.detail = info.Describe();
  if (!info.loaded) {
    XELOGE("could not load {}: {}", path, info.error);
    return result;
  }
  result.base = info.base;
  result.size = info.size;
  result.entry_point = info.entry_point;
  result.stack_size = info.stack_size;
  result.tls_slot_count = info.tls_slot_count;
  result.tls_raw_data_address = info.tls_raw_data_address;
  result.tls_data_size = info.tls_data_size;
  result.tls_raw_data_size = info.tls_raw_data_size;
  result.imports = info.imports;

  uint8_t* guest = memory.TranslateVirtual(info.base);
  if (!guest) {
    result.detail = "no guest memory to load into";
    return result;
  }
  std::memcpy(guest, image.data(), image.size());
  result.loaded = true;
  XELOGI("title image loaded: {}", result.detail);
  // The console's loader binds the import records; the recompiler bound the function
  // thunks, so what is left is the variables - slots the title reads as the kernel
  // variable's address (BUG-038's null call was KeDebugMonitorData's slot holding its
  // raw ordinal).
  PatchImportVariables(memory, result.imports);

  // files/dump-image: the guest image as loaded, and again 40 s later, written to
  // files/image-at-load.bin and files/image-at-40s.bin for comparison with the host's
  // tools/check_xex.py image (BUG-046: part of the title's read-only data was found
  // shifted on the device).
  if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
    struct stat marker;
    const std::string files = dir;
    if (stat((files + "/dump-image").c_str(), &marker) == 0) {
      auto dump = [guest, size = info.size](const std::string& path) {
        if (FILE* out = fopen(path.c_str(), "wb")) {
          const size_t written = fwrite(guest, 1, size, out);
          fclose(out);
          XELOGI("dump-image: {} bytes of the guest image to {}", written, path);
        }
      };
      dump(files + "/image-at-load.bin");
      std::thread([dump, files]() {
        std::this_thread::sleep_for(std::chrono::seconds(40));
        dump(files + "/image-at-40s.bin");
      }).detach();
    }
  }

  // Now decide whether it is the image this build was compiled against.
  const GuestImage guest_image = DescribeGuestImage();
  size_t checked = 0;
  size_t matched = 0;
  for (size_t i = 0; i < guest_image.function_count; ++i) {
    const uint32_t address = uint32_t(warden::game::Mappings()[i].guest);
    if (address < info.base || address + 4 > info.base + info.size) {
      continue;
    }
    ++checked;
    if (ReadBe32(guest + (address - info.base)) == kMflrR12) {
      ++matched;
    }
  }
  result.entry_points_checked = checked;
  result.entry_points_matching = matched;
  result.match_ratio = checked ? double(matched) / double(checked) : 0.0;
  result.matches_build = result.match_ratio >= kMinimumMatchRatio;

  if (result.matches_build) {
    XELOGI("image matches the linked build: {:.1f}% of {} entry points start with "
           "mflr r12",
           result.match_ratio * 100.0, checked);
  } else {
    XELOGE("image does NOT match the linked build: only {:.1f}% of {} entry points "
           "start with mflr r12",
           result.match_ratio * 100.0, checked);
  }
  return result;
}

std::string BootImage::Describe() const {
  std::string out = "TITLE IMAGE\n";
  // Separate buffers for the row and the value. They were the same one at first, so
  // snprintf was formatting a string into the very buffer it was reading the value
  // from - which is undefined, and showed up as "loaded at  loaded at  lo".
  char line[256];
  char value[128];
  auto row = [&](const char* label, const char* text) {
    std::snprintf(line, sizeof(line), "  %-30s %s\n", label, text);
    out += line;
  };

  if (!loaded) {
    row("state", "NOT LOADED");
    row("reason", detail.c_str());
    return out;
  }
  std::snprintf(value, sizeof(value), "%08X, %u bytes", base, size);
  row("loaded at", value);
  std::snprintf(value, sizeof(value), "%08X", entry_point);
  row("entry point", value);
  std::snprintf(value, sizeof(value), "%u KB", stack_size / 1024);
  row("stack requested", value);
  if (patched) {
    row("title update", patch_source.c_str());
  } else if (!patch_source.empty()) {
    row("title update", patch_source.c_str());
  } else {
    row("title update", "none found");
  }
  if (overlay_written || overlay_present) {
    std::snprintf(value, sizeof(value), "%zu written, %zu already present",
                  overlay_written, overlay_present);
    row("patch overlay", value);
  }
  std::snprintf(value, sizeof(value), "%.1f%% of %zu entry points",
                match_ratio * 100.0, entry_points_checked);
  row("mflr r12 at entry", value);

  if (matches_build) {
    row("verdict", "matches the linked recompilation");
  } else {
    // The specific, known reason this fails - stated because the alternative is a
    // bare "mismatch" that sends the reader looking for a bug in the loader, which
    // the host check has already cleared.
    row("verdict", "DOES NOT match the linked build");
    if (!patched) {
      out += "  The game's code was made from a title-updated image (Title Update\n"
             "  6 or 7), and no title update was applied - so this is the base disc\n"
             "  executable and its code differs. Choose the Ultimate Edition's disc 2\n"
             "  (it carries Title Update 6) or the Title Update 7 file in the launcher.\n";
    } else {
      out += "  A title update was applied but the image still does not match.\n"
             "  Either it is the wrong update or the recompilation is from a\n"
             "  different one - check tools/check_xex.py --build ppc_tu6 --patch.\n";
    }
  }
  return out;
}

}  // namespace warden
