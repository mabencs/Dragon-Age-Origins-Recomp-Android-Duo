// The real XMA decoder: DAORecomp's XmaRealDecoder (DAORecomp/apu/xma_decoder.cpp, a
// port of Xenia's XmaContextNew) over FFmpeg's xmaframes codec, behind a plain interface
// so that warden_xma.cc need not see DAORecomp's headers. warden_xma_real.cc includes
// those through dao_compat/, which stands in for the parts of DAORecomp's runtime they
// reach for.

#pragma once

#include <cstdint>

namespace warden::xma {

class RealDecoder;

// Null when FFmpeg has no xmaframes decoder or its state cannot be allocated; the caller
// then falls back to the silent pass. `guest_base` is the flat guest memory base.
RealDecoder* CreateRealDecoder(uint8_t* guest_base);
void DestroyRealDecoder(RealDecoder* decoder);

// One pass over the context at guest `context`, as XmaContextNew::Work: input packets
// consumed, decoded PCM written to the output ring, the context's offsets, valid bits
// and loop state updated. The buffer addresses are the ones the title passed to
// XMAInitializeContext. The caller holds the context's lock.
void RunRealPass(RealDecoder* decoder, uint32_t context, uint32_t input0, uint32_t input1,
                 uint32_t output);

// A new sound: the host-side decode state starts over, as XMAInitializeContext clears the
// context.
void ResetRealDecoder(RealDecoder* decoder);

}  // namespace warden::xma
