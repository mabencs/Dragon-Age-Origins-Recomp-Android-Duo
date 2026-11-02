// DAORecomp's GPU host, as far as its XMA decoder uses it: the notice before a host
// write into guest memory, which unprotects the pages the GPU watches and invalidates its
// copy of them - warden_memory_host_write here (warden_memory.cc), as the file reads use.
// See stdafx.h in this folder.

#pragma once

#include <cstdint>

extern "C" void warden_memory_host_write(uint32_t guest_address, uint32_t length);

namespace dao::gpu {

inline void PrepareGuestWrite(uint32_t address, uint32_t length) {
  warden_memory_host_write(address, length);
}

}  // namespace dao::gpu
