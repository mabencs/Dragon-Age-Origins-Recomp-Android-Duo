// The game's code as a library of its own (PLAN 12.2) - see warden_game_library.h.

#include "warden_game_library.h"

#include <dlfcn.h>

#include <cstring>
#include <set>
#include <string>

#include "warden_gamecode_abi.h"
#include "xenia/base/logging.h"

namespace warden::game {
namespace {

// Constant-initialised, so the Symbols' and Overrides' constructors (dynamic
// initialisation, in any order across the runtime's files) all find them ready.
Symbol* g_symbols = nullptr;
Override* g_overrides = nullptr;

void* g_handle = nullptr;
// One zeroed entry: an empty, terminated table until the game's code is loaded.
const PPCFuncMapping kNoMappings[1] = {};
const PPCFuncMapping* g_mappings = kNoMappings;
uint64_t g_config[4] = {};

}  // namespace

Symbol::Symbol(const char* name_in, void** slot_in) : name(name_in), slot(slot_in), next(g_symbols) {
  g_symbols = this;
}

Override::Override(const char* name_in) : name(name_in), next(g_overrides) {
  g_overrides = this;
}

std::string LibraryPath(const std::string& files_dir) {
  return files_dir + "/gamecode/libdaogame.so";
}

bool Load(const std::string& path, std::string* error) {
  if (g_handle) {
    return true;
  }
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* why = dlerror();
    *error = std::string("the game's code could not be loaded: ") + (why ? why : path);
    return false;
  }
  const auto* abi = static_cast<const char*>(dlsym(handle, "warden_gamecode_abi"));
  if (!abi || std::strcmp(abi, WARDEN_GAMECODE_ABI) != 0) {
    *error = std::string("the game's code was made for another version of the app (") +
             (abi ? abi : "none") + ", this one is " + WARDEN_GAMECODE_ABI + ")";
    dlclose(handle);
    return false;
  }
  const auto* mappings = static_cast<const PPCFuncMapping*>(dlsym(handle, "PPCFuncMappings_tu6"));
  const auto* config = static_cast<const uint64_t*>(dlsym(handle, "warden_gamecode_config"));
  const auto* forwarded = static_cast<const char* const*>(dlsym(handle, "warden_gamecode_overrides"));
  if (!mappings || !config || !forwarded) {
    *error = "the game's code has no function table";
    dlclose(handle);
    return false;
  }
  // The same overrides on both sides: one the game's code does not forward would never
  // run, and a forwarder to one the runtime lacks could not have linked - the ABI says as
  // much, this says which.
  std::set<std::string> theirs;
  for (const char* const* name = forwarded; *name; ++name) {
    theirs.insert(*name);
  }
  std::set<std::string> ours;
  for (Override* override_record = g_overrides; override_record; override_record = override_record->next) {
    ours.insert(override_record->name);
  }
  if (theirs != ours) {
    std::string differ;
    for (const std::string& name : ours) {
      if (!theirs.count(name)) {
        differ += " " + name + " (not forwarded)";
      }
    }
    for (const std::string& name : theirs) {
      if (!ours.count(name)) {
        differ += " " + name + " (no override)";
      }
    }
    *error = "the game's code was made for another version of the app: its overrides differ:" +
             differ;
    dlclose(handle);
    return false;
  }
  std::string missing;
  size_t count = 0;
  for (Symbol* symbol = g_symbols; symbol; symbol = symbol->next) {
    void* address = dlsym(handle, symbol->name);
    if (!address) {
      missing += missing.empty() ? "" : ", ";
      missing += symbol->name;
      continue;
    }
    *symbol->slot = address;
    ++count;
  }
  if (!missing.empty()) {
    *error = "the game's code lacks functions the app calls: " + missing;
    for (Symbol* symbol = g_symbols; symbol; symbol = symbol->next) {
      *symbol->slot = nullptr;
    }
    dlclose(handle);
    return false;
  }
  std::memcpy(g_config, config, sizeof(g_config));
  g_mappings = mappings;
  g_handle = handle;
  XELOGI("game code: {} loaded (ABI {}): image {:08X}+{:X}, code {:08X}+{:X}, {} functions "
         "the runtime calls resolved",
         path, abi, g_config[0], g_config[1], g_config[2], g_config[3], count);
  return true;
}

bool Loaded() { return g_handle != nullptr; }

const PPCFuncMapping* Mappings() { return g_mappings; }

uint64_t ImageBase() { return g_config[0]; }
uint64_t ImageSize() { return g_config[1]; }
uint64_t CodeBase() { return g_config[2]; }
uint64_t CodeSize() { return g_config[3]; }

}  // namespace warden::game
