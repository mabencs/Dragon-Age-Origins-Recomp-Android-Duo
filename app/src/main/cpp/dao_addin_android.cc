// PLAN 10.19: PC add-ins installed as the Xbox 360 version's downloadable content.
//
// A PC add-in (a .dazip: Manifest.xml and Contents/addins/<id>/{core,module}/...) is the
// same thing the 360's downloadable content is: each of the console's packages holds
// <id>/manifest.xml (the same manifest) and <id>/core/ and <id>/module/ with archives
// and talk tables, and the game lists files/content/00000002/, mounts each package and
// loads the add-ins in it (warden_xam.cc, PLAN 10.10). So an add-in is installed by
// converting its files and laying them out as a package there:
//  - its archives (PC ERF V2.0/V2.2) unpacked, each into a folder of its own;
//  - every file converted by DAORecomp's converter (ConvertPcMod over that staging
//    folder: GFF files - tables, items, plots, the module's .cif, talk tables - to the
//    360's byte order and layouts, .dds to .xds, meshes); compiled scripts (.ncs) are the
//    same on both, script sources (.nss) are left out;
//  - each archive packed again as ERF V2.0 (as the 360's scripts archives are), entries
//    sorted by name; talk tables also loose in data/talktables/, where the 360's are;
//  - the add-in's manifest entry written as <id>/manifest.xml, the package's display
//    name beside it (<package>.name, UTF-16 big-endian, as warden_dlc.cc writes).
// A save records the packages installed when it was made, and will not load without
// them: the Mods page says so before a mod with one is turned off or deleted.

#include "stdafx.h"  // dao_compat: what upstream's sources have first
#include "dao_addin_android.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#include "mods/convert/convert.h"
#include "mods/mod_files.h"
#include "mods/mod_install.h"
#include "mods/mods.h"
#include "text.h"
#include "ui/erf.h"
#include "xenia/base/logging.h"

namespace warden::addins {

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kMaxEntries = 65536;
constexpr uint32_t kMaxEntryBytes = 256u << 20;
// An ERF V2.0 entry's name: 32 UTF-16 code units, the last a terminator if shorter.
constexpr size_t kErfNameUnits = 32;
// Where add-ins are converted: under mods/, hidden (a dot) - not a mod.
constexpr char kStageFolder[] = ".addins";
constexpr size_t kDisplayNameBytes = 256;

struct Item {
  std::string uid;
  std::string title;
  std::string xml;  // the <AddInItem ...>...</AddInItem> element as written
};

struct Source {
  fs::path addin;    // .../addins/<uid>
  Item item;
  fs::path aside;    // what to set aside once installed (a .dazip, or the addin folder)
  fs::path package;  // a .dazip's Contents/packages, for the mod (or empty)
};

std::string ReadText(const fs::path& path) {
  const auto data = dao::mods::ReadAll(path, 4u << 20);
  if (!data) {
    return {};
  }
  // UTF-16 little-endian (with its mark) as the toolset may write it: ASCII only kept.
  if (data->size() >= 2 && (*data)[0] == 0xFF && (*data)[1] == 0xFE) {
    std::string out;
    for (size_t i = 2; i + 1 < data->size(); i += 2) {
      const uint16_t c = uint16_t((*data)[i] | (*data)[i + 1] << 8);
      out += c < 0x80 ? char(c) : '?';
    }
    return out;
  }
  size_t start = data->size() >= 3 && (*data)[0] == 0xEF && (*data)[1] == 0xBB ? 3 : 0;
  return std::string(data->begin() + start, data->end());
}

// The add-ins a manifest describes.
std::vector<Item> ReadManifest(const std::string& xml) {
  std::vector<Item> items;
  size_t at = 0;
  while ((at = xml.find("<AddInItem", at)) != std::string::npos) {
    const size_t tag_end = xml.find('>', at);
    size_t end = xml.find("</AddInItem>", at);
    if (tag_end == std::string::npos) {
      break;
    }
    end = end == std::string::npos ? tag_end + 1 : end + std::strlen("</AddInItem>");
    Item item;
    const std::string tag = xml.substr(at, tag_end - at);
    item.uid = std::string(dao::mods::XmlAttribute(tag, "UID"));
    item.xml = xml.substr(at, end - at);
    const size_t title = item.xml.find("<Title");
    if (title != std::string::npos) {
      const size_t title_end = item.xml.find('>', title);
      item.title = std::string(dao::mods::XmlAttribute(
          item.xml.substr(title, title_end == std::string::npos ? std::string::npos
                                                                 : title_end - title),
          "DefaultText"));
    }
    if (item.title.empty()) {
      item.title = std::string(dao::mods::XmlAttribute(tag, "Name"));
    }
    if (item.title.empty()) {
      item.title = item.uid;
    }
    if (!item.uid.empty()) {
      items.push_back(std::move(item));
    }
    at = end;
  }
  return items;
}

// A child folder of `dir` whose name is `name` in any case.
fs::path ChildFolder(const fs::path& dir, const std::string& name) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.is_directory(ec) && dao::Lower(dao::Utf8(entry.path().filename())) ==
                                      dao::Lower(name)) {
      return entry.path();
    }
  }
  return {};
}

bool WriteFile(const fs::path& path, const void* data, size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(static_cast<const char*>(data), std::streamsize(size));
  return bool(out.flush());
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(uint8_t(value >> (8 * i)));
  }
}

void PutName(std::vector<uint8_t>& out, const std::string& text) {
  for (size_t i = 0; i < kErfNameUnits; ++i) {
    const uint16_t c = i < text.size() ? uint8_t(text[i]) : 0;
    out.push_back(uint8_t(c));
    out.push_back(uint8_t(c >> 8));
  }
}

// An ERF V2.0 archive of `files` (name, path), sorted by name, as the 360's own: its
// header and names in UTF-16 little-endian, then each entry's offset and size.
bool WriteErf(const fs::path& path, std::vector<std::pair<std::string, fs::path>> files,
              std::string& error) {
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
    return dao::Lower(a.first) < dao::Lower(b.first);
  });
  std::vector<std::vector<uint8_t>> datas;
  for (const auto& [name, file] : files) {
    if (name.size() >= kErfNameUnits) {
      error = name + ": its name is too long for an archive";
      return false;
    }
    auto data = dao::mods::ReadAll(file, kMaxEntryBytes);
    if (!data) {
      error = name + ": it could not be read";
      return false;
    }
    datas.push_back(std::move(*data));
  }
  std::vector<uint8_t> out;
  const std::string magic = "ERF V2.0";
  for (char c : magic) {
    out.push_back(uint8_t(c));
    out.push_back(0);
  }
  const std::time_t now = std::time(nullptr);
  std::tm tm = {};
  gmtime_r(&now, &tm);
  Put32(out, uint32_t(files.size()));
  Put32(out, uint32_t(tm.tm_year));
  Put32(out, uint32_t(tm.tm_yday));
  Put32(out, 0xFFFFFFFFu);
  uint32_t offset = uint32_t(32 + files.size() * (2 * kErfNameUnits + 8));
  for (size_t i = 0; i < files.size(); ++i) {
    PutName(out, files[i].first);
    Put32(out, offset);
    Put32(out, uint32_t(datas[i].size()));
    offset += uint32_t(datas[i].size());
  }
  for (const auto& data : datas) {
    out.insert(out.end(), data.begin(), data.end());
  }
  if (!WriteFile(path, out.data(), out.size())) {
    error = "it could not be written";
    return false;
  }
  return true;
}

bool IsArchiveName(const fs::path& path) {
  const std::string ext = dao::Lower(dao::Utf8(path.extension()));
  return ext == ".erf" || ext == ".rim";
}

bool IsScriptSource(const std::string& name) { return dao::Lower(name).ends_with(".nss"); }

// Moves a file or folder to the same place under `to_root`.
void SetAside(const fs::path& path, const fs::path& mod, const fs::path& to_root) {
  std::error_code ec;
  const fs::path target = to_root / path.lexically_relative(mod);
  fs::create_directories(target.parent_path(), ec);
  dao::mods::RemoveTree(target);
  fs::rename(path, target, ec);
  if (ec) {
    XELOGW("add-ins: {} could not be set aside: {}", dao::Utf8(path), ec.message());
  }
}

// Merges a folder's files into another (replacing files of the same name).
void MergeInto(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(from, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    // An empty archive (a package's placeholder) is nothing to convert.
    if (dao::mods::IsPcArchive(dao::mods::ReadHead(it->path(), dao::mods::kFormatHeadBytes))) {
      auto opened = dao::ui::PcErfArchive::Open(it->path(), kMaxEntries);
      if (const auto* erf = std::get_if<dao::ui::PcErfArchive>(&opened);
          erf && erf->Entries().empty()) {
        continue;
      }
    }
    const fs::path target = to / it->path().lexically_relative(from);
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
  }
}

class Installer {
 public:
  Installer(fs::path mod, fs::path game, fs::path content, Report& report)
      : mod_(std::move(mod)), game_(std::move(game)), content_(std::move(content)),
        report_(report), work_(mod_.parent_path() / kStageFolder) {}

  ~Installer() { dao::mods::RemoveTree(work_); }

  void Run() {
    Find();
    if (sources_.empty()) {
      return;
    }
    std::set<fs::path> aside;
    std::set<std::string> done;
    for (const Source& source : sources_) {
      if (!done.insert(dao::Lower(source.item.uid)).second) {
        report_.notes.push_back("The add-in " + source.item.uid + " is in it twice: installed once.");
        continue;
      }
      if (InstallOne(source)) {
        aside.insert(source.aside);
        if (!source.package.empty()) {
          MergeInto(source.package, mod_ / "packages");
        }
      }
    }
    const fs::path originals = mod_.parent_path() / dao::mods::kOriginalsFolder /
                               mod_.filename();
    for (const fs::path& path : aside) {
      SetAside(path, mod_, originals);
    }
    WriteList();
  }

 private:
  // The add-ins: in each .dazip, and in each addins/<uid>/ folder that a manifest in
  // the mod names.
  void Find() {
    std::vector<fs::path> dazips, manifests, addins;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(mod_, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const std::string name = dao::Lower(dao::Utf8(it->path().filename()));
      if (it->is_directory(ec)) {
        if (name == "addins") {
          addins.push_back(it->path());
        }
      } else if (name.ends_with(".dazip")) {
        dazips.push_back(it->path());
      } else if (name == "manifest.xml") {
        manifests.push_back(it->path());
      }
    }
    for (const fs::path& dazip : dazips) {
      const fs::path into = work_ / ("dazip" + std::to_string(++unpacked_));
      std::string error;
      if (!dao::mods::UnpackArchive(dazip, into, {}, error)) {
        report_.left_out.push_back(dao::Utf8(dazip.lexically_relative(mod_)) + ": " + error);
        continue;
      }
      const fs::path manifest = into / "Manifest.xml";
      const fs::path contents = ChildFolder(into, "Contents");
      const fs::path addins_dir = contents.empty() ? fs::path() : ChildFolder(contents, "addins");
      const auto items = ReadManifest(ReadText(manifest));
      if (items.empty() || addins_dir.empty()) {
        continue;  // a package without add-ins: the converter's to unpack
      }
      if (!mod_has_toml_) {
        // Named as its add-in's title says (the Mods page shows it).
        std::string name;
        for (char c : items.front().title) {
          if (c != '"' && c != '\\' && uint8_t(c) >= 0x20) {
            name += c;
          }
        }
        std::ofstream(mod_ / "mod.toml") << "name = \"" << name << "\"\n";
        mod_has_toml_ = true;
      }
      const fs::path package = ChildFolder(contents, "packages");
      for (const Item& item : items) {
        const fs::path addin = ChildFolder(addins_dir, item.uid);
        if (addin.empty()) {
          report_.notes.push_back(item.title + ": its manifest names it, but its files are not"
                                  " in the package.");
          continue;
        }
        sources_.push_back({addin, item, dazip, package});
      }
    }
    std::vector<Item> loose_items;
    for (const fs::path& manifest : manifests) {
      for (Item& item : ReadManifest(ReadText(manifest))) {
        loose_items.push_back(std::move(item));
      }
    }
    for (const fs::path& dir : addins) {
      for (const Item& item : loose_items) {
        const fs::path addin = ChildFolder(dir, item.uid);
        if (!addin.empty()) {
          sources_.push_back({addin, item, addin, {}});
        }
      }
    }
  }

  bool InstallOne(const Source& source) {
    const Item& item = source.item;
    const std::string package = PackageName(item.uid);
    const fs::path stage = work_ / package;
    dao::mods::RemoveTree(stage);
    std::error_code ec;
    fs::create_directories(stage, ec);
    XELOGI("add-ins: {} ({}) from {}", item.title, item.uid, dao::Utf8(source.addin));
    // Its files staged: archives unpacked into <archive>.d/, script sources left out.
    std::vector<fs::path> archives;
    size_t sources = 0;
    for (auto it = fs::recursive_directory_iterator(source.addin, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) {
        continue;
      }
      const fs::path relative = it->path().lexically_relative(source.addin);
      const std::string name = dao::Utf8(relative.filename());
      if (IsScriptSource(name)) {
        ++sources;
        continue;
      }
      if (IsArchiveName(relative) &&
          dao::mods::IsPcArchive(dao::mods::ReadHead(it->path(), dao::mods::kFormatHeadBytes))) {
        if (!Unpack(it->path(), stage / (dao::Utf8(relative) + ".d"), item, sources)) {
          return false;
        }
        archives.push_back(relative);
        continue;
      }
      fs::create_directories((stage / relative).parent_path(), ec);
      fs::copy_file(it->path(), stage / relative, fs::copy_options::overwrite_existing, ec);
    }
    // Converted in place (the PC files set aside in .addins/.originals/, removed after).
    const dao::mods::ConvertResult converted = dao::mods::ConvertPcMod(stage, game_);
    for (const std::string& problem : converted.problems) {
      if (problem != "It has no files in the PC version's formats.") {
        report_.problems.push_back(item.title + ": " + problem);
        return false;
      }
    }
    for (const std::string& line : converted.leftOut) {
      // Without its module's description (.cif) an add-in cannot load: not installed.
      if (dao::Lower(line).find(".cif:") != std::string::npos) {
        report_.problems.push_back(item.title + ": " + line);
        return false;
      }
      report_.left_out.push_back(item.title + ": " + line);
    }
    for (const std::string& line : converted.notes) {
      report_.notes.push_back(item.title + ": " + line);
    }
    // Laid out as the console's packages are: <package>/<uid>/...
    const fs::path building = content_ / ("." + package);
    dao::mods::RemoveTree(building);
    const fs::path root = building / item.uid;
    fs::create_directories(root, ec);
    const std::string manifest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Manifest Type=\"AddIn\">\n\t<AddInsList>\n\t\t" + item.xml +
        "\n\t</AddInsList>\n</Manifest>\n";
    WriteFile(root / "manifest.xml", manifest.data(), manifest.size());
    size_t files = 1;
    std::set<fs::path> archive_dirs;
    for (const fs::path& relative : archives) {
      const fs::path dir = stage / (dao::Utf8(relative) + ".d");
      archive_dirs.insert(dir);
      std::vector<std::pair<std::string, fs::path>> entries;
      for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec)) {
          const std::string name = dao::Utf8(entry.path().filename());
          entries.emplace_back(name, entry.path());
          if (dao::Lower(name).ends_with(".tlk")) {
            // Talk tables also where the 360's add-ins keep theirs.
            const fs::path loose = root / relative.parent_path() / "talktables" / name;
            fs::create_directories(loose.parent_path(), ec);
            fs::copy_file(entry.path(), loose, fs::copy_options::overwrite_existing, ec);
            ++files;
          }
        }
      }
      std::string error;
      if (!WriteErf(root / relative, std::move(entries), error)) {
        report_.problems.push_back(item.title + ": " + dao::Utf8(relative) + ": " + error);
        dao::mods::RemoveTree(building);
        return false;
      }
      ++files;
    }
    for (auto it = fs::recursive_directory_iterator(stage, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_directory(ec) && archive_dirs.contains(it->path())) {
        it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(ec)) {
        continue;
      }
      const fs::path relative = it->path().lexically_relative(stage);
      fs::create_directories((root / relative).parent_path(), ec);
      fs::copy_file(it->path(), root / relative, fs::copy_options::overwrite_existing, ec);
      ++files;
    }
    // In place of any earlier install of it.
    const fs::path target = content_ / package;
    dao::mods::RemoveTree(target);
    fs::rename(building, target, ec);
    if (ec) {
      report_.problems.push_back(item.title + ": it could not be installed: " + ec.message());
      dao::mods::RemoveTree(building);
      return false;
    }
    uint8_t display[kDisplayNameBytes] = {};
    for (size_t i = 0; i < item.title.size() && i < kDisplayNameBytes / 2 - 1; ++i) {
      display[2 * i + 1] = uint8_t(item.title[i]);
    }
    WriteFile(content_ / (package + ".name"), display, sizeof(display));
    if (sources) {
      report_.notes.push_back(item.title + ": " + std::to_string(sources) +
                              " script sources (.nss) left out - the game runs the compiled"
                              " ones.");
    }
    XELOGI("add-ins: {} installed as {} ({} files)", item.title, dao::Utf8(target), files);
    report_.installed.push_back({package, item.title, files});
    return true;
  }

  bool Unpack(const fs::path& archive, const fs::path& into, const Item& item, size_t& sources) {
    auto opened = dao::ui::PcErfArchive::Open(archive, kMaxEntries);
    if (auto* error = std::get_if<std::string>(&opened)) {
      report_.problems.push_back(item.title + ": " + dao::Utf8(archive.filename()) + ": " +
                                 *error);
      return false;
    }
    const auto& erf = std::get<dao::ui::PcErfArchive>(opened);
    std::error_code ec;
    fs::create_directories(into, ec);
    for (const auto& entry : erf.Entries()) {
      const std::string name = dao::WideToUtf8(entry.name);
      if (name.empty() || name.find('/') != std::string::npos ||
          name.find('\\') != std::string::npos || name == "." || name == "..") {
        continue;
      }
      if (IsScriptSource(name)) {
        ++sources;
        continue;
      }
      const auto data = erf.Read(entry, kMaxEntryBytes);
      if (!data) {
        report_.left_out.push_back(item.title + ": " + dao::Utf8(archive.filename()) + "/" +
                                   name + ": damaged");
        continue;
      }
      WriteFile(into / name, data->data(), data->size());
    }
    return true;
  }

  // The mod's packages, for the Mods page (Mods.java): "<folder>\t<title>" each.
  void WriteList() {
    std::map<std::string, std::string> list;
    std::ifstream in(mod_ / "addins.txt");
    for (std::string line; std::getline(in, line);) {
      const size_t tab = line.find('\t');
      if (tab != std::string::npos) {
        list[line.substr(0, tab)] = line.substr(tab + 1);
      }
    }
    in.close();
    for (const Installed& installed : report_.installed) {
      list[installed.package] = installed.title;
    }
    if (list.empty()) {
      return;
    }
    std::ofstream out(mod_ / "addins.txt", std::ios::trunc);
    for (const auto& [package, title] : list) {
      out << package << '\t' << title << '\n';
    }
  }

  fs::path mod_, game_, content_;
  Report& report_;
  fs::path work_;
  std::vector<Source> sources_;
  int unpacked_ = 0;
  bool mod_has_toml_ = fs::exists(mod_ / "mod.toml");
};

}  // namespace

std::string PackageName(const std::string& uid) {
  std::string name = "mod-";
  for (char c : uid) {
    name += std::isalnum(uint8_t(c)) || c == '_' || c == '-' ? c : '_';
  }
  return name.substr(0, 42);
}

void Install(const fs::path& mod_folder, const fs::path& game_root, const fs::path& content_dir,
             Report& report) {
  std::error_code ec;
  fs::create_directories(content_dir, ec);
  Installer(mod_folder, game_root, content_dir, report).Run();
}

}  // namespace warden::addins
