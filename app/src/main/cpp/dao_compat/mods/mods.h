// DAORecomp's mods/mods.h, as far as its PC-to-360 converter (mods/convert/convert.cpp,
// PLAN 10.13) needs it: the names of its folders and IsInAddinModule. The mod list
// itself is the app's (Mods.java); see dao_convert_android.cc.

#pragma once

#include <filesystem>

#include "mods/game_files.h"

namespace dao::mods {

// Folders under mods/ starting with this are not mods (work folders).
constexpr char kHiddenPrefix = '.';
// Where converting keeps a mod's PC files: mods/.originals/<mod folder>/.
constexpr char kOriginalsFolder[] = ".originals";

// Whether a path in a mod or package is in an add-in's module (addins/<id>/module/): a
// new area or campaign, which the Xbox 360 version cannot load.
bool IsInAddinModule(const std::filesystem::path& relative);

}  // namespace dao::mods
