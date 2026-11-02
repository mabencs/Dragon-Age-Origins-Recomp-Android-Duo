// The glyph set onto the disc: the atlas descriptions out of the extracted
// guiexport.erf, the set's redirects (warden_glyphs.cc), and the changed descriptions
// into the override tree's packages/core/patch/, which the title reads in place of the
// archive's copies. At the title's load only: rewriting them while the game ran crashed
// its next load (BUG-061, run 350). On Android only - zlib and the file system;
// warden_glyphs.cc is the part the computer can check (tools/check_glyphs.py).

#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "warden_glyphs.h"
#include "xenia/base/logging.h"

namespace warden::glyphs {

namespace {

// guiexport.erf, an ERF V2.1 (tools/dao_erf.py): a 0x20-byte header with the entry
// count at 0x10, then 44-byte entries - a 32-byte name, offset, stored size, size -
// little-endian; an entry whose sizes differ is a zlib stream.
constexpr size_t kErfHeader = 0x20;
constexpr size_t kErfEntry = 44;
constexpr const char* kPatchFolder = "packages/core/patch";

uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool ReadFile(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::stringstream text;
  text << in.rdbuf();
  out = text.str();
  return true;
}

// Every atlas description (atl_*_dat.xml) in the archive, by name.
bool ReadAtlases(const std::string& erf_path, std::map<std::string, std::string>& out,
                 std::string* error) {
  std::string erf;
  if (!ReadFile(erf_path, erf)) {
    *error = "cannot read " + erf_path;
    return false;
  }
  const auto* bytes = reinterpret_cast<const uint8_t*>(erf.data());
  if (erf.size() < kErfHeader || erf.compare(0, 8, "ERF V2.1") != 0) {
    *error = erf_path + " is not an ERF V2.1 archive";
    return false;
  }
  const uint32_t count = Le32(bytes + 0x10);
  if (kErfHeader + uint64_t(count) * kErfEntry > erf.size()) {
    *error = erf_path + ": its index runs past the end";
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* entry = bytes + kErfHeader + size_t(i) * kErfEntry;
    const std::string name(reinterpret_cast<const char*>(entry),
                           strnlen(reinterpret_cast<const char*>(entry), 32));
    if (name.rfind("atl_", 0) != 0 || name.size() < 8 ||
        name.compare(name.size() - 8, 8, "_dat.xml") != 0) {
      continue;
    }
    const uint32_t offset = Le32(entry + 32);
    const uint32_t stored = Le32(entry + 36);
    const uint32_t size = Le32(entry + 40);
    if (uint64_t(offset) + stored > erf.size()) {
      *error = name + " runs past the end of the archive";
      return false;
    }
    std::string text(size, '\0');
    if (stored == size) {
      memcpy(text.data(), bytes + offset, size);
    } else {
      uLongf length = size;
      const int result = uncompress(reinterpret_cast<Bytef*>(text.data()), &length,
                                    bytes + offset, stored);
      if (result != Z_OK || length != size) {
        *error = name + ": zlib says " + std::to_string(result);
        return false;
      }
    }
    out[name] = std::move(text);
  }
  if (out.empty()) {
    *error = erf_path + " holds no atlas descriptions";
    return false;
  }
  return true;
}

bool MakeDirectories(const std::string& path) {
  std::string partial;
  std::stringstream parts(path);
  std::string part;
  if (!path.empty() && path[0] == '/') {
    partial = "/";
  }
  while (std::getline(parts, part, '/')) {
    if (part.empty()) {
      continue;
    }
    partial += part + "/";
    if (mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
      return false;
    }
  }
  return true;
}

// Written whole or not at all: a half-written description would break the UI.
bool WriteFile(const std::string& path, const std::string& text) {
  const std::string part = path + ".part";
  {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(text.data(), std::streamsize(text.size()))) {
      return false;
    }
  }
  return rename(part.c_str(), path.c_str()) == 0;
}

}  // namespace

bool Apply(const std::string& game_data_dir, const std::string& override_root,
           const std::string& record, Set set, std::string* error) {
  std::string scratch;
  error = error ? error : &scratch;
  const std::string erf_path = game_data_dir + "/packages/core/data/guiexport.erf";
  // The archive's size and time: descriptions written from another copy are stale.
  std::string archive;
  {
    struct stat info;
    if (stat(erf_path.c_str(), &info) == 0) {
      archive = std::to_string(info.st_size) + " " + std::to_string(info.st_mtime);
    }
  }
  // What the last start wrote: "set <name>", "archive <size> <time>", then one path per
  // line under the tree.
  std::string previous_set;
  std::string previous_archive;
  std::vector<std::string> previous_files;
  {
    std::string text;
    if (ReadFile(record, text)) {
      std::stringstream lines(text);
      std::string line;
      while (std::getline(lines, line)) {
        if (line.rfind("set ", 0) == 0) {
          previous_set = line.substr(4);
        } else if (line.rfind("archive ", 0) == 0) {
          previous_archive = line.substr(8);
        } else if (!line.empty() && line.find("..") == std::string::npos) {
          previous_files.push_back(line);
        }
      }
    }
  }
  bool intact = previous_set == SetName(set) && previous_archive == archive;
  for (const std::string& file : previous_files) {
    struct stat info;
    intact = intact && stat((override_root + "/" + file).c_str(), &info) == 0;
  }
  if (intact) {
    XELOGI("glyphs: {} prompts, {} descriptions in place", SetName(set), previous_files.size());
    return true;
  }
  for (const std::string& file : previous_files) {
    unlink((override_root + "/" + file).c_str());
  }

  std::vector<std::string> written;
  if (set != Set::kXbox) {
    std::map<std::string, std::string> atlases;
    if (!ReadAtlases(erf_path, atlases, error)) {
      unlink(record.c_str());
      XELOGE("glyphs: {} - the game's own prompts", *error);
      return false;
    }
    std::string report;
    const auto changed = PatchAtlases(atlases, set, &report);
    const std::string folder = override_root + "/" + kPatchFolder;
    if (!MakeDirectories(folder)) {
      *error = "cannot create " + folder;
      unlink(record.c_str());
      XELOGE("glyphs: {} - the game's own prompts", *error);
      return false;
    }
    for (const auto& [name, text] : changed) {
      if (!WriteFile(folder + "/" + name, text)) {
        *error = "cannot write " + folder + "/" + name;
        XELOGE("glyphs: {}", *error);
        continue;
      }
      written.push_back(std::string(kPatchFolder) + "/" + name);
    }
    std::stringstream lines(report);
    std::string line;
    int shown = 0;
    while (std::getline(lines, line) && shown++ < 48) {
      XELOGI("glyphs:{}", line);
    }
  }
  std::string text = std::string("set ") + SetName(set) + "\narchive " + archive + "\n";
  for (const std::string& file : written) {
    text += file + "\n";
  }
  WriteFile(record, text);
  XELOGI("glyphs: {} prompts, {} descriptions written to {}", SetName(set), written.size(),
         override_root);
  return true;
}

}  // namespace warden::glyphs
