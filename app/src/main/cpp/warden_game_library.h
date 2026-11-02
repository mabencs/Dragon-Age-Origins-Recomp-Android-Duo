// The game's code as a library of its own (PLAN 12.2).
//
// The published APK holds none of the game: its code - XenonRecomp's translation of the
// player's own executable - is libdaogame.so, made on the device by the app from the
// player's disc (PLAN 12.5), or on the computer while developing (the `daogame` target,
// pushed by tools/push_gamecode.py), and kept in the app's files (gamecode/). The runtime
// loads it before the guest starts, and everything the runtime reaches in it is resolved
// at that load:
//
//   - the function table - Mappings(), in place of PPCFuncMappings_tu6;
//   - the image's layout as it was translated - ImageSize() and the rest, in place of
//     ppc_config.h's constants (Title Update 6's image is smaller than 7's, and the
//     indirect-call table sits right after the image);
//   - each game function the runtime calls - WARDEN_GAME_FUNC(__imp__tu6_sub_X) in place
//     of its declaration: a pointer of that name, so a call reads as before.
//
// The other way, the game's code reaches the runtime's overrides through forwarders made
// with it (app/src/main/assets/gamecode/shim.cpp): an override is WARDEN_OVERRIDE(name)
// here, and tools/gen_gamecode_recipe.py lists it there.
#pragma once

#include <cstdint>
#include <string>

#include "ppc_context.h"

namespace warden::game {

// <files>/gamecode/libdaogame.so.
std::string LibraryPath(const std::string& files_dir);

// Loads the game's code at `path` and resolves the runtime's references into it. False,
// with *error saying why, when it is missing, was made for another version of the
// runtime (its ABI), or lacks a function the runtime calls. Once loaded it stays.
bool Load(const std::string& path, std::string* error);
bool Loaded();

// The function table, nullptr-terminated; empty before Load.
const PPCFuncMapping* Mappings();

// The image's layout as the loaded code was translated (ppc_config.h's, at the load).
uint64_t ImageBase();
uint64_t ImageSize();
uint64_t CodeBase();
uint64_t CodeSize();

// A game function the runtime calls by name, resolved at Load (WARDEN_GAME_FUNC).
struct Symbol {
  Symbol(const char* name, void** slot);
  const char* const name;
  void** const slot;
  Symbol* next;
};

// One of the runtime's overrides (WARDEN_OVERRIDE): Load checks the game's code forwards
// exactly these - game code made for a runtime with other overrides is refused.
struct Override {
  explicit Override(const char* name);
  const char* const name;
  Override* next;
};

}  // namespace warden::game

// The indirect-call table as the loaded game's code has it: XenonRecomp's PPC_LOOKUP_FUNC
// (ppc_context.h) bakes in this build's ppc_config.h, Title Update 7's image size, and with
// Title Update 6's code (its image 64 KB smaller) every thread's entry and callback read
// an empty slot (12.3, run 597). Force-included into every runtime file
// (app/src/main/cpp/CMakeLists.txt), so none can use the other.
#undef PPC_LOOKUP_FUNC
#define PPC_LOOKUP_FUNC(x, y)                                                          \
  *(PPCFunc**)((x) + ::warden::game::ImageBase() + ::warden::game::ImageSize() +       \
               (uint64_t(uint32_t(y) - ::warden::game::CodeBase()) * 2))

// A game function the runtime calls: a pointer named as the function, filled at Load.
#define WARDEN_GAME_FUNC(name)    \
  static PPCFunc* name = nullptr; \
  static ::warden::game::Symbol warden_game_symbol_##name(#name, reinterpret_cast<void**>(&name))

// The runtime's override of a game function, followed by its body: the game's code calls
// it through its forwarder (shim.cpp, made by tools/gen_gamecode_recipe.py, which finds
// each WARDEN_OVERRIDE - in a macro too), the original stays callable as
// WARDEN_GAME_FUNC(__imp__<name>).
#define WARDEN_OVERRIDE(name)                                                         \
  static ::warden::game::Override warden_override_record_##name(#name);               \
  extern "C" __attribute__((visibility("default"))) PPC_FUNC(warden_override_##name)
