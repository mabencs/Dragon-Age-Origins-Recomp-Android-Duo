// The network, as a console with no cable. Task 5.1; no online functionality, by the
// owner's decision (RESUMING.md §8).
//
// The title's Live code starts Winsock and XNet, then asks whether it is online: the
// Ethernet link, the title's XNADDR, the signed-in user, DNS. Everything here answers
// the way a console with nothing plugged in does: the startups succeed (on the console
// they always do), the link is down, there is no address, DNS finds nothing, a socket
// cannot be made. The generated stubs (tools/gen_import_stubs.py, family "net") return
// SOCKET_ERROR for every other Winsock call, which is what a call on a socket that does
// not exist returns. Winsock *events* are dispatcher events and live with the other
// waits in warden_threads.cc; the title's network thread waits on them and must block
// rather than spin (BUG-035).
//
// What is deliberately not here: any attempt at a real network. Nothing in this port
// goes online (the project is personal use; Live is gone for this title anyway).

#include "warden_net.h"

#include <arpa/inet.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>

#include "ppc_recomp_shared.h"
#include "warden_guest_heap.h"
#include "warden_objects.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

thread_local uint32_t t_wsa_last_error = 0;

constexpr uint32_t kWsaEnetdown = 0x2742;         // WSAENETDOWN: no network at all
constexpr uint32_t kWsaEnotsock = 0x2736;         // WSAENOTSOCK
constexpr uint32_t kWsaEnobufs = 0x2747;          // WSAENOBUFS
constexpr uint32_t kWsaHostNotFound = 0x2AF9;     // WSAHOST_NOT_FOUND
constexpr uint32_t kXnetGetXnaddrNone = 0x1;      // XNET_GET_XNADDR_NONE: no address
constexpr uint32_t kXnetConnectStatusIdle = 0;    // XNET_CONNECT_STATUS_IDLE
constexpr uint32_t kErrorLogonNotLoggedOn = 0x80151802;
constexpr uint32_t kTitleId = 0x454108C0;         // Dragon Age: Origins
constexpr uint32_t kInvalidSocket = 0xFFFFFFFFu;

// Once or a few times per site: the network calls are few and each is worth a line.
#define WARDEN_NET_LOG(limit)   ([] { static std::atomic<int> n{0}; return n.fetch_add(1, std::memory_order_relaxed) < (limit); }())

std::string GuestCString(uint8_t* base, uint32_t address, size_t limit = 256) {
  std::string out;
  if (!address) {
    return out;
  }
  const char* text = reinterpret_cast<const char*>(base + address);
  for (size_t i = 0; i < limit && text[i]; ++i) {
    out += text[i];
  }
  return out;
}

void WriteGuestString(uint8_t* base, uint32_t address, uint32_t capacity,
                      const char* text) {
  if (!address || !capacity) {
    return;
  }
  const size_t length = std::strlen(text);
  const size_t copied = length < capacity - 1 ? length : capacity - 1;
  std::memcpy(base + address, text, copied);
  base[address + copied] = 0;
}

// A small structure the title frees with the matching Release call. One page each
// from the guest heap (its granularity); the title makes a handful of these at
// startup, and Release leaves the page allocated - the heap has no free - which is
// 4 KiB per lookup, bounded by how often the title asks.
uint32_t AllocateGuestStructure(uint32_t size) {
  uint32_t address = 0;
  uint32_t got = 0;
  if (AllocateGuestMemory(0, size, 0x1000, address, got) != 0) {
    return 0;
  }
  return address;
}

void SignalGuestEvent(uint32_t handle) {
  if (!handle) {
    return;
  }
  auto event = LookupObject<GuestEvent>(handle, KernelObject::Type::kEvent);
  if (!event) {
    XELOGW("network: event {:08X} to signal is not an event this runtime knows", handle);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    event->signalled = true;
    NoteSignalStateChanged(event);
  }
  WaitCondition().notify_all();
}

}  // namespace

void SetWsaLastError(uint32_t error) { t_wsa_last_error = error; }
uint32_t WsaLastError() { return t_wsa_last_error; }

}  // namespace warden

using namespace warden;

// ------------------------------------------------------------------- startup

// INT NetDll_XNetStartup(XNCALLER_TYPE caller, const XNetStartupParams* params)
PPC_FUNC(__imp__NetDll_XNetStartup) {
  (void)base;
  if (WARDEN_NET_LOG(4)) {
    XELOGI("[{}] XNetStartup(params {:08X}) -> 0; there is no network", CurrentGuestThreadName(),
           ctx.r4.u32);
  }
  ctx.r3.u64 = 0;
}

// INT NetDll_XNetCleanup(XNCALLER_TYPE caller, void*)
PPC_FUNC(__imp__NetDll_XNetCleanup) {
  (void)base;
  ctx.r3.u64 = 0;
}

// int NetDll_WSAStartup(XNCALLER_TYPE caller, WORD version, WSADATA* data)
//
// WSADATA, 0x190 bytes: wVersion, wHighVersion, szDescription[257], szSystemStatus[129],
// iMaxSockets, iMaxUdpDg, lpVendorInfo. The version asked for is the version granted,
// as Xenia does off Windows.
PPC_FUNC(__imp__NetDll_WSAStartup) {
  const uint32_t version = ctx.r4.u32 & 0xFFFF;
  const uint32_t data = ctx.r5.u32;
  if (data) {
    std::memset(base + data, 0, 0x190);
    PPC_STORE_U16(data + 0x000, uint16_t(version));
    PPC_STORE_U16(data + 0x002, 0x0202);
    WriteGuestString(base, data + 0x004, 257, "Winsock 2.2 - Dragon Age Origins Recomp Android Duo, no network");
    WriteGuestString(base, data + 0x105, 129, "Offline");
  }
  if (WARDEN_NET_LOG(4)) {
    XELOGI("[{}] WSAStartup(version {:04X}) -> 0", CurrentGuestThreadName(), version);
  }
  ctx.r3.u64 = 0;
}

// int NetDll_WSACleanup(XNCALLER_TYPE caller)
PPC_FUNC(__imp__NetDll_WSACleanup) {
  (void)base;
  ctx.r3.u64 = 0;
}

// int NetDll_WSAGetLastError(void)
PPC_FUNC(__imp__NetDll_WSAGetLastError) {
  (void)base;
  ctx.r3.u64 = WsaLastError();
}

// ------------------------------------------------------------ "are we online?"

// DWORD NetDll_XNetGetEthernetLinkStatus(XNCALLER_TYPE caller) - 0: no link.
PPC_FUNC(__imp__NetDll_XNetGetEthernetLinkStatus) {
  (void)base;
  if (WARDEN_NET_LOG(2)) {
    XELOGI("[{}] XNetGetEthernetLinkStatus -> 0 (no link)", CurrentGuestThreadName());
  }
  ctx.r3.u64 = 0;
}

// DWORD NetDll_XNetGetTitleXnAddr(XNCALLER_TYPE caller, XNADDR* addr)
//
// XNADDR is 0x24 bytes: ina, inaOnline, wPortOnline, abEnet[6], abOnline[20]. All zero
// and XNET_GET_XNADDR_NONE: the console has no address. Not PENDING (0), which a title
// polls until it changes.
PPC_FUNC(__imp__NetDll_XNetGetTitleXnAddr) {
  if (ctx.r4.u32) {
    std::memset(base + ctx.r4.u32, 0, 0x24);
  }
  if (WARDEN_NET_LOG(2)) {
    XELOGI("[{}] XNetGetTitleXnAddr -> XNET_GET_XNADDR_NONE", CurrentGuestThreadName());
  }
  ctx.r3.u64 = kXnetGetXnaddrNone;
}

// DWORD NetDll_XNetGetConnectStatus(XNCALLER_TYPE caller, const IN_ADDR ina)
PPC_FUNC(__imp__NetDll_XNetGetConnectStatus) {
  (void)base;
  ctx.r3.u64 = kXnetConnectStatusIdle;
}

// HRESULT XNetLogonGetMachineID(ULONGLONG* machine_id)
PPC_FUNC(__imp__XNetLogonGetMachineID) {
  if (ctx.r3.u32) {
    PPC_STORE_U64(ctx.r3.u32, 0);
  }
  if (WARDEN_NET_LOG(2)) {
    XELOGI("[{}] XNetLogonGetMachineID -> not logged on", CurrentGuestThreadName());
  }
  ctx.r3.u64 = kErrorLogonNotLoggedOn;
}

// DWORD XNetLogonGetTitleID(void) - the running title's ID.
PPC_FUNC(__imp__XNetLogonGetTitleID) {
  (void)base;
  ctx.r3.u64 = kTitleId;
}

// -------------------------------------------------------------- lookups

// INT NetDll_XNetDnsLookup(XNCALLER_TYPE caller, const char* host, WSAEVENT event,
//                          XNDNS** result)
//
// XNDNS, 0x28 bytes: iStatus, cina, aina[8]. The lookup "completes" at once with
// WSAHOST_NOT_FOUND and signals the event, as Xenia's does; the title reads the status
// and releases the structure.
PPC_FUNC(__imp__NetDll_XNetDnsLookup) {
  const std::string host = GuestCString(base, ctx.r4.u32);
  const uint32_t event = ctx.r5.u32;
  const uint32_t result = ctx.r6.u32;
  if (WARDEN_NET_LOG(8)) {
    XELOGI("[{}] XNetDnsLookup(\"{}\") -> host not found", CurrentGuestThreadName(), host);
  }
  if (result) {
    const uint32_t dns = AllocateGuestStructure(0x28);
    if (!dns) {
      PPC_STORE_U32(result, 0);
      ctx.r3.u64 = kWsaEnobufs;
      return;
    }
    std::memset(base + dns, 0, 0x28);
    PPC_STORE_U32(dns + 0, kWsaHostNotFound);  // iStatus
    PPC_STORE_U32(result, dns);
  }
  SignalGuestEvent(event);
  ctx.r3.u64 = 0;
}

// INT NetDll_XNetDnsRelease(XNCALLER_TYPE caller, XNDNS* dns)
PPC_FUNC(__imp__NetDll_XNetDnsRelease) {
  (void)base;
  ctx.r3.u64 = ctx.r4.u32 ? 0 : 0xC000000Du;  // STATUS_INVALID_PARAMETER, as Xenia
}

// INT NetDll_XNetQosServiceLookup(XNCALLER_TYPE caller, DWORD flags, WSAEVENT event,
//                                 XNQOS** result)
//
// XNQOS, 0x20 bytes: cxnqos, cxnqosPending, then one XNQOSINFO. No probes answered.
PPC_FUNC(__imp__NetDll_XNetQosServiceLookup) {
  const uint32_t event = ctx.r5.u32;
  const uint32_t result = ctx.r6.u32;
  if (WARDEN_NET_LOG(4)) {
    XELOGI("[{}] XNetQosServiceLookup(flags {:08X}) -> nothing answered",
           CurrentGuestThreadName(), ctx.r4.u32);
  }
  if (result) {
    const uint32_t qos = AllocateGuestStructure(0x20);
    if (!qos) {
      PPC_STORE_U32(result, 0);
      ctx.r3.u64 = kWsaEnobufs;
      return;
    }
    std::memset(base + qos, 0, 0x20);
    PPC_STORE_U32(result, qos);
  }
  SignalGuestEvent(event);
  ctx.r3.u64 = 0;
}

// INT NetDll_XNetQosRelease(XNCALLER_TYPE caller, XNQOS* qos)
PPC_FUNC(__imp__NetDll_XNetQosRelease) {
  (void)base;
  ctx.r3.u64 = ctx.r4.u32 ? 0 : 0xC000000Du;
}

// --------------------------------------------------------------- utilities

// INT NetDll_XNetRandom(XNCALLER_TYPE caller, BYTE* buffer, UINT length)
//
// Real random bytes: titles seed from this, and a buffer left as it was is the one
// result worse than any number.
PPC_FUNC(__imp__NetDll_XNetRandom) {
  const uint32_t buffer = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  if (buffer && length) {
    static std::mutex mutex;
    static std::mt19937_64 generator{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mutex);
    for (uint32_t i = 0; i < length; ++i) {
      base[buffer + i] = uint8_t(generator());
    }
  }
  ctx.r3.u64 = 0;
}

// unsigned long NetDll_inet_addr(const char* text)
//
// The result is an IN_ADDR: bytes a.b.c.d in memory order, which from a big-endian
// register is a<<24 | b<<16 | c<<8 | d. INADDR_NONE for text that is not an address.
PPC_FUNC(__imp__NetDll_inet_addr) {
  const std::string text = GuestCString(base, ctx.r3.u32, 64);
  if (text.empty()) {
    ctx.r3.u64 = ctx.r3.u32 ? 0 : kInvalidSocket;
    return;
  }
  const in_addr_t value = inet_addr(text.c_str());
  ctx.r3.u64 = value == INADDR_NONE ? kInvalidSocket : ntohl(value);
}

// void NetDll_XNetInAddrToString(XNCALLER_TYPE caller, IN_ADDR address, char* out,
//                                INT size)
PPC_FUNC(__imp__NetDll_XNetInAddrToString) {
  const uint32_t address = ctx.r4.u32;
  char text[20];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (address >> 24) & 0xFF,
                (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF);
  WriteGuestString(base, ctx.r5.u32, ctx.r6.u32, text);
}

// ------------------------------------------------------------------ sockets

// SOCKET NetDll_socket(XNCALLER_TYPE caller, int af, int type, int protocol)
//
// INVALID_SOCKET with WSAENETDOWN: the one socket call that says *why*, so the title's
// error path has a reason to show. Every other socket call is the generated stub's
// SOCKET_ERROR, there being no socket for it to act on.
PPC_FUNC(__imp__NetDll_socket) {
  (void)base;
  if (WARDEN_NET_LOG(4)) {
    XELOGI("[{}] socket(af {}, type {}, protocol {}) -> INVALID_SOCKET, WSAENETDOWN",
           CurrentGuestThreadName(), ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  }
  SetWsaLastError(kWsaEnetdown);
  ctx.r3.u64 = kInvalidSocket;
}

// BOOL NetDll_WSAGetOverlappedResult(XNCALLER_TYPE caller, SOCKET s, WSAOVERLAPPED*,
//                                    DWORD* transferred, BOOL wait, DWORD* flags)
//
// FALSE, not the family's ~0 - which as a BOOL would be TRUE, "completed".
PPC_FUNC(__imp__NetDll_WSAGetOverlappedResult) {
  (void)base;
  SetWsaLastError(kWsaEnotsock);
  ctx.r3.u64 = 0;
}
