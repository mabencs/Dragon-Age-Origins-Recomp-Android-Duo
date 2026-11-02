// The guest filesystem. Task 5.2.
//
// The title addresses its data as D:\packages\core\... - D: being the disc. Here the
// disc is the tree warden_extract.cc wrote into app storage, so a guest path maps onto
// it directly:
//
//   D:\   game:\   \Device\Cdrom0\   (optionally behind \??\)   the game data root
//   <root>:\ - a content root the title mounted (a save)        its folder, writable
//   anything else - cache:\, a hard drive, a memory unit        not found, and logged
//
// **Case-insensitive, per component**, as the console's filesystem is: an exact match
// is tried first and is the common case, then the directory is searched.
//
// **Read-only**, as a disc is: an open asking for write access is refused, and so is a
// disposition that would create or truncate. **Content roots** are the exception: a
// root the title mounted with XamContentCreateEx (a save, "save:\...") is a folder in
// app storage that takes creation, writes, truncation, renames and deletion
// (MountContentRoot, warden_xam.cc).
//
// Structures and status codes follow Xenia's xboxkrnl_io.cc, xboxkrnl_io_info.cc and
// kernel/info/*.h, except where the title's own code says otherwise - see NtOpenFile.
// Register usage for every import here was read off the title's call sites first
// (sub_82AB4xxx, its CreateFile / ReadFile / SetFilePointer wrappers).

#include "warden_fs.h"
#include "warden_debug_mode.h"

#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_objects.h"
#include "warden_threads.h"
#include "warden_trace.h"

// warden_memory.cc: a host write is about to land in guest memory.
extern "C" void warden_memory_host_write(uint32_t guest_address, uint32_t length);
#include "xenia/base/logging.h"

namespace warden {

namespace {

constexpr uint32_t kStatusSuccess = 0x00000000;
constexpr uint32_t kStatusBufferOverflow = 0x80000005;
constexpr uint32_t kStatusNoMoreFiles = 0x80000006;
constexpr uint32_t kStatusEndOfFile = 0xC0000011;
constexpr uint32_t kStatusInfoLengthMismatch = 0xC0000004;
constexpr uint32_t kStatusInvalidHandle = 0xC0000008;
constexpr uint32_t kStatusInvalidParameter = 0xC000000D;
constexpr uint32_t kStatusNoSuchFile = 0xC000000F;
constexpr uint32_t kStatusAccessDenied = 0xC0000022;
constexpr uint32_t kStatusObjectNameInvalid = 0xC0000033;
constexpr uint32_t kStatusObjectNameNotFound = 0xC0000034;
constexpr uint32_t kStatusObjectPathNotFound = 0xC000003A;
constexpr uint32_t kStatusInvalidInfoClass = 0xC0000003;
constexpr uint32_t kStatusFileIsADirectory = 0xC00000BA;
constexpr uint32_t kStatusNotADirectory = 0xC0000103;
constexpr uint32_t kStatusMediaWriteProtected = 0xC00000A2;
constexpr uint32_t kStatusObjectNameCollision = 0xC0000035;
constexpr uint32_t kStatusDiskFull = 0xC000007F;

// Open options that change the answer.
constexpr uint32_t kFileDirectoryFile = 0x00000001;
constexpr uint32_t kFileNonDirectoryFile = 0x00000040;
// Access that a read-only disc refuses: FILE_WRITE_DATA, FILE_APPEND_DATA,
// FILE_WRITE_EA, FILE_WRITE_ATTRIBUTES, DELETE, GENERIC_ALL, GENERIC_WRITE.
constexpr uint32_t kWriteAccess =
    0x00000002 | 0x00000004 | 0x00000010 | 0x00000100 | 0x00010000 | 0x10000000 |
    0x40000000;
// NtCreateFile dispositions.
constexpr uint32_t kFileSupersede = 0;
constexpr uint32_t kFileOpen = 1;
constexpr uint32_t kFileCreate = 2;
constexpr uint32_t kFileOpenIf = 3;
constexpr uint32_t kFileOverwrite = 4;
constexpr uint32_t kFileOverwriteIf = 5;
// IO_STATUS_BLOCK.Information for a successful open.
constexpr uint32_t kFileSuperseded = 0;
constexpr uint32_t kFileOpened = 1;
constexpr uint32_t kFileCreated = 2;
constexpr uint32_t kFileOverwritten = 3;
// The pseudo-handle for \??\, the DOS devices directory, as a root directory.
constexpr uint32_t kDosDevices = 0xFFFFFFFD;

// Information classes, per kernel/info/file.h and volume.h.
constexpr uint32_t kFileBasicInformation = 4;
constexpr uint32_t kFileStandardInformation = 5;
constexpr uint32_t kFileInternalInformation = 6;
constexpr uint32_t kFileNameInformation = 9;
constexpr uint32_t kFileRenameInformation = 10;
constexpr uint32_t kFileDispositionInformation = 13;
constexpr uint32_t kFilePositionInformation = 14;
constexpr uint32_t kFileModeInformation = 16;
constexpr uint32_t kFileAlignmentInformation = 17;
constexpr uint32_t kFileAllocationInformation = 19;
constexpr uint32_t kFileEndOfFileInformation = 20;
constexpr uint32_t kFileSectorInformation = 26;
constexpr uint32_t kFileNetworkOpenInformation = 34;
constexpr uint32_t kFileFsVolumeInformation = 1;
constexpr uint32_t kFileFsSizeInformation = 3;
constexpr uint32_t kFileFsDeviceInformation = 4;
constexpr uint32_t kFileFsAttributeInformation = 5;

// What the disc says about its entries, with READONLY added as Xenia does: every file
// on it is 0x80 and every directory 0x90 (measured with tools/dao_iso.py).
constexpr uint32_t kFileAttributes = 0x81;
constexpr uint32_t kDirectoryAttributes = 0x91;
// XDVDFS has no timestamps; Xenia reports 1970-01-01, as a FILETIME.
constexpr uint64_t kDiscTime = 116444736000000000ull;
constexpr uint64_t kSectorSize = 2048;
// FILE_DIRECTORY_INFORMATION is 0x40 bytes of fields, then the name.
constexpr uint32_t kDirectoryInfoHeader = 0x40;

// The extractor's own completion stamp sits in the root; it is not on the disc.
constexpr std::string_view kExtractionStamp = "extraction-complete.txt";

std::mutex g_root_mutex;
std::string g_root;
// The loose-file tree over the disc (SetGuestOverrideRoot); empty if none.
std::string g_override_root;
// PLAN 10.12: the enabled mods' folders under it, the highest priority first
// (SetGuestModRoots).
std::vector<std::string> g_mod_roots;

std::string Root() {
  std::lock_guard<std::mutex> lock(g_root_mutex);
  return g_root;
}

std::string OverrideRoot() {
  std::lock_guard<std::mutex> lock(g_root_mutex);
  return g_override_root;
}

// Every tree over the disc, the one that wins first: the override tree (the button
// prompts' redirects), then the enabled mods, the last in their load order first.
std::vector<std::string> OverlayRoots() {
  std::lock_guard<std::mutex> lock(g_root_mutex);
  std::vector<std::string> roots;
  if (!g_override_root.empty()) {
    roots.push_back(g_override_root);
  }
  roots.insert(roots.end(), g_mod_roots.begin(), g_mod_roots.end());
  return roots;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && strncasecmp(a.data(), b.data(), a.size()) == 0;
}

bool StartsWithNoCase(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && EqualsNoCase(text.substr(0, prefix.size()), prefix);
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

uint64_t RoundToSector(uint64_t size) {
  return (size + kSectorSize - 1) / kSectorSize * kSectorSize;
}

// An ANSI_STRING in guest memory - { u16 Length; u16 MaximumLength; u32 Buffer } - as a
// view. Its Length counts, not a terminator: FindFirstFile shortens the string it
// passes by changing Length alone.
std::string_view GuestAnsiString(uint8_t* base, uint32_t address) {
  if (!address) {
    return {};
  }
  const uint16_t length = PPC_LOAD_U16(address);
  const uint32_t buffer = PPC_LOAD_U32(address + 4);
  if (!buffer || !length) {
    return {};
  }
  return std::string_view(reinterpret_cast<const char*>(base + buffer), length);
}

// Files the guest must not see: the extractor's stamp, half-written files, and anything
// hidden. None of them is on the disc.
bool IsInternal(std::string_view name, bool at_root) {
  return name.empty() || name.front() == '.' ||
         (name.size() > 5 && name.substr(name.size() - 5) == ".part") ||
         (at_root && name == kExtractionStamp);
}

// Appends a relative guest path's components, resolving `.` and `..` lexically.
// False for a `..` that climbs out of the root.
bool AppendComponents(std::string_view path, std::vector<std::string>& components) {
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find_first_of("\\/", start);
    if (end == std::string_view::npos) {
      end = path.size();
    }
    const std::string_view part = path.substr(start, end - start);
    if (part == "..") {
      if (components.empty()) {
        return false;
      }
      components.pop_back();
    } else if (!part.empty() && part != ".") {
      components.emplace_back(part);
    }
    start = end + 1;
  }
  return true;
}

// Where a guest path points: a volume - "" for the disc, else a mounted content root's
// name, lower-case - and the components under it.
struct GuestLocation {
  std::string volume;
  std::vector<std::string> components;
};

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// The content roots the title mounted (XamContentCreateEx): name -> folder.
std::mutex g_mounts_mutex;
std::map<std::string, std::string> g_mounts;

std::optional<std::string> MountedFolder(const std::string& volume) {
  std::lock_guard<std::mutex> lock(g_mounts_mutex);
  auto it = g_mounts.find(volume);
  if (it == g_mounts.end()) {
    return std::nullopt;
  }
  return it->second;
}

// The host folder a volume is in; empty if it is not there (any more).
std::string VolumeFolder(const std::string& volume) {
  return volume.empty() ? Root() : MountedFolder(volume).value_or(std::string());
}

// A full guest path's location, or nullopt if it names a device this runtime does not
// provide.
std::optional<GuestLocation> ParseGuestPath(std::string_view path) {
  if (StartsWithNoCase(path, "\\??\\")) {
    path.remove_prefix(4);
  }
  static constexpr std::string_view kDevices[] = {"D:", "game:", "\\Device\\Cdrom0"};
  for (std::string_view device : kDevices) {
    if (StartsWithNoCase(path, device) &&
        (path.size() == device.size() || path[device.size()] == '\\' ||
         path[device.size()] == '/')) {
      GuestLocation location;
      if (!AppendComponents(path.substr(device.size()), location.components)) {
        return std::nullopt;
      }
      return location;
    }
  }
  // A mounted content root: "<root>:" before the first separator.
  const size_t colon = path.find(':');
  if (colon != std::string_view::npos && colon > 0 && path.find_first_of("\\/") > colon) {
    const std::string volume = Lower(path.substr(0, colon));
    if (MountedFolder(volume)) {
      GuestLocation location;
      location.volume = volume;
      if (!AppendComponents(path.substr(colon + 1), location.components)) {
        return std::nullopt;
      }
      return location;
    }
  }
  return std::nullopt;
}

struct HostEntry {
  std::string path;
  bool directory = false;
  uint64_t size = 0;
};

// The host file a guest location names. `parent_missing` says which failure it was,
// because the console distinguishes a missing file from a missing directory on the way.
std::atomic<bool> g_skip_intro_logos{false};

// The logo movies the option hides, by file name, case-insensitively. (The new game's
// intro movie cannot be hidden the same way: its cutscene waits for the movie and never
// ends - tried 2026-10-25.)
bool IsHiddenName(const std::string& name) {
  if (!g_skip_intro_logos.load(std::memory_order_relaxed)) {
    return false;
  }
  static constexpr const char* kIntroLogos[] = {
      "dragon_age_ea_logo.bik", "bwlogo.bik", "edge_logo_withaudio.bik"};
  for (const char* logo : kIntroLogos) {
    if (strcasecmp(name.c_str(), logo) == 0) {
      return true;
    }
  }
  return false;
}

std::optional<HostEntry> ResolveOnVolume(const GuestLocation& location,
                                         bool& parent_missing) {
  const std::vector<std::string>& components = location.components;
  const bool disc = location.volume.empty();
  if (disc && !components.empty() && IsHiddenName(components.back())) {
    static std::atomic<int> said{0};
    if (said.fetch_add(1, std::memory_order_relaxed) < 6) {
      XELOGI("skip_intro_logos: hiding {}", components.back());
    }
    return std::nullopt;
  }
  parent_missing = false;
  std::string current = VolumeFolder(location.volume);
  if (current.empty()) {
    parent_missing = true;
    return std::nullopt;
  }
  for (size_t i = 0; i < components.size(); ++i) {
    const std::string& component = components[i];
    const bool at_root = disc && i == 0;
    if (IsInternal(component, at_root)) {
      parent_missing = i + 1 < components.size();
      return std::nullopt;
    }
    std::string candidate = current + "/" + component;
    struct stat info;
    if (stat(candidate.c_str(), &info) == 0) {
      current = std::move(candidate);
      continue;
    }
    // Not under that spelling. The console does not care about case, so look.
    bool found = false;
    if (DIR* dir = opendir(current.c_str())) {
      while (const dirent* entry = readdir(dir)) {
        if (!IsInternal(entry->d_name, at_root) &&
            EqualsNoCase(entry->d_name, component)) {
          current += "/";
          current += entry->d_name;
          found = true;
          break;
        }
      }
      closedir(dir);
    }
    if (!found) {
      parent_missing = i + 1 < components.size();
      return std::nullopt;
    }
  }
  struct stat info;
  if (stat(current.c_str(), &info) != 0) {
    return std::nullopt;
  }
  HostEntry entry;
  entry.path = current;
  entry.directory = S_ISDIR(info.st_mode);
  entry.size = entry.directory ? 0 : uint64_t(info.st_size);
  return entry;
}

// One tree's entry for disc components, found as the disc's are (an exact spelling
// first, then without regard to case); nullopt if the tree has none.
std::optional<std::string> TreeEntry(std::string current,
                                     const std::vector<std::string>& components) {
  if (current.empty()) {
    return std::nullopt;
  }
  for (const std::string& component : components) {
    std::string candidate = current + "/" + component;
    struct stat info;
    if (stat(candidate.c_str(), &info) == 0) {
      current = std::move(candidate);
      continue;
    }
    bool found = false;
    if (DIR* dir = opendir(current.c_str())) {
      while (const dirent* entry = readdir(dir)) {
        if (EqualsNoCase(entry->d_name, component)) {
          current += "/";
          current += entry->d_name;
          found = true;
          break;
        }
      }
      closedir(dir);
    }
    if (!found) {
      return std::nullopt;
    }
  }
  return current;
}

// The entry the trees over the disc have for disc components: the first tree's that has
// one (OverlayRoots' order); nullopt if none has.
std::optional<std::string> OverrideEntry(const std::vector<std::string>& components) {
  for (const std::string& root : OverlayRoots()) {
    if (auto entry = TreeEntry(root, components)) {
      return entry;
    }
  }
  return std::nullopt;
}

// A guest location's host entry: on the disc, the override tree's file of that name
// first (SetGuestOverrideRoot), and a directory only the tree has.
std::optional<HostEntry> Resolve(const GuestLocation& location, bool& parent_missing) {
  if (location.volume.empty() && !location.components.empty()) {
    struct stat info;
    auto overlay = OverrideEntry(location.components);
    if (overlay && stat(overlay->c_str(), &info) == 0) {
      if (S_ISREG(info.st_mode)) {
        static std::atomic<int> said{0};
        if (said.fetch_add(1, std::memory_order_relaxed) < 32) {
          XELOGI("override: {} from {}", location.components.back(), *overlay);
        }
        parent_missing = false;
        HostEntry entry;
        entry.path = *overlay;
        entry.size = uint64_t(info.st_size);
        return entry;
      }
      if (S_ISDIR(info.st_mode)) {
        // A directory on both is the disc's; the tree's entries are listed with it.
        if (auto disc = ResolveOnVolume(location, parent_missing)) {
          return disc;
        }
        parent_missing = false;
        HostEntry entry;
        entry.path = *overlay;
        entry.directory = true;
        return entry;
      }
    }
  }
  return ResolveOnVolume(location, parent_missing);
}

struct DirectoryEntry {
  std::string name;
  bool directory = false;
  uint64_t size = 0;
};

// A directory's entries as the guest sees them, sorted without regard to case - the
// order XDVDFS keeps them in. For a disc directory (`disc_components` its path), the
// override tree's entries of the same directory too, its files in place of the disc's
// of the same name.
std::vector<DirectoryEntry> ListDirectory(const std::string& host_path, bool at_root,
                                          const std::vector<std::string>* disc_components) {
  std::vector<DirectoryEntry> entries;
  auto list = [&](const std::string& folder, bool overlay) {
    DIR* dir = opendir(folder.c_str());
    if (!dir) {
      return;
    }
    while (const dirent* entry = readdir(dir)) {
      if (IsInternal(entry->d_name, at_root) || IsHiddenName(entry->d_name)) {
        continue;
      }
      struct stat info;
      const std::string path = folder + "/" + entry->d_name;
      if (stat(path.c_str(), &info) != 0) {
        continue;
      }
      DirectoryEntry out;
      out.name = entry->d_name;
      out.directory = S_ISDIR(info.st_mode);
      out.size = out.directory ? 0 : uint64_t(info.st_size);
      auto same = overlay ? std::find_if(entries.begin(), entries.end(),
                                         [&](const DirectoryEntry& listed) {
                                           return EqualsNoCase(listed.name, out.name);
                                         })
                          : entries.end();
      if (same != entries.end()) {
        same->directory = out.directory;
        same->size = out.size;
      } else {
        entries.push_back(std::move(out));
      }
    }
    closedir(dir);
  };
  list(host_path, false);
  if (disc_components) {
    // Every tree's entries of the directory, the one that wins last (its sizes kept).
    const std::vector<std::string> roots = OverlayRoots();
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
      auto overlay = disc_components->empty() ? std::optional<std::string>(*it)
                                              : TreeEntry(*it, *disc_components);
      if (overlay && *overlay != host_path) {
        list(*overlay, true);
      }
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const DirectoryEntry& a, const DirectoryEntry& b) {
              return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
            });
  return entries;
}

// A wildcard match - `*` any run, `?` any one character - without regard to case.
// `*.*` is everything, as Windows has always treated it, dot or no dot.
bool Matches(std::string_view pattern, std::string_view name) {
  if (pattern == "*.*") {
    pattern = "*";
  }
  size_t p = 0, n = 0, star = std::string_view::npos, resume = 0;
  while (n < name.size()) {
    if (p < pattern.size() &&
        (pattern[p] == '?' ||
         std::tolower(static_cast<unsigned char>(pattern[p])) ==
             std::tolower(static_cast<unsigned char>(name[n])))) {
      ++p;
      ++n;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      resume = n;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      n = ++resume;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

class GuestFile : public KernelObject {
 public:
  GuestFile(GuestLocation location, HostEntry entry)
      : KernelObject(Type::kFile),
        volume(std::move(location.volume)),
        components(std::move(location.components)),
        host(std::move(entry)),
        writable(!volume.empty()) {}
  ~GuestFile() override {
    if (fd >= 0) {
      close(fd);
    }
    // A content root's file the title marked for deletion goes when its last handle
    // closes, as on the console.
    if (delete_pending) {
      const int result = host.directory ? rmdir(host.path.c_str()) : unlink(host.path.c_str());
      XELOGI("deleted {}{}", host.path,
             result == 0 ? "" : fmt::format(" - failed: {}", strerror(errno)));
    }
  }

  // Where it is in the guest's namespace, so an open relative to it can be resolved:
  // the volume ("" the disc, else a content root) and the components under it.
  const std::string volume;
  std::vector<std::string> components;  // changed by a rename
  HostEntry host;                       // its size by writes, its path by a rename
  // A content root's file: writes, truncation, renames and deletion are allowed.
  const bool writable;
  bool delete_pending = false;

  std::mutex mutex;
  // Opened on the first read, not at NtOpenFile: a title opens far more files than it
  // reads, and the handheld's descriptor limit is not large.
  int fd = -1;
  // The file position a read without an explicit offset continues from.
  uint64_t position = 0;

  // Directory enumeration, one entry per NtQueryDirectoryFile.
  bool listed = false;
  std::vector<DirectoryEntry> listing;
  std::string pattern;
  size_t next = 0;
  int logged_entries = 0;  // entries of this listing written to the log so far
  // Reads of this file, for the log: the first few are logged, then every 256th with
  // the running total. Per file, so an archive mount of hundreds of small reads cannot
  // hide the movie read that follows it (a global budget did exactly that, BUG-039).
  std::atomic<uint32_t> reads{0};
  std::atomic<uint64_t> bytes_read_total{0};
};

std::string Joined(const std::vector<std::string>& components,
                   const std::string& volume = std::string()) {
  std::string out = volume.empty() ? std::string("D:") : volume + ":";
  for (const std::string& component : components) {
    out += "\\";
    out += component;
  }
  return out;
}

// Enough to see what the title opens; capped, since a running title opens a lot. One
// budget per call site - a directory walk of a few hundred entries must not use up the
// opens and reads that follow it.
// PLAN 11.1: no budget in Debugging Mode - the whole log.
bool ShouldLog(std::atomic<int>& logged, int limit) {
  return logged.fetch_add(1, std::memory_order_relaxed) < limit || debug_mode::On();
}
#define WARDEN_LOG_BUDGET(limit)                     ([] {                                                static std::atomic<int> logged{0};                 return ShouldLog(logged, limit);                 }())

// Whether to log this read of `file`, and its ordinal and the file's running total.
bool ShouldLogRead(GuestFile* file, uint32_t bytes, uint32_t& ordinal, uint64_t& total) {
  if (!file) {
    ordinal = 0;
    total = 0;
    return true;
  }
  ordinal = file->reads.fetch_add(1, std::memory_order_relaxed) + 1;
  total = file->bytes_read_total.fetch_add(bytes, std::memory_order_relaxed) + bytes;
  // The first four, then each power of two, then every 1024th: a file read once and a
  // file streamed for minutes both show their shape in a few lines.
  return ordinal <= 8 || (ordinal & (ordinal - 1)) == 0 || ordinal % 1024 == 0;
}

void WriteIoStatus(uint8_t* base, uint32_t io_status, uint32_t status, uint32_t information) {
  if (io_status) {
    PPC_STORE_U32(io_status + 0, status);
    PPC_STORE_U32(io_status + 4, information);
  }
}

// Resolves OBJECT_ATTRIBUTES - { u32 RootDirectory; u32 ObjectName; u32 Attributes } -
// to components under the root. A root directory other than 0 or \?? makes the name
// relative to that open directory.
uint32_t ComponentsOf(uint8_t* base, uint32_t attributes, std::string_view& name,
                      GuestLocation& location) {
  const uint32_t root_directory = PPC_LOAD_U32(attributes + 0);
  name = Trim(GuestAnsiString(base, PPC_LOAD_U32(attributes + 4)));
  if (root_directory != 0 && root_directory != kDosDevices) {
    auto parent = LookupObject<GuestFile>(root_directory, KernelObject::Type::kFile);
    if (!parent) {
      return kStatusInvalidHandle;
    }
    location.volume = parent->volume;
    location.components = parent->components;
    return AppendComponents(name, location.components) ? kStatusSuccess
                                                       : kStatusObjectNameInvalid;
  }
  if (auto parsed = ParseGuestPath(name)) {
    location = std::move(*parsed);
    return kStatusSuccess;
  }
  // A device this runtime does not have - say which, it is the next thing to add.
  XELOGW("no such device for \"{}\"", name);
  return kStatusObjectPathNotFound;
}

// Creates a content root's file or folder: the parent folder must be there; the name is
// as the title spelled it.
uint32_t CreateOnVolume(const GuestLocation& location, bool directory) {
  if (location.components.empty()) {
    return kStatusObjectNameInvalid;
  }
  GuestLocation parent;
  parent.volume = location.volume;
  parent.components.assign(location.components.begin(), location.components.end() - 1);
  bool missing = false;
  auto folder = Resolve(parent, missing);
  if (!folder || !folder->directory) {
    return kStatusObjectPathNotFound;
  }
  const std::string& leaf = location.components.back();
  if (IsInternal(leaf, false)) {
    return kStatusObjectNameInvalid;
  }
  const std::string path = folder->path + "/" + leaf;
  if (directory) {
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
      return errno == ENOSPC ? kStatusDiskFull : kStatusAccessDenied;
    }
  } else {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
      return errno == ENOSPC ? kStatusDiskFull : kStatusAccessDenied;
    }
    close(fd);
  }
  return kStatusSuccess;
}

// The open shared by NtOpenFile and NtCreateFile. On the disc an open is read-only and
// creates nothing; on a content root the dispositions are the NT ones.
void OpenFile(PPCContext& ctx, uint8_t* base, const char* who, uint32_t handle_out,
              uint32_t access, uint32_t attributes, uint32_t io_status,
              uint32_t disposition, uint32_t options) {
  uint32_t status = kStatusSuccess;
  uint32_t handle = 0xFFFFFFFF;
  uint32_t information = kFileOpened;
  std::string_view name;
  GuestLocation location;

  if (!handle_out || !attributes) {
    status = kStatusInvalidParameter;
  } else {
    status = ComponentsOf(base, attributes, name, location);
  }
  const bool writable = !location.volume.empty();
  if (status == kStatusSuccess && (access & kWriteAccess) && !writable) {
    status = kStatusAccessDenied;
  }
  const bool creates = disposition == kFileCreate || disposition == kFileOpenIf ||
                       disposition == kFileOverwriteIf || disposition == kFileSupersede;
  const bool truncates = disposition == kFileOverwrite || disposition == kFileOverwriteIf ||
                         disposition == kFileSupersede;
  std::optional<HostEntry> entry;
  if (status == kStatusSuccess) {
    bool parent_missing = false;
    entry = Resolve(location, parent_missing);
    if (!writable) {
      if (!entry) {
        // A disc cannot create what is not there.
        status = creates          ? kStatusMediaWriteProtected
                 : parent_missing ? kStatusObjectPathNotFound
                                  : kStatusObjectNameNotFound;
      } else if (disposition == kFileCreate || truncates) {
        status = kStatusMediaWriteProtected;
      }
    } else if (!entry) {
      if (!creates) {
        status = parent_missing ? kStatusObjectPathNotFound : kStatusObjectNameNotFound;
      } else {
        status = CreateOnVolume(location, (options & kFileDirectoryFile) != 0);
        if (status == kStatusSuccess) {
          entry = Resolve(location, parent_missing);
          information = kFileCreated;
          if (!entry) {
            status = kStatusAccessDenied;
          }
        }
      }
    } else if (disposition == kFileCreate) {
      status = kStatusObjectNameCollision;
    } else if (truncates) {
      if (entry->directory) {
        status = kStatusFileIsADirectory;
      } else if (truncate(entry->path.c_str(), 0) != 0) {
        status = kStatusAccessDenied;
      } else {
        entry->size = 0;
        information = disposition == kFileSupersede ? kFileSuperseded : kFileOverwritten;
      }
    }
    if (status == kStatusSuccess) {
      if ((options & kFileDirectoryFile) && !entry->directory) {
        status = kStatusNotADirectory;
      } else if ((options & kFileNonDirectoryFile) && entry->directory) {
        status = kStatusFileIsADirectory;
      }
    }
  }
  if (status == kStatusSuccess) {
    handle = AddObject(std::make_shared<GuestFile>(location, *entry));
    // PLAN 11.1: a save's savegame.das opened to be read - the save being loaded.
    if (writable && !(access & kWriteAccess) && !truncates && !entry->directory &&
        EqualsNoCase(name.size() >= 12 ? name.substr(name.size() - 12) : name,
                     "savegame.das")) {
      debug_mode::NoteSaveRead(entry->path);
    }
  }

  if (handle_out) {
    PPC_STORE_U32(handle_out, handle);
  }
  WriteIoStatus(base, io_status, status, status == kStatusSuccess ? information : 0);
  if (writable || WARDEN_LOG_BUDGET(1500)) {
    XELOGI("[{}] {}(\"{}\", access {:08X}, disposition {}, options {:08X}) -> {:08X}{}",
           CurrentGuestThreadName(), who, name, access, disposition, options, status,
           status == kStatusSuccess ? fmt::format(" handle {:08X}", handle) : "");
  }
  ctx.r3.u64 = status;
}

// Opens the host file behind a GuestFile on first use. Caller holds file->mutex.
bool EnsureOpen(GuestFile& file) {
  if (file.fd >= 0) {
    return true;
  }
  file.fd = open(file.host.path.c_str(), (file.writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
  if (file.fd < 0) {
    XELOGE("cannot open {}: {}", file.host.path, strerror(errno));
    return false;
  }
  return true;
}

// Reads `length` bytes into guest memory at `buffer`, from `offset` or, if that is
// none, the file position; the position ends up after what was read either way.
uint32_t ReadInto(GuestFile& file, uint8_t* base, uint32_t buffer, uint32_t length,
                  std::optional<uint64_t> offset, uint32_t& bytes_read) {
  bytes_read = 0;
  std::lock_guard<std::mutex> lock(file.mutex);
  if (file.host.directory) {
    return kStatusFileIsADirectory;
  }
  if (!EnsureOpen(file)) {
    return kStatusAccessDenied;
  }
  const uint64_t at = offset ? *offset : file.position;
  if (at >= file.host.size) {
    file.position = at;
    return kStatusEndOfFile;
  }
  uint8_t* cursor = base + buffer;
  uint64_t remaining = std::min<uint64_t>(length, file.host.size - at);
  uint64_t position = at;
  // The read lands straight in guest memory. Pages the GPU watches are
  // write-protected, and pread() into one fails with EFAULT instead of faulting the
  // process, so the memory system is told first: it unprotects them and invalidates
  // the GPU's copy (a texture read from an archive into memory the GPU already has).
  warden_memory_host_write(buffer, uint32_t(remaining));
  while (remaining) {
    const ssize_t got = pread(file.fd, cursor, size_t(remaining), off_t(position));
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got < 0) {
      static std::atomic<int> failures{0};
      if (failures.fetch_add(1) < 16) {
        XELOGE("[{}] read of {} into {:08X} ({} bytes at {}) failed: {} ({})",
               warden::CurrentGuestThreadName(), file.components.empty() ? "?" : file.components.back(),
               uint32_t(cursor - base), remaining, position, strerror(errno), errno);
      }
      break;
    }
    if (got == 0) {
      break;
    }
    cursor += got;
    position += uint64_t(got);
    remaining -= uint64_t(got);
    bytes_read += uint32_t(got);
  }
  file.position = position;
  return kStatusSuccess;
}

void WriteNetworkOpenInformation(uint8_t* base, uint32_t at, const HostEntry& entry) {
  for (uint32_t time = 0; time < 32; time += 8) {
    PPC_STORE_U64(at + time, kDiscTime);
  }
  PPC_STORE_U64(at + 32, RoundToSector(entry.size));
  PPC_STORE_U64(at + 40, entry.size);
  PPC_STORE_U32(at + 48, entry.directory ? kDirectoryAttributes : kFileAttributes);
  PPC_STORE_U32(at + 52, 0);
}

}  // namespace

void SetSkipIntroLogos(bool skip) {
  g_skip_intro_logos.store(skip, std::memory_order_relaxed);
  XELOGI("skip_intro_logos: {}", skip ? "on - the logo movies are reported missing" : "off");
}

void MountContentRoot(const std::string& root, const std::string& host_directory) {
  std::string name = Lower(root);
  while (!name.empty() && name.back() == ':') {
    name.pop_back();
  }
  {
    std::lock_guard<std::mutex> lock(g_mounts_mutex);
    g_mounts[name] = host_directory;
  }
  XELOGI("content root {}: is {}", name, host_directory);
}

void UnmountContentRoot(const std::string& root) {
  std::string name = Lower(root);
  while (!name.empty() && name.back() == ':') {
    name.pop_back();
  }
  bool was = false;
  {
    std::lock_guard<std::mutex> lock(g_mounts_mutex);
    was = g_mounts.erase(name) != 0;
  }
  XELOGI("content root {}: closed{}", name, was ? "" : " (was not open)");
}

void SetGuestFileRoot(const std::string& host_directory) {
  std::lock_guard<std::mutex> lock(g_root_mutex);
  g_root = host_directory;
  XELOGI("guest D:\\ is {}", host_directory);
}

void SetGuestModRoots(const std::vector<std::string>& host_directories) {
  std::lock_guard<std::mutex> lock(g_root_mutex);
  g_mod_roots = host_directories;
  for (const std::string& root : host_directories) {
    XELOGI("guest D:\\ under it: the mod {}", root);
  }
}

void SetGuestOverrideRoot(const std::string& host_directory) {
  std::lock_guard<std::mutex> lock(g_root_mutex);
  g_override_root = host_directory;
  XELOGI("guest D:\\ overridden by {}", host_directory.empty() ? "nothing" : host_directory);
}

}  // namespace warden

using namespace warden;

// NTSTATUS NtOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
//                     PIO_STATUS_BLOCK io_status, ULONG share_access, ULONG open_options)
//
// **Six arguments**, read off the title's own call (its FindFirstFile, sub_82AB8170):
// r7 is the share access - 3, read and write - and r8 the open options - 0x4021,
// directory + synchronous + backup intent. Xenia's NtOpenFile takes five and reads the
// share access as the options (BUG-033).
PPC_FUNC(__imp__NtOpenFile) {
  OpenFile(ctx, base, "NtOpenFile", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
           kFileOpen, ctx.r8.u32);
}

// NTSTATUS NtCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
//                       PIO_STATUS_BLOCK io_status, PLARGE_INTEGER allocation_size,
//                       ULONG file_attributes, ULONG share_access, ULONG disposition,
//                       ULONG create_options)
//
// Nine arguments: the ninth, the options, is on the stack at r1 + 0x54. The title's
// CreateDirectory wrapper (sub_82AB4368) passes disposition 2 - FILE_CREATE - and
// directory options; a disc refuses that, which the title tolerates.
PPC_FUNC(__imp__NtCreateFile) {
  OpenFile(ctx, base, "NtCreateFile", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
           ctx.r10.u32, PPC_LOAD_U32(ctx.r1.u32 + 0x54));
}

// NTSTATUS NtReadFile(HANDLE file, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
//                     PIO_STATUS_BLOCK io_status, PVOID buffer, ULONG length,
//                     PLARGE_INTEGER byte_offset)
//
// Synchronous: the read completes here, the status block says how much, and the event
// - if the title passed one - is set, which is what an overlapped caller waits on. An
// APC routine would need the thread's APC queue, which does not exist; it is reported
// once if a title relies on it.
PPC_FUNC(__imp__NtReadFile) {
  const uint32_t event_handle = ctx.r4.u32;
  const uint32_t apc_routine = ctx.r5.u32;
  const uint32_t io_status = ctx.r7.u32;
  const uint32_t buffer = ctx.r8.u32;
  const uint32_t length = ctx.r9.u32;
  const uint32_t offset_pointer = ctx.r10.u32;

  auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile);
  uint32_t status;
  uint32_t bytes_read = 0;
  if (!file) {
    status = kStatusInvalidHandle;
  } else if (!buffer) {
    status = kStatusInvalidParameter;
  } else {
    std::optional<uint64_t> offset;
    if (offset_pointer) {
      const uint64_t value = PPC_LOAD_U64(offset_pointer);
      // -1 means "the current position", as on Windows.
      if (value != 0xFFFFFFFFFFFFFFFFull) {
        offset = value;
      }
    }
    status = ReadInto(*file, base, buffer, length, offset, bytes_read);
  }
  WriteIoStatus(base, io_status, status, bytes_read);
  if (apc_routine & ~1u) {
    static std::atomic<bool> said{false};
    if (!said.exchange(true)) {
      XELOGW("NtReadFile: an APC routine was passed and will not be called");
    }
  }
  if (event_handle) {
    if (auto event = LookupObject<GuestEvent>(event_handle, KernelObject::Type::kEvent)) {
      {
        std::lock_guard<std::mutex> lock(WaitMutex());
        event->signalled = true;
        NoteSignalStateChanged(event);
      }
      WaitCondition().notify_all();
    }
  }
  uint32_t ordinal;
  uint64_t total;
  const bool log_read = ShouldLogRead(file.get(), bytes_read, ordinal, total);
  // BUG-039: the path through the title's code to a movie's first and last header
  // reads, to find where its streaming should start.
  if (file && (ordinal == 1 || ordinal == 5) && !file->components.empty()) {
    const std::string& leaf = file->components.back();
    if (leaf.size() > 4 && strcasecmp(leaf.c_str() + leaf.size() - 4, ".bik") == 0) {
      LogGuestTraceNow(ordinal == 1 ? "first read of a movie" : "fifth read of a movie");
    }
  }
  if (log_read) {
    XELOGI("[{}] NtReadFile({}, {} bytes{}) -> {:08X}, {} read (read {} of this file, "
           "{} bytes so far)", CurrentGuestThreadName(),
           file ? Joined(file->components, file->volume) : std::string("?"), length,
           offset_pointer ? fmt::format(" at {}", PPC_LOAD_U64(offset_pointer)) : "",
           status, bytes_read, ordinal, total);
  }
  ctx.r3.u64 = status;
}

// NTSTATUS NtReadFileScatter(HANDLE file, HANDLE event, PIO_APC_ROUTINE apc,
//                            PVOID apc_context, PIO_STATUS_BLOCK io_status,
//                            PFILE_SEGMENT_ELEMENT segments, ULONG length,
//                            PLARGE_INTEGER byte_offset)
//
// Like NtReadFile, into a list of 4 KiB pages: the segment array holds one guest
// address per page, terminated by a null.
PPC_FUNC(__imp__NtReadFileScatter) {
  const uint32_t event_handle = ctx.r4.u32;
  const uint32_t io_status = ctx.r7.u32;
  const uint32_t segments = ctx.r8.u32;
  const uint32_t length = ctx.r9.u32;
  const uint32_t offset_pointer = ctx.r10.u32;
  constexpr uint32_t kPage = 0x1000;

  auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile);
  uint32_t status = kStatusSuccess;
  uint32_t total = 0;
  if (!file) {
    status = kStatusInvalidHandle;
  } else if (!segments) {
    status = kStatusInvalidParameter;
  } else {
    std::optional<uint64_t> offset;
    if (offset_pointer) {
      const uint64_t value = PPC_LOAD_U64(offset_pointer);
      if (value != 0xFFFFFFFFFFFFFFFFull) {
        offset = value;
      }
    }
    uint32_t remaining = length;
    for (uint32_t i = 0; remaining && status == kStatusSuccess; ++i) {
      const uint32_t page = PPC_LOAD_U32(segments + 4 * i);
      if (!page) {
        break;
      }
      const uint32_t chunk = std::min(remaining, kPage);
      uint32_t got = 0;
      status = ReadInto(*file, base, page, chunk, offset, got);
      if (offset) {
        *offset += got;
      }
      total += got;
      remaining -= chunk;
      if (status == kStatusEndOfFile && total) {
        status = kStatusSuccess;  // a short read, not a failure
        break;
      }
    }
  }
  WriteIoStatus(base, io_status, status, total);
  if (event_handle) {
    if (auto event = LookupObject<GuestEvent>(event_handle, KernelObject::Type::kEvent)) {
      {
        std::lock_guard<std::mutex> lock(WaitMutex());
        event->signalled = true;
        NoteSignalStateChanged(event);
      }
      WaitCondition().notify_all();
    }
  }
  uint32_t ordinal;
  uint64_t file_total;
  if (ShouldLogRead(file.get(), total, ordinal, file_total)) {
    XELOGI("[{}] NtReadFileScatter({}, {} bytes) -> {:08X}, {} read (read {} of this file, "
           "{} bytes so far)", CurrentGuestThreadName(),
           file ? Joined(file->components, file->volume) : std::string("?"), length, status, total,
           ordinal, file_total);
  }
  ctx.r3.u64 = status;
}

// NTSTATUS NtWriteFile(HANDLE file, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
//                      PIO_STATUS_BLOCK io_status, PVOID buffer, ULONG length,
//                      PLARGE_INTEGER byte_offset)
//
// A content root's files take writes, synchronously, as reads are: at the offset, or at
// the file position when there is none or it is -1 or -2 (the title's read wrapper
// passes -1 for that, see NtReadFile); the event is set when done. The disc does not.
PPC_FUNC(__imp__NtWriteFile) {
  const uint32_t event_handle = ctx.r4.u32;
  const uint32_t io_status = ctx.r7.u32;
  const uint32_t buffer = ctx.r8.u32;
  const uint32_t length = ctx.r9.u32;
  const uint32_t offset_pointer = ctx.r10.u32;

  auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile);
  uint32_t status = kStatusSuccess;
  uint32_t written = 0;
  uint64_t at = 0;
  if (!file) {
    status = kStatusInvalidHandle;
  } else if (!file->writable) {
    status = kStatusMediaWriteProtected;
  } else if (!buffer && length) {
    status = kStatusInvalidParameter;
  } else {
    std::lock_guard<std::mutex> lock(file->mutex);
    if (file->host.directory) {
      status = kStatusFileIsADirectory;
    } else if (!EnsureOpen(*file)) {
      status = kStatusAccessDenied;
    } else {
      at = file->position;
      if (offset_pointer) {
        const uint64_t value = PPC_LOAD_U64(offset_pointer);
        if (value < 0xFFFFFFFFFFFFFFFEull) {
          at = value;
        }
      }
      const uint8_t* cursor = base + buffer;
      uint64_t remaining = length;
      uint64_t position = at;
      while (remaining) {
        const ssize_t put = pwrite(file->fd, cursor, size_t(remaining), off_t(position));
        if (put < 0 && errno == EINTR) {
          continue;
        }
        if (put <= 0) {
          status = errno == ENOSPC ? kStatusDiskFull : kStatusAccessDenied;
          break;
        }
        cursor += put;
        position += uint64_t(put);
        remaining -= uint64_t(put);
        written += uint32_t(put);
      }
      file->position = position;
      file->host.size = std::max(file->host.size, position);
    }
  }
  WriteIoStatus(base, io_status, status, written);
  if (event_handle) {
    if (auto event = LookupObject<GuestEvent>(event_handle, KernelObject::Type::kEvent)) {
      {
        std::lock_guard<std::mutex> lock(WaitMutex());
        event->signalled = true;
        NoteSignalStateChanged(event);
      }
      WaitCondition().notify_all();
    }
  }
  if (WARDEN_LOG_BUDGET(2000) || status != kStatusSuccess) {
    // The guest buffer too: where the bytes came from (BUG-049 - the save thumbnail's).
    XELOGI("[{}] NtWriteFile({}, {} bytes at {} from {:08X}) -> {:08X}, {} written",
           CurrentGuestThreadName(), file ? Joined(file->components, file->volume) : std::string("?"),
           length, at, buffer, status, written);
  }
  ctx.r3.u64 = status;
}

// NTSTATUS NtFlushBuffersFile(HANDLE file, PIO_STATUS_BLOCK io_status)
PPC_FUNC(__imp__NtFlushBuffersFile) {
  // A content root's file is synced to storage: a save that the title has flushed
  // should survive the app being killed.
  if (auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile)) {
    std::lock_guard<std::mutex> lock(file->mutex);
    if (file->writable && file->fd >= 0) {
      fsync(file->fd);
    }
  }
  WriteIoStatus(base, ctx.r4.u32, kStatusSuccess, 0);
  ctx.r3.u64 = kStatusSuccess;
}

// NTSTATUS NtQueryInformationFile(HANDLE file, PIO_STATUS_BLOCK io_status, PVOID info,
//                                 ULONG length, FILE_INFORMATION_CLASS class)
//
// The title's GetFileSize / SetFilePointer wrappers ask for position (14), network
// open (34) and standard information; the rest follow Xenia's xboxkrnl_io_info.cc.
PPC_FUNC(__imp__NtQueryInformationFile) {
  const uint32_t io_status = ctx.r4.u32;
  const uint32_t info = ctx.r5.u32;
  const uint32_t length = ctx.r6.u32;
  const uint32_t info_class = ctx.r7.u32;

  auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile);
  uint32_t status = kStatusSuccess;
  uint32_t written = 0;
  uint32_t needed = 0;
  switch (info_class) {
    case kFileInternalInformation:
    case kFilePositionInformation:
    case kFileAllocationInformation:
    case kFileEndOfFileInformation:
      needed = 8;
      break;
    case kFileModeInformation:
    case kFileAlignmentInformation:
    case kFileSectorInformation:
      needed = 4;
      break;
    case kFileBasicInformation:
      needed = 40;
      break;
    case kFileStandardInformation:
      needed = 24;
      break;
    case kFileNetworkOpenInformation:
      needed = 56;
      break;
    default:
      status = kStatusInvalidInfoClass;
      break;
  }
  if (status == kStatusSuccess && length < needed) {
    status = kStatusInfoLengthMismatch;
  } else if (status == kStatusSuccess && !file) {
    status = kStatusInvalidHandle;
  } else if (status == kStatusSuccess && !info) {
    status = kStatusInvalidParameter;
  }
  if (status == kStatusSuccess) {
    std::memset(base + info, 0, length);
    std::lock_guard<std::mutex> lock(file->mutex);
    switch (info_class) {
      case kFileInternalInformation:
        PPC_STORE_U64(info, uint64_t(std::hash<std::string>{}(file->host.path)));
        break;
      case kFilePositionInformation:
        PPC_STORE_U64(info, file->position);
        break;
      case kFileAllocationInformation:
        PPC_STORE_U64(info, RoundToSector(file->host.size));
        break;
      case kFileEndOfFileInformation:
        PPC_STORE_U64(info, file->host.size);
        break;
      case kFileModeInformation:
      case kFileAlignmentInformation:
      case kFileSectorInformation:
        PPC_STORE_U32(info, 0);
        break;
      case kFileBasicInformation:
        for (uint32_t time = 0; time < 32; time += 8) {
          PPC_STORE_U64(info + time, kDiscTime);
        }
        PPC_STORE_U32(info + 32,
                      file->host.directory ? kDirectoryAttributes : kFileAttributes);
        break;
      case kFileStandardInformation:
        // AllocationSize, EndOfFile, NumberOfLinks, DeletePending, Directory.
        PPC_STORE_U64(info, RoundToSector(file->host.size));
        PPC_STORE_U64(info + 8, file->host.size);
        PPC_STORE_U32(info + 16, 1);
        base[info + 20] = 0;
        base[info + 21] = file->host.directory ? 1 : 0;
        break;
      case kFileNetworkOpenInformation:
        WriteNetworkOpenInformation(base, info, file->host);
        break;
    }
    written = needed;
  }
  WriteIoStatus(base, io_status, status, written);
  ctx.r3.u64 = status;
}

namespace {

// FILE_RENAME_INFORMATION as the title passes it: { u32 ReplaceIfExists; u32
// RootDirectory; ANSI_STRING FileName } - a full path, or one relative to RootDirectory.
// Within the same content root only.
uint32_t RenameOnVolume(uint8_t* base, GuestFile& file, uint32_t info, uint32_t length) {
  if (length < 16 || !info) {
    return kStatusInfoLengthMismatch;
  }
  const bool replace = PPC_LOAD_U32(info) != 0;
  const uint32_t root_directory = PPC_LOAD_U32(info + 4);
  const std::string_view name = Trim(GuestAnsiString(base, info + 8));
  GuestLocation target;
  if (root_directory != 0 && root_directory != kDosDevices) {
    auto parent = LookupObject<GuestFile>(root_directory, KernelObject::Type::kFile);
    if (!parent) {
      return kStatusInvalidHandle;
    }
    target.volume = parent->volume;
    target.components = parent->components;
    if (!AppendComponents(name, target.components)) {
      return kStatusObjectNameInvalid;
    }
  } else if (auto parsed = ParseGuestPath(name)) {
    target = std::move(*parsed);
  } else {
    return kStatusObjectPathNotFound;
  }
  if (target.volume != file.volume || target.components.empty()) {
    return kStatusAccessDenied;
  }
  GuestLocation parent;
  parent.volume = target.volume;
  parent.components.assign(target.components.begin(), target.components.end() - 1);
  bool missing = false;
  auto folder = Resolve(parent, missing);
  if (!folder || !folder->directory) {
    return kStatusObjectPathNotFound;
  }
  bool exists_missing = false;
  auto existing = Resolve(target, exists_missing);
  std::lock_guard<std::mutex> lock(file.mutex);
  const std::string path = existing ? existing->path
                                    : folder->path + "/" + target.components.back();
  if (existing && existing->path != file.host.path && !replace) {
    return kStatusObjectNameCollision;
  }
  if (rename(file.host.path.c_str(), path.c_str()) != 0) {
    XELOGW("rename of {} to {} failed: {}", file.host.path, path, strerror(errno));
    return kStatusAccessDenied;
  }
  XELOGI("renamed {} to {}", file.host.path, path);
  file.host.path = path;
  file.components = target.components;
  return kStatusSuccess;
}

}  // namespace

// NTSTATUS NtSetInformationFile(HANDLE file, PIO_STATUS_BLOCK io_status, PVOID info,
//                               ULONG length, FILE_INFORMATION_CLASS class)
//
// Position (14) is a seek - the title's SetFilePointer. On a content root, also the end
// of file (20, a truncation or extension), deletion when closed (13), a rename (10), and
// times, attributes and the space to reserve (4, 19), which change nothing on the host.
// On the disc those are refused.
PPC_FUNC(__imp__NtSetInformationFile) {
  const uint32_t io_status = ctx.r4.u32;
  const uint32_t info = ctx.r5.u32;
  const uint32_t length = ctx.r6.u32;
  const uint32_t info_class = ctx.r7.u32;

  auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile);
  uint32_t status = kStatusSuccess;
  uint32_t written = 0;
  if (!file) {
    status = kStatusInvalidHandle;
  } else if (info_class == kFilePositionInformation) {
    if (length < 8 || !info) {
      status = kStatusInfoLengthMismatch;
    } else {
      std::lock_guard<std::mutex> lock(file->mutex);
      file->position = PPC_LOAD_U64(info);
      written = 8;
    }
  } else if ((info_class == kFileBasicInformation ||
              info_class == kFileAllocationInformation ||
              info_class == kFileEndOfFileInformation ||
              info_class == kFileDispositionInformation ||
              info_class == kFileRenameInformation) &&
             !file->writable) {
    status = kStatusMediaWriteProtected;
  } else if (info_class == kFileBasicInformation ||
             info_class == kFileAllocationInformation) {
    written = 0;
  } else if (info_class == kFileEndOfFileInformation) {
    if (length < 8 || !info) {
      status = kStatusInfoLengthMismatch;
    } else {
      std::lock_guard<std::mutex> lock(file->mutex);
      const uint64_t size = PPC_LOAD_U64(info);
      if (file->host.directory) {
        status = kStatusFileIsADirectory;
      } else if (!EnsureOpen(*file) || ftruncate(file->fd, off_t(size)) != 0) {
        status = kStatusAccessDenied;
      } else {
        file->host.size = size;
        written = 8;
      }
    }
  } else if (info_class == kFileDispositionInformation) {
    if (length < 1 || !info) {
      status = kStatusInfoLengthMismatch;
    } else {
      std::lock_guard<std::mutex> lock(file->mutex);
      file->delete_pending = base[info] != 0;
    }
  } else if (info_class == kFileRenameInformation) {
    status = RenameOnVolume(base, *file, info, length);
  } else {
    XELOGW("NtSetInformationFile: class {} is not handled", info_class);
    status = kStatusInvalidInfoClass;
  }
  WriteIoStatus(base, io_status, status, written);
  ctx.r3.u64 = status;
}

// NTSTATUS NtQueryFullAttributesFile(POBJECT_ATTRIBUTES attributes,
//                                    PFILE_NETWORK_OPEN_INFORMATION info)
//
// The title's GetFileAttributes: does the path exist, and what is it.
PPC_FUNC(__imp__NtQueryFullAttributesFile) {
  const uint32_t attributes = ctx.r3.u32;
  const uint32_t info = ctx.r4.u32;
  std::string_view name;
  GuestLocation location;
  uint32_t status = (!attributes || !info) ? kStatusInvalidParameter
                                           : ComponentsOf(base, attributes, name, location);
  if (status == kStatusSuccess) {
    bool parent_missing = false;
    auto entry = Resolve(location, parent_missing);
    if (!entry) {
      status = kStatusNoSuchFile;
    } else {
      WriteNetworkOpenInformation(base, info, *entry);
    }
  }
  if (WARDEN_LOG_BUDGET(300)) {
    XELOGI("[{}] NtQueryFullAttributesFile(\"{}\") -> {:08X}", CurrentGuestThreadName(), name,
           status);
  }
  ctx.r3.u64 = status;
}

// NTSTATUS NtQueryVolumeInformationFile(HANDLE file, PIO_STATUS_BLOCK io_status,
//                                       PVOID info, ULONG length, FS_INFORMATION_CLASS class)
//
// What the disc looks like as a volume: no label, 2 KiB sectors, read-only, named
// "CDROM" as Xenia's disc device is.
PPC_FUNC(__imp__NtQueryVolumeInformationFile) {
  const uint32_t io_status = ctx.r4.u32;
  const uint32_t info = ctx.r5.u32;
  const uint32_t length = ctx.r6.u32;
  const uint32_t info_class = ctx.r7.u32;

  auto file = LookupObject<GuestFile>(ctx.r3.u32, KernelObject::Type::kFile);
  uint32_t status = kStatusSuccess;
  uint32_t written = 0;
  if (!file) {
    status = kStatusInvalidHandle;
  } else if (!info) {
    status = kStatusInvalidParameter;
  } else {
    std::memset(base + info, 0, length);
    switch (info_class) {
      case kFileFsVolumeInformation:  // creation time, serial, label length, flag, label
        if (length < 18) {
          status = kStatusInfoLengthMismatch;
        } else {
          written = 18;
        }
        break;
      case kFileFsSizeInformation:
        if (length < 24) {
          status = kStatusInfoLengthMismatch;
        } else {
          // A content root: the 20 GB drive with 16 GB free XamContentGetDeviceData
          // reports. The disc: nothing available.
          PPC_STORE_U64(info, file->writable ? 0xA00000 : 0x1000000);
          PPC_STORE_U64(info + 8, file->writable ? 0x800000 : 0);
          PPC_STORE_U32(info + 16, 1);          // sectors per unit
          PPC_STORE_U32(info + 20, uint32_t(kSectorSize));
          written = 24;
        }
        break;
      case kFileFsDeviceInformation:
        if (length < 8) {
          status = kStatusInfoLengthMismatch;
        } else {
          // FILE_DEVICE_DISK for a content root; FILE_DEVICE_CD_ROM, removable and
          // read-only, for the disc.
          PPC_STORE_U32(info, file->writable ? 7 : 2);
          PPC_STORE_U32(info + 4, file->writable ? 0 : (0x1 | 0x2));
          written = 8;
        }
        break;
      case kFileFsAttributeInformation: {
        static constexpr char kName[] = "CDROM";
        const uint32_t name_length = sizeof(kName) - 1;
        if (length < 12) {
          status = kStatusInfoLengthMismatch;
        } else {
          PPC_STORE_U32(info, file->writable ? 0 : 0x00080000);  // FILE_READ_ONLY_VOLUME
          PPC_STORE_U32(info + 4, 255);         // longest component
          PPC_STORE_U32(info + 8, name_length);
          if (length >= 12 + name_length) {
            std::memcpy(base + info + 12, kName, name_length);
            written = 12 + name_length;
          } else {
            status = kStatusBufferOverflow;
            written = 12;
          }
        }
        break;
      }
      default:
        XELOGW("NtQueryVolumeInformationFile: class {} is not handled", info_class);
        status = kStatusInvalidInfoClass;
        break;
    }
  }
  WriteIoStatus(base, io_status, status, written);
  ctx.r3.u64 = status;
}

// NTSTATUS NtQueryDirectoryFile(HANDLE directory, HANDLE event, PIO_APC_ROUTINE apc,
//     PVOID apc_context, PIO_STATUS_BLOCK io_status, PVOID buffer, ULONG length,
//     PANSI_STRING file_name, BOOLEAN restart_scan)
//
// One entry per call, as Xenia does and as the title's FindFirstFile and FindNextFile
// expect: a call with a name restarts the scan with that pattern and returns the first
// match; a call without one continues from the last. `restart_scan` is the ninth
// argument, so it is on the stack at r1 + 0x54 - where Xenia's shim reads it and where
// the title's call stores it.
//
// FILE_DIRECTORY_INFORMATION, big-endian: NextEntryOffset, FileIndex, then the creation,
// last access, last write and change times and EndOfFile and AllocationSize as 64-bit
// values from +8, FileAttributes at +0x38, FileNameLength at +0x3C, the name at +0x40.
PPC_FUNC(__imp__NtQueryDirectoryFile) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t event = ctx.r4.u32;
  const uint32_t io_status = ctx.r7.u32;
  const uint32_t buffer = ctx.r8.u32;
  const uint32_t length = ctx.r9.u32;
  const std::string pattern(Trim(GuestAnsiString(base, ctx.r10.u32)));
  const bool restart = PPC_LOAD_U32(ctx.r1.u32 + 0x54) != 0;

  uint32_t status = kStatusSuccess;
  uint32_t written = 0;
  std::string found_name;
  bool log_this = true;
  constexpr int kListingLogEntries = 48;
  auto file = LookupObject<GuestFile>(handle, KernelObject::Type::kFile);
  if (length < 72) {
    status = kStatusInfoLengthMismatch;
  } else if (!file) {
    status = kStatusInvalidHandle;
  } else if (!file->host.directory || !buffer) {
    status = kStatusInvalidParameter;
  } else {
    std::lock_guard<std::mutex> lock(file->mutex);
    // A content root changes under the title's feet (it writes there), so a new scan
    // lists it again.
    if (!file->listed || (file->writable && (!pattern.empty() || restart))) {
      file->listing =
          ListDirectory(file->host.path, file->volume.empty() && file->components.empty(),
                        file->volume.empty() ? &file->components : nullptr);
      file->listed = true;
    }
    if (!pattern.empty()) {
      file->pattern = pattern;
      file->next = 0;
    } else if (restart) {
      file->next = 0;
    }
    const std::string& rule = file->pattern.empty() ? std::string("*") : file->pattern;
    size_t index = file->next;
    while (index < file->listing.size() && !Matches(rule, file->listing[index].name)) {
      ++index;
    }
    if (index >= file->listing.size()) {
      status = pattern.empty() ? kStatusNoMoreFiles : kStatusNoSuchFile;
    } else {
      const DirectoryEntry& entry = file->listing[index];
      const uint32_t needed = kDirectoryInfoHeader + uint32_t(entry.name.size());
      if (needed > length) {
        status = kStatusBufferOverflow;  // and the scan does not advance
      } else {
        std::memset(base + buffer, 0, kDirectoryInfoHeader);
        PPC_STORE_U32(buffer + 0x00, 0);                  // NextEntryOffset: the only one
        PPC_STORE_U32(buffer + 0x04, uint32_t(index + 1));
        for (uint32_t time = 0x08; time <= 0x20; time += 8) {
          PPC_STORE_U64(buffer + time, kDiscTime);
        }
        PPC_STORE_U64(buffer + 0x28, entry.size);
        PPC_STORE_U64(buffer + 0x30, RoundToSector(entry.size));
        PPC_STORE_U32(buffer + 0x38,
                      entry.directory ? kDirectoryAttributes : kFileAttributes);
        PPC_STORE_U32(buffer + 0x3C, uint32_t(entry.name.size()));
        std::memcpy(base + buffer + kDirectoryInfoHeader, entry.name.data(),
                    entry.name.size());
        file->next = index + 1;
        written = needed;
        found_name = entry.name;
      }
    }
    // The budget is per listing, not per call site: the voice-over directory has
    // hundreds of entries, and the listing after it is the one that matters. The end of
    // every listing is logged regardless, so a walk's extent stays visible.
    log_this = file->logged_entries++ < kListingLogEntries || status != kStatusSuccess;
  }

  WriteIoStatus(base, io_status, status, written);
  if (event) {
    if (auto signal = LookupObject<GuestEvent>(event, KernelObject::Type::kEvent)) {
      {
        std::lock_guard<std::mutex> lock(WaitMutex());
        signal->signalled = true;
        NoteSignalStateChanged(signal);
      }
      WaitCondition().notify_all();
    }
  }
  if (log_this && WARDEN_LOG_BUDGET(3000)) {
    XELOGI("[{}] NtQueryDirectoryFile({}, \"{}\"{}) -> {:08X}{}", CurrentGuestThreadName(),
           file ? Joined(file->components, file->volume) : std::string("?"), pattern,
           restart ? ", restart" : "", status,
           found_name.empty() ? "" : fmt::format(" \"{}\"", found_name));
  }
  ctx.r3.u64 = status;
}
