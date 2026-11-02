// PLAN 10.19: .7z and .rar archives unpacked, for the Mods page's imports - mods come as
// these as often as .zip (upstream unpacks them with Windows' tar.exe, which reads
// both). .7z through 7-Zip's C decoder (third_party/7zip/C, public domain), .rar (RAR 4
// and RAR 5) through libarchive's readers (third_party/libarchive, BSD 2-Clause; PLAN
// 11.3 - UnRAR's library, used first, carries a restriction the app's GPL-3.0 cannot
// take). Entries outside the folder (absolute, or with "..") are skipped, and an archive
// unpacking to more than a mod may hold is refused.

#include "dao_archive_android.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "xenia/base/logging.h"

extern "C" {
#include "7z.h"
#include "7zCrc.h"
#include "7zFile.h"
}

#include "archive.h"
#include "archive_entry.h"

namespace warden::archives {

namespace {

namespace fs = std::filesystem;

constexpr uint64_t kMaxUnpacked = 8ull << 30;

// UTF-16 or UTF-32 code units as UTF-8.
template <typename Unit>
std::string ToUtf8(const Unit* text, size_t length) {
  std::string out;
  for (size_t i = 0; i < length && text[i]; ++i) {
    uint32_t c = uint32_t(text[i]);
    if (sizeof(Unit) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < length &&
        uint32_t(text[i + 1]) >= 0xDC00 && uint32_t(text[i + 1]) <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00);
      ++i;
    } else if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
      c = 0xFFFD;
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// An entry's path, '/'-separated, if it stays inside the folder.
bool SafePath(std::string& path) {
  for (char& c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos) {
    return false;
  }
  size_t at = 0;
  while (at <= path.size()) {
    const size_t slash = path.find('/', at);
    const std::string part = path.substr(at, slash == std::string::npos ? std::string::npos
                                                                         : slash - at);
    if (part == "..") {
      return false;
    }
    if (slash == std::string::npos) {
      break;
    }
    at = slash + 1;
  }
  return true;
}

// ---- .7z ----

void* Alloc7z(ISzAllocPtr, size_t size) { return size ? std::malloc(size) : nullptr; }
void Free7z(ISzAllocPtr, void* address) { std::free(address); }
const ISzAlloc kAlloc7z = {Alloc7z, Free7z};

std::string Unpack7z(const std::string& archive, const fs::path& into) {
  CFileInStream stream;
  if (InFile_Open(&stream.file, archive.c_str()) != 0) {
    return "It could not be opened.";
  }
  FileInStream_CreateVTable(&stream);
  stream.wres = 0;
  CLookToRead2 look;
  LookToRead2_CreateVTable(&look, False);
  constexpr size_t kBuffer = 1 << 18;
  std::vector<Byte> buffer(kBuffer);
  look.buf = buffer.data();
  look.bufSize = kBuffer;
  look.realStream = &stream.vt;
  LookToRead2_INIT(&look)
  static const bool crc = (CrcGenerateTable(), true);
  (void)crc;
  CSzArEx db;
  SzArEx_Init(&db);
  std::string error;
  if (SzArEx_Open(&db, &look.vt, &kAlloc7z, &kAlloc7z) != SZ_OK) {
    error = "It is not a .7z archive this app can read.";
  }
  UInt32 block = 0xFFFFFFFF;
  Byte* out = nullptr;
  size_t out_size = 0;
  uint64_t total = 0;
  std::vector<UInt16> name;
  for (UInt32 i = 0; error.empty() && i < db.NumFiles; ++i) {
    const size_t length = SzArEx_GetFileNameUtf16(&db, i, nullptr);
    name.assign(length + 1, 0);
    SzArEx_GetFileNameUtf16(&db, i, name.data());
    std::string path = ToUtf8(name.data(), length);
    if (!SafePath(path)) {
      continue;
    }
    std::error_code ec;
    if (SzArEx_IsDir(&db, i)) {
      fs::create_directories(into / path, ec);
      continue;
    }
    total += SzArEx_GetFileSize(&db, i);
    if (total > kMaxUnpacked) {
      error = "It is larger than the space a mod may take.";
      break;
    }
    size_t offset = 0, size = 0;
    if (SzArEx_Extract(&db, &look.vt, i, &block, &out, &out_size, &offset, &size, &kAlloc7z,
                       &kAlloc7z) != SZ_OK) {
      error = "It is damaged, or uses a compression this app cannot read.";
      break;
    }
    const fs::path target = into / path;
    fs::create_directories(target.parent_path(), ec);
    FILE* file = std::fopen(target.c_str(), "wb");
    if (!file || std::fwrite(out + offset, 1, size, file) != size) {
      error = "It could not be written out.";
    }
    if (file) {
      std::fclose(file);
    }
  }
  ISzAlloc_Free(&kAlloc7z, out);
  SzArEx_Free(&db, &kAlloc7z);
  File_Close(&stream.file);
  return error;
}

// ---- .rar ----

std::string UnpackRar(const std::string& archive, const fs::path& into) {
  struct archive* reader = archive_read_new();
  archive_read_support_format_rar(reader);
  archive_read_support_format_rar5(reader);
  if (archive_read_open_filename(reader, archive.c_str(), 1 << 18) != ARCHIVE_OK) {
    archive_read_free(reader);
    return "It is not a .rar archive this app can read.";
  }
  std::string error;
  uint64_t total = 0;
  struct archive_entry* entry = nullptr;
  int read;
  std::vector<char> scratch;
  while (error.empty() && (read = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
    if (archive_entry_is_encrypted(entry)) {
      error = "It is protected by a password.";
      break;
    }
    const char* name = archive_entry_pathname_utf8(entry);
    std::string path = name ? name : (archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "");
    if (archive_entry_filetype(entry) != AE_IFREG || !SafePath(path)) {
      if (archive_entry_filetype(entry) == AE_IFDIR && SafePath(path)) {
        std::error_code ec;
        fs::create_directories(into / path, ec);
      }
      archive_read_data_skip(reader);
      continue;
    }
    const fs::path target = into / path;
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    FILE* file = std::fopen(target.c_str(), "wb");
    if (!file) {
      error = "It could not be written out.";
      break;
    }
    const void* block = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;
    int result;
    while ((result = archive_read_data_block(reader, &block, &size, &offset)) == ARCHIVE_OK) {
      total += size;
      if (total > kMaxUnpacked) {
        error = "It is larger than the space a mod may take.";
        break;
      }
      if (std::fseek(file, long(offset), SEEK_SET) != 0 ||
          std::fwrite(block, 1, size, file) != size) {
        error = "It could not be written out.";
        break;
      }
    }
    std::fclose(file);
    if (error.empty() && result != ARCHIVE_EOF) {
      const char* why = archive_error_string(reader);
      error = archive_read_has_encrypted_entries(reader) > 0
                  ? "It is protected by a password."
                  : std::string("It is damaged, or uses something this app cannot read (") +
                        (why ? why : "no reason given") + ").";
    }
  }
  if (error.empty() && read != ARCHIVE_EOF) {
    const char* why = archive_error_string(reader);
    error = archive_read_has_encrypted_entries(reader) > 0
                ? "It is protected by a password."
                : std::string("It is damaged (") + (why ? why : "no reason given") + ").";
  }
  archive_read_free(reader);
  return error;
}

}  // namespace

std::string Unpack(const std::string& archive, const std::string& into) {
  uint8_t magic[6] = {};
  if (FILE* file = std::fopen(archive.c_str(), "rb")) {
    std::fread(magic, 1, sizeof(magic), file);
    std::fclose(file);
  }
  std::error_code ec;
  fs::create_directories(into, ec);
  std::string error;
  if (std::memcmp(magic, "7z\xBC\xAF\x27\x1C", 6) == 0) {
    error = Unpack7z(archive, into);
  } else if (std::memcmp(magic, "Rar!", 4) == 0) {
    error = UnpackRar(archive, into);
  } else {
    error = "It is not a .7z or .rar archive.";
  }
  XELOGI("archives: {} unpacked into {}: {}", archive, into, error.empty() ? "done" : error);
  return error;
}

}  // namespace warden::archives
