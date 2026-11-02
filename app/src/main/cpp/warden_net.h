// The network, as a console with no cable. See warden_net.cc.
#ifndef WARDEN_NET_H_
#define WARDEN_NET_H_

#include <cstdint>

namespace warden {

// Winsock's per-thread last error, what NetDll_WSAGetLastError returns.
void SetWsaLastError(uint32_t error);
uint32_t WsaLastError();

// Winsock values the imports in two files share.
constexpr uint32_t kWsaInvalidHandle = 6;          // WSA_INVALID_HANDLE
constexpr uint32_t kWsaInvalidParameter = 0x57;    // WSA_INVALID_PARAMETER
constexpr uint32_t kWsaWaitTimeout = 0x102;        // WSA_WAIT_TIMEOUT
constexpr uint32_t kWsaWaitFailed = 0xFFFFFFFFu;   // WSA_WAIT_FAILED
constexpr uint32_t kWsaInfinite = 0xFFFFFFFFu;     // WSA_INFINITE

}  // namespace warden

#endif  // WARDEN_NET_H_
