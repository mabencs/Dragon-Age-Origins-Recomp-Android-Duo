#include "warden_xex.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <aes.h>
}

namespace warden {

namespace {

// From XenonRecomp's XenonUtils/xex.h. The retail XEX2 key, and the blank IV both
// decryptions use.
constexpr uint8_t kRetailKey[16] = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                                    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};
constexpr uint8_t kBlankIv[16] = {};

constexpr uint32_t kHeaderFileFormatInfo = 0x000003FF;
constexpr uint32_t kHeaderEntryPoint = 0x00010100;
constexpr uint32_t kHeaderTlsInfo = 0x00020104;  // slot count, raw data address, sizes
constexpr uint32_t kHeaderImageBase = 0x00010201;
constexpr uint32_t kHeaderDefaultStackSize = 0x00020200;

constexpr uint16_t kEncryptionNone = 0;
constexpr uint16_t kEncryptionNormal = 1;
constexpr uint16_t kCompressionNone = 0;
constexpr uint16_t kCompressionBasic = 1;

// XEX2 is big-endian throughout.
uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}
uint16_t Be16(const uint8_t* p) {
  return uint16_t((uint32_t(p[0]) << 8) | uint32_t(p[1]));
}

// Offsets into Xex2Header.
constexpr size_t kHeaderMagic = 0x00;
constexpr size_t kHeaderModuleFlags = 0x04;
constexpr size_t kHeaderHeaderSize = 0x08;
constexpr size_t kHeaderSecurityOffset = 0x10;
constexpr size_t kHeaderHeaderCount = 0x14;
constexpr size_t kOptionalHeaderTable = 0x18;

// Offsets within Xex2SecurityInfo, built up field by field rather than written as a
// total. The first attempt added these by hand and landed on 0x14C - four bytes early,
// which produced a wrong image key, which decrypted the body to noise. Noise is not an
// error: it looks exactly like code until something runs it, which is why
// tools/check_xex.py exists and why this is now spelled out.
constexpr size_t kSecurityHeaderSize = 0;                            // 4
constexpr size_t kSecurityImageSize = kSecurityHeaderSize + 4;       // 4
constexpr size_t kSecurityRsaSignature = kSecurityImageSize + 4;     // 0x100
constexpr size_t kSecurityUnknown = kSecurityRsaSignature + 0x100;   // 4
constexpr size_t kSecurityImageFlags = kSecurityUnknown + 4;         // 4
constexpr size_t kSecurityLoadAddress = kSecurityImageFlags + 4;     // 4
constexpr size_t kSecuritySectionDigest = kSecurityLoadAddress + 4;  // 0x14
constexpr size_t kSecurityImportCount = kSecuritySectionDigest + 0x14;   // 4
constexpr size_t kSecurityImportDigest = kSecurityImportCount + 4;      // 0x14
constexpr size_t kSecurityMediaId = kSecurityImportDigest + 0x14;       // 0x10
constexpr size_t kSecurityAesKey = kSecurityMediaId + 0x10;             // 0x10
static_assert(kSecurityAesKey == 0x150, "Xex2SecurityInfo layout changed");

// The low byte of a header key encodes how to read its value: 0 means the value field
// *is* the data, 1 means the value field is the data (one dword), anything else means
// the value is an offset from the start of the file. Getting this wrong is why the
// file format info looked absent on a first attempt - see docs/ASSETS.md.
const uint8_t* OptionalHeader(const uint8_t* file, size_t file_size, uint32_t key,
                              uint32_t* inline_value) {
  if (file_size < kOptionalHeaderTable) {
    return nullptr;
  }
  const uint32_t count = Be32(file + kHeaderHeaderCount);
  if (count > 256) {
    return nullptr;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const size_t entry = kOptionalHeaderTable + size_t(i) * 8;
    if (entry + 8 > file_size) {
      return nullptr;
    }
    if (Be32(file + entry) != key) {
      continue;
    }
    const uint32_t value = Be32(file + entry + 4);
    if ((key & 0xFF) <= 1) {
      if (inline_value) {
        *inline_value = value;
      }
      return file + entry + 4;
    }
    if (value >= file_size) {
      return nullptr;
    }
    return file + value;
  }
  return nullptr;
}

}  // namespace

std::string XexImage::Describe() const {
  if (!loaded) {
    return "not loaded: " + (error.empty() ? std::string("unknown") : error);
  }
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer),
                "base %08X size %u (%u data + %u zero in %u blocks), entry %08X, "
                "stack %u KB, enc %u comp %u",
                base, size, data_bytes, zero_bytes, block_count, entry_point,
                stack_size / 1024, encryption_type, compression_type);
  return buffer;
}

// XEX_HEADER_IMPORT_LIBRARIES: a string table of library names, then one block per
// library - its size, a 20-byte digest, id, version, minimum version, the name's
// index and the record count, then the records: guest addresses of 4-byte slots in
// the image. Each slot's image value is (type << 24) | ordinal, type 0 a variable and
// 1 a function thunk (Xenia's xex_module.cc). The value is read from the assembled
// basefile, since the slots live in the image, not the container.
void ReadImports(const uint8_t* file, size_t file_size, XexImage& image,
                 const std::vector<uint8_t>& basefile) {
  constexpr uint32_t kHeaderImportLibraries = 0x000103FF;
  const uint8_t* table = OptionalHeader(file, file_size, kHeaderImportLibraries, nullptr);
  if (!table) {
    return;
  }
  const uint8_t* end = file + file_size;
  if (table + 12 > end) {
    return;
  }
  const uint32_t string_table_size = Be32(table + 4);
  const uint32_t library_count = Be32(table + 8);
  const uint8_t* strings = table + 12;
  if (strings + string_table_size > end) {
    return;
  }
  std::vector<std::string> names;
  for (const uint8_t* p = strings; p < strings + string_table_size;) {
    const char* text = reinterpret_cast<const char*>(p);
    const size_t length = strnlen(text, size_t(strings + string_table_size - p));
    if (length) {
      names.emplace_back(text, length);
    }
    p += length + 1;
  }
  const uint8_t* library = strings + string_table_size;
  for (uint32_t i = 0; i < library_count && library + 40 <= end; ++i) {
    const uint32_t size = Be32(library);
    const uint16_t name_index = Be16(library + 36);
    const uint16_t count = Be16(library + 38);
    const uint8_t* records = library + 40;
    if (records + size_t(count) * 4 > end || size < 40) {
      break;
    }
    const std::string name = name_index < names.size() ? names[name_index] : "?";
    for (uint16_t r = 0; r < count; ++r) {
      XexImport import;
      import.library = name;
      import.slot = Be32(records + 4 * r);
      const uint32_t offset = import.slot - image.base;
      if (import.slot < image.base || offset + 4 > basefile.size()) {
        continue;
      }
      const uint32_t value = Be32(basefile.data() + offset);
      import.ordinal = value & 0xFFFF;
      import.variable = (value >> 24) == 0;
      image.imports.push_back(import);
    }
    library += size;
  }
}

XexImage LoadXex(const uint8_t* file, size_t file_size, std::vector<uint8_t>& out) {
  XexImage image;
  out.clear();

  if (file_size < 0x100 || std::memcmp(file + kHeaderMagic, "XEX2", 4) != 0) {
    image.error = "not an XEX2 file";
    return image;
  }
  const uint32_t header_size = Be32(file + kHeaderHeaderSize);
  const uint32_t security_offset = Be32(file + kHeaderSecurityOffset);
  if (header_size > file_size || security_offset + kSecurityAesKey + 16 > file_size) {
    image.error = "header or security info outside the file";
    return image;
  }
  const uint8_t* security = file + security_offset;

  uint32_t value = 0;
  if (OptionalHeader(file, file_size, kHeaderImageBase, &value)) {
    image.base = value;
  }
  if (OptionalHeader(file, file_size, kHeaderEntryPoint, &value)) {
    image.entry_point = value;
  }
  if (OptionalHeader(file, file_size, kHeaderDefaultStackSize, &value)) {
    image.stack_size = value;
  }
  // Thread-local storage: how many KeTlsAlloc slots a thread has, and the static
  // TLS block (the executable's .tls section) every thread starts with a copy of.
  // The title reaches both straight through r13 (warden_guest.cc).
  if (const uint8_t* tls = OptionalHeader(file, file_size, kHeaderTlsInfo, nullptr)) {
    image.tls_slot_count = Be32(tls + 0);
    image.tls_raw_data_address = Be32(tls + 4);
    image.tls_data_size = Be32(tls + 8);
    image.tls_raw_data_size = Be32(tls + 12);
  }

  const uint8_t* format = OptionalHeader(file, file_size, kHeaderFileFormatInfo, nullptr);
  if (!format) {
    image.error = "no file format info header";
    return image;
  }
  const uint32_t info_size = Be32(format);
  image.encryption_type = Be16(format + 4);
  image.compression_type = Be16(format + 6);

  if (image.compression_type != kCompressionNone &&
      image.compression_type != kCompressionBasic) {
    // Type 2 is LZX and type 3 is a delta patch. Neither is this title, and
    // implementing either on a guess would be worse than saying so.
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer),
                  "compression type %u is not supported (only none and basic)",
                  image.compression_type);
    image.error = buffer;
    return image;
  }

  // The body is everything after the headers.
  const size_t body_size = file_size - header_size;
  std::vector<uint8_t> body(body_size);
  std::memcpy(body.data(), file + header_size, body_size);

  if (image.encryption_type == kEncryptionNormal) {
    // The image key is itself encrypted, with the retail key.
    uint8_t image_key[16];
    std::memcpy(image_key, security + kSecurityAesKey, sizeof(image_key));
    AES_ctx context;
    AES_init_ctx_iv(&context, kRetailKey, kBlankIv);
    AES_CBC_decrypt_buffer(&context, image_key, sizeof(image_key));

    // CBC over the whole body. Note the body length must be a multiple of 16 for
    // this to be meaningful; XEX bodies are, and a file that is not would decrypt to
    // noise rather than fail, which the caller's checks are there to catch.
    if (body_size % 16 != 0) {
      image.error = "encrypted body is not a multiple of the AES block size";
      return image;
    }
    AES_init_ctx_iv(&context, image_key, kBlankIv);
    AES_CBC_decrypt_buffer(&context, body.data(), body_size);
  } else if (image.encryption_type != kEncryptionNone) {
    image.error = "unknown encryption type";
    return image;
  }

  if (image.compression_type == kCompressionNone) {
    image.size = Be32(security + kSecurityImageSize);
    if (image.size > body_size) {
      image.error = "image size larger than the file body";
      return image;
    }
    out.assign(body.begin(), body.begin() + image.size);
    image.data_bytes = image.size;
    image.block_count = 1;
    image.loaded = true;
    ReadImports(file, file_size, image, out);
    return image;
  }

  // Basic compression: a list of (dataSize, zeroSize) pairs. The block count comes
  // from infoSize the same way XenonRecomp derives it - the info holds one trailing
  // terminator block beyond the real ones.
  if (info_size < 8) {
    image.error = "file format info too small for a block list";
    return image;
  }
  const uint32_t block_count = (info_size / 8) - 1;
  const uint8_t* blocks = format + 8;

  uint64_t total = 0;
  for (uint32_t i = 0; i < block_count; ++i) {
    total += Be32(blocks + size_t(i) * 8) + Be32(blocks + size_t(i) * 8 + 4);
  }
  if (!total || total > 0x40000000ull) {
    image.error = "implausible assembled image size";
    return image;
  }

  out.assign(size_t(total), 0);
  size_t source = 0;
  size_t destination = 0;
  for (uint32_t i = 0; i < block_count; ++i) {
    const uint32_t data_size = Be32(blocks + size_t(i) * 8);
    const uint32_t zero_size = Be32(blocks + size_t(i) * 8 + 4);
    if (source + data_size > body_size || destination + data_size + zero_size > total) {
      image.error = "block list runs outside the file";
      out.clear();
      return image;
    }
    std::memcpy(out.data() + destination, body.data() + source, data_size);
    source += data_size;
    destination += data_size;
    // The gap is already zero from the assign above; advancing is enough.
    destination += zero_size;
    image.data_bytes += data_size;
    image.zero_bytes += zero_size;
  }

  image.block_count = block_count;
  image.size = uint32_t(total);
  image.loaded = true;
  ReadImports(file, file_size, image, out);
  return image;
}

XexImage LoadXexFile(const std::string& path, std::vector<uint8_t>& out) {
  XexImage image;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    image.error = "cannot open " + path;
    return image;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(file);
    image.error = "empty file: " + path;
    return image;
  }
  // Braces, not parentheses: `vector<uint8_t> contents(size_t(size))` is a function
  // declaration, not a variable - the most vexing parse.
  std::vector<uint8_t> contents(static_cast<size_t>(size), 0);
  const size_t read = std::fread(contents.data(), 1, contents.size(), file);
  std::fclose(file);
  if (read != contents.size()) {
    image.error = "short read on " + path;
    return image;
  }
  return LoadXex(contents.data(), contents.size(), out);
}

}  // namespace warden
