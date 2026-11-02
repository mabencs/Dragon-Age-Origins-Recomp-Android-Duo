// Hooks on the title's own code, for reading what the game is doing: its mode, its own
// log, the scripts it runs, the screens it opens, the cutscenes it plays, character
// generation. They found BUG-046 (the device's patched image differed from the one
// the code was recompiled from, and "New Game" took a path the console never takes),
// and they are the quickest way to see where the game goes next.
//
// How: the recompiler emits every function as a weak `tu6_sub_X` alias of
// `__imp__tu6_sub_X`; a strong `tu6_sub_X` here takes both the direct calls and the
// function table's indirect ones, and calls the original by its `__imp__` name (the
// same as warden_fmod.cc). Every hook is cheap when it is not logging, and every log
// is bounded.
//
// What the pieces are (TU7 addresses; DAORecomp's kernel/game_modes.h and
// ui/photo_mode.cpp name the mode controller):
//   - Game modes: 0 explore, 1 combat, 2 dead, 3 menu (GUI), 4 conversation,
//     5 cutscene, 6 fly camera, 7 fixed camera, 8 pregame (title screen), 9 loading,
//     10 movie, 11 character generation, 12 level-up, 13 party picker - the factory
//     sub_823D9648 names the classes in that order. The controller's current mode is a
//     pointer at +120, its number at +96. sub_82348010(controller, mode) requests one,
//     sub_82348590(controller) is the controller's update.
//   - The engine's strings: UTF-16 with a small-string buffer - capacity at +24 (8 or
//     more: heap pointer at +4, else the characters inline at +4), length at +20.
//   - sub_82A13FC8(level, event name, message, -1, 0) is the title's own log.
//   - sub_824B1EE8(event, script name) runs a script on an event; sub_824B18F8(event)
//     is the event's type. sub_82330808(string, ASCII) constructs an engine string
//     (what the native script registration, sub_82306F10, names each script with).
//   - sub_824C2F10(out, section, key) reads the engine's configuration.
//   - sub_8224B008(gui, &flags, &name) opens a GUI screen ("GameModePregame",
//     "GUIPopup", "GameModeCutscene"...).
//   - sub_82A8FF68(cutscene, plot, flag, name) plays a cutscene.
//   - sub_824B85B8(creature, mode, flag) starts character generation (mode 11) unless
//     the game object's +636 / +637 or +1672 of the object at 0x832220EC say not to;
//     sub_8244B228(mode) is the character generation mode's Enter.
//   - sub_825E4AD8(object, command) is the start menu's command handler (UTF-16:
//     "NewGame"...), sub_825D4C38(gui, command, args) the Scaleform bridge the menus'
//     ActionScript calls (ASCII command names).
//   - sub_822AF0D0(module, ...) initialises the module object from the campaign file;
//     its script name ("sp_module") is the string at +504.

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "warden_autosave.h"
#include "warden_debug_mode.h"
#include "warden_deck.h"
#include "warden_fsr3.h"
#include "warden_photo_mode.h"
#include "warden_guest_registers.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/d3d12/dao_frame_trace.h"

WARDEN_GAME_FUNC(__imp__tu6_sub_82348010);
WARDEN_GAME_FUNC(__imp__tu6_sub_82348590);
WARDEN_GAME_FUNC(__imp__tu6_sub_82A13FC8);
WARDEN_GAME_FUNC(__imp__tu6_sub_824B1EE8);
WARDEN_GAME_FUNC(__imp__tu6_sub_82330808);
WARDEN_GAME_FUNC(__imp__tu6_sub_82330878);
WARDEN_GAME_FUNC(__imp__tu6_sub_828D42E8);
WARDEN_GAME_FUNC(__imp__tu6_sub_824B18F8);
WARDEN_GAME_FUNC(__imp__tu6_sub_824C2F10);
WARDEN_GAME_FUNC(__imp__tu6_sub_8224B008);
WARDEN_GAME_FUNC(__imp__tu6_sub_82A8FF68);
WARDEN_GAME_FUNC(__imp__tu6_sub_824B85B8);
WARDEN_GAME_FUNC(__imp__tu6_sub_826CB6A8);
WARDEN_GAME_FUNC(__imp__tu6_sub_8244B228);
WARDEN_GAME_FUNC(__imp__tu6_sub_825E4AD8);
WARDEN_GAME_FUNC(__imp__tu6_sub_825D4C38);
WARDEN_GAME_FUNC(__imp__tu6_sub_822AF0D0);
WARDEN_GAME_FUNC(__imp__tu6_sub_82698458);

namespace {

constexpr uint32_t kCurrentModeOffset = 120;
constexpr uint32_t kModeNumberOffset = 96;

const char* ModeName(int32_t mode) {
  switch (mode) {
    case 0: return "explore";
    case 1: return "combat";
    case 2: return "dead";
    case 3: return "menu";
    case 4: return "conversation";
    case 5: return "cutscene";
    case 6: return "fly camera";
    case 7: return "fixed camera";
    case 8: return "pregame";
    case 9: return "loading";
    case 10: return "movie";
    case 11: return "character generation";
    case 12: return "level-up";
    case 13: return "party picker";
    case -1: return "none";
    default: return "?";
  }
}

int32_t CurrentMode(uint8_t* base, uint32_t controller) {
  if (!controller) {
    return -1;
  }
  const uint32_t current = PPC_LOAD_U32(controller + kCurrentModeOffset);
  return current ? int32_t(PPC_LOAD_U32(current + kModeNumberOffset)) : -1;
}

std::string EngineString(uint8_t* base, uint32_t s) {
  if (!s) {
    return "(null)";
  }
  const uint32_t length = PPC_LOAD_U32(s + 20);
  const uint32_t capacity = PPC_LOAD_U32(s + 24);
  const uint32_t data = capacity >= 8 ? PPC_LOAD_U32(s + 4) : s + 4;
  std::string out;
  for (uint32_t i = 0; data && i < length && i < 256; ++i) {
    const uint16_t c = PPC_LOAD_U16(data + i * 2);
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

std::string GuestAscii(uint8_t* base, uint32_t address) {
  std::string out;
  for (uint32_t i = 0; address && i < 128; ++i) {
    const uint8_t c = PPC_LOAD_U8(address + i);
    if (!c) {
      break;
    }
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

std::string GuestUtf16(uint8_t* base, uint32_t address) {
  std::string out;
  for (uint32_t i = 0; address && i < 128; ++i) {
    const uint16_t c = PPC_LOAD_U16(address + i * 2);
    if (!c) {
      break;
    }
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

uint32_t EventType(PPCContext& ctx, uint8_t* base, uint32_t event) {
  PPCContext probe = ctx;
  probe.r3.u64 = event;
  __imp__tu6_sub_824B18F8(probe, base);
  return probe.r3.u32;
}

// Below `limit`, true; the counter keeps counting so the cost stays one atomic add.
// PLAN 11.1: no budget in Debugging Mode - the whole log.
bool Budget(std::atomic<int>& counter, int limit) {
  return counter.fetch_add(1, std::memory_order_relaxed) < limit || warden::debug_mode::On();
}

std::atomic<int> g_mode_requests{0};
std::atomic<int32_t> g_last_mode{-2};
std::atomic<int> g_mode_changes{0};
std::atomic<int> g_title_logs{0};
std::atomic<int> g_dispatches{0};
std::atomic<int> g_config_lookups{0};
std::atomic<int> g_gui_opens{0};
std::atomic<int> g_cutscenes{0};
std::atomic<int> g_start_chargen{0};
std::atomic<int> g_chargen_enters{0};
std::atomic<int> g_menu_commands{0};
std::atomic<int> g_gui_commands{0};
std::atomic<int> g_module_inits{0};

}  // namespace

// The game asks its controller for a mode: logged with the chain that asked, the first
// few times.
WARDEN_OVERRIDE(tu6_sub_82348010) {
  const int32_t mode = int32_t(ctx.r4.u32);
  const int32_t before = CurrentMode(base, ctx.r3.u32);
  const int n = g_mode_requests.fetch_add(1, std::memory_order_relaxed);
  if (n < 200) {
    XELOGI("[{}] game: mode {} ({}) requested, current {} ({}){}", warden::CurrentGuestThreadName(),
           mode, ModeName(mode), before, ModeName(before), n < 12 ? "; asked by:" : "");
    if (n < 12) {
      warden::LogGuestCallChainOnFault(base);
    }
  }
  __imp__tu6_sub_82348010(ctx, base);
}

// files/trace-mode: "<mode> [<swaps> [<nth>]]" - when the game enters that mode for the
// nth time (default the first), the frame trace (FT lines) is taken <swaps> swaps later
// (default 90, about three seconds), once per run. 11 is character creation, 0
// exploring; on the ostagar route the second time in mode 0 is Ostagar.
namespace {
struct TraceAtMode {
  bool read = false;
  int32_t mode = -100;
  uint32_t swaps = 90;
  uint32_t nth = 1;
  uint32_t seen = 0;
  bool done = false;
};
TraceAtMode g_trace_at_mode;

void MaybeRequestTrace(int32_t mode) {
  TraceAtMode& t = g_trace_at_mode;
  if (!t.read) {
    t.read = true;
    if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
      if (FILE* file = fopen((std::string(dir) + "/trace-mode").c_str(), "r")) {
        int m = 0;
        unsigned s = 0;
        unsigned n = 0;
        const int got = fscanf(file, "%d %u %u", &m, &s, &n);
        if (got >= 1) {
          t.mode = m;
        }
        if (got >= 2) {
          t.swaps = s;
        }
        if (got >= 3 && n) {
          t.nth = n;
        }
        fclose(file);
        XELOGI("trace-mode: the frame {} swaps after entering mode {} ({}) for time {} will "
               "be traced", t.swaps, t.mode, ModeName(t.mode), t.nth);
      }
    }
  }
  if (!t.done && mode == t.mode && ++t.seen == t.nth) {
    t.done = true;
    xe::gpu::dao_trace::RequestTrace(t.swaps);
    XELOGI("trace-mode: mode {} entered; tracing the frame {} swaps from now", mode, t.swaps);
  }
}
}  // namespace

// files/send-message (PLAN 9.10, 9.13; a test marker): messages for the engine, one a
// line, `<mode> <seconds> <message...>` - sent once each, once the game has been in that
// mode that many seconds without leaving it, through the engine's own SendMessage (sub_825E47B0, which
// the UI's ExternalCommands.SendMessage calls with the message's text: it parses the
// name and its arguments, "SaveGameMessage -1 2", into a message object and posts it).
// Sent on the update thread, from the mode controller's update.
WARDEN_GAME_FUNC(__imp__tu6_sub_825E47B0);
WARDEN_GAME_FUNC(__imp__tu6_sub_823904D8);
WARDEN_GAME_FUNC(__imp__tu6_sub_824BB520);
WARDEN_GAME_FUNC(__imp__tu6_sub_82390360);
namespace {
struct PendingMessage {
  int32_t mode;
  double seconds;
  std::string text;
  bool sent = false;
};
std::vector<PendingMessage> g_messages;
int32_t g_message_mode = -1;
int64_t g_message_mode_since = 0;

int64_t SteadyNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void ReadMessages() {
  static bool read = false;
  if (read) {
    return;
  }
  read = true;
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  FILE* file = dir ? fopen((std::string(dir) + "/send-message").c_str(), "r") : nullptr;
  if (!file) {
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    int mode = 0;
    double seconds = 0;
    int used = 0;
    if (sscanf(line, "%d %lf %n", &mode, &seconds, &used) >= 2 && used > 0) {
      std::string text(line + used);
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
      }
      if (!text.empty() && mode >= 0 && mode < 16) {
        g_messages.push_back({mode, seconds, text});
        XELOGI("send-message: \"{}\" {} s into mode {} ({})", text, seconds, mode,
               ModeName(mode));
      }
    }
  }
  fclose(file);
}

void SendEngineMessage(PPCContext& ctx, uint8_t* base, const std::string& text) {
  const uint32_t sp = ctx.r1.u32;
  const uint32_t buffer = sp - 384;
  const size_t length = std::min<size_t>(text.size(), 250);
  for (size_t i = 0; i < length; ++i) {
    PPC_STORE_U8(buffer + uint32_t(i), uint8_t(text[i]));
  }
  PPC_STORE_U8(buffer + uint32_t(length), 0);
  PPCContext send = ctx;
  send.r1.u64 = sp - 512;
  send.r3.u64 = 0;
  send.r4.u64 = buffer;
  __imp__tu6_sub_825E47B0(send, base);
  XELOGI("[{}] send-message: \"{}\" sent", warden::CurrentGuestThreadName(), text);
}

// The deck's (QueueEngineMessage), sent as they come.
std::mutex g_queued_mutex;
std::vector<std::string> g_queued;

void SendQueuedMessages(PPCContext& ctx, uint8_t* base) {
  std::vector<std::string> due;
  {
    std::lock_guard<std::mutex> lock(g_queued_mutex);
    due.swap(g_queued);
  }
  for (const std::string& text : due) {
    SendEngineMessage(ctx, base, text);
  }
}

void SendDueMessages(PPCContext& ctx, uint8_t* base, int32_t mode) {
  if (g_messages.empty() || mode < 0 || mode >= 16) {
    return;
  }
  const int64_t now = SteadyNanos();
  if (mode != g_message_mode) {
    g_message_mode = mode;
    g_message_mode_since = now;
  }
  for (PendingMessage& message : g_messages) {
    if (!message.sent && message.mode == mode &&
        now - g_message_mode_since >= int64_t(message.seconds * 1e9)) {
      message.sent = true;
      SendEngineMessage(ctx, base, message.text);
    }
  }
}
}  // namespace

// PLAN 9.13: the autosave as the game is put away (warden_autosave.h), Settings ->
// Autosave when put away (files/autosave-on-pause: "on" turns it on; off without it -
// the owner's default, 10.1 - and the options panel switches it while the game runs).
// The save is the engine's own DoAutoSave (sub_824BB520, what some 200 compiled scripts
// call): if the game allows saving (a flag at [0x832E20EC]+216) it sends the save
// manager ([game]+292) the save message with the autosave's arguments (-1, 1, its name),
// and the load/save state machine's request method (sub_8238FE48, which also checks
// the mode allows a save) takes it to state 3 - the very path an area load's autosave
// takes (runs 387-388). Called on the update thread, on the frame after the pause, and
// only if the game is still exploring then.
namespace {
std::atomic<bool> g_autosave_requested{false};

// -1 until the file is read; then 0 or 1, and the options panel's from then on.
std::atomic<int> g_autosave_wanted{-1};

bool AutosaveWanted() {
  int wanted = g_autosave_wanted.load();
  if (wanted < 0) {
    bool on = false;
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    if (FILE* file = dir ? fopen((std::string(dir) + "/autosave-on-pause").c_str(), "r")
                         : nullptr) {
      char text[16] = {};
      on = fscanf(file, "%15s", text) == 1 && strcmp(text, "on") == 0;
      fclose(file);
    }
    XELOGI("autosave on pause: {}", on ? "on (the game's own autosave, while exploring)"
                                       : "off");
    int unread = -1;
    g_autosave_wanted.compare_exchange_strong(unread, on ? 1 : 0);
    wanted = g_autosave_wanted.load();
  }
  return wanted == 1;
}

// The load/save state machine (class vtable 82188678; its per-frame update
// sub_82390360, kept below with files/save-chain): its state, logged around the call.
std::atomic<uint32_t> g_save_machine{0};
std::atomic<uint32_t> g_save_machine_updates{0};

void SendAutosaveIfAsked(PPCContext& ctx, uint8_t* base, int32_t mode) {
  if (!g_autosave_requested.exchange(false)) {
    return;
  }
  if (mode != 0) {
    XELOGI("autosave on pause: the game left explore mode (now {}, {}); none asked for",
           mode, ModeName(mode));
    return;
  }
  const uint32_t machine = g_save_machine.load();
  const uint16_t before = machine ? PPC_LOAD_U16(machine + 144) : 0;
  PPCContext call = ctx;
  call.r1.u64 = ctx.r1.u32 - 512;
  __imp__tu6_sub_824BB520(call, base);
  XELOGI("autosave on pause: DoAutoSave called (the load/save state machine {} -> {})",
         before, machine ? PPC_LOAD_U16(machine + 144) : 0);
}
}  // namespace

namespace warden {
bool AutosaveOnPauseEnabled() { return AutosaveWanted(); }

void SetAutosaveOnPause(bool on) { g_autosave_wanted.store(on ? 1 : 0); }

bool RequestAutosave(bool forced) {
  if (!forced && !AutosaveOnPauseEnabled()) {
    return false;
  }
  const int32_t mode = g_last_mode.load();
  if (mode != 0) {
    XELOGI("autosave on pause: not exploring (mode {}, {}); none", mode, ModeName(mode));
    return false;
  }
  g_autosave_requested.store(true);
  XELOGI("autosave on pause: asked for at the next frame");
  return true;
}

// The engine's game mode as the controller's update last returned it (-2 before the
// first): the deck's quickbar guard (PLAN 8.8) - the HUD's own copy does not see menus.
int32_t GameMode() { return g_last_mode.load(); }

void QueueEngineMessage(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_queued_mutex);
  g_queued.push_back(text);
}
}  // namespace warden

// The controller's update: the current mode, logged when it changes.
void CheckRouteCall(PPCContext& ctx, uint8_t* base);  // PLAN 10.11, below

void RunCheats(PPCContext& ctx, uint8_t* base, int32_t mode);  // PLAN 10.14, below

WARDEN_OVERRIDE(tu6_sub_82348590) {
  const uint32_t controller = ctx.r3.u32;
  warden::photo::BeforeModeUpdate(ctx, base, controller);  // PLAN 9.20
  __imp__tu6_sub_82348590(ctx, base);
  warden::photo::AfterModeUpdate(ctx, base, controller);
  const int32_t mode = CurrentMode(base, controller);
  ReadMessages();
  SendQueuedMessages(ctx, base);
  SendDueMessages(ctx, base, mode);
  SendAutosaveIfAsked(ctx, base, mode);
  if (mode != g_last_mode.exchange(mode)) {
    // PLAN 10.11: unbudgeted - the routes and the areas' tour read the mode from it,
    // and a session of debug menus changes it hundreds of times.
    XELOGI("[{}] game: mode is now {} ({})", warden::CurrentGuestThreadName(), mode,
           ModeName(mode));
    MaybeRequestTrace(mode);
  }
  CheckRouteCall(ctx, base);  // PLAN 10.11: every frame, whatever the mode
  RunCheats(ctx, base, mode);  // PLAN 10.14
}

// files/save-chain (PLAN 9.13, a test marker): the guest call chain at each save's
// "module pre-saving" - what started the save.
static bool SaveChainWanted() {
  static const bool wanted = [] {
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    return dir && access((std::string(dir) + "/save-chain").c_str(), F_OK) == 0;
  }();
  return wanted;
}

// The title's own log, as `title log LEVEL: event - message`.
WARDEN_OVERRIDE(tu6_sub_82A13FC8) {
  const bool budget = Budget(g_title_logs, 2000);
  if (budget || SaveChainWanted()) {
    const std::string event = EngineString(base, ctx.r4.u32);
    if (budget) {
      XELOGI("[{}] title log {}: {} - {}", warden::CurrentGuestThreadName(), ctx.r3.u32,
             event, EngineString(base, ctx.r5.u32));
    }
    if (SaveChainWanted() && event == "EVENT_TYPE_MODULE_PRESAVE") {
      XELOGI("[{}] save-chain: the call chain at the module's pre-save (lr {:08X})",
             warden::CurrentGuestThreadName(), uint32_t(ctx.lr));
      warden::LogGuestCallChainOnFault(base);
    }
  }
  __imp__tu6_sub_82A13FC8(ctx, base);
}

// files/route - an automatic route through the game, for testing without hands (plan
// 6.0). One instruction per line:
//   skip-start          the module's own start script (sp_module_start.ncs, on the
//                       module load and start events, 20 and 19) is given a script name
//                       that does not exist, so a new game falls back to the default
//                       start area with a default player - what BUG-046 did by accident,
//                       without the intro or character creation.
//   run <script> [mode] once the game is in that mode (default 0, exploring), the next
//                       event the module script handles is also dispatched to
//                       <script>.ncs, once - the console build registers its scripts,
//                       the developer debug scripts included (zz_pre_debug and the
//                       like), natively by name.
//   call <script> [mode] the same moment, but the script's native main is looked up in
//                       the engine's script registry (sub_82330878, by name without
//                       .ncs) and called directly, outside the dispatcher.
namespace {
struct Route {
  bool read = false;
  bool skip_start = false;
  std::string script;
  int32_t mode = 0;
  bool call = false;
  bool ran = false;
};
Route g_route;

void ReadRoute() {
  Route& r = g_route;
  if (r.read) {
    return;
  }
  r.read = true;
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  FILE* file = dir ? fopen((std::string(dir) + "/route").c_str(), "r") : nullptr;
  if (!file) {
    return;
  }
  char line[160];
  while (fgets(line, sizeof(line), file)) {
    char word[32] = {};
    char script[64] = {};
    int mode = 0;
    const int got = sscanf(line, "%31s %63s %d", word, script, &mode);
    if (got >= 1 && std::string(word) == "skip-start") {
      r.skip_start = true;
    } else if (got >= 2 && (std::string(word) == "run" || std::string(word) == "call")) {
      r.script = script;
      r.mode = got >= 3 ? mode : 0;
      r.call = std::string(word) == "call";
    }
  }
  fclose(file);
  XELOGI("route: skip-start {}; {} \"{}\" in mode {} ({})", r.skip_start,
         r.call ? "call" : "run", r.script, r.mode, ModeName(r.mode));
}

// Builds an engine string from ASCII with the engine's constructor (sub_82330808) at
// `name`, the text at `text`, both in scratch space below sp; the calls' frames go
// below sp - 512.
void MakeEngineString(PPCContext& ctx, uint8_t* base, uint32_t sp, uint32_t name,
                      uint32_t text, const std::string& value) {
  for (size_t i = 0; i < 63; ++i) {
    PPC_STORE_U8(text + uint32_t(i), i < value.size() ? uint8_t(value[i]) : uint8_t(0));
  }
  for (uint32_t i = 0; i < 64; i += 4) {
    PPC_STORE_U32(name + i, 0);
  }
  PPCContext make = ctx;
  make.r1.u64 = sp - 512;
  make.r3.u64 = name;
  make.r4.u64 = text;
  __imp__tu6_sub_82330808(make, base);
}

// Calls a script's native main, found in the engine's registry by name (no .ncs):
// sub_82330878(name) returns the registry slot, whose first word is the main.
bool CallScriptMain(PPCContext& ctx, uint8_t* base, const std::string& script) {
  const uint32_t sp = ctx.r1.u32;
  MakeEngineString(ctx, base, sp, sp - 128, sp - 256, script);
  PPCContext find = ctx;
  find.r1.u64 = sp - 512;
  find.r3.u64 = sp - 128;
  __imp__tu6_sub_82330878(find, base);
  const uint32_t slot = find.r3.u32;
  const uint32_t main = slot ? PPC_LOAD_U32(slot) : 0;
  XELOGI("route: registry slot for {} is {:08X}, main {:08X}", script, slot, main);
  if (!main) {
    return false;
  }
  PPCFunc* function = PPC_LOOKUP_FUNC(base, main);
  if (!function) {
    XELOGE("route: {:08X} is not a function", main);
    return false;
  }
  PPCContext run = ctx;
  run.r1.u64 = sp - 512;
  function(run, base);
  XELOGI("route: {}'s main returned {:08X}", script, run.r3.u32);
  return true;
}

// Dispatches `event` to the script `script` (with its .ncs) the way the engine does: the
// name is built as an engine string by the engine's own constructor (sub_82330808, which
// takes ASCII), in scratch space below the caller's stack pointer - the callees' frames
// go below that. Returns the dispatcher's result.
uint32_t DispatchToScript(PPCContext& ctx, uint8_t* base, uint32_t event,
                          const std::string& script) {
  const uint32_t sp = ctx.r1.u32;
  const uint32_t name = sp - 128;
  MakeEngineString(ctx, base, sp, name, sp - 256, script);
  PPCContext run = ctx;
  run.r1.u64 = sp - 512;
  run.r3.u64 = event;
  run.r4.u64 = name;
  __imp__tu6_sub_824B1EE8(run, base);
  return run.r3.u32;
}
}  // namespace

// BUG-085, BUG-086: the game's occlusion-query culling off, as DAORecomp does
// (gpu/d3d_queries.cpp). The renderer culls objects by drawing each candidate's bounding
// box inside a D3D occlusion query and reading the sample count back a frame later;
// Xenia's ZPD emulation does not reproduce the counter snapshots those counts are made of,
// so an object reads 0 samples and is skipped - on the Thor whole areas' environment
// (the Arl of Denerim's estate: 143 draws a frame against about 400, the characters and
// props, which are not queried, alone drawn; Awakening's Smuggler's Cove looking out of
// its tunnel). The engine has its own switch for running without queries (the byte at
// 0x832E2404 in the TU build, set at renderer init, sub_8266F388, from a config value):
// queries are then skipped and every one reports visible. files/occlusion-queries keeps
// them (for comparison).
WARDEN_GAME_FUNC(__imp__tu6_sub_8266F388);
WARDEN_OVERRIDE(tu6_sub_8266F388) {
  __imp__tu6_sub_8266F388(ctx, base);
  constexpr uint32_t kQueriesDisabledFlag = 0x832E2404;
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  const bool keep = dir && access((std::string(dir) + "/occlusion-queries").c_str(), F_OK) == 0;
  if (keep) {
    XELOGI("game: occlusion-query culling kept (files/occlusion-queries); the flag is {}",
           base[kQueriesDisabledFlag]);
  } else if (!base[kQueriesDisabledFlag]) {
    base[kQueriesDisabledFlag] = 1;
    XELOGI("game: occlusion-query culling off (BUG-085: Xenia's ZPD counts are unreliable for it)");
  }
}

// zz_pre_debug's native main (the prelude debug script, registered by sub_82306F10):
// logged, to see whether the route reached it.
WARDEN_OVERRIDE(tu6_sub_828D42E8) {
  XELOGI("[{}] route: zz_pre_debug's main entered", warden::CurrentGuestThreadName());
  __imp__tu6_sub_828D42E8(ctx, base);
  XELOGI("route: zz_pre_debug's main returned {:08X}", ctx.r3.u32);
}

// PLAN 10.11: files/route-call - a script called by name while the game runs, as the
// route's `call` does once: the file's first word is the script, looked up in the
// registry and its main called on the game thread (checked about once a second from the
// per-frame GUI hook), and the file removed. So one boot can open any debug menu on
// demand and chain area jumps, for the areas' profiling. A `!name` is dispatched as
// name.ncs instead, with the module script's next event (the dispatcher hook below).
std::string g_pending_dispatch;
int64_t g_pending_since = 0;  // when it was asked for (ms); any script's event after 3 s

// The engine's RunScript (what the PC version's console "runscript" called, and what
// DAORecomp's cheats call: its ui/cheats.cpp names these for the TU build):
// RunScript(engine, name, object, objectValid, event) runs the script `name` (one of the
// engine's wide strings) with `object` as its OBJECT_SELF, event -1 for none; GetHero()
// is the player's character; the wide string is made by sub_8261CB00(string, chars)
// from UTF-16 and freed by sub_82253270(string, 1, 0). The script engine is the pointer
// at 0x8324246C.
WARDEN_GAME_FUNC(__imp__tu6_sub_8263FFD8);  // RunScript
WARDEN_GAME_FUNC(__imp__tu6_sub_824BB4B0);  // GetHero
WARDEN_GAME_FUNC(__imp__tu6_sub_8261CB00);  // wide string: construct from chars
WARDEN_GAME_FUNC(__imp__tu6_sub_82253270);  // wide string: destroy
// GetModule() and SetLocalString(object, name, value), the scripting commands (their wide
// strings passed by value: the command destroys them) - for RUNSCRIPT_VAR, the module
// variable the console's "runscript <name> <value>" set and the debug scripts read.
WARDEN_GAME_FUNC(__imp__tu6_sub_824B3AE8);  // GetModule
WARDEN_GAME_FUNC(__imp__tu6_sub_824AFCE8);  // SetLocalString
constexpr uint32_t kScriptEngine = 0x8324246C;

// Writes `text` (ASCII, up to 63 characters) as UTF-16 at `chars` and makes an engine
// wide string of it at `string`, both in scratch space below sp.
void MakeWideString(PPCContext& ctx, uint8_t* base, uint32_t sp, uint32_t string, uint32_t chars,
                    const std::string& text) {
  for (size_t i = 0; i < 64; ++i) {
    PPC_STORE_U16(chars + uint32_t(i) * 2, i < text.size() && i < 63 ? uint16_t(uint8_t(text[i])) : 0);
  }
  PPCContext run = ctx;
  run.r1.u64 = sp - 512;
  run.r3.u64 = string;
  run.r4.u64 = chars;
  __imp__tu6_sub_8261CB00(run, base);
}

// The module's RUNSCRIPT_VAR set to `value` (empty clears it).
void SetRunscriptVar(PPCContext& ctx, uint8_t* base, const std::string& value) {
  const uint32_t sp = ctx.r1.u32;
  PPCContext run = ctx;
  run.r1.u64 = sp - 512;
  __imp__tu6_sub_824B3AE8(run, base);
  const uint32_t module = run.r3.u32;
  if (module == 0xFFFFFFFFu) {
    XELOGW("route: no module for RUNSCRIPT_VAR");
    return;
  }
  const uint32_t variable = sp - 160, text = sp - 192, chars = sp - 384;
  MakeWideString(ctx, base, sp, variable, chars, "RUNSCRIPT_VAR");
  MakeWideString(ctx, base, sp, text, chars, value);
  run = ctx;
  run.r1.u64 = sp - 512;
  run.r3.u64 = module;
  run.r4.u64 = variable;
  run.r5.u64 = text;
  __imp__tu6_sub_824AFCE8(run, base);
}

// PLAN 10.11: a script run by name through RunScript as the player's character - the
// downloadable content's .ncs scripts (zz_gwb_debug, zz_vgk_debug and the like) and the
// natively registered ones alike; the debug scripts open their dialogue with the hero.
// A value, if given, is the module's RUNSCRIPT_VAR while it runs (the console's
// "runscript zz_drk market"), emptied after.
bool RunScriptAsHero(PPCContext& ctx, uint8_t* base, const std::string& name,
                     const std::string& value = std::string(), uint32_t self = 0) {
  const uint32_t engine = PPC_LOAD_U32(kScriptEngine);
  if (!engine) {
    XELOGW("route: no script engine yet for {}", name);
    return false;
  }
  const uint32_t sp = ctx.r1.u32;
  const uint32_t string = sp - 128;  // the wide string object (28 bytes)
  const uint32_t chars = sp - 384;   // its text, UTF-16, up to 63 characters
  for (size_t i = 0; i < 64; ++i) {
    PPC_STORE_U16(chars + uint32_t(i) * 2, i < name.size() ? uint16_t(uint8_t(name[i])) : 0);
  }
  if (!value.empty()) {
    SetRunscriptVar(ctx, base, value);
    for (size_t i = 0; i < 64; ++i) {  // the name's text again (SetRunscriptVar used chars)
      PPC_STORE_U16(chars + uint32_t(i) * 2, i < name.size() ? uint16_t(uint8_t(name[i])) : 0);
    }
  }
  PPCContext run = ctx;
  run.r1.u64 = sp - 512;
  __imp__tu6_sub_824BB4B0(run, base);
  // PLAN 10.14: a party member of the caller's choosing, else the player's character.
  const uint32_t hero = self ? self : run.r3.u32;
  if (hero == 0xFFFFFFFFu) {
    XELOGW("route: no player's character to run {} as", name);
    return false;
  }
  run = ctx;
  run.r1.u64 = sp - 512;
  run.r3.u64 = string;
  run.r4.u64 = chars;
  __imp__tu6_sub_8261CB00(run, base);
  run = ctx;
  run.r1.u64 = sp - 512;
  run.r3.u64 = engine;
  run.r4.u64 = string;
  run.r5.u64 = hero;
  run.r6.u64 = 1;
  run.r7.u64 = 0xFFFFFFFFu;
  __imp__tu6_sub_8263FFD8(run, base);
  const uint32_t result = run.r3.u32;
  run = ctx;
  run.r1.u64 = sp - 512;
  run.r3.u64 = string;
  run.r4.u64 = 1;
  run.r5.u64 = 0;
  __imp__tu6_sub_82253270(run, base);
  if (!value.empty()) {
    SetRunscriptVar(ctx, base, std::string());
  }
  XELOGI("route: RunScript({}{}{}) as the player's character {:08X} returned {:08X}", name,
         value.empty() ? "" : " ", value, hero, result);
  return true;
}

// ---- PLAN 10.14: the cheats menu, upstream's (ui/cheats.cpp) ----
//
// The developers' own debug scripts, as the PC console's "runscript <name> <value>" ran
// them, through RunScriptAsHero: pc_immortal (makes the creature it runs as immortal, or
// mortal again), zz_override_skills 1 or 0 (every skill check passes, or as the game
// decides), killallhostiles, zz_money <coppers> and addxp <points>. Asked for from the
// options panel (Runtime::ApplyOption -> warden::RequestCheat), run at the next frame the
// game explores or fights, on the game's own thread.
WARDEN_GAME_FUNC(__imp__tu6_sub_824C0EA8);  // GetPartyList(result, -1)
WARDEN_GAME_FUNC(__imp__tu6_sub_8236CE70);  // DestroyArray(array)
WARDEN_GAME_FUNC(__imp__tu6_sub_824B3918);  // IsImmortal(creature)

namespace {
struct CheatRequest {
  std::string name;
  std::string value;
};
std::mutex g_cheats_mutex;
std::vector<CheatRequest> g_cheats;

// The party's creatures, the player's character among them (at most 8).
std::vector<uint32_t> PartyMembers(PPCContext& ctx, uint8_t* base) {
  const uint32_t array = (ctx.r1.u32 - 480) & ~0xFu;  // 20 bytes, below the hook's frame
  PPCContext call = ctx;
  call.r1.u64 = ctx.r1.u32 - 768;
  call.r3.u64 = array;
  call.r4.u64 = 0xFFFFFFFFu;
  __imp__tu6_sub_824C0EA8(call, base);
  std::vector<uint32_t> members;
  const uint32_t begin = PPC_LOAD_U32(array + 8), end = PPC_LOAD_U32(array + 12);
  if (begin % 4 == 0 && end >= begin && (end - begin) % 4 == 0) {
    for (uint32_t at = begin; at < end && members.size() < 8; at += 4) {
      members.push_back(PPC_LOAD_U32(at));
    }
  }
  call = ctx;
  call.r1.u64 = ctx.r1.u32 - 768;
  call.r3.u64 = array;
  __imp__tu6_sub_8236CE70(call, base);
  return members;
}

bool IsImmortal(PPCContext& ctx, uint8_t* base, uint32_t creature) {
  PPCContext call = ctx;
  call.r1.u64 = ctx.r1.u32 - 768;
  call.r3.u64 = creature;
  __imp__tu6_sub_824B3918(call, base);
  return call.r3.u32 != 0;
}
}  // namespace

namespace warden {
void RequestCheat(const std::string& name, const std::string& value) {
  std::lock_guard<std::mutex> lock(g_cheats_mutex);
  if (g_cheats.size() < 16) {
    g_cheats.push_back({name, value});
  }
  XELOGI("cheats: {} {} asked for, at the next frame of play", name, value);
}
}  // namespace warden

void RunCheats(PPCContext& ctx, uint8_t* base, int32_t mode) {
  if (mode != 0 && mode != 1) {  // exploring or fighting
    return;
  }
  std::vector<CheatRequest> requests;
  {
    std::lock_guard<std::mutex> lock(g_cheats_mutex);
    if (g_cheats.empty()) {
      return;
    }
    requests.swap(g_cheats);
  }
  for (const CheatRequest& request : requests) {
    if (request.name == "cheat-immortal") {
      // pc_immortal turns each member it runs as: the mortal ones when on, the
      // immortal ones when off.
      const bool on = request.value == "on";
      int turned = 0;
      for (const uint32_t member : PartyMembers(ctx, base)) {
        if (IsImmortal(ctx, base, member) != on) {
          RunScriptAsHero(ctx, base, "pc_immortal", std::string(), member);
          ++turned;
        }
      }
      XELOGI("cheats: the party {} ({} turned)", on ? "immortal" : "mortal again", turned);
    } else if (request.name == "cheat-skills") {
      RunScriptAsHero(ctx, base, "zz_override_skills", request.value == "on" ? "1" : "0");
    } else if (request.name == "cheat-kill") {
      RunScriptAsHero(ctx, base, "killallhostiles");
    } else if (request.name == "cheat-gold") {
      // In coppers: 100 to a silver, 100 silver to a gold.
      const long gold = std::atol(request.value.c_str());
      if (gold > 0) {
        RunScriptAsHero(ctx, base, "zz_money", std::to_string(gold * 10000));
      }
    } else if (request.name == "cheat-xp") {
      const long points = std::atol(request.value.c_str());
      if (points > 0) {
        RunScriptAsHero(ctx, base, "addxp", std::to_string(points));
      }
    }
  }
}

int64_t NowMs() {
  return int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count());
}

void CheckRouteCall(PPCContext& ctx, uint8_t* base) {
  static int64_t last_check = 0;
  const int64_t now = int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count());
  if (now - last_check < 1000) {
    return;
  }
  last_check = now;
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  const std::string path = dir ? std::string(dir) + "/route-call" : std::string();
  FILE* file = dir ? fopen(path.c_str(), "r") : nullptr;
  if (!file) {
    return;
  }
  char name[64] = {};
  char value[64] = {};  // a second word: RUNSCRIPT_VAR for a `!name value`
  const bool got = fscanf(file, "%63s %63s", name, value) >= 1;
  fclose(file);
  remove(path.c_str());
  if (!got) {
    return;
  }
  // A leading ! runs <name> through the engine's RunScript as the player's character
  // (the PC console's "runscript"): the downloadable content's modules (PLAN 10.10)
  // ship their scripts as .ncs resources, their debug scripts included (zz_gwb_debug,
  // zz_vgk_debug and the like), which the registry does not hold.
  if (name[0] == '!') {
    XELOGI("route: route-call !{} {} in mode {} ({}): RunScript as the player's character",
           name + 1, value, g_last_mode.load(), ModeName(g_last_mode.load()));
    RunScriptAsHero(ctx, base, name + 1, value);
    return;
  }
  // A leading ~ dispatches <name>.ncs through the engine's dispatcher with the module
  // script's next event instead (tried first for the DLC's scripts: no menu opened).
  if (name[0] == '~') {
    g_pending_dispatch = name + 1;
    g_pending_since = now;
    XELOGI("route: route-call ~{}: {}.ncs is dispatched with the module script's next event",
           name + 1, name + 1);
    return;
  }
  // A leading ? asks only whether the script is in the registry.
  if (name[0] == '?') {
    const uint32_t sp = ctx.r1.u32;
    MakeEngineString(ctx, base, sp, sp - 128, sp - 256, name + 1);
    PPCContext find = ctx;
    find.r1.u64 = sp - 512;
    find.r3.u64 = sp - 128;
    __imp__tu6_sub_82330878(find, base);
    const uint32_t slot = find.r3.u32;
    XELOGI("route: route-call ?{}: registry slot {:08X}, main {:08X}", name + 1, slot,
           slot ? PPC_LOAD_U32(slot) : 0);
    return;
  }
  XELOGI("route: route-call {} in mode {} ({})", name, g_last_mode.load(),
         ModeName(g_last_mode.load()));
  if (!CallScriptMain(ctx, base, name)) {
    XELOGW("route: route-call {} is not in the registry", name);
  }
}

// A script run on an event.
WARDEN_OVERRIDE(tu6_sub_824B1EE8) {
  ReadRoute();
  const bool log = Budget(g_dispatches, 300);
  const bool route = g_route.skip_start || (!g_route.script.empty() && !g_route.ran) ||
                     !g_pending_dispatch.empty();
  const std::string script = log || route ? EngineString(base, ctx.r4.u32) : std::string();
  const uint32_t event = ctx.r3.u32;
  if (log) {
    XELOGI("[{}] game: event type {} -> script \"{}\"", warden::CurrentGuestThreadName(),
           EventType(ctx, base, event), script);
  }
  // skip-start hides the module's start script - the main campaign's
  // sp_module_start.ncs, and a downloadable campaign's (gxa_module_start.ncs is
  // Awakening's, lel_module_start.ncs Leliana's Song's) - so New Game falls back to
  // the module's default start area with a default player.
  if (g_route.skip_start && script.find("module_start") != std::string::npos) {
    XELOGI("route: event type {} not given to {} (skip-start)", EventType(ctx, base, event),
           script);
    ctx.r3.u64 = DispatchToScript(ctx, base, event, "warden_no_such_script.ncs");
    return;
  }
  __imp__tu6_sub_824B1EE8(ctx, base);
  // PLAN 10.11: a route-call's !name, dispatched as name.ncs with the module script's
  // event (a creature's or a placeable's would make that object the script's self) -
  // or with any script's after 3 s, should the module get none.
  if (!g_pending_dispatch.empty() &&
      (script.find("module") != std::string::npos || NowMs() - g_pending_since > 3000)) {
    const std::string pending = g_pending_dispatch;
    g_pending_dispatch.clear();
    const uint32_t result = ctx.r3.u32;
    XELOGI("route: dispatching event type {} (of {}) to {}.ncs in mode {} ({})",
           EventType(ctx, base, event), script, pending, g_last_mode.load(),
           ModeName(g_last_mode.load()));
    const uint32_t ran = DispatchToScript(ctx, base, event, pending + ".ncs");
    XELOGI("route: {}.ncs returned {:08X}", pending, ran);
    ctx.r3.u64 = result;
  }
  if (!g_route.script.empty() && !g_route.ran && script == "module_core.ncs" &&
      g_last_mode.load() == g_route.mode) {
    g_route.ran = true;
    const uint32_t result = ctx.r3.u32;
    if (g_route.call) {
      XELOGI("route: mode {} ({}) reached; calling {}'s main", g_route.mode,
             ModeName(g_route.mode), g_route.script);
      CallScriptMain(ctx, base, g_route.script);
    } else {
      XELOGI("route: mode {} ({}) reached; dispatching event type {} to {}.ncs",
             g_route.mode, ModeName(g_route.mode), EventType(ctx, base, event),
             g_route.script);
      const uint32_t ran = DispatchToScript(ctx, base, event, g_route.script + ".ncs");
      XELOGI("route: {}.ncs returned {:08X}", g_route.script, ran);
    }
    ctx.r3.u64 = result;
  }
}

// A configuration lookup (DebugOptions / SkipCharGen and the like).
WARDEN_OVERRIDE(tu6_sub_824C2F10) {
  const uint32_t out = ctx.r3.u32;
  const bool log = Budget(g_config_lookups, 200);
  const std::string section = log ? EngineString(base, ctx.r4.u32) : std::string();
  const std::string key = log ? EngineString(base, ctx.r5.u32) : std::string();
  __imp__tu6_sub_824C2F10(ctx, base);
  if (log) {
    XELOGI("[{}] game: config {} / {} -> \"{}\"", warden::CurrentGuestThreadName(), section, key,
           EngineString(base, out));
  }
}

// A GUI screen opened. The cursor's hover screens are opened every frame and skipped.
WARDEN_OVERRIDE(tu6_sub_8224B008) {
  CheckRouteCall(ctx, base);  // PLAN 10.11
  const std::string name = EngineString(base, ctx.r5.u32);
  const bool log = name.rfind("CursorOver", 0) != 0 && Budget(g_gui_opens, 300);
  const uint32_t flags = ctx.r4.u32 ? PPC_LOAD_U32(ctx.r4.u32) : 0;
  __imp__tu6_sub_8224B008(ctx, base);
  if (log) {
    XELOGI("[{}] game: GUI open \"{}\" (flags {:08X}) -> {:08X}", warden::CurrentGuestThreadName(),
           name, flags, ctx.r3.u32);
  }
}

// A cutscene played.
WARDEN_OVERRIDE(tu6_sub_82A8FF68) {
  if (Budget(g_cutscenes, 40)) {
    XELOGI("[{}] game: play cutscene \"{}\" (plot \"{}\", flag {}, name \"{}\")",
           warden::CurrentGuestThreadName(), EngineString(base, ctx.r3.u32),
           EngineString(base, ctx.r4.u32), ctx.r5.u32, EngineString(base, ctx.r6.u32));
  }
  __imp__tu6_sub_82A8FF68(ctx, base);
}

// StartCharGen, with the three bytes that can refuse it.
WARDEN_OVERRIDE(tu6_sub_824B85B8) {
  if (Budget(g_start_chargen, 8)) {
    PPCContext probe = ctx;
    __imp__tu6_sub_826CB6A8(probe, base);
    const uint32_t game = probe.r3.u32;
    const uint32_t globals = PPC_LOAD_U32(0x832220ECu);
    XELOGW("[{}] game: StartCharGen(creature {:08X}, mode {}, flag {}): game {:08X} +636 {} +637 {}, "
           "globals {:08X} +1672 {}; asked by:",
           warden::CurrentGuestThreadName(), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, game,
           game ? PPC_LOAD_U8(game + 636) : 0, game ? PPC_LOAD_U8(game + 637) : 0, globals,
           globals ? PPC_LOAD_U8(globals + 1672) : 0);
    warden::LogGuestCallChainOnFault(base);
  }
  __imp__tu6_sub_824B85B8(ctx, base);
}

// The character generation mode's Enter.
WARDEN_OVERRIDE(tu6_sub_8244B228) {
  const bool log = Budget(g_chargen_enters, 8);
  if (log) {
    XELOGW("[{}] game: character generation mode entered (mode object {:08X})",
           warden::CurrentGuestThreadName(), ctx.r3.u32);
  }
  __imp__tu6_sub_8244B228(ctx, base);
}

// The start menu's commands ("NewGame"...).
WARDEN_OVERRIDE(tu6_sub_825E4AD8) {
  if (Budget(g_menu_commands, 100)) {
    XELOGI("[{}] game: start menu command \"{}\"", warden::CurrentGuestThreadName(),
           GuestUtf16(base, ctx.r4.u32));
  }
  __imp__tu6_sub_825E4AD8(ctx, base);
}

// The Scaleform menus' calls into the game. Only the command names: the arguments are
// ActionScript values, not strings.
WARDEN_OVERRIDE(tu6_sub_825D4C38) {
  if (Budget(g_gui_commands, 300)) {
    const std::string command = GuestAscii(base, ctx.r4.u32);
    if (command != "GetTlkString" && command != "GetNumMembers") {
      XELOGI("[{}] game: GUI command \"{}\"", warden::CurrentGuestThreadName(), command);
    }
  }
  __imp__tu6_sub_825D4C38(ctx, base);
}

// The 3D scene's tiling: sub_826955E8 returns the scene's tile count and stores a
// pointer to its tile rectangles at *r4 - with the game's 2x MSAA two tiles, 1280x512
// and 1280x208, at 0x8215E5A8 (D3DRECT {x1, y1, x2, y2}[2]). It runs every frame,
// sets the scene's MSAA type (0x8323E83C, D3DMULTISAMPLE_TYPE, 0 none) itself, and the
// engine makes its render targets from the result. By default it is made one 1280x720
// tile without MSAA, as DAORecomp's SceneTilesHook (gpu/video.cpp) does for its
// upscaler: the Adreno is the limit in the 3D scene, and at Ostagar's King's Camp one
// tile without MSAA runs at about 20 frames a second against about 13.5 for the
// console's two 2x MSAA tiles (runs 183 and 176; the second tile re-draws about a
// third of the scene). files/scene-msaa keeps the console's tiles and 2x MSAA.
WARDEN_GAME_FUNC(__imp__tu6_sub_826955E8);

namespace {
constexpr uint32_t kSceneMsaaType = 0x8323E83C;
constexpr uint32_t kSceneTileRects = 0x8215E5A8;

bool SingleTileScene() {
  static const bool single = [] {
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    FILE* file = dir ? fopen((std::string(dir) + "/scene-msaa").c_str(), "r") : nullptr;
    if (file) {
      fclose(file);
      XELOGW("scene-msaa: the 3D scene renders as the console's two 2x MSAA tiles");
      return false;
    }
    XELOGI("scene: the 3D scene renders as one 1280x720 tile without MSAA "
           "(files/scene-msaa for the console's tiles)");
    return true;
  }();
  return single;
}
}  // namespace

// PLAN 9.8: the engine's dynamic shadows and post-processing - the PC version's
// EnableShadows and EnableFrameBufferEffects, left in the 360 engine, as DAORecomp's
// graphics menu switches them (ui/graphics_menu.cpp): the shadows bit (2) of the render
// flags' last byte and the frame buffer effects byte, which the engine sets from its
// options. While Settings -> Dynamic shadows or Post-processing says off (files/shadows,
// files/post-processing holding "off"), they are cleared every frame before the scene's
// tiling, as DAORecomp does; only the shadows bit of the flags changes, atomically.
// PLAN 10.1: the options panel switches them while the game runs (SetDynamicShadows,
// SetPostProcessing); switched back on, each is the engine's own option again, as its
// options code would set it (the options object at 0x832E20F0, its two option bytes).
namespace {
constexpr uint32_t kRenderFlagsLowByte = 0x832E238F;
constexpr uint8_t kRenderFlagShadows = 2;
constexpr uint32_t kFrameBufferEffects = 0x832E23D4;
constexpr uint32_t kEngineOptions = 0x832E20F0;
constexpr uint32_t kOptionEnableShadows = 0x510;
constexpr uint32_t kOptionEnableFrameBufferEffects = 0x520;

bool SettingOff(const char* file) {
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  FILE* f = dir ? fopen((std::string(dir) + "/" + file).c_str(), "r") : nullptr;
  if (!f) {
    return false;
  }
  char text[16] = {};
  const bool off = fscanf(f, "%15s", text) == 1 && std::string(text) == "off";
  fclose(f);
  return off;
}

// What the settings ask for: read from the files once, then the options panel's.
std::atomic<bool> g_shadows_on{true};
std::atomic<bool> g_effects_on{true};
std::once_flag g_switches_read;

void ReadEngineSwitches() {
  std::call_once(g_switches_read, [] {
    const bool shadows_off = SettingOff("shadows");
    const bool effects_off = SettingOff("post-processing");
    g_shadows_on.store(!shadows_off);
    g_effects_on.store(!effects_off);
    XELOGI("engine: dynamic shadows {}", shadows_off ? "off (files/shadows)" : "the game's own");
    XELOGI("engine: post-processing {}",
           effects_off ? "off (files/post-processing)" : "the game's own");
  });
}

void ApplyEngineSwitches(uint8_t* base) {
  ReadEngineSwitches();
  // Whether a switch is held off now (the scene's thread only).
  static bool shadows_forced = false;
  static bool effects_forced = false;
  const bool shadows = g_shadows_on.load(std::memory_order_relaxed);
  const bool effects = g_effects_on.load(std::memory_order_relaxed);
  if (shadows && effects && !shadows_forced && !effects_forced) {
    return;
  }
  // Switched back on: the engine's own option, as its options code applies it.
  const uint32_t options = PPC_LOAD_U32(kEngineOptions);
  auto option = [&](uint32_t offset) { return !options || PPC_LOAD_U8(options + offset) != 0; };
  // The flags' other bits are the engine's, changed as it renders: only this one
  // changes, atomically.
  uint8_t* flags = base + kRenderFlagsLowByte;
  if (!shadows) {
    if (__atomic_load_n(flags, __ATOMIC_RELAXED) & kRenderFlagShadows) {
      __atomic_fetch_and(flags, uint8_t(~kRenderFlagShadows), __ATOMIC_RELAXED);
    }
    shadows_forced = true;
  } else if (shadows_forced) {
    if (option(kOptionEnableShadows)) {
      __atomic_fetch_or(flags, kRenderFlagShadows, __ATOMIC_RELAXED);
    }
    shadows_forced = false;
    XELOGI("engine: dynamic shadows on again");
  }
  if (!effects) {
    if (PPC_LOAD_U8(kFrameBufferEffects)) {
      PPC_STORE_U8(kFrameBufferEffects, 0);
    }
    effects_forced = true;
  } else if (effects_forced) {
    PPC_STORE_U8(kFrameBufferEffects, uint8_t(option(kOptionEnableFrameBufferEffects)));
    effects_forced = false;
    XELOGI("engine: post-processing on again");
  }
}
}  // namespace

namespace warden {
void SetDynamicShadows(bool on) {
  ReadEngineSwitches();
  g_shadows_on.store(on);
}

void SetPostProcessing(bool on) {
  ReadEngineSwitches();
  g_effects_on.store(on);
}
}  // namespace warden

// The tiling with MSAA on (the game's setting): at the renderer's 1280x720 two tiles
// with 2x MSAA (type 1, the rectangles at 0x8215E5A8); at any other size one tile with
// 4x MSAA (type 2, the rectangle at 0x8215E5C8) - the console's 640x480. With FSR 3.1's
// smaller render sizes (below) the game would take that second path; one tile without
// MSAA covers the screen at any size.
WARDEN_OVERRIDE(tu6_sub_826955E8) {
  const uint32_t rects_out = ctx.r4.u32;
  ApplyEngineSwitches(base);
  __imp__tu6_sub_826955E8(ctx, base);
  // PLAN 9.16: a render size whose colour and depth (32 bits a sample each, no MSAA)
  // need more than the EDRAM's 2,048 tiles of 80x16 - 1600x900 and 1920x1080 - is drawn
  // as two tiles, the top and the bottom half (the split on a 16-row tile edge), the
  // way the console draws its two 2x MSAA tiles at 1280x720: the count returned, the
  // rectangles {left, top, right, bottom} in the console's two-tile table.
  {
    uint32_t width = 0, height = 0;
    warden::fsr3::RenderSize(width, height);
    const uint32_t edram_tiles = ((width + 79) / 80) * ((height + 15) / 16) * 2;
    // The console's own two-tile table, as the title left it (once).
    static bool table_logged = false;
    if (!table_logged) {
      table_logged = true;
      XELOGI("scene: the console's tile table {} {} {} {} / {} {} {} {}; the call gave {} "
             "tile(s), rectangles at {:08X}, MSAA type {}",
             PPC_LOAD_U32(kSceneTileRects + 0), PPC_LOAD_U32(kSceneTileRects + 4),
             PPC_LOAD_U32(kSceneTileRects + 8), PPC_LOAD_U32(kSceneTileRects + 12),
             PPC_LOAD_U32(kSceneTileRects + 16), PPC_LOAD_U32(kSceneTileRects + 20),
             PPC_LOAD_U32(kSceneTileRects + 24), PPC_LOAD_U32(kSceneTileRects + 28),
             ctx.r3.s32, PPC_LOAD_U32(rects_out), PPC_LOAD_U32(kSceneMsaaType));
    }
    // A test switch: files/hack-scene-tiles holding 1 draws the big sizes as one tile
    // (overflowing the EDRAM), 2 as two; read about once a second, so one boot shows both.
    static int forced_tiles = 0;
    static auto forced_checked = std::chrono::steady_clock::time_point();
    if (std::chrono::steady_clock::now() - forced_checked > std::chrono::seconds(1)) {
      forced_checked = std::chrono::steady_clock::now();
      const char* dir = std::getenv("WARDEN_FILES_DIR");
      int value = 0;
      if (FILE* file = dir ? fopen((std::string(dir) + "/hack-scene-tiles").c_str(), "r")
                           : nullptr) {
        if (fscanf(file, "%d", &value) != 1) {
          value = 0;
        }
        fclose(file);
      }
      if (value != forced_tiles) {
        XELOGW("scene: files/hack-scene-tiles {} (was {})", value, forced_tiles);
        forced_tiles = value;
      }
    }
    if (edram_tiles > 2048 && forced_tiles == 1) {
      PPC_STORE_U32(kSceneMsaaType, 0);
      PPC_STORE_U32(rects_out, 0);
      ctx.r3.u64 = 1;
      return;
    }
    if (edram_tiles > 2048) {
      const uint32_t split = ((height / 2) + 15) & ~15u;
      PPC_STORE_U32(kSceneMsaaType, 0);
      PPC_STORE_U32(kSceneTileRects + 0, 0);
      PPC_STORE_U32(kSceneTileRects + 4, 0);
      PPC_STORE_U32(kSceneTileRects + 8, width);
      PPC_STORE_U32(kSceneTileRects + 12, split);
      PPC_STORE_U32(kSceneTileRects + 16, 0);
      PPC_STORE_U32(kSceneTileRects + 20, split);
      PPC_STORE_U32(kSceneTileRects + 24, width);
      PPC_STORE_U32(kSceneTileRects + 28, height);
      PPC_STORE_U32(rects_out, kSceneTileRects);
      ctx.r3.u64 = 2;
      static bool logged = false;
      if (!logged) {
        logged = true;
        XELOGI("scene: {}x{} needs {} EDRAM tiles - drawn as two, rows 0-{} and {}-{}",
               width, height, edram_tiles, split, split, height);
      }
      return;
    }
  }
  const bool msaa_tiles = ctx.r3.s32 == 2 || PPC_LOAD_U32(kSceneMsaaType) != 0;
  if (!msaa_tiles) {
    return;
  }
  if (SingleTileScene()) {
    uint32_t width = 0, height = 0;
    warden::fsr3::RenderSize(width, height);
    PPC_STORE_U32(kSceneMsaaType, 0);
    PPC_STORE_U32(kSceneTileRects + 8, width);  // the first tile covers the screen
    PPC_STORE_U32(kSceneTileRects + 12, height);
    PPC_STORE_U32(rects_out, kSceneTileRects);
    ctx.r3.u64 = 1;
  } else if (ctx.r3.s32 == 2) {
    PPC_STORE_U32(kSceneTileRects + 12, 512);
  }
}

// PLAN 9.4: FSR 3.1's render sizes. The game's device set-up, sub_82692230(renderer,
// &width, &height), takes 1280x720 whatever the video mode (640x480 without HD or
// widescreen), stores it in the engine's two size globals, builds Direct3D's present
// parameters from it and makes the device - sub_82D8E8E0(adapter, type, window,
// flags, parameters, &device) - then gives the renderer the parameters; the engine
// takes its screen size from them (run 398). At FSR 3.1's Balanced or Performance
// (warden::fsr3::RenderSize) the device is made at the smaller size: the parameters'
// back buffer width and height (+0, +4) and the two globals rewritten as it is made.
WARDEN_GAME_FUNC(__imp__tu6_sub_82692230);
WARDEN_GAME_FUNC(__imp__tu6_sub_82D8E8E0);

namespace {
uint32_t g_device_size_globals[2] = {};
bool g_device_setup = false;
}  // namespace

WARDEN_OVERRIDE(tu6_sub_82692230) {
  g_device_size_globals[0] = ctx.r4.u32;
  g_device_size_globals[1] = ctx.r5.u32;
  g_device_setup = true;
  __imp__tu6_sub_82692230(ctx, base);
  g_device_setup = false;
}

WARDEN_OVERRIDE(tu6_sub_82D8E8E0) {
  uint32_t width = 0, height = 0;
  warden::fsr3::RenderSize(width, height);
  const uint32_t parameters = ctx.r7.u32;
  if (g_device_setup && parameters && PPC_LOAD_U32(parameters) == 1280 &&
      PPC_LOAD_U32(parameters + 4) == 720 && (width != 1280 || height != 720)) {
    PPC_STORE_U32(parameters, width);
    PPC_STORE_U32(parameters + 4, height);
    PPC_STORE_U32(g_device_size_globals[0], width);
    PPC_STORE_U32(g_device_size_globals[1], height);
    XELOGI("render size: the game's device made at {}x{}, not its 1280x720 (FSR 3.1's "
           "profile or the Rendering resolution option)",
           width, height);
  }
  __imp__tu6_sub_82D8E8E0(ctx, base);
}

// The module object initialised from the campaign file: its script name after.
WARDEN_OVERRIDE(tu6_sub_822AF0D0) {
  const uint32_t module = ctx.r3.u32;
  __imp__tu6_sub_822AF0D0(ctx, base);
  if (Budget(g_module_inits, 4)) {
    XELOGI("[{}] game: module {:08X} initialised, script \"{}\"", warden::CurrentGuestThreadName(),
           module, EngineString(base, module + 504));
  }
}

// BUG-052 experiment: the Direct3D device is used by two threads during an area load -
// DragonAge::Render (the scene) and Eclipse::Update (the GUI and the portraits) - and
// the portraits' face constants come out zero. With DAO_HACK_BLOCK_RENDER_LOAD
// (files/hack-block-render-load), the render thread's wrapped Direct3D calls wait while
// the mode is 9 (loading), up to 8 s each time, so the update thread has the device to
// itself: if the portraits then have faces, the two threads' commands interfere.
namespace {
void DaoBlockRenderDuringLoad(const char* name) {
  static const bool on = std::getenv("DAO_HACK_BLOCK_RENDER_LOAD") != nullptr;
  if (!on || g_last_mode.load(std::memory_order_relaxed) != 9) {
    return;
  }
  if (warden::CurrentGuestThreadName().find("Render") == std::string::npos) {
    return;
  }
  static std::atomic<int> logged{0};
  const bool say = Budget(logged, 8);
  if (say) {
    XELOGI("[{}] BUG-052 experiment: {} held while the area loads",
           warden::CurrentGuestThreadName(), name);
  }
  int waited = 0;
  while (g_last_mode.load(std::memory_order_relaxed) == 9 && waited < 8000) {
    usleep(1000);
    ++waited;
  }
  if (say) {
    XELOGI("[{}] BUG-052 experiment: released after {} ms (mode {})",
           warden::CurrentGuestThreadName(), waited, g_last_mode.load());
  }
}
}  // namespace

// BUG-052: the GUI's per-frame 3D render (sub_82685330 - it draws the portraits when they
// need drawing), marked per thread, so that warden_d3d_probe.cc's wrappers (and the
// probes below) log every Direct3D call made inside it - the first 600 - with its
// arguments, the 16 bytes r5 points at, and the caller. All of these probes are on only
// with the portrait investigation's marker, files/hack-resolve-dump.
namespace {
thread_local int t_model_render = 0;
std::atomic<int> g_d3d_logged{0};
bool DaoPortraitInvestigation() {
  static const bool on = std::getenv("DAO_HACK_RESOLVE_DUMP_PITCH") != nullptr;
  return on;
}
}  // namespace

namespace warden {
bool DaoInModelRender() { return t_model_render > 0; }
void DaoD3DCall(const char* name, PPCContext& ctx, uint8_t* base) {
  // Only in combat (mode 1), where the fight's working portrait redraw happens - the
  // load's renders were logged in run 266 - and on the update thread (the render thread
  // draws the scene through the same model routines): the first 1,500 calls.
  if (g_last_mode.load(std::memory_order_relaxed) != 1 ||
      CurrentGuestThreadName().find("Update") == std::string::npos ||
      !Budget(g_d3d_logged, 1500)) {
    return;
  }
  std::string data;
  const uint32_t r5 = ctx.r5.u32;
  if (r5 >= 0x40000000 && r5 < 0xF0000000) {
    for (uint32_t i = 0; i < 4; ++i) {
      data += fmt::format(" {:08X}", PPC_LOAD_U32(r5 + 4 * i));
    }
  }
  XELOGI("[{}] BUG-052 d3d: {} r3..r8 {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} from {:08X}; "
         "at r5:{}", CurrentGuestThreadName(), name, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
         ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, uint32_t(ctx.lr), data);
}

// A Direct3D call whose r4 or r5 names the portraits' face slots (c186) - the upload
// that fills c186-c232 is not sub_82D941F8 (run 270): its arguments, caller and mode,
// the first 40, with the call chain for the first six.
std::atomic<int> g_arg_matches{0};
void DaoWatchAddress(uint32_t guest_address, uint32_t length);  // warden_memory.cc
void DaoD3DArgMatch(const char* name, PPCContext& ctx, uint8_t* base) {
  if (!DaoPortraitInvestigation()) {
    return;
  }
  // sub_82D94C28 at startup takes 0x2E8 for something else (run 271) - not constants.
  if (std::string(name) == "sub_82D94C28") {
    return;
  }
  // sub_82DA1050 (device, start, count, shadow offset): the submission writing dirty
  // vertex constants from the device's shadow into the command buffer (run 271:
  // c186, 3, 0x23C0). The first time it names c186, the shadow's c186-c232 get the
  // write watch - whoever writes the face's bones there.
  if (std::string(name) == "sub_82DA1050" && ctx.r4.u32 == 186) {
    static std::atomic<bool> watched{false};
    if (!watched.exchange(true)) {
      DaoWatchAddress(ctx.r3.u32 + ctx.r6.u32, 47 * 16);
    }
  }
  const int n = g_arg_matches.fetch_add(1) + 1;
  if (n > 40) {
    return;
  }
  XELOGI("[{}] BUG-052 face-slot call {}: {} r3..r8 {:08X} {:08X} {:08X} {:08X} {:08X} "
         "{:08X} from {:08X} (in sub_{:08X}), mode {}", CurrentGuestThreadName(), n, name,
         ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32,
         uint32_t(ctx.lr), GuestFunctionContaining(uint32_t(ctx.lr)), g_last_mode.load());
  if (n <= 6) {
    LogGuestCallChainOnFault(base);
  }
}
}  // namespace warden

// The mark is set around sub_82685330 in the render path above the head's draw (the
// GUI's update -> sub_82670210 -> sub_82670040 -> sub_82685330 -> sub_82689390 -> ...
// -> the head's draw), in any mode. It runs every frame (run 267), so its own log line
// (the first 40 calls) mostly shows the main menu.
WARDEN_GAME_FUNC(__imp__tu6_sub_82685330);
WARDEN_OVERRIDE(tu6_sub_82685330) {
  if (!DaoPortraitInvestigation()) {
    __imp__tu6_sub_82685330(ctx, base);
    return;
  }
  __imp__tu6_sub_82685330(ctx, base);
}

// The mark itself is on the model routines above the head's draw (sub_825FD6B8 ->
// sub_82606C50 -> ... -> sub_82698558), which the GUI's render reaches only to draw
// models - the portraits.
#define DAO_MODEL_RENDER(address)                      \
  WARDEN_GAME_FUNC(__imp__tu6_sub_##address);          \
  WARDEN_OVERRIDE(tu6_sub_##address) {                 \
    if (!DaoPortraitInvestigation()) {                 \
      __imp__tu6_sub_##address(ctx, base);             \
      return;                                          \
    }                                                  \
    ++t_model_render;                                  \
    __imp__tu6_sub_##address(ctx, base);               \
    --t_model_render;                                  \
  }
DAO_MODEL_RENDER(825FD6B8)
DAO_MODEL_RENDER(82606C50)
#undef DAO_MODEL_RENDER

// BUG-052: the shader-parameter upload sub_826183A0 (a virtual method; r3 is the
// parameter record: +8 the data, +18 the start register, +20 the limit, +28 the type,
// +32 the bytes, +36 must be set). For the skinning palettes (start c33, limit 100 or
// more), each record's bytes are logged when they change - a palette growing as bones
// join the skeleton - and the first one seen during a load gets a write watch on its
// fields (+8..+40): its writer is the palette's builder.
namespace warden {
void DaoWatchAddress(uint32_t guest_address, uint32_t length);  // warden_memory.cc
}  // namespace warden
namespace {
struct DaoPaletteRecord {
  uint32_t address;
  uint32_t bytes;
};
std::mutex g_palettes_mutex;
DaoPaletteRecord g_palettes[64];
int g_palette_count = 0;
}  // namespace

WARDEN_GAME_FUNC(__imp__tu6_sub_826183A0);
WARDEN_OVERRIDE(tu6_sub_826183A0) {
  if (DaoPortraitInvestigation()) {
    const uint32_t record = ctx.r3.u32;
    const uint32_t start = PPC_LOAD_U16(record + 18);
    const uint32_t limit = PPC_LOAD_U16(record + 20);
    if (start == 33 && limit >= 100) {
      const uint32_t bytes = PPC_LOAD_U32(record + 32);
      bool changed = false;
      {
        std::lock_guard<std::mutex> lock(g_palettes_mutex);
        int slot = -1;
        for (int i = 0; i < g_palette_count; ++i) {
          if (g_palettes[i].address == record) {
            slot = i;
          }
        }
        if (slot < 0 && g_palette_count < 64) {
          slot = g_palette_count++;
          g_palettes[slot] = {record, ~0u};
        }
        if (slot >= 0 && g_palettes[slot].bytes != bytes) {
          g_palettes[slot].bytes = bytes;
          changed = true;
        }
      }
      static std::atomic<int> logged{0};
      if (changed && Budget(logged, 80)) {
        XELOGI("[{}] BUG-052 palette: record {:08X} bytes {} ({} vectors, limit {}), data "
               "{:08X}, type {}, mode {}, from {:08X}", warden::CurrentGuestThreadName(),
               record, bytes, bytes / 16, limit, PPC_LOAD_U32(record + 8),
               PPC_LOAD_U16(record + 28), g_last_mode.load(), uint32_t(ctx.lr));
      }
      static std::atomic<bool> watched{false};
      if (g_last_mode.load(std::memory_order_relaxed) == 9 && !watched.exchange(true)) {
        warden::DaoWatchAddress(record + 8, 32);
      }
    }
  }
  __imp__tu6_sub_826183A0(ctx, base);
}

// BUG-052: the vertex shader constant setter sub_82D941F8 (device, start register,
// data, count), wrapped on its own (not in warden_d3d_probe.cc's list): any upload that
// reaches c150 or above - where the portraits' face bones live, c186-c232 - is logged
// in every mode, with the caller and the first data, the first 80 times; inside the GUI
// render it is also logged like the other Direct3D calls.
WARDEN_GAME_FUNC(__imp__tu6_sub_82D941F8);
WARDEN_OVERRIDE(tu6_sub_82D941F8) {
  if (DaoPortraitInvestigation()) {
    if (warden::DaoInModelRender()) {
      warden::DaoD3DCall("sub_82D941F8", ctx, base);
    }
    // Uploads that cover any of the face's slots, c186-c204 or c209-c232 (c205-c208 are
    // the scene's own constants, set every frame).
    const uint32_t start = ctx.r4.u32;
    const uint32_t count = ctx.r6.u32;
    const uint32_t end = start + count;  // exclusive
    const bool face = start < 256 && count &&
                      ((start < 205 && end > 186) || (start < 233 && end > 209));
    static std::atomic<int> logged{0};
    if (face && Budget(logged, 40)) {
      const uint32_t data = ctx.r5.u32;
      XELOGI("[{}] BUG-052 high constants: c{}..c{} from {:08X} (data {:08X}: {:08X} {:08X} "
             "{:08X} {:08X}), mode {}", warden::CurrentGuestThreadName(), start,
             start + count - 1, uint32_t(ctx.lr), data, PPC_LOAD_U32(data),
             PPC_LOAD_U32(data + 4), PPC_LOAD_U32(data + 8), PPC_LOAD_U32(data + 12),
             g_last_mode.load());
    }
  }
  __imp__tu6_sub_82D941F8(ctx, base);
}

// BUG-052: the Direct3D functions that build DRAW_INDX packets (the generated code puts
// 0x2200 in them), wrapped. A call while an area loads (mode 9) with the skinned head's
// index count, 10,788, among its argument registers is the portraits being recorded:
// its registers, the caller (lr) and the guest call chain above it are logged, the
// first twelve times.
namespace {
std::atomic<int> g_draw_probes{0};
void DaoDrawProbe(const char* name, PPCContext& ctx, uint8_t* base) {
  DaoBlockRenderDuringLoad(name);
  if (!DaoPortraitInvestigation()) {
    return;
  }
  if (warden::DaoInModelRender()) {
    warden::DaoD3DCall(name, ctx, base);
  }
  if (g_last_mode.load(std::memory_order_relaxed) != 9) {
    return;
  }
  const uint32_t args[8] = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
                            ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
  bool head = false;
  for (uint32_t arg : args) {
    head = head || arg == 10788;
  }
  if (!head || !Budget(g_draw_probes, 12)) {
    return;
  }
  const uint32_t lr = uint32_t(ctx.lr);
  XELOGI("[{}] BUG-052 draw probe: {} r3..r10 {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} "
         "{:08X} {:08X}, called from {:08X} (in sub_{:08X}); the chain above:",
         warden::CurrentGuestThreadName(), name, args[0], args[1], args[2], args[3], args[4],
         args[5], args[6], args[7], lr, warden::GuestFunctionContaining(lr));
  warden::LogGuestCallChainOnFault(base);
}
}  // namespace

#define DAO_DRAW_PROBE(address)                     \
  WARDEN_GAME_FUNC(__imp__tu6_sub_##address);       \
  WARDEN_OVERRIDE(tu6_sub_##address) {              \
    DaoDrawProbe("sub_" #address, ctx, base);       \
    __imp__tu6_sub_##address(ctx, base);            \
  }
DAO_DRAW_PROBE(82D8F170)
DAO_DRAW_PROBE(82D93E98)
DAO_DRAW_PROBE(82D99650)
DAO_DRAW_PROBE(82D9A150)
DAO_DRAW_PROBE(82D9CC98)
DAO_DRAW_PROBE(82D9D178)
DAO_DRAW_PROBE(82D9D730)
DAO_DRAW_PROBE(82D9DB20)
DAO_DRAW_PROBE(82D9F580)
DAO_DRAW_PROBE(82D8F018)
DAO_DRAW_PROBE(82D95ED0)
DAO_DRAW_PROBE(82DA9600)
#undef DAO_DRAW_PROBE

// BUG-052: the Direct3D calls the portrait's mesh setup makes (sub_82697950, called by
// the head's draw routine sub_82698558), wrapped: while an area loads, their arguments
// and the 16 bytes r5 points at, four times each - one of them should upload the
// skinning palette, and its source is where the face's bones would be.
namespace {
void DaoStateProbe(const char* name, std::atomic<int>& budget, PPCContext& ctx,
                   uint8_t* base) {
  DaoBlockRenderDuringLoad(name);
  if (!DaoPortraitInvestigation()) {
    return;
  }
  if (warden::DaoInModelRender()) {
    warden::DaoD3DCall(name, ctx, base);
  }
  if (g_last_mode.load(std::memory_order_relaxed) != 9) {
    return;
  }
  // Called from inside sub_82697950 (which ends where sub_82697C78 begins) - a range
  // test, not a function-table scan, since these run thousands of times a second.
  const uint32_t lr = uint32_t(ctx.lr);
  if (lr < 0x82697950 || lr >= 0x82697C78 || !Budget(budget, 4)) {
    return;
  }
  std::string data;
  const uint32_t r5 = ctx.r5.u32;
  if (r5 >= 0x40000000 && r5 < 0xF0000000) {
    for (uint32_t i = 0; i < 4; ++i) {
      data += fmt::format(" {:08X}", PPC_LOAD_U32(r5 + 4 * i));
    }
  }
  XELOGI("[{}] BUG-052 state probe: {} r3..r8 {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} "
         "from {:08X}; at r5:{}", warden::CurrentGuestThreadName(), name, ctx.r3.u32,
         ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, lr, data);
}
}  // namespace

#define DAO_STATE_PROBE(address)                                        \
  WARDEN_GAME_FUNC(__imp__tu6_sub_##address);                           \
  WARDEN_OVERRIDE(tu6_sub_##address) {                                  \
    static std::atomic<int> budget{0};                                  \
    DaoStateProbe("sub_" #address, budget, ctx, base);                  \
    __imp__tu6_sub_##address(ctx, base);                                \
  }
DAO_STATE_PROBE(82D9CB18)
DAO_STATE_PROBE(82D949B0)
DAO_STATE_PROBE(82D94798)
DAO_STATE_PROBE(82D94590)
DAO_STATE_PROBE(82D8D340)
DAO_STATE_PROBE(82D8D198)
#undef DAO_STATE_PROBE

// The deck's faces by name (the owner, 2026-11-02: the hero's row showed Gorim's face and
// his hers - the deck placed each portrait render by guesswork, the burst's order and
// the resolve's address). sub_824F5E50 builds a party member's portrait: its name,
// "portrait_" + the creature's tag + a running count ("portrait_gen00fl_alistair2" - the
// name the HUD's Party.Members.N.Portrait gives), the wide string at the portrait's
// +3120, then a 146x146 texture under that name, handed to sub_825FDDA8(portrait,
// texture) from 824F602C; the texture is drawn right after. The name goes to the deck
// (warden_deck.cc), which gives the next new resolve address it. Session 41, on the
// morrigan route: portrait_player0, then morrigan1 and alistair2, made in the order their
// faces were first drawn.
WARDEN_GAME_FUNC(__imp__tu6_sub_825FDDA8);
WARDEN_OVERRIDE(tu6_sub_825FDDA8) {
  if (uint32_t(ctx.lr) == 0x824F602C) {
    const uint32_t portrait = ctx.r3.u32;
    std::string name;
    if (portrait >= 0x40000000 && portrait < 0xF0000000) {
      const uint32_t chars = PPC_LOAD_U32(portrait + 3120);
      for (uint32_t i = 0; chars >= 0x40000000 && chars < 0xF0000000 && i < 96; ++i) {
        const uint16_t c = PPC_LOAD_U16(chars + 2 * i);
        if (c == 0 || c > 0x7E || c < 0x20) break;
        name += char(c);
      }
    }
    if (name.rfind("portrait_", 0) == 0) {
      warden::deck::NotePortraitMade(name);
    } else {
      XELOGW("Deck: a portrait made without a name the deck can read ({:08X})", portrait);
    }
  }
  __imp__tu6_sub_825FDDA8(ctx, base);
}

// PLAN 9.13, with files/save-chain: the save path's functions - who asks for a save, with
// what. sub_823027E8(save manager, user, slot, flag) saves to a slot of the save
// manager's list (the game object's +292, whose update sub_82300B68 runs every frame);
// sub_822D0370 builds the worker job that writes it (vtable 82174570, run by
// sub_822CFC30, which creates the content); sub_82415048 and sub_82415158, called only
// through a table, both call sub_823027E8 from a request record (at r4); sub_82390360
// reaches it through sub_823906C8 and sub_82391E30. Each logged with its arguments and
// the guest call chain, a few times.
namespace {
void SaveProbe(const char* name, std::atomic<int>& budget, PPCContext& ctx, uint8_t* base) {
  if (!SaveChainWanted() || !Budget(budget, 6)) {
    return;
  }
  std::string record;
  const uint32_t r4 = ctx.r4.u32;
  if (r4 >= 0x40000000 && r4 < 0xF0000000) {
    for (uint32_t i = 0; i < 6; ++i) {
      record += fmt::format(" {:08X}", PPC_LOAD_U32(r4 + 4 * i));
    }
  }
  XELOGI("[{}] save-chain: {} r3..r7 {:08X} {:08X} {:08X} {:08X} {:08X} from {:08X}; at r4:{}",
         warden::CurrentGuestThreadName(), name, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
         ctx.r6.u32, ctx.r7.u32, uint32_t(ctx.lr), record);
  warden::LogGuestCallChainOnFault(base);
}
}  // namespace

#define DAO_SAVE_PROBE(address)                                         \
  WARDEN_GAME_FUNC(__imp__tu6_sub_##address);                           \
  WARDEN_OVERRIDE(tu6_sub_##address) {                                  \
    static std::atomic<int> budget{0};                                  \
    SaveProbe("sub_" #address, budget, ctx, base);                      \
    __imp__tu6_sub_##address(ctx, base);                                \
  }
DAO_SAVE_PROBE(823027E8)
DAO_SAVE_PROBE(822D0370)
DAO_SAVE_PROBE(82415048)
DAO_SAVE_PROBE(82415158)
#undef DAO_SAVE_PROBE

// With files/save-chain (9.13): the load/save state machine's SetState - each new state,
// the object's fields around the state (+128..+255, +320..+399), and the guest call
// chain where it enters state 2 or 3 - to see how a load sets it up for an autosave.
WARDEN_OVERRIDE(tu6_sub_823904D8) {
  static std::atomic<int> budget{0};
  if (SaveChainWanted() && Budget(budget, 60)) {
    const uint32_t self = ctx.r3.u32;
    std::string fields;
    for (uint32_t offset = 128; offset < 400; offset += 4) {
      if (offset == 256) {
        offset = 320;
      }
      fields += fmt::format(" {:X}", PPC_LOAD_U32(self + offset));
    }
    XELOGI("[{}] save-chain: SetState({:08X}, {}) from {:08X}, from state {}; +128..:{}",
           warden::CurrentGuestThreadName(), self, ctx.r4.u32, uint32_t(ctx.lr),
           PPC_LOAD_U16(self + 144), fields);
    if (ctx.r4.u32 == 2 || ctx.r4.u32 == 3) {
      warden::LogGuestCallChainOnFault(base);
    }
  }
  __imp__tu6_sub_823904D8(ctx, base);
}

// The load/save state machine's update, probed as above, and its object kept for the
// autosave's log (9.13), with how often it runs.
WARDEN_OVERRIDE(tu6_sub_82390360) {
  static std::atomic<int> budget{0};
  SaveProbe("sub_82390360", budget, ctx, base);
  g_save_machine_updates.fetch_add(1, std::memory_order_relaxed);
  if (g_save_machine.exchange(ctx.r3.u32) != ctx.r3.u32) {
    XELOGI("[{}] autosave on pause: the load/save state machine is {:08X}",
           warden::CurrentGuestThreadName(), ctx.r3.u32);
  }
  __imp__tu6_sub_82390360(ctx, base);
}

// DAORecomp's effect parameter fix (upstream 48fd991 and 5c56514,
// DAORecomp/gpu/effect_params.cpp; taken 2026-10-30): a crash on loading an area with input
// held - the Ostagar save after Awakening's - when an effect made while its resource
// streams in has a null parameter descriptor. An effect keeps four parallel vectors at
// +28 (descriptors), +44 (slots), +60 (values) and +76 (extras); its bind (vtable slot 22,
// tu6_sub_82698458) passes each to the per-parameter bind (tu6_sub_82697038) with the
// descriptor's field +48, and read null+48 on the render thread. Here: the game's loop with
// the null entries unbound, as the effect's unbind clears every slot (r5-r8 0), so the
// effect draws without that parameter. A broken vector (end before begin, or over 4096
// entries) is left to the game's own code.
WARDEN_GAME_FUNC(__imp__tu6_sub_82697038);  // the per-parameter bind
WARDEN_OVERRIDE(tu6_sub_82698458) {
  constexpr uint32_t kDescriptors = 28, kSlots = 44, kValues = 60, kExtras = 76;
  constexpr uint32_t kDescriptorField = 48;
  constexpr uint32_t kOwnValues = 0xFFFFFFFF;
  constexpr uint32_t kMaxParameters = 4096;
  const uint32_t effect = ctx.r3.u32;
  const uint32_t begin = PPC_LOAD_U32(effect + kDescriptors);
  const uint32_t end = PPC_LOAD_U32(effect + kDescriptors + 4);
  if (end < begin || (end - begin) / 4 > kMaxParameters) {
    __imp__tu6_sub_82698458(ctx, base);
    return;
  }
  // The game's loop: the count taken once, the vectors read each time.
  const uint32_t count = (end - begin) / 4;
  const uint32_t state = ctx.r4.u32;
  const uint32_t all_value = ctx.r6.u32;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t offset = i * 4;
    const uint32_t descriptor = PPC_LOAD_U32(PPC_LOAD_U32(effect + kDescriptors) + offset);
    ctx.r3.u64 = state;
    ctx.r4.u64 = PPC_LOAD_U32(PPC_LOAD_U32(effect + kSlots) + offset);
    ctx.r8.u64 = 0;
    if (descriptor) {
      ctx.r5.u64 = PPC_LOAD_U32(descriptor + kDescriptorField);
      ctx.r6.u64 = all_value != kOwnValues
                       ? all_value
                       : PPC_LOAD_U32(PPC_LOAD_U32(effect + kValues) + offset);
      ctx.r7.u64 = PPC_LOAD_U32(PPC_LOAD_U32(effect + kExtras) + offset);
    } else {
      static std::atomic<bool> logged{false};
      if (!logged.load(std::memory_order_relaxed) && !logged.exchange(true)) {
        XELOGW("[{}] game: effect {:08X} has a parameter that was not loaded when it was "
               "made - unbound (DAORecomp's Ostagar load fix)",
               warden::CurrentGuestThreadName(), effect);
      }
      ctx.r5.u64 = 0;
      ctx.r6.u64 = 0;
      ctx.r7.u64 = 0;
    }
    __imp__tu6_sub_82697038(ctx, base);
  }
}
