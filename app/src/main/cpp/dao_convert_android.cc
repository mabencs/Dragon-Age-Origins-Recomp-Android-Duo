// PLAN 10.13: DAORecomp's PC-to-360 mod converter (mods/convert/), on Android. Upstream's
// sources compile as they are (gff, mesh, texture, game_files, ui/erf) or with the few
// edits tools/assemble_dao_convert.py makes (convert.cpp, mod_files.cpp, files.cpp: wide
// paths, Windows' file lock); this file stands in for the rest of upstream they reach:
// stb's zlib decoders (over zlib), unpacking a .dazip (a small zip reader, in place of
// Windows' tar.exe), a DAModder manifest as mod.toml, IsInAddinModule - and the call the
// app makes (NativeBridge.convertMod) with its report.
#include "stdafx.h"  // dao_compat: what upstream's sources have first
#include "dao_convert_android.h"

#include "dao_addin_android.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "mods/convert/convert.h"
#include "mods/mod_files.h"
#include "mods/mod_install.h"
#include "mods/mods.h"
#include "text.h"
#include "xenia/base/logging.h"

namespace fs = std::filesystem;

// ---- stb's zlib decoders, over zlib ----

namespace {
int Inflate(char* buffer, int len, const char* ibuffer, int ilen, int window_bits) {
  if (len < 0 || ilen < 0) {
    return -1;
  }
  z_stream stream = {};
  if (inflateInit2(&stream, window_bits) != Z_OK) {
    return -1;
  }
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(ibuffer));
  stream.avail_in = uInt(ilen);
  stream.next_out = reinterpret_cast<Bytef*>(buffer);
  stream.avail_out = uInt(len);
  const int result = inflate(&stream, Z_FINISH);
  const int written = int(stream.total_out);
  inflateEnd(&stream);
  return result == Z_STREAM_END || (result == Z_BUF_ERROR && stream.avail_out == 0) ? written
                                                                                    : -1;
}
}  // namespace

int stbi_zlib_decode_buffer(char* buffer, int len, const char* ibuffer, int ilen) {
  return Inflate(buffer, len, ibuffer, ilen, 15);
}

int stbi_zlib_decode_noheader_buffer(char* buffer, int len, const char* ibuffer, int ilen) {
  return Inflate(buffer, len, ibuffer, ilen, -15);
}

namespace dao::mods {

bool IsInAddinModule(const fs::path& relative) {
  std::vector<std::string> parts;
  for (const auto& part : relative) {
    parts.push_back(Lower(Utf8(part)));
  }
  for (size_t i = 0; i + 2 < parts.size(); ++i) {
    if (parts[i] == "addins" && parts[i + 2] == "module") {
      return true;
    }
  }
  return false;
}

// ---- a zip (a .dazip), unpacked ----

namespace {
uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
constexpr uint64_t kMaxUnpacked = 8ull << 30;
}  // namespace

bool UnpackArchive(const fs::path& archive, const fs::path& into, std::stop_token stop,
                   std::string& error) {
  std::ifstream file(archive, std::ios::binary);
  if (!file) {
    error = "it could not be opened";
    return false;
  }
  file.seekg(0, std::ios::end);
  const uint64_t size = uint64_t(file.tellg());
  // The end of the central directory: in the last 64 KiB and 22 bytes.
  const uint64_t tail = std::min<uint64_t>(size, 65557);
  std::vector<uint8_t> end(tail);
  file.seekg(std::streamoff(size - tail));
  file.read(reinterpret_cast<char*>(end.data()), std::streamsize(tail));
  int64_t eocd = -1;
  for (int64_t i = int64_t(tail) - 22; i >= 0; --i) {
    if (Le32(&end[size_t(i)]) == 0x06054B50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) {
    error = "it is not a zip archive";
    return false;
  }
  const uint32_t count = Le16(&end[size_t(eocd) + 10]);
  const uint32_t directory_size = Le32(&end[size_t(eocd) + 12]);
  const uint32_t directory_offset = Le32(&end[size_t(eocd) + 16]);
  if (uint64_t(directory_offset) + directory_size > size) {
    error = "its directory is damaged";
    return false;
  }
  std::vector<uint8_t> directory(directory_size);
  file.seekg(directory_offset);
  file.read(reinterpret_cast<char*>(directory.data()), directory_size);
  std::error_code ec;
  fs::create_directories(into, ec);
  uint64_t unpacked = 0;
  size_t at = 0;
  for (uint32_t n = 0; n < count && !stop.stop_requested(); ++n) {
    if (at + 46 > directory.size() || Le32(&directory[at]) != 0x02014B50) {
      error = "its directory is damaged";
      return false;
    }
    const uint16_t method = Le16(&directory[at + 10]);
    const uint32_t packed_size = Le32(&directory[at + 20]);
    const uint32_t full_size = Le32(&directory[at + 24]);
    const uint16_t name_length = Le16(&directory[at + 28]);
    const uint16_t extra_length = Le16(&directory[at + 30]);
    const uint16_t comment_length = Le16(&directory[at + 32]);
    const uint32_t local_offset = Le32(&directory[at + 42]);
    if (at + 46 + name_length > directory.size()) {
      error = "its directory is damaged";
      return false;
    }
    std::string name(reinterpret_cast<const char*>(&directory[at + 46]), name_length);
    at += 46 + name_length + extra_length + comment_length;
    for (char& c : name) {
      if (c == '\\') {
        c = '/';
      }
    }
    // Only what is inside: no absolute paths, no "..".
    if (name.empty() || name.front() == '/' || name.find("..") != std::string::npos) {
      continue;
    }
    const fs::path target = into / name;
    if (name.back() == '/') {
      fs::create_directories(target, ec);
      continue;
    }
    unpacked += full_size;
    if (unpacked > kMaxUnpacked) {
      error = "it unpacks to more than a mod could hold";
      return false;
    }
    uint8_t local[30];
    file.seekg(local_offset);
    if (!file.read(reinterpret_cast<char*>(local), 30) || Le32(local) != 0x04034B50) {
      error = "an entry is damaged";
      return false;
    }
    file.seekg(std::streamoff(local_offset) + 30 + Le16(local + 26) + Le16(local + 28));
    std::vector<uint8_t> packed(packed_size);
    if (!file.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed_size))) {
      error = "an entry is cut short";
      return false;
    }
    std::vector<uint8_t> data;
    if (method == 0) {
      data = std::move(packed);
    } else if (method == 8) {
      data.resize(full_size);
      if (full_size && Inflate(reinterpret_cast<char*>(data.data()), int(full_size),
                               reinterpret_cast<const char*>(packed.data()), int(packed_size),
                               -15) != int(full_size)) {
        error = "an entry could not be unpacked";
        return false;
      }
    } else {
      error = "it uses a compression this app cannot read";
      return false;
    }
    fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
  }
  return !stop.stop_requested();
}

// ---- a DAModder manifest as mod.toml ----

namespace {
std::string Attribute(const std::string& xml, const char* element, const char* attribute) {
  const size_t at = xml.find(element);
  if (at == std::string::npos) {
    return {};
  }
  const size_t close = xml.find('>', at);
  const std::string tag = xml.substr(at, close == std::string::npos ? std::string::npos
                                                                    : close - at);
  return std::string(XmlAttribute(tag, attribute));
}

std::string TomlString(std::string text) {
  std::string out;
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c == '\n' || c == '\r' ? ' ' : c;
  }
  return "\"" + out + "\"";
}
}  // namespace

void DescribeFromManifest(const fs::path& modFolder, const fs::path& manifestXml) {
  const auto data = ReadAll(manifestXml, 1 << 20);
  if (!data) {
    return;
  }
  const std::string xml(data->begin(), data->end());
  std::string name = Attribute(xml, "<Name", "Value");
  if (name.empty()) {
    name = Attribute(xml, "<LocalizedString", "DefaultText");
  }
  if (name.empty()) {
    name = Utf8(modFolder.filename());
  }
  std::ofstream out(modFolder / "mod.toml", std::ios::trunc);
  out << "name = " << TomlString(name) << "\n";
  if (const std::string v = Attribute(xml, "<Version", "Value"); !v.empty()) {
    out << "version = " << TomlString(v) << "\n";
  }
  if (const std::string a = Attribute(xml, "<Author", "Value"); !a.empty()) {
    out << "author = " << TomlString(a) << "\n";
  }
}

}  // namespace dao::mods

// ---- the app's call ----

namespace warden {

std::string ConvertMod(const std::string& mod_folder, const std::string& game_root,
                       const std::string& content_dir) {
  XELOGI("convert: {} (the game at {})", mod_folder, game_root);
  const fs::path mod(mod_folder);
  dao::mods::CleanUpConversions(mod.parent_path());
  // PLAN 10.19: its add-ins first, installed as downloadable content; then the rest.
  addins::Report added;
  addins::Install(mod, fs::path(game_root), fs::path(content_dir), added);
  dao::mods::ConvertResult result = dao::mods::ConvertPcMod(mod, fs::path(game_root));
  if (!added.installed.empty()) {
    // With its add-ins installed, a mod with nothing else to convert is done.
    std::erase(result.problems, std::string("It has no files in the PC version's formats."));
  }
  size_t files = result.files;
  for (const auto& installed : added.installed) {
    files += installed.files;
  }
  const bool converted = result.converted || !added.installed.empty();
  // One line each: what happened, then the lists, each line "<kind>\t<text>".
  std::string report = fmt::format("result\t{}\t{}\n", converted ? 1 : 0, files);
  for (const auto& installed : added.installed) {
    report += fmt::format("addin\t{}\t{}\t{}\n", installed.title, installed.package,
                          installed.files);
  }
  for (const auto* lines : {&added.problems, &result.problems}) {
    for (const auto& line : *lines) {
      report += "problem\t" + line + "\n";
    }
  }
  for (const auto* lines : {&added.left_out, &result.leftOut}) {
    for (const auto& line : *lines) {
      report += "left-out\t" + line + "\n";
    }
  }
  for (const auto* lines : {&added.notes, &result.notes}) {
    for (const auto& line : *lines) {
      report += "note\t" + line + "\n";
    }
  }
  XELOGI("convert: {} - {} files written, {} add-ins installed, {} problems, {} left out",
         mod_folder, files, added.installed.size(),
         added.problems.size() + result.problems.size(),
         added.left_out.size() + result.leftOut.size());
  return report;
}

}  // namespace warden
