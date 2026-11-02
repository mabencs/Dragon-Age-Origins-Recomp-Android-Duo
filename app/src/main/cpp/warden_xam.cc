// XAM's message requests: the title asks the system for things through them. Task 5.4.
//
// `XMsgStartIORequest` is XAM's asynchronous request: an application id, a message,
// an overlapped structure to complete, and a buffer. The title sends one when it
// reaches its main menu (the stub's E_NOTIMPL left the request pending for ever and
// the menu never arrived - BUG-043 is what follows). `XMsgInProcessCall` is the
// synchronous form. DAORecomp answers both as "done, success" and completes the
// overlapped structure, which is what this does; the messages themselves are logged,
// so a request that needs a real answer (a profile setting, a content enumeration)
// can be given one when it shows up.
//
// X_OVERLAPPED, Xenia's layout: result +0, length +4, context +8, event +0xC,
// completion routine +0x10, completion context +0x14, extended error +0x18.

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

#include <zlib.h>

#include "ppc_recomp_shared.h"
#include "warden_autosave.h"
#include "warden_fs.h"
#include "warden_guest.h"
#include "warden_guest_registers.h"
#include "warden_keyboard.h"
#include "warden_lifecycle.h"
#include "warden_objects.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"

namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorIoPending = 0x3E5;

// Writes the result last: a thread polling the result then sees the length and the
// extended error already there. The overlapped's event, if any, is signalled; a
// completion routine would need an APC on the requesting thread and is only reported.
void CompleteOverlapped(uint8_t* base, uint32_t overlapped, uint32_t result,
                        uint32_t length, uint32_t extended_error) {
  if (!overlapped) {
    return;
  }
  PPC_STORE_U32(overlapped + 4, length);
  PPC_STORE_U32(overlapped + 0x18, extended_error);
  std::atomic_thread_fence(std::memory_order_release);
  PPC_STORE_U32(overlapped + 0, result);
  const uint32_t event = PPC_LOAD_U32(overlapped + 0xC);
  if (event) {
    auto object = warden::LookupObject<warden::GuestEvent>(event,
                                                           warden::KernelObject::Type::kEvent);
    if (object) {
      {
        std::lock_guard<std::mutex> lock(warden::WaitMutex());
        object->signalled = true;
        warden::NoteSignalStateChanged(object);
      }
      warden::WaitCondition().notify_all();
    } else {
      XELOGW("XAM: overlapped {:08X} names event {:08X}, which is not an event handle",
             overlapped, event);
    }
  }
  const uint32_t routine = PPC_LOAD_U32(overlapped + 0x10);
  if (routine) {
    XELOGW("XAM: overlapped {:08X} has a completion routine {:08X}; not called (no APC "
           "delivery yet)", overlapped, routine);
  }
}

// The music player (app 0xFA) answers its getters through pointers in the argument
// block: nothing playing, full volume, the title in control - as Xenia's XMP app and
// DAORecomp answer them. Otherwise the caller reads whatever its stack held.
void AnswerMusicPlayer(uint8_t* base, uint32_t app, uint32_t message, uint32_t args) {
  constexpr uint32_t kMusicPlayerApp = 0xFA;
  if (app != kMusicPlayerApp || !args || args > 0xFFFFFFF0u) {
    return;
  }
  auto put = [&](uint32_t slot, uint32_t value) {
    const uint32_t at = PPC_LOAD_U32(args + 4 * slot);
    if (at) {
      PPC_STORE_U32(at, value);
    }
  };
  switch (message) {
    case 0x00070009:  // XMPGetStatus(client, state*): idle
      put(1, 0);
      break;
    case 0x0007000B: {  // XMPGetVolume(client, float*)
      const uint32_t at = PPC_LOAD_U32(args + 4);
      if (at) {
        PPC_STORE_U32(at, 0x3F800000u);  // 1.0f
      }
      break;
    }
    case 0x0007001B:  // XMPGetPlaybackController(client, controller*, locked*)
      put(1, 0);
      put(2, 0);
      break;
    case 0x00070029:  // XMPGetPlaybackBehavior(client, three outputs)
      put(1, 0);
      put(2, 0);
      put(3, 0);
      break;
    default:
      break;
  }
}

void StartIORequest(PPCContext& ctx, uint8_t* base, const char* name) {
  const uint32_t app = ctx.r3.u32;
  const uint32_t message = ctx.r4.u32;
  const uint32_t overlapped = ctx.r5.u32;
  const uint32_t buffer = ctx.r6.u32;
  XELOGI("[{}] {}(app {:02X}, message {:08X}, overlapped {:08X}, buffer {:08X}+{:X}) -> "
         "completed as success",
         warden::CurrentGuestThreadName(), name, app, message, overlapped, buffer,
         ctx.r7.u32);
  AnswerMusicPlayer(base, app, message, buffer);
  CompleteOverlapped(base, overlapped, kErrorSuccess, 0, 0);
  ctx.r3.u64 = overlapped ? kErrorIoPending : kErrorSuccess;
}

}  // namespace

// DWORD XMsgStartIORequest(DWORD app, DWORD message, XOVERLAPPED* overlapped,
//                          void* buffer, DWORD length)
PPC_FUNC(__imp__XMsgStartIORequest) { StartIORequest(ctx, base, "XMsgStartIORequest"); }

// DWORD XMsgStartIORequestEx(DWORD app, DWORD message, XOVERLAPPED* overlapped,
//                            void* buffer, DWORD length, void* task_arguments)
PPC_FUNC(__imp__XMsgStartIORequestEx) {
  StartIORequest(ctx, base, "XMsgStartIORequestEx");
}

// DWORD XMsgInProcessCall(DWORD app, DWORD message, DWORD arg1, DWORD arg2)
PPC_FUNC(__imp__XMsgInProcessCall) {
  XELOGI("[{}] XMsgInProcessCall(app {:02X}, message {:08X}, {:08X}, {:08X}) -> success",
         warden::CurrentGuestThreadName(), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  AnswerMusicPlayer(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  ctx.r3.u64 = kErrorSuccess;
}

// ------------------------------------------------------------------ the user
//
// One local profile, signed in, as DAORecomp presents it: the title wants a user to
// own its settings and saves, and with the sign-in state unanswered it sat on a black
// title screen. The XUID is DAORecomp's offline one (top nibble E = offline), the
// name is the project's.

namespace {

constexpr uint64_t kUserXuid = 0xB13EBABEBABEBABEull;
constexpr const char* kUserName = "Warden";
constexpr uint32_t kErrorNoSuchUser = 0x80070525;  // E_NO_SUCH_USER
constexpr uint32_t kErrorInvalidArgument = 0x80070057;

}  // namespace

// XUSER_SIGNIN_STATE XamUserGetSigninState(DWORD user) - 1: signed in locally
PPC_FUNC(__imp__XamUserGetSigninState) {
  (void)base;
  ctx.r3.u64 = ctx.r3.u32 == 0 ? 1 : 0;
}

// HRESULT XamUserGetXUID(DWORD user, DWORD type_mask, XUID* xuid)
PPC_FUNC(__imp__XamUserGetXUID) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t out = ctx.r5.u32;
  if (!out) {
    ctx.r3.u64 = kErrorInvalidArgument;
    return;
  }
  PPC_STORE_U64(out, user == 0 ? kUserXuid : 0);
  ctx.r3.u64 = user == 0 ? 0 : kErrorNoSuchUser;
}

// HRESULT XamUserGetSigninInfo(DWORD user, DWORD flags, XUSER_SIGNIN_INFO* info)
//
// Xenia's layout: xuid +0, flags +8, signin_state +0xC, guest_number +0x10,
// sponsor_user_index +0x14, name[16] +0x18.
PPC_FUNC(__imp__XamUserGetSigninInfo) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t info = ctx.r5.u32;
  if (!info) {
    ctx.r3.u64 = kErrorInvalidArgument;
    return;
  }
  std::memset(base + info, 0, 0x28);
  if (user >= 4) {
    ctx.r3.u64 = kErrorNoSuchUser;
    return;
  }
  if (user == 0) {
    PPC_STORE_U64(info + 0, kUserXuid);
    PPC_STORE_U32(info + 0xC, 1);
    std::strncpy(reinterpret_cast<char*>(base + info + 0x18), kUserName, 15);
  }
  XELOGI("[{}] XamUserGetSigninInfo(user {}, flags {:08X}) -> {}",
         warden::CurrentGuestThreadName(), user, ctx.r4.u32, user == 0 ? kUserName : "nobody");
  ctx.r3.u64 = 0;
}

// DWORD XGetGameRegion() - every region bit: no region lock.
PPC_FUNC(__imp__XGetGameRegion) {
  (void)base;
  ctx.r3.u64 = 0xFFFF;
}

// DWORD XamGetSystemVersion() - the dashboard version DAORecomp reports.
PPC_FUNC(__imp__XamGetSystemVersion) {
  (void)base;
  ctx.r3.u64 = 0x20446700;
}

// HANDLE XamNotifyCreateListener(ULONGLONG mask, DWORD max_version)
//
// A listener the title can wait on and poll: a manual-reset event that stays clear,
// since no notification is ever delivered (there is no sign-in change to announce).
PPC_FUNC(__imp__XamNotifyCreateListener) {
  (void)base;
  const uint32_t handle =
      warden::AddObject(std::make_shared<warden::GuestEvent>(true, false));
  XELOGI("[{}] XamNotifyCreateListener(mask {:016X}) -> {:08X}",
         warden::CurrentGuestThreadName(), ctx.r3.u64, handle);
  ctx.r3.u64 = handle;
}

// BOOL XNotifyGetNext(HANDLE listener, DWORD match_id, DWORD* id, DWORD* param)
PPC_FUNC(__imp__XNotifyGetNext) {
  if (ctx.r5.u32) {
    PPC_STORE_U32(ctx.r5.u32, 0);
  }
  if (ctx.r6.u32) {
    PPC_STORE_U32(ctx.r6.u32, 0);
  }
  ctx.r3.u64 = 0;  // nothing pending
}

// ---------------------------------------------------------------- storage and
// achievements: what the title asks for at its title screen

namespace {

constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kErrorNoMoreFiles = 18;
constexpr uint32_t kErrorDeviceNotConnected = 0x48F;
constexpr uint32_t kHddDeviceId = 1;

}  // namespace

// DWORD XamContentGetDeviceData(DWORD device_id, XDEVICE_DATA* data)
//
// One storage device, as DAORecomp: device 1, a 20 GB hard drive with 16 GB free,
// named "Hard Drive". X_CONTENT_DEVICE_DATA: id +0, type +4 (1 = HDD), total bytes +8,
// free bytes +16, name +24 (28 UTF-16 code units).
PPC_FUNC(__imp__XamContentGetDeviceData) {
  const uint32_t device_id = ctx.r3.u32;
  const uint32_t data = ctx.r4.u32;
  if (device_id != kHddDeviceId || !data) {
    XELOGI("[{}] XamContentGetDeviceData(device {}) -> not connected",
           warden::CurrentGuestThreadName(), device_id);
    ctx.r3.u64 = kErrorDeviceNotConnected;
    return;
  }
  std::memset(base + data, 0, 0x50);
  PPC_STORE_U32(data + 0, kHddDeviceId);
  PPC_STORE_U32(data + 4, 1);
  PPC_STORE_U64(data + 8, 20ull << 30);
  PPC_STORE_U64(data + 16, 16ull << 30);
  const char* name = "Hard Drive";
  for (uint32_t i = 0; name[i] && i < 27; ++i) {
    PPC_STORE_U16(data + 24 + i * 2, uint16_t(name[i]));
  }
  XELOGI("[{}] XamContentGetDeviceData(device {}) -> the hard drive, 20 GB with 16 GB free",
         warden::CurrentGuestThreadName(), device_id);
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamUserCreateAchievementEnumerator(DWORD title_id, DWORD user, ULONGLONG xuid,
//                                          DWORD flags, DWORD offset, DWORD count,
//                                          DWORD* buffer_size, HANDLE* handle)
//
// An enumerator with no achievements in it, as DAORecomp: items are 0x1C-byte
// XACHIEVEMENT_DETAILS without strings; the buffer the title must bring is count of
// them. The title gets "no more files" from the first XamEnumerate.
PPC_FUNC(__imp__XamUserCreateAchievementEnumerator) {
  const uint32_t user = ctx.r4.u32;
  const uint32_t count = ctx.r8.u32;
  const uint32_t buffer_size = ctx.r9.u32;
  const uint32_t handle_out = ctx.r10.u32;
  if (!count || !buffer_size || !handle_out) {
    ctx.r3.u64 = kErrorInvalidParameter;
    return;
  }
  PPC_STORE_U32(buffer_size, count * 0x1C);
  const uint32_t handle =
      warden::AddObject(std::make_shared<warden::GuestEnumerator>("achievements", 0x1C, count));
  PPC_STORE_U32(handle_out, handle);
  XELOGI("[{}] XamUserCreateAchievementEnumerator(title {:08X}, user {}, flags {:08X}, "
         "offset {}, count {}) -> {:08X}, nothing to enumerate",
         warden::CurrentGuestThreadName(), ctx.r3.u32, user, ctx.r6.u32, ctx.r7.u32, count,
         handle);
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamEnumerate(HANDLE handle, DWORD flags, void* buffer, DWORD buffer_size,
//                    DWORD* items_returned, XOVERLAPPED* overlapped)
//
// The next page of an enumerator's items. When nothing is left: ERROR_NO_MORE_FILES
// synchronously, or - through an overlapped - ERROR_FUNCTION_FAILED with the
// overlapped's extended error carrying HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES), as
// DAORecomp does.
PPC_FUNC(__imp__XamEnumerate) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t buffer = ctx.r5.u32;
  const uint32_t buffer_size = ctx.r6.u32;
  const uint32_t items_returned = ctx.r7.u32;
  const uint32_t overlapped = ctx.r8.u32;
  auto enumerator =
      warden::LookupObject<warden::GuestEnumerator>(handle, warden::KernelObject::Type::kEnumerator);
  if (!enumerator) {
    ctx.r3.u64 = 6;  // ERROR_INVALID_HANDLE
    return;
  }
  if (items_returned) {
    PPC_STORE_U32(items_returned, 0);
  }
  const uint32_t total = enumerator->item_size ? uint32_t(enumerator->items.size() / enumerator->item_size) : 0;
  const uint32_t remaining = total > enumerator->next ? total - enumerator->next : 0;
  const uint32_t fit = enumerator->item_size ? buffer_size / enumerator->item_size : 0;
  const uint32_t count = std::min({remaining, enumerator->items_per_call, fit});
  if (!count || !buffer) {
    XELOGI("[{}] XamEnumerate({:08X} {}) -> no more items",
           warden::CurrentGuestThreadName(), handle, enumerator->what);
    if (overlapped) {
      CompleteOverlapped(base, overlapped, 1627 /* ERROR_FUNCTION_FAILED */, 0,
                         0x80070000 | kErrorNoMoreFiles);
      ctx.r3.u64 = kErrorIoPending;
    } else {
      ctx.r3.u64 = kErrorNoMoreFiles;
    }
    return;
  }
  std::memcpy(base + buffer, enumerator->items.data() + size_t(enumerator->next) * enumerator->item_size,
              size_t(count) * enumerator->item_size);
  enumerator->next += count;
  if (items_returned) {
    PPC_STORE_U32(items_returned, count);
  }
  if (overlapped) {
    CompleteOverlapped(base, overlapped, kErrorSuccess, count, 0);
    ctx.r3.u64 = kErrorIoPending;
  } else {
    ctx.r3.u64 = kErrorSuccess;
  }
}

// DWORD XamContentGetDeviceState(DWORD device_id, XOVERLAPPED* overlapped)
PPC_FUNC(__imp__XamContentGetDeviceState) {
  const uint32_t device_id = ctx.r3.u32;
  const uint32_t overlapped = ctx.r4.u32;
  const uint32_t result = device_id == kHddDeviceId ? kErrorSuccess : kErrorDeviceNotConnected;
  XELOGI("[{}] XamContentGetDeviceState(device {}) -> {}", warden::CurrentGuestThreadName(),
         device_id, result ? "not connected" : "connected");
  if (overlapped) {
    CompleteOverlapped(base, overlapped, result, 0, result ? 0x80070000 | result : 0);
    ctx.r3.u64 = kErrorIoPending;
  } else {
    ctx.r3.u64 = result;
  }
}

// DWORD XamContentCreateEnumerator(DWORD user, DWORD device_id, DWORD content_type,
//                                  DWORD flags, DWORD items_per_enumerate,
//                                  DWORD* buffer_size, HANDLE* handle)
//
// The content of one type on the device - saves (type 1), downloadable content (2) -
// as 0x134-byte XCONTENT_DATA items (device id +0, type +4, display name +8 as 128
// UTF-16 code units, file name +0x108 as 42 bytes): the folders in
// files/content/<type>/ that XamContentCreateEx made, with the display names it kept,
// as DAORecomp lists its saves folder.
namespace {

constexpr uint32_t kXContentDataSize = 0x134;
constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorAccessDenied = 5;
constexpr uint32_t kErrorAlreadyExists = 183;

// files/content/<type, 8 hex digits> - where a content type's folders are.
std::string ContentTypeFolder(uint32_t content_type) {
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  if (!dir) {
    return std::string();
  }
  return fmt::format("{}/content/{:08X}", dir, content_type);
}

bool MakeFolders(const std::string& path) {
  std::string partial;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || (path[i] == '/' && i > 0)) {
      partial = path.substr(0, i);
      if (mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
        return false;
      }
    }
  }
  return true;
}

void RemoveTree(const std::string& path) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) {
    return;
  }
  if (S_ISDIR(st.st_mode)) {
    if (DIR* dir = opendir(path.c_str())) {
      while (const dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
          RemoveTree(path + "/" + name);
        }
      }
      closedir(dir);
    }
    rmdir(path.c_str());
  } else {
    unlink(path.c_str());
  }
}

// A content name the title may use as a folder name: printable, no separators, no
// device colon, not hidden or a dot-path.
bool IsContentName(const std::string& name) {
  if (name.empty() || name.size() > 42 || name[0] == '.') {
    return false;
  }
  for (char c : name) {
    if (c < 0x20 || c > 0x7E || c == '/' || c == '\\' || c == ':') {
      return false;
    }
  }
  return true;
}

std::string RootName(uint8_t* base, uint32_t address) {
  std::string root;
  for (uint32_t i = 0; address && i < 32; ++i) {
    const char c = char(PPC_LOAD_U8(address + i));
    if (!c) {
      break;
    }
    root += c;
  }
  while (!root.empty() && root.back() == ':') {
    root.pop_back();
  }
  return root;
}

uint32_t FinishContent(PPCContext& ctx, uint8_t* base, uint32_t overlapped, uint32_t result) {
  if (overlapped) {
    CompleteOverlapped(base, overlapped, result, 0, result ? 0x80070000 | result : 0);
    ctx.r3.u64 = kErrorIoPending;
  } else {
    ctx.r3.u64 = result;
  }
  return result;
}

}  // namespace

PPC_FUNC(__imp__XamContentCreateEnumerator) {
  const uint32_t device_id = ctx.r4.u32;
  const uint32_t content_type = ctx.r5.u32;
  const uint32_t items_per_enumerate = std::max<uint32_t>(ctx.r7.u32, 1);
  const uint32_t buffer_size = ctx.r8.u32;
  const uint32_t handle_out = ctx.r9.u32;
  if (!handle_out) {
    ctx.r3.u64 = kErrorInvalidParameter;
    return;
  }
  if (device_id && device_id != kHddDeviceId) {
    ctx.r3.u64 = kErrorDeviceNotConnected;
    return;
  }
  if (buffer_size) {
    PPC_STORE_U32(buffer_size, items_per_enumerate * kXContentDataSize);
  }
  auto enumerator = std::make_shared<warden::GuestEnumerator>("content", kXContentDataSize,
                                                              items_per_enumerate);
  // The type's folders, sorted, each an item in the guest's byte order: the display
  // name kept beside the folder (<name>.name, the title's own big-endian bytes).
  std::vector<std::string> names;
  const std::string folder = ContentTypeFolder(content_type);
  // PLAN 10.10: a scripted route's new game is played without downloadable content
  // (files/hide-dlc, tools/route.sh) - with Awakening installed, New Game first asks for
  // the campaign, and the route's presses would land a step early (run 494).
  const bool hide = content_type == 2 && [] {
    const char* files = std::getenv("WARDEN_FILES_DIR");
    struct stat st;
    return files && stat((std::string(files) + "/hide-dlc").c_str(), &st) == 0;
  }();
  // PLAN 10.19: but the add-ins installed from mods (mod-<id>) stay: they extend a
  // campaign and add no choice to New Game.
  if (hide) {
    XELOGI("XamContentCreateEnumerator: downloadable content hidden but mods' add-ins "
           "(files/hide-dlc)");
  }
  // PLAN 10.11: files/dlc-only - the downloadable content limited to the packages it
  // names (folder names, a word each, any prefix of one), so a test game's saves need only
  // those (a save records every package installed when it was made, and will not load
  // without them - the reference build, with only Awakening installed, refused one).
  std::vector<std::string> only;
  if (content_type == 2) {
    const char* files = std::getenv("WARDEN_FILES_DIR");
    if (FILE* f = files ? fopen((std::string(files) + "/dlc-only").c_str(), "r") : nullptr) {
      char word[128];
      while (fscanf(f, "%127s", word) == 1) {
        only.push_back(word);
      }
      fclose(f);
      XELOGI("XamContentCreateEnumerator: downloadable content limited to {} package(s) "
             "(files/dlc-only)", only.size());
    }
  }
  // PLAN 10.19: files/dlc-off - the packages of add-in mods turned off on the Mods page
  // (folder names, a word each, whole).
  std::vector<std::string> off;
  if (content_type == 2) {
    const char* files = std::getenv("WARDEN_FILES_DIR");
    if (FILE* f = files ? fopen((std::string(files) + "/dlc-off").c_str(), "r") : nullptr) {
      char word[128];
      while (fscanf(f, "%127s", word) == 1) {
        off.push_back(word);
      }
      fclose(f);
    }
  }
  if (DIR* dir = folder.empty() ? nullptr : opendir(folder.c_str())) {
    while (const dirent* entry = readdir(dir)) {
      struct stat st;
      const std::string name = entry->d_name;
      const bool wanted =
          (only.empty() || std::any_of(only.begin(), only.end(), [&](const std::string& o) {
            return name.compare(0, o.size(), o) == 0;
          })) &&
          std::find(off.begin(), off.end(), name) == off.end() &&
          (!hide || name.compare(0, 4, "mod-") == 0);
      if (wanted && IsContentName(name) && stat((folder + "/" + name).c_str(), &st) == 0 &&
          S_ISDIR(st.st_mode)) {
        names.push_back(name);
      }
    }
    closedir(dir);
  }
  std::sort(names.begin(), names.end());
  for (const std::string& name : names) {
    std::string item(kXContentDataSize, '\0');
    auto put32 = [&](size_t at, uint32_t value) {
      item[at] = char(value >> 24);
      item[at + 1] = char(value >> 16);
      item[at + 2] = char(value >> 8);
      item[at + 3] = char(value);
    };
    put32(0, kHddDeviceId);
    put32(4, content_type);
    if (FILE* file = fopen((folder + "/" + name + ".name").c_str(), "rb")) {
      fread(&item[8], 1, 256, file);
      fclose(file);
    }
    std::memcpy(&item[0x108], name.data(), std::min<size_t>(name.size(), 42));
    enumerator->items += item;
  }
  const uint32_t handle = warden::AddObject(enumerator);
  PPC_STORE_U32(handle_out, handle);
  XELOGI("[{}] XamContentCreateEnumerator(user {}, device {}, content type {}, flags {:08X}, "
         "{} per call) -> {:08X}, {} item(s)",
         warden::CurrentGuestThreadName(), ctx.r3.u32, device_id, content_type, ctx.r6.u32,
         items_per_enumerate, handle, names.size());
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamContentGetCreator(DWORD user, const XCONTENT_DATA* data, BOOL* is_creator,
//                            ULONGLONG* creator_xuid, XOVERLAPPED* overlapped)
//
// Every piece of content is the signed-in user's own.
PPC_FUNC(__imp__XamContentGetCreator) {
  const uint32_t is_creator = ctx.r5.u32;
  const uint32_t creator_xuid = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  if (is_creator) {
    PPC_STORE_U32(is_creator, 1);
  }
  if (creator_xuid) {
    PPC_STORE_U64(creator_xuid, kUserXuid);
  }
  XELOGI("[{}] XamContentGetCreator -> the signed-in user", warden::CurrentGuestThreadName());
  if (overlapped) {
    CompleteOverlapped(base, overlapped, kErrorSuccess, 0, 0);
    ctx.r3.u64 = kErrorIoPending;
  } else {
    ctx.r3.u64 = kErrorSuccess;
  }
}

// PLAN 9.13: which mounted root is an autosave being written (created with "create
// always" under an `_a!` name), so that its close counts as an autosave written.
namespace {
std::mutex g_autosave_mutex;
std::map<std::string, std::string> g_autosave_writes;  // root -> content name
std::atomic<uint64_t> g_autosaves_written{0};
}  // namespace

namespace warden {
uint64_t AutosavesWritten() { return g_autosaves_written.load(std::memory_order_acquire); }
}  // namespace warden

// DWORD XamContentCreateEx(DWORD user, LPCSTR root_name, const XCONTENT_DATA* data,
//                          DWORD flags, DWORD* disposition, DWORD* license_mask,
//                          DWORD cache_size, ULONGLONG content_size,
//                          XOVERLAPPED* overlapped)
//
// A save: the content's folder, files/content/<type>/<file name>, mounted as the
// title's root ("save:") until XamContentClose, writable (warden_fs.cc). The low four
// bits of the flags are the disposition, as DAORecomp reads them: 1 create new, 2
// create always, 3 open existing, 4 open always, 5 truncate existing. Content that is
// replaced (2 or 5 on an existing folder) is moved to files/content/<type>/.backup/
// first, so the previous save can be recovered by hand if the new one never completes.
// The display name is kept beside the folder for the enumerator, for new content only
// (an open passes a fresh one, the time now). Nine arguments: the overlapped is on the
// stack at r1 + 0x54.
//
// Written whole or not at all (the owner, 2026-11-02: autosaves corrupted when the app
// was swiped away while one was being written - the process ended mid-write, in the
// save's own folder): new content (create new, create always, truncate) is written into
// files/content/<type>/.writing/<name>, mounted as the root, and committed only at
// XamContentClose - the content there before moved to .backup, the new folder renamed
// into place, its display name written. A save cut off leaves the one before untouched
// and a stale .writing folder, removed when that name is next written. (A leading dot:
// the enumerator, the Saves page and the restore page never list it.)
namespace {
struct PendingCommit {
  std::string folder;   // where the content belongs
  std::string staging;  // where it is being written
  std::string type_folder;
  std::string name;
  std::vector<uint8_t> display_name;  // 256 bytes, for <folder>.name
};
std::mutex g_commit_mutex;
std::map<std::string, PendingCommit> g_pending_commits;  // by root

void AskSaveName(const std::string& folder);
}  // namespace

PPC_FUNC(__imp__XamContentCreateEx) {
  const uint32_t user = ctx.r3.u32;
  const std::string root = RootName(base, ctx.r4.u32);
  const uint32_t data = ctx.r5.u32;
  const uint32_t flags = ctx.r6.u32;
  const uint32_t disposition_out = ctx.r7.u32;
  const uint32_t license_out = ctx.r8.u32;
  const uint32_t overlapped = PPC_LOAD_U32(ctx.r1.u32 + 0x54);
  if (!data || root.empty()) {
    FinishContent(ctx, base, overlapped, kErrorInvalidParameter);
    return;
  }
  const uint32_t content_type = PPC_LOAD_U32(data + 4);
  std::string name;
  for (uint32_t i = 0; i < 42; ++i) {
    const char c = char(PPC_LOAD_U8(data + 0x108 + i));
    if (!c) {
      break;
    }
    name += c;
  }
  const std::string type_folder = ContentTypeFolder(content_type);
  if (!IsContentName(name) || type_folder.empty()) {
    XELOGW("[{}] XamContentCreateEx: refused the content name \"{}\"",
           warden::CurrentGuestThreadName(), name);
    FinishContent(ctx, base, overlapped, kErrorInvalidParameter);
    return;
  }
  const std::string folder = type_folder + "/" + name;
  // files/save-chain (PLAN 9.13, a test marker): the guest call chain of each save's
  // write - what the save runs on.
  static const bool save_chain = [] {
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    return dir && access((std::string(dir) + "/save-chain").c_str(), F_OK) == 0;
  }();
  if (save_chain && (flags & 0xF) == 2) {
    XELOGI("[{}] save-chain: the call chain of the write to {} (lr {:08X})",
           warden::CurrentGuestThreadName(), name, uint32_t(ctx.lr));
    warden::LogGuestCallChainOnFault(base);
  }
  struct stat st;
  const bool exists = stat(folder.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
  uint32_t result = kErrorSuccess;
  uint32_t disposition = 0;
  bool replace = false;
  switch (flags & 0xF) {
    case 1:  // create new
      if (exists) {
        result = kErrorAlreadyExists;
      } else {
        disposition = 1;
      }
      break;
    case 2:  // create always
      replace = exists;
      disposition = 1;
      break;
    case 3:  // open existing
      if (!exists) {
        result = kErrorFileNotFound;
      } else {
        disposition = 2;
      }
      break;
    case 4:  // open always
      disposition = exists ? 2 : 1;
      break;
    case 5:  // truncate existing
      if (!exists) {
        result = kErrorFileNotFound;
      } else {
        replace = true;
        disposition = 1;
      }
      break;
    default:
      result = kErrorInvalidParameter;
      break;
  }
  // New content is written in .writing/ and committed at XamContentClose (above).
  const std::string staging = type_folder + "/.writing/" + name;
  if (result == kErrorSuccess) {
    const std::string target = disposition == 1 ? staging : folder;
    if (disposition == 1) {
      RemoveTree(staging);  // a write cut off before
    }
    if (!MakeFolders(target)) {
      XELOGW("XamContentCreateEx: could not make {} ({})", target, strerror(errno));
      result = kErrorAccessDenied;
      disposition = 0;
    }
  }
  if (result == kErrorSuccess) {
    {
      std::lock_guard<std::mutex> lock(g_commit_mutex);
      if (disposition == 1) {
        PendingCommit commit;
        commit.folder = folder;
        commit.staging = staging;
        commit.type_folder = type_folder;
        commit.name = name;
        commit.display_name.assign(base + data + 8, base + data + 8 + 256);
        g_pending_commits[root] = std::move(commit);
      } else {
        g_pending_commits.erase(root);
      }
    }
    warden::MountContentRoot(root, disposition == 1 ? staging : folder);
    std::lock_guard<std::mutex> lock(g_autosave_mutex);
    if ((flags & 0xF) == 2 && name.find("_a!") != std::string::npos) {
      g_autosave_writes[root] = name;
    } else {
      g_autosave_writes.erase(root);
    }
  }
  if (disposition_out) {
    PPC_STORE_U32(disposition_out, disposition);
  }
  if (license_out) {
    // Downloadable content (type 2) is the player's own: every licence. Saves have none.
    PPC_STORE_U32(license_out, content_type == 2 ? 0xFFFFFFFF : 0);
  }
  XELOGI("[{}] XamContentCreateEx(user {}, \"{}:\", type {}, \"{}\", flags {:08X}) -> {} "
         "(disposition {}{})", warden::CurrentGuestThreadName(), user, root, content_type,
         name, flags, result, disposition,
         disposition == 1 ? (replace ? ", written aside until closed, the old one then kept in"
                                       " .backup"
                                     : ", written aside until closed")
                          : "");
  FinishContent(ctx, base, overlapped, result);
}

// DWORD XamContentClose(LPCSTR root_name, XOVERLAPPED* overlapped)
PPC_FUNC(__imp__XamContentClose) {
  const std::string root = RootName(base, ctx.r3.u32);
  const uint32_t overlapped = ctx.r4.u32;
  warden::UnmountContentRoot(root);
  XELOGI("[{}] XamContentClose(\"{}:\")", warden::CurrentGuestThreadName(), root);
  // New content written whole: committed into place now (XamContentCreateEx above).
  {
    std::lock_guard<std::mutex> lock(g_commit_mutex);
    const auto pending = g_pending_commits.find(root);
    if (pending != g_pending_commits.end()) {
      const PendingCommit& commit = pending->second;
      struct stat st;
      bool kept = true;
      if (stat(commit.folder.c_str(), &st) == 0) {
        const std::string backups = commit.type_folder + "/.backup";
        const std::string backup = backups + "/" + commit.name;
        MakeFolders(backups);
        RemoveTree(backup);
        kept = rename(commit.folder.c_str(), backup.c_str()) == 0;
        if (!kept) {
          XELOGW("XamContentClose: could not move {} aside ({}); removed instead",
                 commit.folder, strerror(errno));
          RemoveTree(commit.folder);
        }
      }
      if (rename(commit.staging.c_str(), commit.folder.c_str()) == 0) {
        if (FILE* file = fopen((commit.folder + ".name").c_str(), "wb")) {
          fwrite(commit.display_name.data(), 1, commit.display_name.size(), file);
          fclose(file);
        }
        XELOGI("[{}] {} committed{}", warden::CurrentGuestThreadName(), commit.name,
               kept ? "" : " (the one before could not be kept)");
        // A manual save (the player's, "_u!"): its name asked for (below).
        if (commit.type_folder.size() >= 8 &&
            commit.type_folder.compare(commit.type_folder.size() - 8, 8, "00000001") == 0 &&
            commit.name.find("_u!") != std::string::npos) {
          AskSaveName(commit.folder);
        }
      } else {
        XELOGW("XamContentClose: could not put {} in place ({})", commit.folder,
               strerror(errno));
      }
      g_pending_commits.erase(pending);
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_autosave_mutex);
    const auto write = g_autosave_writes.find(root);
    if (write != g_autosave_writes.end()) {
      XELOGI("[{}] an autosave written: {}", warden::CurrentGuestThreadName(), write->second);
      g_autosave_writes.erase(write);
      g_autosaves_written.fetch_add(1, std::memory_order_release);
    }
  }
  FinishContent(ctx, base, overlapped, kErrorSuccess);
}

// ---------------------------------------------------------------- the system UI
//
// The console's blades - the storage device selector, message boxes, the sign-in and
// keyboard panes - are answered without showing anything, as DAORecomp does: the one
// storage device is chosen, a message box's focused button is pressed, sign-in is
// already done. Each is logged, with the text where there is text, because the
// title's reaction to the answer is the next thing to read. UTF-16 text is big-endian
// in guest memory.

namespace {

constexpr uint32_t kErrorCancelled = 1223;

std::string NarrowGuestText(uint8_t* base, uint32_t address) {
  std::string out;
  for (uint32_t i = 0; address && i < 512; ++i) {
    const uint16_t c = PPC_LOAD_U16(address + i * 2);
    if (!c) {
      break;
    }
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

void FinishUI(PPCContext& ctx, uint8_t* base, uint32_t overlapped, uint32_t result) {
  if (overlapped) {
    CompleteOverlapped(base, overlapped, result, 0, result ? 0x80070000 | result : 0);
    ctx.r3.u64 = kErrorIoPending;
  } else {
    ctx.r3.u64 = result;
  }
}

}  // namespace

// DWORD XamShowDeviceSelectorUI(DWORD user, DWORD content_type, DWORD content_flags,
//                               ULONGLONG total_requested, DWORD* device_id,
//                               XOVERLAPPED* overlapped)
//
// What the title asks for when the player answers "Yes" to "You have not selected a
// storage device": the hard drive is chosen for them.
PPC_FUNC(__imp__XamShowDeviceSelectorUI) {
  const uint32_t device_id_out = ctx.r7.u32;
  const uint32_t overlapped = ctx.r8.u32;
  if (device_id_out) {
    PPC_STORE_U32(device_id_out, kHddDeviceId);
  }
  XELOGI("[{}] XamShowDeviceSelectorUI(user {}, content type {:08X}, flags {:08X}, {} bytes "
         "wanted) -> the hard drive",
         warden::CurrentGuestThreadName(), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u64);
  FinishUI(ctx, base, overlapped, kErrorSuccess);
}

// DWORD XamShowSigninUI(DWORD panes, DWORD flags) - user 0 is signed in already.
PPC_FUNC(__imp__XamShowSigninUI) {
  (void)base;
  XELOGI("[{}] XamShowSigninUI(panes {}, flags {:08X}) -> nothing to do",
         warden::CurrentGuestThreadName(), ctx.r3.u32, ctx.r4.u32);
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamShowMessageBoxUI(DWORD user, LPCWSTR title, LPCWSTR text, DWORD button_count,
//                           LPCWSTR* buttons, DWORD focus_button, DWORD flags,
//                           MESSAGEBOX_RESULT* result, XOVERLAPPED* overlapped)
//
// Nine arguments: the overlapped is on the stack at r1 + 0x54. The focused button is
// pressed, as DAORecomp does; the title's own prompts (the storage question above) are
// not these - they are its Scaleform UI - so this is the system's own boxes only.
PPC_FUNC(__imp__XamShowMessageBoxUI) {
  const uint32_t focus = ctx.r8.u32;
  const uint32_t result_out = ctx.r10.u32;
  const uint32_t overlapped = PPC_LOAD_U32(ctx.r1.u32 + 0x54);
  std::string buttons;
  const uint32_t button_count = std::min<uint32_t>(ctx.r6.u32, 4);
  for (uint32_t i = 0; ctx.r7.u32 && i < button_count; ++i) {
    buttons += (i ? " / " : "") + NarrowGuestText(base, PPC_LOAD_U32(ctx.r7.u32 + i * 4));
  }
  XELOGW("[{}] XamShowMessageBoxUI(\"{}\": \"{}\", buttons {}, flags {:08X}) -> button {}",
         warden::CurrentGuestThreadName(), NarrowGuestText(base, ctx.r4.u32),
         NarrowGuestText(base, ctx.r5.u32), buttons, ctx.r9.u32, focus);
  if (result_out) {
    PPC_STORE_U32(result_out, focus);
  }
  FinishUI(ctx, base, overlapped, kErrorSuccess);
}

PPC_FUNC(__imp__XamShowMessageBoxUIEx) { __imp__XamShowMessageBoxUI(ctx, base); }

// DWORD XamShowDirtyDiscErrorUI(DWORD user) - a read failed. DAORecomp exits here; this
// logs and returns, so the log of what failed before it is what to read.
PPC_FUNC(__imp__XamShowDirtyDiscErrorUI) {
  (void)base;
  XELOGE("[{}] XamShowDirtyDiscErrorUI: the title says a disc read failed - see the file "
         "log above", warden::CurrentGuestThreadName());
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamShowAchievementsUI(DWORD user, DWORD flags)
PPC_FUNC(__imp__XamShowAchievementsUI) {
  (void)base;
  XELOGI("[{}] XamShowAchievementsUI -> nothing shown", warden::CurrentGuestThreadName());
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamShowMarketplaceUI(...) - no online functionality in this app: nothing shown.
PPC_FUNC(__imp__XamShowMarketplaceUI) {
  (void)base;
  XELOGI("[{}] XamShowMarketplaceUI -> nothing shown (no online functionality)",
         warden::CurrentGuestThreadName());
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamShowKeyboardUI(DWORD user, DWORD flags, LPCWSTR default_text, LPCWSTR title,
//                         LPCWSTR description, LPWSTR buffer, DWORD length,
//                         XOVERLAPPED* overlapped)
//
// The on-screen keyboard (a character's name), answered by the app's text dialog
// (warden_keyboard.h): the prompt is filed and the call returns pending, as the
// console's does, and GameActivity's answer writes the buffer and completes the
// overlapped - the title waits on it meanwhile. Without an overlapped (a synchronous
// call), with a prompt already waiting, or with files/keyboard-default (unattended
// runs), the default text is accepted as typed, as before.
namespace {

struct PendingKeyboard {
  bool pending = false;
  warden::KeyboardPrompt prompt;
  uint32_t buffer = 0;
  uint32_t length = 0;
  uint32_t overlapped = 0;
  // The app's own question (a save's name, below), answered here rather than in the
  // title's buffer.
  std::function<void(const std::u16string&, bool)> answer;
};
std::mutex g_keyboard_mutex;
PendingKeyboard g_keyboard;

bool KeyboardDefaultMarker() {
  static const bool marker = [] {
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    struct stat st;
    return dir && stat((std::string(dir) + "/keyboard-default").c_str(), &st) == 0;
  }();
  return marker;
}

std::u16string GuestUtf16String(uint8_t* base, uint32_t address, uint32_t max_chars) {
  std::u16string text;
  for (uint32_t i = 0; address && i < max_chars; ++i) {
    const uint16_t c = PPC_LOAD_U16(address + i * 2);
    if (!c) {
      break;
    }
    text += char16_t(c);
  }
  return text;
}

// Writes `text` into the title's buffer of `length` characters, terminator included.
void WriteKeyboardText(uint8_t* base, uint32_t buffer, uint32_t length,
                       const std::u16string& text) {
  uint32_t i = 0;
  for (; i < text.size() && i + 1 < length; ++i) {
    PPC_STORE_U16(buffer + i * 2, uint16_t(text[i]));
  }
  PPC_STORE_U16(buffer + i * 2, 0);
}

std::string Narrow(const std::u16string& text) {
  std::string out;
  for (char16_t c : text) {
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

}  // namespace

namespace warden {

bool PendingKeyboardPrompt(KeyboardPrompt* prompt) {
  std::lock_guard<std::mutex> lock(g_keyboard_mutex);
  if (!g_keyboard.pending) {
    return false;
  }
  if (prompt) {
    *prompt = g_keyboard.prompt;
  }
  return true;
}

void AnswerKeyboardPrompt(const std::u16string& text, bool accepted) {
  std::unique_lock<std::mutex> lock(g_keyboard_mutex);
  if (!g_keyboard.pending) {
    return;
  }
  g_keyboard.pending = false;
  if (g_keyboard.answer) {
    auto answer = std::move(g_keyboard.answer);
    g_keyboard.answer = nullptr;
    lock.unlock();
    answer(text, accepted);
    return;
  }
  uint8_t* base = GuestMemoryBase();
  if (!base) {
    return;
  }
  if (accepted) {
    WriteKeyboardText(base, g_keyboard.buffer, g_keyboard.length, text);
    XELOGI("XamShowKeyboardUI: answered \"{}\"", Narrow(text));
    CompleteOverlapped(base, g_keyboard.overlapped, kErrorSuccess, 0, 0);
  } else {
    XELOGI("XamShowKeyboardUI: cancelled");
    CompleteOverlapped(base, g_keyboard.overlapped, kErrorCancelled, 0,
                       0x80070000 | kErrorCancelled);
  }
}

}  // namespace warden

PPC_FUNC(__imp__XamShowKeyboardUI) {
  const uint32_t default_text = ctx.r5.u32;
  const uint32_t buffer = ctx.r8.u32;
  const uint32_t length = ctx.r9.u32;
  const uint32_t overlapped = ctx.r10.u32;
  if (!buffer || !length) {
    ctx.r3.u64 = kErrorInvalidParameter;
    return;
  }
  warden::KeyboardPrompt prompt;
  prompt.title = GuestUtf16String(base, ctx.r6.u32, 256);
  prompt.description = GuestUtf16String(base, ctx.r7.u32, 1024);
  prompt.default_text = GuestUtf16String(base, default_text, length - 1);
  prompt.max_chars = length - 1;
  bool filed = false;
  if (overlapped && !KeyboardDefaultMarker()) {
    std::lock_guard<std::mutex> lock(g_keyboard_mutex);
    if (!g_keyboard.pending) {
      g_keyboard.pending = true;
      g_keyboard.prompt = prompt;
      g_keyboard.buffer = buffer;
      g_keyboard.length = length;
      g_keyboard.overlapped = overlapped;
      filed = true;
    }
  }
  if (filed) {
    PPC_STORE_U32(overlapped + 4, 0);
    PPC_STORE_U32(overlapped + 0, kErrorIoPending);
    XELOGI("[{}] XamShowKeyboardUI(\"{}\": \"{}\", default \"{}\", {} chars) -> waiting for "
           "the app's keyboard", warden::CurrentGuestThreadName(), Narrow(prompt.title),
           Narrow(prompt.description), Narrow(prompt.default_text), prompt.max_chars);
    ctx.r3.u64 = kErrorIoPending;
    return;
  }
  WriteKeyboardText(base, buffer, length, prompt.default_text);
  XELOGW("[{}] XamShowKeyboardUI(\"{}\": \"{}\", default \"{}\", {} chars) -> the default, as "
         "typed{}", warden::CurrentGuestThreadName(), Narrow(prompt.title),
         Narrow(prompt.description), Narrow(prompt.default_text), prompt.max_chars,
         overlapped ? (KeyboardDefaultMarker() ? " (keyboard-default)" : " (a prompt waits)")
                    : " (synchronous)");
  FinishUI(ctx, base, overlapped, kErrorSuccess);
}

// ---------------------------------------------------------------- a save's name
//
// The owner (2026-11-02): an on-screen keyboard for naming a manual save. The 360's
// Save Game box shows a name - the area's - that only a keyboard plugged into the
// console could change (the title has no keystroke input here, and no button reaches a
// keyboard: session 41's probe). So as each manual save is committed (XamContentClose
// above), the app asks for its name with the same dialog the character's name uses
// (warden_keyboard.h, GameActivity), the game's name typed in already; the answer
// renames the save in its savegame.met. Cancel keeps the game's.
//
// savegame.met: "GFF V4.0X360SVMT" V2.0, big-endian, with a string table - struct
// count (20), string count (24), string table (28), data (32), then the struct
// template (36: fourcc, field count, field table, size) and its 12-byte fields (label,
// type, offset into the data). Strings (type 14) are indices into the table of ASCII
// strings, each ended by a zero, the data 16-aligned after it. Field 0x41A8 is the
// save's own name, 0x41A0 the Location line (both the area's at first, one string;
// session 41's first try renamed the location). The rename appends the new name to the
// table and points 0x41A8 at it; savegame.met.x is the file's CRC-32, big-endian.
// Written to a temporary file and renamed into place. The lists, though, show the
// content's display name - <folder>.name, "<character>_<name>_<yyyymmddhhmmss>" in
// UTF-16 big-endian (the enumerator's) - so its middle is renamed too; an underscore
// typed becomes a space, the three parts kept apart.
namespace {

constexpr uint32_t kMetSaveName = 0x41A8;
constexpr uint32_t kMetCharacter = 0x41A7;
constexpr uint32_t kMetMaxName = 40;

uint32_t MetWord(const std::vector<uint8_t>& d, size_t at) {
  return at + 4 <= d.size() ? (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) |
                                  (uint32_t(d[at + 2]) << 8) | d[at + 3]
                            : 0;
}

void SetMetWord(std::vector<uint8_t>& d, size_t at, uint32_t value) {
  d[at] = uint8_t(value >> 24);
  d[at + 1] = uint8_t(value >> 16);
  d[at + 2] = uint8_t(value >> 8);
  d[at + 3] = uint8_t(value);
}

struct Met {
  std::vector<uint8_t> bytes;
  uint32_t string_count = 0, string_offset = 0, data_offset = 0;
  size_t name_slot = 0;  // where 0x41A8's string index is
  std::string character;  // 0x41A7
  std::vector<std::string> strings;
};

bool ReadMet(const std::string& path, Met& met) {
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) {
    return false;
  }
  uint8_t chunk[4096];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) {
    met.bytes.insert(met.bytes.end(), chunk, chunk + got);
  }
  fclose(file);
  const auto& d = met.bytes;
  if (d.size() < 52 || memcmp(d.data(), "GFF V4.0X360SVMT", 16) != 0 || MetWord(d, 20) < 1) {
    return false;
  }
  met.string_count = MetWord(d, 24);
  met.string_offset = MetWord(d, 28);
  met.data_offset = MetWord(d, 32);
  const uint32_t field_count = MetWord(d, 40);
  const uint32_t field_table = MetWord(d, 44);
  if (met.string_offset > met.data_offset || met.data_offset > d.size() ||
      field_table + 12ull * field_count > met.string_offset) {
    return false;
  }
  size_t at = met.string_offset;
  for (uint32_t i = 0; i < met.string_count; ++i) {
    std::string text;
    while (at < met.data_offset && d[at]) {
      text += char(d[at++]);
    }
    if (at >= met.data_offset) {
      return false;
    }
    ++at;
    met.strings.push_back(text);
  }
  for (uint32_t i = 0; i < field_count; ++i) {
    const size_t field = field_table + 12 * i;
    if (MetWord(d, field) == kMetSaveName && (MetWord(d, field + 4) & 0xFFFF) == 14) {
      met.name_slot = met.data_offset + MetWord(d, field + 8);
    }
    if (MetWord(d, field) == kMetCharacter && (MetWord(d, field + 4) & 0xFFFF) == 14) {
      const uint32_t index = MetWord(d, met.data_offset + MetWord(d, field + 8));
      if (index < met.strings.size()) {
        met.character = met.strings[index];
      }
    }
  }
  return met.name_slot && met.name_slot + 4 <= d.size() &&
         MetWord(d, met.name_slot) < met.strings.size();
}

// The display name's middle renamed: "<character>_<old>_<time>" to
// "<character>_<name>_<time>".
bool RenameDisplayName(const std::string& folder, const std::string& character,
                       const std::string& name) {
  const std::string path = folder + ".name";
  uint8_t bytes[256] = {};
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) {
    return false;
  }
  const size_t got = fread(bytes, 1, sizeof(bytes), file);
  fclose(file);
  std::string display;
  for (size_t i = 0; i + 1 < got; i += 2) {
    const uint16_t c = uint16_t(bytes[i] << 8 | bytes[i + 1]);
    if (!c) break;
    display += c < 0x80 ? char(c) : '?';
  }
  const size_t time = display.rfind('_');
  const std::string prefix = character + "_";
  if (time == std::string::npos || display.compare(0, prefix.size(), prefix) != 0 ||
      time < prefix.size()) {
    XELOGW("save name: {}'s display name \"{}\" is not <character>_<name>_<time>", folder,
           display);
    return false;
  }
  const std::string renamed = prefix + name + display.substr(time);
  uint8_t out[256] = {};
  for (size_t i = 0; i < renamed.size() && 2 * i + 3 < sizeof(out); ++i) {
    out[2 * i + 1] = uint8_t(renamed[i]);
  }
  const std::string temporary = path + ".renaming";
  bool written = false;
  if (FILE* write = fopen(temporary.c_str(), "wb")) {
    written = fwrite(out, 1, sizeof(out), write) == sizeof(out);
    written = fclose(write) == 0 && written;
  }
  if (!written || rename(temporary.c_str(), path.c_str()) != 0) {
    remove(temporary.c_str());
    return false;
  }
  return true;
}

bool RenameSave(const std::string& folder, const std::string& name) {
  const std::string path = folder + "/Saves/savegame.met";
  Met met;
  if (!ReadMet(path, met)) {
    XELOGW("save name: {} is not a save's savegame.met this app can read", path);
    return false;
  }
  // The table again with the new name at its end, the data after it 16-aligned.
  std::vector<uint8_t> out(met.bytes.begin(), met.bytes.begin() + met.string_offset);
  for (const std::string& text : met.strings) {
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
  }
  out.insert(out.end(), name.begin(), name.end());
  out.push_back(0);
  while (out.size() % 16) {
    out.push_back(0);
  }
  const uint32_t data_offset = uint32_t(out.size());
  out.insert(out.end(), met.bytes.begin() + met.data_offset, met.bytes.end());
  SetMetWord(out, 24, met.string_count + 1);
  SetMetWord(out, 32, data_offset);
  SetMetWord(out, met.name_slot - met.data_offset + data_offset, met.string_count);
  const uint32_t crc = uint32_t(crc32(0, out.data(), uInt(out.size())));
  const uint8_t check[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8),
                            uint8_t(crc)};
  const std::string temporary = path + ".renaming";
  const std::string check_path = path + ".x";
  const std::string check_temporary = check_path + ".renaming";
  bool written = false;
  if (FILE* file = fopen(temporary.c_str(), "wb")) {
    written = fwrite(out.data(), 1, out.size(), file) == out.size();
    written = fclose(file) == 0 && written;
  }
  if (FILE* file = fopen(check_temporary.c_str(), "wb")) {
    written = fwrite(check, 1, 4, file) == 4 && written;
    written = fclose(file) == 0 && written;
  } else {
    written = false;
  }
  if (!written || rename(temporary.c_str(), path.c_str()) != 0 ||
      rename(check_temporary.c_str(), check_path.c_str()) != 0) {
    XELOGW("save name: {} could not be renamed ({})", folder, strerror(errno));
    remove(temporary.c_str());
    remove(check_temporary.c_str());
    return false;
  }
  if (!RenameDisplayName(folder, met.character, name)) {
    XELOGW("save name: {}'s display name not renamed - the lists keep the old one", folder);
  }
  return true;
}

void AskSaveName(const std::string& folder) {
  if (KeyboardDefaultMarker()) {
    return;  // unattended runs keep the game's name
  }
  Met met;
  if (!ReadMet(folder + "/Saves/savegame.met", met)) {
    XELOGW("save name: {} - its savegame.met not read; not asked", folder);
    return;
  }
  const std::string current = met.strings[MetWord(met.bytes, met.name_slot)];
  warden::KeyboardPrompt prompt;
  prompt.title = u"Name this save";
  prompt.description = u"The name the game's save lists show. Cancel keeps the game's own.";
  prompt.default_text = std::u16string(current.begin(), current.end());
  prompt.max_chars = kMetMaxName;
  std::lock_guard<std::mutex> lock(g_keyboard_mutex);
  if (g_keyboard.pending) {
    XELOGI("save name: a prompt is already waiting; {} keeps \"{}\"", folder, current);
    return;
  }
  g_keyboard.pending = true;
  g_keyboard.prompt = prompt;
  g_keyboard.answer = [folder, current](const std::u16string& text, bool accepted) {
    // Printable ASCII only (the table's strings), trimmed, at most kMetMaxName.
    std::string name;
    for (char16_t c : text) {
      if (c >= 0x20 && c <= 0x7E && name.size() < kMetMaxName) {
        name += c == '_' ? ' ' : char(c);
      }
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (!accepted || name.empty() || name == current) {
      XELOGI("save name: {} keeps \"{}\"", folder, current);
      return;
    }
    if (RenameSave(folder, name)) {
      XELOGI("save name: {} renamed \"{}\" (was \"{}\")", folder, name, current);
    }
  };
  XELOGI("save name: asked for {} (the game's \"{}\")", folder, current);
}

}  // namespace

// ---------------------------------------------------------------- the user's
// name, privileges and profile settings

// DWORD XamUserGetName(DWORD user, char* buffer, DWORD length)
PPC_FUNC(__imp__XamUserGetName) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t buffer = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  if (user >= 4 || !buffer) {
    ctx.r3.u64 = kErrorInvalidParameter;
    return;
  }
  if (user != 0) {
    if (length) {
      PPC_STORE_U8(buffer, 0);
    }
    ctx.r3.u64 = 1101;  // ERROR_NO_SUCH_USER
    return;
  }
  const uint32_t n = std::min<uint32_t>(length ? length - 1 : 0, uint32_t(strlen(kUserName)));
  std::memcpy(base + buffer, kUserName, n);
  if (length) {
    PPC_STORE_U8(buffer + n, 0);
  }
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamUserCheckPrivilege(DWORD user, DWORD privilege, BOOL* granted)
//
// A locally signed-in profile has no Live privileges - multiplayer sessions,
// communications, purchases, user-created content - and this app has no online
// functionality, so none is granted. DAORecomp grants them all; its network is a stub
// too. Logged per privilege, the first time, in case the title keeps a feature behind
// one that it should have offline.
PPC_FUNC(__imp__XamUserCheckPrivilege) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t privilege = ctx.r4.u32;
  const uint32_t out = ctx.r5.u32;
  if (out) {
    PPC_STORE_U32(out, 0);
  }
  static std::mutex mutex;
  static std::map<uint32_t, bool> seen;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!seen[privilege]) {
      seen[privilege] = true;
      XELOGI("[{}] XamUserCheckPrivilege(user {}, privilege {}) -> not granted (a local profile)",
             warden::CurrentGuestThreadName(), user, privilege);
    }
  }
  ctx.r3.u64 = (user == 0 || user == 0xFF) ? kErrorSuccess : 1101;  // ERROR_NO_SUCH_USER
}

// DWORD XamUserAreUsersFriends(DWORD user, ULONGLONG* xuids, DWORD count, BOOL* are_friends,
//                              XOVERLAPPED* overlapped) - nobody is.
PPC_FUNC(__imp__XamUserAreUsersFriends) {
  const uint32_t are_friends = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  if (are_friends) {
    PPC_STORE_U32(are_friends, 0);
  }
  FinishUI(ctx, base, overlapped, kErrorSuccess);
}

PPC_FUNC(__imp__XamUserGetMembershipTierFromXUID) {
  (void)base;
  ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetOnlineCountryFromXUID) {
  (void)base;
  ctx.r3.u64 = 103;  // XONLINE_COUNTRY_UNITED_STATES
}

// The gamer profile settings: the console keeps them in the profile, and the title
// reads the well-known ones (y-axis inversion, vibration, difficulty...) and writes
// its own (its title-specific settings blob). Kept per setting id as raw bytes under
// `files/profile/<id>.bin`, written through when the title writes them, so they
// survive a restart - a save-settings path the menu's options screen needs. Layouts
// (big-endian): the result is `count` (u32), a pointer to the settings (u32), then
// 40-byte X_USER_PROFILE_SETTING entries - source +0 (1 default, 2 title), xuid or
// user index +8, setting id +16, data type +24 (the id's top nibble: 1 int32, 2 int64,
// 3 double, 4 wstring, 5 float, 6 binary, 7 datetime), the value at +32 (int32 and
// float in the first word, int64/double/datetime as 8 bytes, wstring/binary as size
// +32 and pointer +36 into the data that follows the entries). A wstring or binary
// setting's maximum size is bits 16-27 of its id.
namespace {

std::mutex g_profile_mutex;
std::map<uint32_t, std::vector<uint8_t>> g_profile_settings;
bool g_profile_loaded = false;

std::string ProfileDirectory() {
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  return dir ? std::string(dir) + "/profile" : std::string();
}

void LoadProfile() {
  if (g_profile_loaded) {
    return;
  }
  g_profile_loaded = true;
  // Settings are read lazily by id (below); the directory only needs to exist.
  const std::string dir = ProfileDirectory();
  if (!dir.empty()) {
    mkdir(dir.c_str(), 0700);
  }
}

const std::vector<uint8_t>* StoredSetting(uint32_t id) {
  auto it = g_profile_settings.find(id);
  if (it != g_profile_settings.end()) {
    return &it->second;
  }
  const std::string dir = ProfileDirectory();
  if (dir.empty()) {
    return nullptr;
  }
  FILE* file = fopen(fmt::format("{}/{:08X}.bin", dir, id).c_str(), "rb");
  if (!file) {
    return nullptr;
  }
  std::vector<uint8_t> data;
  uint8_t chunk[4096];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) {
    data.insert(data.end(), chunk, chunk + got);
  }
  fclose(file);
  return &(g_profile_settings[id] = std::move(data));
}

void SaveSetting(uint32_t id, const std::vector<uint8_t>& data) {
  g_profile_settings[id] = data;
  const std::string dir = ProfileDirectory();
  if (dir.empty()) {
    return;
  }
  const std::string path = fmt::format("{}/{:08X}.bin", dir, id);
  const std::string temp = path + ".tmp";
  FILE* file = fopen(temp.c_str(), "wb");
  if (!file) {
    XELOGW("profile setting {:08X}: cannot write {}", id, temp);
    return;
  }
  const bool written = fwrite(data.data(), 1, data.size(), file) == data.size();
  if (fclose(file) != 0 || !written || rename(temp.c_str(), path.c_str()) != 0) {
    XELOGW("profile setting {:08X}: cannot save it", id);
    remove(temp.c_str());
  }
}

// DAORecomp's defaults for the well-known gamer settings (int32 unless noted).
int32_t DefaultSetting(uint32_t id) {
  switch (id) {
    case 0x10040003: return 1;  // OPTION_CONTROLLER_VIBRATION
    case 0x10040018: return 1;  // GAMER_CONTROL_SENSITIVITY (medium)
    case 0x10040021: return 1;  // GAMER_ACTION_AUTO_AIM
    case 0x10040022: return 1;  // GAMER_ACTION_AUTO_CENTER
    default: return 0;          // y-axis inversion off, difficulty normal, the rest 0
  }
}

constexpr uint32_t kSettingSize = 40;

}  // namespace

// DWORD XamUserReadProfileSettings(DWORD title_id, DWORD user, DWORD xuid_count,
//                                  ULONGLONG* xuids, DWORD setting_count,
//                                  DWORD* setting_ids, DWORD* buffer_size, void* buffer,
//                                  XOVERLAPPED* overlapped)
//
// Nine arguments: the overlapped is on the stack at r1 + 0x54. Called once with no
// buffer to learn the size, then with one.
PPC_FUNC(__imp__XamUserReadProfileSettings) {
  const uint32_t title_id = ctx.r3.u32;
  const uint32_t user = ctx.r4.u32;
  const uint32_t xuid_count = ctx.r5.u32;
  const uint32_t xuids = ctx.r6.u32;
  const uint32_t setting_count = ctx.r7.u32;
  const uint32_t setting_ids = ctx.r8.u32;
  const uint32_t buffer_size_ptr = ctx.r9.u32;
  const uint32_t buffer = ctx.r10.u32;
  const uint32_t overlapped = PPC_LOAD_U32(ctx.r1.u32 + 0x54);
  if (!setting_count || setting_count > 32 || !buffer_size_ptr || !setting_ids) {
    ctx.r3.u64 = kErrorInvalidParameter;
    return;
  }
  const uint32_t users = xuids ? std::max<uint32_t>(xuid_count, 1) : 1;
  uint32_t data_size = 0;
  for (uint32_t i = 0; i < setting_count; ++i) {
    const uint32_t id = PPC_LOAD_U32(setting_ids + i * 4);
    const uint32_t type = id >> 28;
    if (type == 4 || type == 6) {
      data_size += (id >> 16) & 0xFFF;
    }
  }
  const uint32_t needed = 8 + users * (setting_count * kSettingSize + data_size);
  const uint32_t buffer_size = PPC_LOAD_U32(buffer_size_ptr);
  if (!buffer || buffer_size < needed) {
    if (!buffer_size) {
      PPC_STORE_U32(buffer_size_ptr, needed);
    }
    XELOGI("[{}] XamUserReadProfileSettings(title {:08X}, user {}, {} settings) -> needs {} "
           "bytes", warden::CurrentGuestThreadName(), title_id, user, setting_count, needed);
    ctx.r3.u64 = 122;  // ERROR_INSUFFICIENT_BUFFER
    return;
  }

  std::lock_guard<std::mutex> lock(g_profile_mutex);
  LoadProfile();
  std::memset(base + buffer, 0, needed);
  PPC_STORE_U32(buffer, setting_count);
  PPC_STORE_U32(buffer + 4, buffer + 8);
  uint32_t data = buffer + 8 + setting_count * kSettingSize;
  std::string ids;
  for (uint32_t i = 0; i < setting_count; ++i) {
    const uint32_t id = PPC_LOAD_U32(setting_ids + i * 4);
    const uint32_t entry = buffer + 8 + i * kSettingSize;
    const uint32_t type = id >> 28;
    PPC_STORE_U32(entry + 16, id);
    PPC_STORE_U64(entry + 8, xuids ? kUserXuid : uint64_t(user) << 32);
    PPC_STORE_U8(entry + 24, uint8_t(type));
    const std::vector<uint8_t>* stored = StoredSetting(id);
    if (type == 4 || type == 6) {
      const uint32_t max_size = (id >> 16) & 0xFFF;
      PPC_STORE_U32(entry + 36, data);
      if (stored) {
        const uint32_t size = std::min<uint32_t>(max_size, uint32_t(stored->size()));
        std::memcpy(base + data, stored->data(), size);
        PPC_STORE_U32(entry + 32, size);
        PPC_STORE_U32(entry + 0, 2);  // from the title
      } else {
        PPC_STORE_U32(entry + 32, 0);
        PPC_STORE_U32(entry + 0, 1);  // default
      }
      data += max_size;
    } else if (stored && stored->size() >= 8) {
      std::memcpy(base + entry + 32, stored->data(), 8);  // stored as the title wrote it
      PPC_STORE_U32(entry + 0, 2);
    } else {
      PPC_STORE_U32(entry + 32, uint32_t(DefaultSetting(id)));
      PPC_STORE_U32(entry + 0, 1);
    }
    ids += fmt::format("{}{:08X}{}", i ? " " : "", id, stored ? "*" : "");
  }
  XELOGI("[{}] XamUserReadProfileSettings(title {:08X}, user {}) -> {} (* stored)",
         warden::CurrentGuestThreadName(), title_id, user, ids);
  FinishUI(ctx, base, overlapped, kErrorSuccess);
}

// DWORD XamUserWriteProfileSettings(DWORD title_id, DWORD user, DWORD setting_count,
//                                   X_USER_PROFILE_SETTING* settings, XOVERLAPPED* overlapped)
PPC_FUNC(__imp__XamUserWriteProfileSettings) {
  const uint32_t title_id = ctx.r3.u32;
  const uint32_t user = ctx.r4.u32;
  const uint32_t setting_count = ctx.r5.u32;
  const uint32_t settings = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  if (!setting_count || !settings) {
    ctx.r3.u64 = kErrorInvalidParameter;
    return;
  }
  std::lock_guard<std::mutex> lock(g_profile_mutex);
  LoadProfile();
  std::string ids;
  for (uint32_t i = 0; i < setting_count; ++i) {
    const uint32_t entry = settings + i * kSettingSize;
    const uint32_t id = PPC_LOAD_U32(entry + 16);
    const uint32_t type = id >> 28;
    std::vector<uint8_t> value;
    if (type == 4 || type == 6) {
      const uint32_t size = std::min<uint32_t>(PPC_LOAD_U32(entry + 32), (id >> 16) & 0xFFF);
      const uint32_t pointer = PPC_LOAD_U32(entry + 36);
      if (pointer) {
        value.assign(base + pointer, base + pointer + size);
      }
    } else {
      value.assign(base + entry + 32, base + entry + 40);  // as the title wrote it
    }
    SaveSetting(id, value);
    ids += fmt::format("{}{:08X} ({} bytes)", i ? ", " : "", id, value.size());
  }
  XELOGI("[{}] XamUserWriteProfileSettings(title {:08X}, user {}) -> {} saved",
         warden::CurrentGuestThreadName(), title_id, user, ids);
  FinishUI(ctx, base, overlapped, kErrorSuccess);
}

// ------------------------------------------------------------- the title's own end (4.9)
//
// On the console these never return: the title is unloaded for the dashboard or another
// title. Here the app ends - warden_lifecycle records it, Java finishes the activity and
// the process - and the calling thread waits for that.

// void XamLoaderTerminateTitle()
PPC_FUNC(__imp__XamLoaderTerminateTitle) {
  (void)ctx;
  (void)base;
  warden::RequestExit("the title ended itself (XamLoaderTerminateTitle)", false);
  warden::ParkForever();
}

// void XamLoaderLaunchTitle(const char* path, ULONG flags)
//
// The dashboard (no path) or another executable in this one's place: either way, the
// end of this app - there is no other title to run.
PPC_FUNC(__imp__XamLoaderLaunchTitle) {
  const uint32_t path = ctx.r3.u32;
  std::string target = "the dashboard";
  if (path) {
    target.clear();
    for (uint32_t i = 0; i < 260 && base[path + i]; ++i) {
      target += char(base[path + i]);
    }
  }
  warden::RequestExit("the title launched " + target + " (XamLoaderLaunchTitle)", false);
  warden::ParkForever();
}
