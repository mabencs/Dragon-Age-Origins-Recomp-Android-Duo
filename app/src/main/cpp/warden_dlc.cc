// PLAN 10.10: downloadable content from its Xbox Live packages (LIVE/PIRS, STFS), installed
// as DAORecomp's launcher installs it (third_party/DAORecomp, content/install.cpp,
// InstallDlc): each package's files extracted into the runtime's folder for content type 2
// (files/content/00000002/<content ID>/ - the console's own name for the package, the
// header's SHA-1 in hex), outside the listed folder first and moved in whole, and its
// display name kept beside it (<content ID>.name, UTF-16 big-endian, as XamContentCreateEx
// keeps names). The game lists that folder (warden_xam.cc), mounts each package, grants it
// every licence and loads the add-ins in it - as Awakening's (GameData.finishAwakening).
//
// A package comes as a file of its own (copied from a console's
// Content\0000000000000000\454108C0\00000002\) or inside a disc image: Ultimate Edition's
// disc 2 holds the DLC's packages, read here where they lie. Only this game's packages of
// type 2 are installed; inside a disc, anything else is passed over quietly.

#include "warden_dlc.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

#include "warden_stfs.h"
#include "warden_xdvdfs.h"
#include "xenia/base/logging.h"

namespace warden::dlc {

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kTitleId = 0x454108C0;
// The disc this port is made for (the En/Es/It press): its title update is the one taken.
constexpr uint32_t kMediaId = 0x186A1115;
constexpr uint32_t kContentMarketplace = 0x00000002;
constexpr uint32_t kContentInstaller = 0x000B0000;
// The display name as the game keeps it: 128 UTF-16 code units.
constexpr size_t kDisplayNameBytes = 256;

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// The display name for the launch screen (the code units below 0x80 as they are, others as
// '?': only the words around it are ours).
std::string Ascii(const std::u16string& text) {
  std::string out;
  for (char16_t c : text) {
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

bool WriteAll(const std::string& path, const void* data, size_t size) {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) {
    return false;
  }
  const bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;
  return std::fclose(file) == 0 && ok;
}

struct Outcome {
  int installed = 0;
  std::vector<std::string> lines;
};

// The progress of the install running: the packages' bytes done and in all.
std::atomic<uint64_t> g_done{0};
std::atomic<uint64_t> g_total{0};

// One package read through `read_at` (`size` bytes). `label` names it when it has no
// display name; `in_disc` passes over quietly what is not this game's content.
// PLAN 12.3: a disc's title update for this game's disc, copied whole to `path` (written
// aside, then put in place), unless one of that size is there already - disc 2 holds the
// same Title Update 6 twice (at its root and in ESI/).
void TakeTitleUpdate(const Stfs::ReadAt& read_at, uint64_t size, const std::string& name,
                     const std::string& path, Outcome& outcome) {
  std::error_code ec;
  if (fs::is_regular_file(path, ec) && fs::file_size(path, ec) == size) {
    return;
  }
  std::vector<uint8_t> data(size_t(size), 0);
  if (!read_at(0, data.data(), data.size())) {
    outcome.lines.push_back("Could not read " + name + ".");
    return;
  }
  const std::string part = path + ".part";
  if (!WriteAll(part, data.data(), data.size())) {
    outcome.lines.push_back("Could not copy " + name + ": " + std::strerror(errno) + ".");
    return;
  }
  fs::rename(part, path, ec);
  if (ec) {
    fs::remove(part, ec);
    outcome.lines.push_back("Could not copy " + name + ".");
    return;
  }
  XELOGI("DLC: {} taken from the disc image as the title update ({} bytes) to {}", name,
         size, path);
  outcome.lines.push_back("Took " + name + " from it.");
}

void InstallPackage(const Stfs::ReadAt& read_at, uint64_t size, const std::string& label,
                    const std::string& content_dir, bool in_disc,
                    const std::string& title_update_path, Outcome& outcome) {
  Stfs package(read_at, size);
  if (!package.Open()) {
    if (!in_disc) {
      outcome.lines.push_back(label + ": " + package.error() + ".");
    }
    return;
  }
  const std::string name =
      package.display_name().empty() ? label : Ascii(package.display_name());
  if (package.title_id() != kTitleId) {
    if (!in_disc) {
      outcome.lines.push_back(name + " is not for Dragon Age: Origins.");
    }
    return;
  }
  if (package.content_type() == kContentInstaller) {
    if (!in_disc) {
      outcome.lines.push_back(name + " is a title update - the Title update row takes it.");
    } else if (!title_update_path.empty() && package.media_id() == kMediaId) {
      TakeTitleUpdate(read_at, size, name, title_update_path, outcome);
    }
    return;
  }
  if (package.content_type() != kContentMarketplace) {
    if (!in_disc) {
      outcome.lines.push_back(name + " is not downloadable content.");
    }
    return;
  }
  const std::string id = package.content_id();
  std::error_code ec;
  fs::create_directories(content_dir, ec);
  // Installed already, under the console's name (the content ID, a digit after it) or ours.
  for (const auto& entry : fs::directory_iterator(content_dir, ec)) {
    if (entry.is_directory(ec) &&
        Lower(entry.path().filename().string()).rfind(Lower(id), 0) == 0) {
      outcome.lines.push_back(name + " is installed already.");
      return;
    }
  }
  // Extracted under a name the game does not list (it begins with a dot), then moved in.
  const std::string work = content_dir + "/." + id.substr(0, 8) + ".installing";
  fs::remove_all(work, ec);
  std::vector<uint8_t> data;
  for (const Stfs::File& file : package.files()) {
    data.clear();
    const std::string path = work + "/" + file.path;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (!package.ReadFile(file, data) || !WriteAll(path, data.data(), data.size())) {
      const std::string why = package.error().empty() ? std::strerror(errno) : package.error();
      fs::remove_all(work, ec);
      outcome.lines.push_back("Could not install " + name + ": " + why + ".");
      return;
    }
    g_done += file.size;
  }
  fs::rename(work, content_dir + "/" + id, ec);
  if (ec) {
    fs::remove_all(work, ec);
    outcome.lines.push_back("Could not install " + name + ": it could not be moved in.");
    return;
  }
  uint8_t display[kDisplayNameBytes] = {};
  const std::u16string& title = package.display_name();
  for (size_t i = 0; i < title.size() && i < kDisplayNameBytes / 2 - 1; ++i) {
    display[2 * i] = uint8_t(title[i] >> 8);
    display[2 * i + 1] = uint8_t(title[i]);
  }
  WriteAll(content_dir + "/" + id + ".name", display, sizeof(display));
  XELOGI("DLC: installed {} ({}, {} files) in {}", name, id, package.files().size(),
         content_dir);
  outcome.lines.push_back("Installed " + name + ".");
  ++outcome.installed;
}

bool IsPackageMagic(const char* magic) {
  return std::memcmp(magic, "LIVE", 4) == 0 || std::memcmp(magic, "PIRS", 4) == 0 ||
         std::memcmp(magic, "CON ", 4) == 0;
}

}  // namespace

std::string Install(int fd, const std::string& content_dir,
                    const std::string& title_update_path) {
  struct stat st = {};
  if (fd < 0 || fstat(fd, &st) != 0) {
    if (fd >= 0) {
      close(fd);
    }
    return "The file could not be read.";
  }
  const uint64_t size = uint64_t(st.st_size);
  auto read_fd = [fd](uint64_t offset, void* out, size_t length) {
    uint8_t* cursor = static_cast<uint8_t*>(out);
    while (length) {
      const ssize_t got = pread(fd, cursor, length, off_t(offset));
      if (got <= 0) {
        if (got < 0 && errno == EINTR) {
          continue;
        }
        return false;
      }
      cursor += got;
      offset += uint64_t(got);
      length -= size_t(got);
    }
    return true;
  };

  Outcome outcome;
  g_done = 0;
  g_total = 0;
  char magic[4] = {};
  if (size >= 4 && read_fd(0, magic, 4) && IsPackageMagic(magic)) {
    g_total = size;
    InstallPackage(read_fd, size, "The package", content_dir, false, "", outcome);
    g_done = size;
  } else {
    // A disc image: every Live package in it (Ultimate Edition's disc 2).
    Xdvdfs iso(read_fd);
    if (!iso.Open()) {
      outcome.lines.push_back("Not an Xbox 360 package or disc image.");
    } else {
      // The packages first (their total for the progress), then each installed.
      struct Found {
        std::string path;
        Xdvdfs::Entry entry;
      };
      std::vector<Found> found;
      std::function<void(const std::string&)> walk = [&](const std::string& path) {
        std::vector<Xdvdfs::Entry> entries;
        if (!iso.List(path, entries)) {
          return;
        }
        for (const Xdvdfs::Entry& entry : entries) {
          const std::string child = path.empty() ? entry.name : path + "/" + entry.name;
          if (entry.is_directory()) {
            if (!(path.empty() && entry.name == "$SystemUpdate")) {
              walk(child);
            }
            continue;
          }
          char head[4] = {};
          if (entry.size >= 0x1800 && iso.Read(entry, 0, head, 4) && IsPackageMagic(head)) {
            found.push_back(Found{child, entry});
            g_total += entry.size;
          }
        }
      };
      walk("");
      for (const Found& package : found) {
        const uint64_t start = iso.FileOffset(package.entry);
        const uint64_t length = package.entry.size;
        const uint64_t before = g_done;
        InstallPackage(
            [&read_fd, start, length](uint64_t offset, void* out, size_t count) {
              return offset + count <= length && read_fd(start + offset, out, count);
            },
            length, package.path, content_dir, true, title_update_path, outcome);
        g_done = before + length;  // a package passed over counts as done too
      }
      if (outcome.lines.empty()) {
        outcome.lines.push_back(found.empty() ? "No downloadable content packages in the disc"
                                                " image."
                                              : "No downloadable content for this game in the"
                                                " disc image.");
      }
    }
  }
  close(fd);
  std::string text;
  for (const std::string& line : outcome.lines) {
    text += (text.empty() ? "" : " ") + line;
  }
  XELOGI("DLC: {}", text);
  return text;
}

void Progress(uint64_t& done, uint64_t& total) {
  done = g_done;
  total = g_total;
}

}  // namespace warden::dlc
