// Kernel objects behind guest handles. See warden_objects.h.

#include "warden_objects.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

#include "ppc_recomp_shared.h"
#include "warden_guest.h"
#include "warden_guest_heap.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

namespace warden {

namespace {

// Handles are multiples of 4 from a high base, as the console's are: a title may use
// the low two bits of a handle as flags, and a value nowhere near a small integer is
// easier to tell apart from one in a register dump.
constexpr uint32_t kFirstHandle = 0xF8000004;

// Guest bodies are carved from 64 KiB regions, 0x100 bytes each.
constexpr uint32_t kBodySize = 0x100;
constexpr uint32_t kBodyRegion = 0x10000;

struct Table {
  std::mutex mutex;
  std::unordered_map<uint32_t, std::shared_ptr<KernelObject>> objects;
  uint32_t next = kFirstHandle;
  // Bodies: every object that has one, by its guest address, kept alive here so a
  // Ke* call on a closed handle's body still finds the object.
  std::unordered_map<uint32_t, std::shared_ptr<KernelObject>> bodies;
  uint32_t body_next = 0;
  uint32_t body_end = 0;
};

Table& TheTable() {
  static Table table;
  return table;
}

std::mutex g_wait_mutex;
std::condition_variable g_wait_condition;

}  // namespace

uint32_t AddObject(std::shared_ptr<KernelObject> object) {
  Table& table = TheTable();
  std::lock_guard<std::mutex> lock(table.mutex);
  // Never reused while the process lives: a stale handle the title closes twice then
  // fails as a stale handle, instead of closing somebody else's object.
  const uint32_t handle = table.next;
  table.next += 4;
  table.objects.emplace(handle, std::move(object));
  return handle;
}

std::shared_ptr<KernelObject> LookupObject(uint32_t handle) {
  Table& table = TheTable();
  std::lock_guard<std::mutex> lock(table.mutex);
  auto found = table.objects.find(handle);
  return found == table.objects.end() ? nullptr : found->second;
}

bool CloseObjectHandle(uint32_t handle) {
  Table& table = TheTable();
  std::lock_guard<std::mutex> lock(table.mutex);
  return table.objects.erase(handle) != 0;
}

uint32_t GuestBodyOf(const std::shared_ptr<KernelObject>& object) {
  Table& table = TheTable();
  std::lock_guard<std::mutex> lock(table.mutex);
  if (object->guest_body) {
    return object->guest_body;
  }
  if (table.body_next + kBodySize > table.body_end) {
    uint32_t base = 0;
    uint32_t size = 0;
    if (AllocateGuestMemory(0, kBodyRegion, 0x10000, base, size) != 0) {
      XELOGE("no guest memory for an object body");
      return 0;
    }
    table.body_next = base;
    table.body_end = base + size;
  }
  const uint32_t body = table.body_next;
  table.body_next += kBodySize;
  uint8_t* host = GuestMemoryBase();
  if (host) {
    std::memset(host + body, 0, kBodySize);
    host[body] = object->GuestTypeCode();
  }
  object->guest_body = body;
  table.bodies.emplace(body, object);
  return body;
}

void AdoptGuestBody(const std::shared_ptr<KernelObject>& object, uint32_t body) {
  Table& table = TheTable();
  std::lock_guard<std::mutex> lock(table.mutex);
  uint8_t* host = GuestMemoryBase();
  if (host) {
    host[body] = object->GuestTypeCode();
  }
  object->guest_body = body;
  table.bodies[body] = object;
}

std::shared_ptr<KernelObject> ObjectAtGuestBody(uint32_t guest_address) {
  Table& table = TheTable();
  std::lock_guard<std::mutex> lock(table.mutex);
  auto found = table.bodies.find(guest_address);
  if (found != table.bodies.end()) {
    return found->second;
  }
  // Not one of ours. A title keeps many of its events and semaphores inside its own
  // structures, initialised in place - KeInitializeEvent is inlined by the compiler -
  // and hands the kernel their addresses. Adopt it from its dispatcher header: type at
  // +0, signal state at +4, and for a semaphore the limit at +16. The same object is
  // handed back for that address from then on, so its state lives here.
  uint8_t* host = GuestMemoryBase();
  if (!host || !guest_address) {
    return nullptr;
  }
  const uint8_t type = host[guest_address];
  uint32_t signal_state;
  std::memcpy(&signal_state, host + guest_address + 4, sizeof(signal_state));
  signal_state = __builtin_bswap32(signal_state);
  std::shared_ptr<KernelObject> object;
  if (type == 0 || type == 1) {
    object = std::make_shared<GuestEvent>(type == 0, signal_state != 0);
  } else if (type == 5) {
    uint32_t limit;
    std::memcpy(&limit, host + guest_address + 16, sizeof(limit));
    limit = __builtin_bswap32(limit);
    object = std::make_shared<GuestSemaphore>(int32_t(signal_state), int32_t(limit));
  } else {
    return nullptr;
  }
  object->guest_body = guest_address;
  table.bodies.emplace(guest_address, object);
  XELOGI("adopted the title's own {} at {:08X} (state {})",
         type == 5 ? "semaphore" : type == 0 ? "manual-reset event" : "auto-reset event",
         guest_address, signal_state);
  return object;
}

std::mutex& WaitMutex() { return g_wait_mutex; }
std::condition_variable& WaitCondition() { return g_wait_condition; }

void NoteSignalStateChanged(const std::shared_ptr<KernelObject>& object) {
  // The guest body's signal_state at +4, big-endian, for a title that peeks at it.
  const uint32_t body = object->guest_body;
  uint8_t* host = GuestMemoryBase();
  if (!body || !host) {
    return;
  }
  uint32_t state = 0;
  if (object->type() == KernelObject::Type::kSemaphore) {
    state = uint32_t(static_cast<GuestSemaphore*>(object.get())->count);
  } else {
    state = object->IsSignalled() ? 1 : 0;
  }
  const uint32_t swapped = __builtin_bswap32(state);
  std::memcpy(host + body + 4, &swapped, sizeof(swapped));
}

}  // namespace warden

// NTSTATUS NtClose(HANDLE handle)
//
// Any kind of handle. The object goes when the last reference does - an operation
// still holding it (a read in progress, a wait, a running thread) finishes first.
PPC_FUNC(__imp__NtClose) {
  (void)base;
  ctx.r3.u64 = warden::CloseObjectHandle(ctx.r3.u32)
                   ? 0x00000000u    // STATUS_SUCCESS
                   : 0xC0000008u;   // STATUS_INVALID_HANDLE
}
