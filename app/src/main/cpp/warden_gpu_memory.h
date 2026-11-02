// GPU memory accounting. See warden_gpu_memory.cc.
#ifndef WARDEN_GPU_MEMORY_H_
#define WARDEN_GPU_MEMORY_H_

#include <string>

namespace warden::gpu {

// The Vulkan memory this process holds, by the code that allocated it: a line for the
// log, or "" if nothing changed by 16 MB since the last report.
std::string GpuMemoryReport();

}  // namespace warden::gpu

#endif  // WARDEN_GPU_MEMORY_H_
