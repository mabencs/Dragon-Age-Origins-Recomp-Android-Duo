// The XMA decoder contexts. BUG-039, task 4.5.
//
// The title's audio engine (FMOD over XAudio) decodes its sound banks on the console's
// XMA hardware: it allocates decoder contexts, points them at input packets and an
// output ring, and enables them; the hardware consumes packets and fills the ring.
// With XMACreateContext a stub, FMOD's start-up failed and it unregistered the render
// callback 43 ms after registering it - and with it went the audio clock the intro
// movies wait on.
//
// This is DAORecomp's design (DAORecomp/apu/audio.cpp, xma_layout.h): a pool of
// contexts in physical memory, the 16 exports reading and writing the context's bit
// fields, and a worker that runs a pass over each enabled context - taking input
// packets, writing output blocks, keeping the read and write offsets, the valid bits and
// the loop state as the hardware would. The pass is DAORecomp's real decoder, a port of
// Xenia's XmaContextNew over FFmpeg's xmaframes codec (warden_xma_real.h); if FFmpeg
// has no such decoder, the silent pass below - Xenia's XmaContextFake - stands in.
//
// Context layout: 16 big-endian dwords (Xenia's XMA_CONTEXT_DATA). Each field is a bit
// range in one dword, counted from bit 0 of the byte-swapped value.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "ppc_recomp_shared.h"
#include "warden_guest.h"
#include "warden_guest_heap.h"
#include "warden_suspend.h"
#include "warden_threads.h"
#include "warden_xma_real.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

namespace {

// ------------------------------------------------------------------ the layout

struct Field {
  uint32_t dword;
  uint32_t shift;
  uint32_t width;
};

// DWORD 0
constexpr Field kInputBuffer0PacketCount{0, 0, 12};
constexpr Field kLoopCount{0, 12, 8};
constexpr Field kInputBuffer0Valid{0, 20, 1};
constexpr Field kInputBuffer1Valid{0, 21, 1};
constexpr Field kOutputBufferBlockCount{0, 22, 5};
constexpr Field kOutputBufferWriteOffset{0, 27, 5};
// DWORD 1
constexpr Field kInputBuffer1PacketCount{1, 0, 12};
constexpr Field kLoopSubframeEnd{1, 14, 3};
constexpr Field kLoopSubframeSkip{1, 17, 3};
constexpr Field kSubframeDecodeCount{1, 20, 4};
constexpr Field kSampleRate{1, 27, 2};
constexpr Field kIsStereo{1, 29, 1};
constexpr Field kOutputBufferValid{1, 31, 1};
// DWORD 2
constexpr Field kInputBufferReadOffset{2, 0, 26};
constexpr Field kErrorStatus{2, 26, 5};
// DWORD 3, 4
constexpr Field kLoopStart{3, 0, 26};
constexpr Field kLoopEnd{4, 0, 26};
constexpr Field kCurrentBuffer{4, 31, 1};
// DWORD 9
constexpr Field kOutputBufferReadOffset{9, 0, 5};
// Buffer addresses, physical as Xenia stores them.
constexpr uint32_t kInputBuffer0PtrDword = 5;
constexpr uint32_t kInputBuffer1PtrDword = 6;
constexpr uint32_t kOutputBufferPtrDword = 7;

constexpr uint32_t kContextCount = 320;
constexpr uint32_t kContextSize = 64;
constexpr uint32_t kBytesPerPacket = 2048;
constexpr uint32_t kBitsPerPacket = kBytesPerPacket * 8;
constexpr uint32_t kBitsPerPacketHeader = 32;
constexpr uint32_t kBytesPerBlock = 256;  // the output granule the context counts in

uint32_t LoadWord(const uint8_t* host) {
  uint32_t value;
  std::memcpy(&value, host, sizeof(value));
  return __builtin_bswap32(value);
}

void StoreWord(uint8_t* host, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(host, &value, sizeof(value));
}

uint32_t GetBits(const uint8_t* context, Field field) {
  const uint32_t mask = (field.width == 32) ? ~0u : ((1u << field.width) - 1);
  return (LoadWord(context + 4 * field.dword) >> field.shift) & mask;
}

void SetBits(uint8_t* context, Field field, uint32_t value) {
  const uint32_t mask = ((field.width == 32) ? ~0u : ((1u << field.width) - 1)) << field.shift;
  uint8_t* word = context + 4 * field.dword;
  StoreWord(word, (LoadWord(word) & ~mask) | ((value << field.shift) & mask));
}

// ------------------------------------------------------------------ the pool

struct Slot {
  std::mutex mutex;
  bool allocated = false;            // under mutex
  std::atomic<bool> enabled{false};  // set by XMAEnableContext, cleared when a pass starts
  uint32_t input_guest[2] = {0, 0};  // the guest addresses the title gave
  uint32_t output_guest = 0;
  uint32_t pending_blocks = 0;       // silent blocks staged from the last packet (fake pass)
  // The real decoder's state, created on the context's first pass (under mutex).
  warden::xma::RealDecoder* real = nullptr;
  bool real_tried = false;
};

std::array<Slot, kContextCount> g_slots;
std::atomic<uint32_t> g_pool{0};  // guest address of context 0

std::mutex g_kick_mutex;
std::condition_variable g_kick;
bool g_kicked = false;

std::atomic<uint64_t> g_passes{0};
// Calls of each import since the last report: what the title's audio engine does
// with the decoder, and whether it is polling for progress that never comes.
constexpr const char* kCallNames[] = {
    "Create", "Release", "Init", "Enable", "Disable", "SetIn0", "IsIn0", "SetIn1", "IsIn1",
    "SetReadOff", "GetReadOff", "SetOutValid", "IsOutValid", "SetOutRead", "GetOutRead",
    "GetOutWrite"};
std::atomic<uint32_t> g_calls[16];
enum Call { kCreate, kRelease, kInit, kEnable, kDisable, kSetIn0, kIsIn0, kSetIn1, kIsIn1,
            kSetReadOff, kGetReadOff, kSetOutValid, kIsOutValid, kSetOutRead, kGetOutRead,
            kGetOutWrite };
void Count(Call call) { g_calls[call].fetch_add(1, std::memory_order_relaxed); }
std::atomic<uint64_t> g_packets{0};

uint8_t* Base() { return warden::GuestMemoryBase(); }

int SlotIndex(uint32_t address) {
  const uint32_t pool = g_pool.load(std::memory_order_acquire);
  if (!pool || address < pool) {
    return -1;
  }
  const uint32_t offset = address - pool;
  if (offset % kContextSize || offset / kContextSize >= kContextCount) {
    return -1;
  }
  return int(offset / kContextSize);
}

uint32_t SlotAddress(uint32_t index) {
  return g_pool.load(std::memory_order_acquire) + index * kContextSize;
}

uint32_t ToPhysical(uint32_t guest) {
  if (!guest) {
    return 0;
  }
  xe::Memory* memory = warden::GuestMemory();
  const uint32_t physical = memory ? memory->GetPhysicalAddress(guest) : UINT32_MAX;
  return physical == UINT32_MAX ? guest : physical;
}

bool EnsurePool() {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  if (g_pool.load(std::memory_order_acquire)) {
    return true;
  }
  uint32_t base = 0;
  uint32_t size = 0;
  if (warden::AllocatePhysicalMemory(kContextCount * kContextSize, 0x1000, base, size) != 0 ||
      !base) {
    XELOGE("XMA: no physical memory for the {}-context pool", kContextCount);
    return false;
  }
  std::memset(Base() + base, 0, size);
  g_pool.store(base, std::memory_order_release);
  XELOGI("XMA: context pool of {} at {:08X}; the decoder is FFmpeg's xmaframes", kContextCount,
         base);
  return true;
}

// ------------------------------------------------------------------ the pass

struct OutputRing {
  uint8_t* host = nullptr;
  uint32_t capacity = 0;
  uint32_t read = 0;
  uint32_t write = 0;
  bool Empty() const { return read == write; }
  uint32_t WriteCount() const {
    if (read == write) {
      return capacity;
    }
    return write < read ? read - write : capacity - write + read;
  }
  void WriteSilence(uint32_t count) {
    count = std::min(count, capacity);
    const uint32_t first = std::min(count, capacity - write);
    std::memset(host + write, 0, first);
    std::memset(host, 0, count - first);
    write = (write + count) % capacity;
  }
};

bool AnyInputValid(const uint8_t* context) {
  return GetBits(context, kInputBuffer0Valid) || GetBits(context, kInputBuffer1Valid);
}

void SwapInputBuffer(uint8_t* context) {
  if (GetBits(context, kCurrentBuffer) == 0) {
    SetBits(context, kInputBuffer0Valid, 0);
  } else {
    SetBits(context, kInputBuffer1Valid, 0);
  }
  SetBits(context, kCurrentBuffer, GetBits(context, kCurrentBuffer) ^ 1);
  SetBits(context, kInputBufferReadOffset, kBitsPerPacketHeader);
}

void UpdateLoop(uint8_t* context) {
  const uint32_t loop_count = GetBits(context, kLoopCount);
  if (!loop_count) {
    return;
  }
  const uint32_t start = std::max(kBitsPerPacketHeader, GetBits(context, kLoopStart));
  const uint32_t end = std::max(kBitsPerPacketHeader, GetBits(context, kLoopEnd));
  if (GetBits(context, kInputBufferReadOffset) != end) {
    return;
  }
  SetBits(context, kInputBufferReadOffset, start);
  if (loop_count != 255) {
    SetBits(context, kLoopCount, loop_count - 1);
  }
}

// Takes the next packet of the current input buffer: 4 silent blocks for mono, 8 for
// stereo, the fixed count Xenia's fake context uses.
void TakePacket(uint8_t* context, Slot& slot) {
  if (!AnyInputValid(context) || slot.pending_blocks) {
    return;
  }
  UpdateLoop(context);
  const uint32_t current = GetBits(context, kCurrentBuffer);
  const uint32_t packet_count =
      GetBits(context, current ? kInputBuffer1PacketCount : kInputBuffer0PacketCount);
  const uint32_t index = GetBits(context, kInputBufferReadOffset) / kBitsPerPacket;
  if (index >= packet_count) {
    SwapInputBuffer(context);
    return;
  }
  const uint32_t next = index + 1;
  if (next >= packet_count) {
    SwapInputBuffer(context);
  } else {
    SetBits(context, kInputBufferReadOffset, next * kBitsPerPacket + kBitsPerPacketHeader);
  }
  slot.pending_blocks = GetBits(context, kIsStereo) ? 8 : 4;
  g_packets.fetch_add(1, std::memory_order_relaxed);
}

// One pass over a context, as Xenia's XmaContextFake::Work. Caller holds slot.mutex.
void RunPass(uint32_t address, Slot& slot) {
  uint8_t* base = Base();
  uint8_t* context = base + address;
  if (!GetBits(context, kOutputBufferValid) || !slot.output_guest) {
    return;
  }
  const uint32_t subframes = GetBits(context, kSubframeDecodeCount);
  const uint32_t block_count = GetBits(context, kOutputBufferBlockCount);
  if (!subframes || !block_count) {
    return;  // Xenia spins forever here; nothing can be produced
  }
  OutputRing ring;
  ring.host = base + slot.output_guest;
  ring.capacity = block_count * kBytesPerBlock;
  ring.read = (GetBits(context, kOutputBufferReadOffset) * kBytesPerBlock) % ring.capacity;
  ring.write = (GetBits(context, kOutputBufferWriteOffset) * kBytesPerBlock) % ring.capacity;
  int32_t free_blocks = int32_t(ring.WriteCount() / kBytesPerBlock);
  const int32_t minimum = int32_t(subframes) * 2 - 1;
  if (minimum > free_blocks) {
    return;
  }
  while (free_blocks >= minimum) {
    TakePacket(context, slot);
    const uint32_t count = std::min(slot.pending_blocks, subframes);
    if (count) {
      ring.WriteSilence(count * kBytesPerBlock);
      slot.pending_blocks -= count;
      free_blocks -= int32_t(count);
    }
    if (!AnyInputValid(context) || GetBits(context, kErrorStatus) == 4) {
      break;
    }
  }
  SetBits(context, kOutputBufferWriteOffset, ring.write / kBytesPerBlock);
  if (ring.Empty()) {
    SetBits(context, kOutputBufferValid, 0);
  }
  g_passes.fetch_add(1, std::memory_order_relaxed);
}

// The real decoder's pass when FFmpeg has one, the silent pass otherwise - as DAORecomp's
// RunDecodePass. Caller holds slot.mutex.
void RunDecodePass(uint32_t address, Slot& slot) {
  if (!slot.real && !slot.real_tried) {
    slot.real_tried = true;
    slot.real = warden::xma::CreateRealDecoder(Base());
    if (!slot.real) {
      static std::once_flag said;
      std::call_once(said, [] {
        XELOGW("XMA: no xmaframes decoder in FFmpeg; the title's sound stays silent");
      });
    }
  }
  if (slot.real) {
    warden::xma::RunRealPass(slot.real, address, slot.input_guest[0], slot.input_guest[1],
                             slot.output_guest);
    g_passes.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  RunPass(address, slot);
}

// A context taken, given back or given a new sound: the decode state starts over, as
// DAORecomp's ResetSlotDecoders. Caller holds slot.mutex.
void ResetDecoder(Slot& slot) {
  if (slot.real) {
    warden::xma::ResetRealDecoder(slot.real);
  }
}

bool RunIfEnabled(uint32_t index) {
  Slot& slot = g_slots[index];
  if (!slot.enabled.load(std::memory_order_acquire)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(slot.mutex);
  if (!slot.allocated || !slot.enabled.load(std::memory_order_acquire)) {
    return false;
  }
  slot.enabled.store(false, std::memory_order_release);
  RunDecodePass(SlotAddress(index), slot);
  return true;
}

// The decoder runs no guest code; it only reads and writes guest memory.
void DecoderMain() {
  auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    bool worked = false;
    for (uint32_t i = 0; i < kContextCount; ++i) {
      worked |= RunIfEnabled(i);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_report) {
      next_report = now + std::chrono::seconds(5);
      uint32_t allocated = 0;
      for (Slot& slot : g_slots) {
        std::lock_guard<std::mutex> lock(slot.mutex);
        allocated += slot.allocated ? 1 : 0;
      }
      std::string calls;
      for (int i = 0; i < 16; ++i) {
        const uint32_t n = g_calls[i].exchange(0);
        if (n) {
          calls += fmt::format(" {} {}", kCallNames[i], n);
        }
      }
      // g_packets counts the silent pass's packets only: the real decoder keeps its own.
      XELOGI("XMA: {} contexts allocated, {} passes ({} packets through the silent pass); "
             "calls in the last 5 s:{}", allocated, g_passes.load(), g_packets.load(),
             calls.empty() ? " none" : calls.c_str());
      // Each allocated context's state, when few enough to read.
      if (allocated && allocated <= 12) {
        std::string states;
        for (uint32_t i = 0; i < kContextCount; ++i) {
          Slot& slot = g_slots[i];
          std::lock_guard<std::mutex> lock(slot.mutex);
          if (!slot.allocated) {
            continue;
          }
          const uint8_t* c = Base() + SlotAddress(i);
          states += fmt::format(
              "\n  ctx {}: in0 valid {} ({} pk) in1 valid {} ({} pk) current {} read {} | out valid {} "
              "blocks {} read {} write {} | subframes {} stereo {} loop {} | enabled {} pending {}",
              i, GetBits(c, kInputBuffer0Valid), GetBits(c, kInputBuffer0PacketCount),
              GetBits(c, kInputBuffer1Valid), GetBits(c, kInputBuffer1PacketCount),
              GetBits(c, kCurrentBuffer), GetBits(c, kInputBufferReadOffset),
              GetBits(c, kOutputBufferValid), GetBits(c, kOutputBufferBlockCount),
              GetBits(c, kOutputBufferReadOffset), GetBits(c, kOutputBufferWriteOffset),
              GetBits(c, kSubframeDecodeCount), GetBits(c, kIsStereo), GetBits(c, kLoopCount),
              slot.enabled.load() ? 1 : 0, slot.pending_blocks);
        }
        XELOGI("XMA contexts:{}", states);
      }
    }
    if (worked) {
      continue;
    }
    // BUG-119: every 2 ms while the title runs; with it suspended (put away, the lid), only
    // a kick or a tenth of a second wakes the decoder - nothing enables a context then.
    std::unique_lock<std::mutex> lock(g_kick_mutex);
    g_kick.wait_for(lock, std::chrono::milliseconds(warden::GuestSuspended() ? 100 : 2),
                    [] { return g_kicked; });
    g_kicked = false;
  }
}

void Kick() {
  static std::once_flag once;
  std::call_once(once, [] { std::thread(DecoderMain).detach(); });
  {
    std::lock_guard<std::mutex> lock(g_kick_mutex);
    g_kicked = true;
  }
  g_kick.notify_one();
}

// Runs fn on the context's bytes with its slot locked (when it is a pool context).
template <typename Fn>
void WithContext(uint32_t address, Fn&& fn) {
  if (!address) {
    return;
  }
  const int index = SlotIndex(address);
  std::unique_lock<std::mutex> lock;
  if (index >= 0) {
    lock = std::unique_lock<std::mutex>(g_slots[index].mutex);
  }
  fn(Base() + address, index >= 0 ? &g_slots[index] : nullptr);
}

uint32_t ReadField(uint32_t address, Field field) {
  uint32_t value = 0;
  WithContext(address, [&](uint8_t* context, Slot*) { value = GetBits(context, field); });
  return value;
}

void WriteField(uint32_t address, Field field, uint32_t value) {
  WithContext(address, [&](uint8_t* context, Slot*) { SetBits(context, field, value); });
}

}  // namespace

// NTSTATUS XMACreateContext(void** context)
PPC_FUNC(__imp__XMACreateContext) {
  Count(kCreate);
  const uint32_t out = ctx.r3.u32;
  uint32_t address = 0;
  if (EnsurePool()) {
    for (uint32_t i = 0; i < kContextCount && !address; ++i) {
      Slot& slot = g_slots[i];
      std::lock_guard<std::mutex> lock(slot.mutex);
      if (slot.allocated) {
        continue;
      }
      slot.allocated = true;
      slot.enabled.store(false, std::memory_order_release);
      slot.pending_blocks = 0;
      ResetDecoder(slot);
      address = SlotAddress(i);
      std::memset(base + address, 0, kContextSize);
    }
  }
  if (out) {
    PPC_STORE_U32(out, address);
  }
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 3) {
    XELOGI("[{}] XMACreateContext -> {:08X}", warden::CurrentGuestThreadName(), address);
  }
  ctx.r3.u64 = address ? 0 : 0xC0000017u;  // STATUS_NO_MEMORY
}

// void XMAReleaseContext(void* context)
PPC_FUNC(__imp__XMAReleaseContext) {
  Count(kRelease);
  const int index = SlotIndex(ctx.r3.u32);
  if (index >= 0) {
    Slot& slot = g_slots[index];
    std::lock_guard<std::mutex> lock(slot.mutex);
    slot.allocated = false;
    slot.enabled.store(false, std::memory_order_release);
    slot.pending_blocks = 0;
    ResetDecoder(slot);
    std::memset(base + ctx.r3.u32, 0, kContextSize);
  }
  ctx.r3.u64 = 0;
}

// HRESULT XMAInitializeContext(void* context, const XMA_CONTEXT_INIT* init)
//
// XMA_CONTEXT_INIT, 56 bytes: input 0 pointer and packet count, input 1 pointer and
// packet count, input read offset, output pointer and block count, work buffer,
// subframe decode count, channel count, sample rate, then the loop: start, end, count,
// subframe end, subframe skip.
PPC_FUNC(__imp__XMAInitializeContext) {
  Count(kInit);
  const uint32_t address = ctx.r3.u32;
  const uint32_t init = ctx.r4.u32;
  if (!address || !init) {
    ctx.r3.u64 = 0x80070057u;  // E_INVALIDARG
    return;
  }
  const uint32_t input0 = PPC_LOAD_U32(init + 0);
  const uint32_t input0_packets = PPC_LOAD_U32(init + 4);
  const uint32_t input1 = PPC_LOAD_U32(init + 8);
  const uint32_t input1_packets = PPC_LOAD_U32(init + 12);
  const uint32_t read_offset = PPC_LOAD_U32(init + 16);
  const uint32_t output = PPC_LOAD_U32(init + 20);
  const uint32_t output_blocks = PPC_LOAD_U32(init + 24);
  const uint32_t subframes = PPC_LOAD_U32(init + 32);
  const uint32_t channels = PPC_LOAD_U32(init + 36);
  const uint32_t sample_rate = PPC_LOAD_U32(init + 40);
  const uint32_t loop_start = PPC_LOAD_U32(init + 44);
  const uint32_t loop_end = PPC_LOAD_U32(init + 48);
  const uint32_t loop_count = PPC_LOAD_U8(init + 52);
  const uint32_t loop_subframe_end = PPC_LOAD_U8(init + 53);
  const uint32_t loop_subframe_skip = PPC_LOAD_U8(init + 54);
  WithContext(address, [&](uint8_t* context, Slot* slot) {
    std::memset(context, 0, kContextSize);
    StoreWord(context + 4 * kInputBuffer0PtrDword, ToPhysical(input0));
    SetBits(context, kInputBuffer0PacketCount, input0_packets);
    StoreWord(context + 4 * kInputBuffer1PtrDword, ToPhysical(input1));
    SetBits(context, kInputBuffer1PacketCount, input1_packets);
    SetBits(context, kInputBufferReadOffset, read_offset);
    StoreWord(context + 4 * kOutputBufferPtrDword, ToPhysical(output));
    SetBits(context, kOutputBufferBlockCount, output_blocks);
    SetBits(context, kSubframeDecodeCount, subframes);
    // Xenia stores is_stereo as channel_count >= 1, and so does DAORecomp.
    SetBits(context, kIsStereo, channels >= 1);
    SetBits(context, kSampleRate, sample_rate);
    SetBits(context, kLoopStart, loop_start);
    SetBits(context, kLoopEnd, loop_end);
    SetBits(context, kLoopCount, loop_count);
    SetBits(context, kLoopSubframeEnd, loop_subframe_end);
    SetBits(context, kLoopSubframeSkip, loop_subframe_skip);
    if (slot) {
      slot->pending_blocks = 0;
      ResetDecoder(*slot);
      slot->input_guest[0] = input0;
      slot->input_guest[1] = input1;
      slot->output_guest = output;
    }
  });
  ctx.r3.u64 = 0;
}

// HRESULT XMAEnableContext(void* context)
//
// The pass runs here before returning, as Xenia's kick waits for it: the title's next
// call is often XMADisableContext(wait), which must see the pass done.
PPC_FUNC(__imp__XMAEnableContext) {
  Count(kEnable);
  (void)base;
  const int index = SlotIndex(ctx.r3.u32);
  if (index >= 0) {
    g_slots[index].enabled.store(true, std::memory_order_release);
    RunIfEnabled(uint32_t(index));
  }
  Kick();
  ctx.r3.u64 = 0;
}

// HRESULT XMADisableContext(void* context, BOOL wait)
//
// A pass in progress finishes first when wait is set; otherwise S_FALSE-like 0x80000000
// says it was busy.
PPC_FUNC(__imp__XMADisableContext) {
  Count(kDisable);
  (void)base;
  const int index = SlotIndex(ctx.r3.u32);
  uint32_t result = 0;
  if (index >= 0) {
    Slot& slot = g_slots[index];
    slot.enabled.store(false, std::memory_order_release);
    if (ctx.r4.u32) {
      std::lock_guard<std::mutex> lock(slot.mutex);
    } else {
      std::unique_lock<std::mutex> lock(slot.mutex, std::try_to_lock);
      if (!lock.owns_lock()) {
        result = 0x80000000u;
      }
    }
  }
  ctx.r3.u64 = result;
}

PPC_FUNC(__imp__XMASetInputBuffer0Valid) {
  Count(kSetIn0);
  (void)base;
  WriteField(ctx.r3.u32, kInputBuffer0Valid, 1);
  Kick();
  ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAIsInputBuffer0Valid) {
  Count(kIsIn0);
  (void)base;
  ctx.r3.u64 = ReadField(ctx.r3.u32, kInputBuffer0Valid);
}

PPC_FUNC(__imp__XMASetInputBuffer1Valid) {
  Count(kSetIn1);
  (void)base;
  WriteField(ctx.r3.u32, kInputBuffer1Valid, 1);
  Kick();
  ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAIsInputBuffer1Valid) {
  Count(kIsIn1);
  (void)base;
  ctx.r3.u64 = ReadField(ctx.r3.u32, kInputBuffer1Valid);
}

PPC_FUNC(__imp__XMASetInputBufferReadOffset) {
  Count(kSetReadOff);
  (void)base;
  WriteField(ctx.r3.u32, kInputBufferReadOffset, ctx.r4.u32);
  Kick();
  ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAGetInputBufferReadOffset) {
  Count(kGetReadOff);
  (void)base;
  ctx.r3.u64 = ReadField(ctx.r3.u32, kInputBufferReadOffset);
}

PPC_FUNC(__imp__XMASetOutputBufferValid) {
  Count(kSetOutValid);
  (void)base;
  WriteField(ctx.r3.u32, kOutputBufferValid, 1);
  Kick();
  ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAIsOutputBufferValid) {
  Count(kIsOutValid);
  (void)base;
  ctx.r3.u64 = ReadField(ctx.r3.u32, kOutputBufferValid);
}

PPC_FUNC(__imp__XMASetOutputBufferReadOffset) {
  Count(kSetOutRead);
  (void)base;
  WriteField(ctx.r3.u32, kOutputBufferReadOffset, ctx.r4.u32);
  Kick();
  ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAGetOutputBufferReadOffset) {
  Count(kGetOutRead);
  (void)base;
  ctx.r3.u64 = ReadField(ctx.r3.u32, kOutputBufferReadOffset);
}

PPC_FUNC(__imp__XMAGetOutputBufferWriteOffset) {
  Count(kGetOutWrite);
  (void)base;
  ctx.r3.u64 = ReadField(ctx.r3.u32, kOutputBufferWriteOffset);
}
