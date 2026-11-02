// DAORecomp's guest memory, as far as its XMA decoder uses it: Translate() is base +
// address, the flat guest base this runtime shares with the recompiled code and the GPU
// (warden_memory.cc). warden_xma.cc defines g_memory and sets its base before the first
// decoder pass. See stdafx.h in this folder.

#pragma once

#include <cstdint>

namespace dao::kernel {

struct GuestMemory {
  uint8_t* base = nullptr;

  template <typename T = void>
  T* Translate(uint32_t address) const noexcept {
    return reinterpret_cast<T*>(base + address);
  }
};

extern GuestMemory g_memory;

}  // namespace dao::kernel
