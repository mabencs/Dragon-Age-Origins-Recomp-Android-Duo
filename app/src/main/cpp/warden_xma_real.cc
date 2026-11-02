// warden_xma_real.h over DAORecomp's XmaRealDecoder. Compiled with dao_compat/ first on
// the include path (CMakeLists.txt, warden_xma_decoder), so DAORecomp's headers find
// stand-ins for its precompiled header, log, guest memory and GPU host, and its decoder
// sources compile unmodified.

#include "warden_xma_real.h"

#include "stdafx.h"

#include "apu/xma_decoder.h"
#include "kernel/memory.h"

// The guest memory DAORecomp's decoder translates addresses through: base + address.
namespace dao::kernel {
GuestMemory g_memory;
}  // namespace dao::kernel

namespace warden::xma {

class RealDecoder {
 public:
  dao::apu::XmaRealDecoder decoder;
};

RealDecoder* CreateRealDecoder(uint8_t* guest_base) {
  dao::kernel::g_memory.base = guest_base;
  auto* real = new RealDecoder();
  if (!real->decoder.Available()) {
    delete real;
    return nullptr;
  }
  return real;
}

void DestroyRealDecoder(RealDecoder* decoder) { delete decoder; }

void RunRealPass(RealDecoder* decoder, uint32_t context, uint32_t input0, uint32_t input1,
                 uint32_t output) {
  decoder->decoder.Work(context, dao::apu::XmaBuffers{{input0, input1}, output}, nullptr);
}

void ResetRealDecoder(RealDecoder* decoder) { decoder->decoder.Reset(); }

}  // namespace warden::xma
