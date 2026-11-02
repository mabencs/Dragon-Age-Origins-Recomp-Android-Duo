// DAORecomp's precompiled header, as far as the parts of DAORecomp/apu this port
// compiles unmodified need it - the XMA decoder (apu/xma_decoder.cpp, apu/xma_trace.cpp):
// the standard headers, fmt, XenonUtils' big-endian be<T>, and the log macros. Not
// DAORecomp's own: no Windows headers and no game build. The other headers in this folder
// stand in for DAORecomp's runtime in the same way (kernel/memory.h, gpu/xenia_host.h,
// log.h) and come first on the include path for those sources only.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
// PLAN 10.13, for the mod converter: the rest of what DAORecomp's precompiled header
// gives its sources.
#include <climits>
#include <set>
#include <stop_token>
#include <variant>

#include <fmt/format.h>

#include "byteswap.h"  // XenonUtils: ByteSwap
#include "xbox.h"  // XenonUtils: be<T>

#include "log.h"
