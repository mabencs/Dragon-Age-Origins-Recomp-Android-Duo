// Self-test for the guest memory model. See warden_memory_check.cc for why it exists.
#ifndef WARDEN_MEMORY_CHECK_H_
#define WARDEN_MEMORY_CHECK_H_

#include <string>

namespace xe {
class Memory;
}

namespace warden {

// Exercises view aliasing and the fault-based write watch, and reports what happened.
// Must be called once at startup, before anything else uses the memory.
std::string CheckMemory(xe::Memory& memory);

}  // namespace warden

#endif  // WARDEN_MEMORY_CHECK_H_
