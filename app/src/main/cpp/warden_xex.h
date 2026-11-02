// Loading default.xex into guest memory. Task 5.0e.
//
// The algorithm is XenonRecomp's (XenonUtils/xex.cpp), deliberately: that is the code
// that produced the recompilation, so an image assembled any other way could disagree
// with what the translated functions were written against. XenonRecomp's own loader is
// not compiled in because it pulls `Image`, `SymbolTable`, `std::set` and a SHA-1
// implementation along with it - host tooling that has no business in the APK - so this
// is the same steps over the same structures, checked against the same disc.
//
// Measured for this title (docs/ASSETS.md): AES-128-CBC encrypted, compression type 1,
// which is a raw block list and not LZX. So: decrypt the image key with the retail key,
// decrypt the body with it, then copy four blocks with zero-fill between them. No
// decompressor.
//
// Pure, and takes no guest memory: it fills a buffer. That keeps it testable on the
// host against the real disc image, which matters more here than saving one 19 MB copy
// at startup - the same reasoning as the XDVDFS reader taking a callable.
#ifndef WARDEN_XEX_H_
#define WARDEN_XEX_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace warden {

// One import record: the slot in the image the title reads the import through. A
// variable import's slot must hold the variable's address (warden_kernel_variables.cc);
// a function import's thunk is already bound by the recompiler and needs nothing.
struct XexImport {
  std::string library;   // "xboxkrnl.exe" or "xam.xex"
  uint32_t slot = 0;     // guest address of the 4-byte record
  uint32_t ordinal = 0;
  bool variable = false;  // record type 0 is a variable, 1 a function thunk
};

struct XexImage {
  bool loaded = false;
  uint32_t base = 0;         // where it belongs in guest memory
  uint32_t size = 0;         // assembled size, data + zero-fill
  uint32_t entry_point = 0;
  uint32_t stack_size = 0;
  // XEX_HEADER_TLS_INFO: KeTlsAlloc slots per thread, and the static TLS block (the
  // .tls section) each thread gets a copy of - its image address and sizes.
  uint32_t tls_slot_count = 0;
  uint32_t tls_raw_data_address = 0;
  uint32_t tls_data_size = 0;
  uint32_t tls_raw_data_size = 0;
  uint32_t encryption_type = 0;
  uint32_t compression_type = 0;
  uint32_t block_count = 0;
  uint32_t data_bytes = 0;   // from the blocks, before zero-fill
  uint32_t zero_bytes = 0;
  std::vector<XexImport> imports;  // every record, from XEX_HEADER_IMPORT_LIBRARIES
  std::string error;

  std::string Describe() const;
};

// Decrypts and assembles `file` into `out`, which is resized to the image size.
XexImage LoadXex(const uint8_t* file, size_t file_size, std::vector<uint8_t>& out);

// Convenience: reads the file from disk first.
XexImage LoadXexFile(const std::string& path, std::vector<uint8_t>& out);

}  // namespace warden

#endif  // WARDEN_XEX_H_
