// Kernel objects behind guest handles. Tasks 5.2 and 5.1h.
//
// One table for every kind of object a handle can name - files, events, semaphores,
// threads - because that is how the console's object manager works: NtClose takes any
// handle, a wait takes any waitable one, and a title is entitled to close a file and
// an event with the same call.
//
// Waiting is shared rather than per object: one mutex guards every object's signal
// state and one condition variable wakes every waiter, who re-checks the object it
// wants. Simpler than a wait handle per object, and at startup's scale - a handful of
// threads, tens of objects - no slower.
#ifndef WARDEN_OBJECTS_H_
#define WARDEN_OBJECTS_H_

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace warden {

class KernelObject {
 public:
  enum class Type { kFile, kEvent, kSemaphore, kThread, kMutant, kEnumerator };

  explicit KernelObject(Type type) : type_(type) {}
  virtual ~KernelObject() = default;
  Type type() const { return type_; }

  // The dispatcher side, for objects a thread can wait on. Both are called with the
  // wait mutex held. A file is never signalled.
  virtual bool IsSignalled() const { return false; }
  // A wait on this object has been satisfied: an auto-reset event clears, a
  // semaphore counts down, a manual-reset event and a finished thread stay as they are.
  virtual void Consume() {}

  // The console's X_OBJECT_TYPES code, written into the guest body's dispatcher
  // header: 0 manual-reset event, 1 auto-reset event, 5 semaphore, 6 thread.
  // A file is not a dispatcher object and has none; 0xFF says so if ever handed out.
  virtual uint8_t GuestTypeCode() const { return 0xFF; }

  // Where the object lives in guest memory, allocated on first request: what
  // ObReferenceObjectByHandle hands out and what the Ke* calls take back. 0 until
  // then. Guarded by the table's own lock, not the wait mutex.
  uint32_t guest_body = 0;

 private:
  Type type_;
};

class GuestEvent : public KernelObject {
 public:
  GuestEvent(bool manual_reset, bool initial)
      : KernelObject(Type::kEvent), manual_reset(manual_reset), signalled(initial) {}
  bool IsSignalled() const override { return signalled; }
  void Consume() override {
    if (!manual_reset) {
      signalled = false;
    }
  }
  uint8_t GuestTypeCode() const override { return manual_reset ? 0 : 1; }

  const bool manual_reset;
  bool signalled;
};

class GuestSemaphore : public KernelObject {
 public:
  GuestSemaphore(int32_t count, int32_t limit)
      : KernelObject(Type::kSemaphore), count(count), limit(limit) {}
  bool IsSignalled() const override { return count > 0; }
  void Consume() override { --count; }
  uint8_t GuestTypeCode() const override { return 5; }

  int32_t count;
  const int32_t limit;
};

// A guest thread: the host thread running it is in warden_threads.cc. Signalled once
// it has finished, which is what waiting on a thread handle waits for.
// An identity for the calling thread, for mutant ownership: the address of a
// thread_local, unique per thread for its lifetime.
inline const void* CurrentWaiterIdentity() {
  static thread_local char tag;
  return &tag;
}

// A mutex the title created with NtCreateMutant (its CreateMutex). Recursive: the
// owning thread acquires it again without blocking. Signalled - acquirable - when
// free or already the caller's. IsSignalled and Consume run on the waiting thread,
// under the wait mutex, so the caller's identity is the waiter's.
class GuestMutant : public KernelObject {
 public:
  explicit GuestMutant(bool initially_owned) : KernelObject(Type::kMutant) {
    if (initially_owned) {
      owner = CurrentWaiterIdentity();
      count = 1;
    }
  }
  bool IsSignalled() const override {
    return count == 0 || owner == CurrentWaiterIdentity();
  }
  void Consume() override {
    owner = CurrentWaiterIdentity();
    ++count;
  }
  uint8_t GuestTypeCode() const override { return 2; }  // MutantObject
  const void* owner = nullptr;
  uint32_t count = 0;
};

class GuestThread : public KernelObject {
 public:
  GuestThread() : KernelObject(Type::kThread) {}
  bool IsSignalled() const override { return finished; }
  uint8_t GuestTypeCode() const override { return 6; }

  uint32_t thread_id = 0;
  std::string name;
  // How the title asked for it to start, as ExCreateThread received it.
  uint32_t xapi_thread_startup = 0;
  uint32_t start_address = 0;
  uint32_t start_context = 0;
  uint32_t stack_size = 0;
  // The thread's guest structures - PCR, KTHREAD, TLS - allocated at creation
  // (warden_guest.h, AllocateGuestThreadBlock); guest_body is the KTHREAD inside it.
  uint32_t guest_block = 0;
  // Starts suspended when created so; NtResumeThread lets it go. Under the wait mutex.
  uint32_t suspend_count = 0;
  bool finished = false;
  uint32_t exit_code = 0;
};

// A XAM enumerator - achievements, content - that the title walks with XamEnumerate:
// items of one size, handed out a page at a time. Not a dispatcher object. The items
// are stored as the title reads them (big-endian), `item_size` bytes each.
class GuestEnumerator : public KernelObject {
 public:
  GuestEnumerator(const char* what, uint32_t item_size, uint32_t items_per_call)
      : KernelObject(Type::kEnumerator),
        what(what),
        item_size(item_size),
        items_per_call(items_per_call) {}

  const char* what;
  uint32_t item_size;
  uint32_t items_per_call;
  std::string items;  // item_size bytes per item
  uint32_t next = 0;  // items handed out so far
};

// Adds an object and returns the guest handle for it - never 0, and never one of the
// pseudo-handles (-1 the current process, -2 the current thread, 0xFFFFFFFD the DOS
// devices directory).
uint32_t AddObject(std::shared_ptr<KernelObject> object);

// The object a handle names, or null. Typed, so a file handle passed where an event is
// wanted is a lookup failure rather than a cast.
std::shared_ptr<KernelObject> LookupObject(uint32_t handle);

template <typename T>
std::shared_ptr<T> LookupObject(uint32_t handle, KernelObject::Type type) {
  std::shared_ptr<KernelObject> object = LookupObject(handle);
  if (!object || object->type() != type) {
    return nullptr;
  }
  return std::static_pointer_cast<T>(object);
}

// Removes a handle. False if it named nothing. The object itself lives on while
// anything still holds it - a thread's own record, a wait in progress.
bool CloseObjectHandle(uint32_t handle);

// The object's address in guest memory, allocating one the first time. The body is
// 0x100 zeroed bytes with a dispatcher header - type code at +0, signal state at +4 -
// which is as much as the title has been seen to read.
uint32_t GuestBodyOf(const std::shared_ptr<KernelObject>& object);
// Uses memory the caller already has as the object's body - a thread's KTHREAD inside
// its guest block - rather than one from the body pool. Writes the type code; the
// address answers ObjectAtGuestBody from then on.
void AdoptGuestBody(const std::shared_ptr<KernelObject>& object, uint32_t body);
// The reverse: the object whose body is at this guest address, or null.
std::shared_ptr<KernelObject> ObjectAtGuestBody(uint32_t guest_address);

// The one lock and wake-up for every waitable object's state.
std::mutex& WaitMutex();
std::condition_variable& WaitCondition();
// Keeps the guest body's signal state in step. Call with the wait mutex held after a
// change.
void NoteSignalStateChanged(const std::shared_ptr<KernelObject>& object);

}  // namespace warden

#endif  // WARDEN_OBJECTS_H_
