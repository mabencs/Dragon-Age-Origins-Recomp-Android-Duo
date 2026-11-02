// DAORecomp's mods/mod_install.h, as far as its converter (PLAN 10.13) needs it:
// unpacking a .dazip (a zip) and a mod.toml from a DAModder manifest. Upstream unpacks
// with Windows' tar.exe; here a small zip reader over zlib (dao_convert_android.cc).

#pragma once

#include <filesystem>
#include <stop_token>
#include <string>

#include "mods/mods.h"

namespace dao::mods {

bool UnpackArchive(const std::filesystem::path& archive, const std::filesystem::path& into,
                   std::stop_token stop, std::string& error);

void DescribeFromManifest(const std::filesystem::path& modFolder,
                          const std::filesystem::path& manifestXml);

}  // namespace dao::mods
