// glyphs_host - app/src/main/cpp/warden_glyphs.cc on the computer, for PLAN 7.5.
//
//   glyphs_host <dir of atl_*_dat.xml> <set> <out dir>
//
// Reads every atlas description in the directory (`python tools/dao_erf.py extract
// guiexport.erf <dir>` writes them), applies the set's redirects, writes the changed
// descriptions to <out dir> and prints the report. tools/check_glyphs.py builds this and
// renders what each redirected image would then show.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "warden_glyphs.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: glyphs_host <xml dir> <xbox|nintendo|playstation> <out dir>\n");
    return 2;
  }
  const auto set = warden::glyphs::ParseSet(argv[2]);
  if (!set) {
    std::fprintf(stderr, "unknown set %s\n", argv[2]);
    return 2;
  }
  std::map<std::string, std::string> atlases;
  for (const auto& entry : fs::directory_iterator(argv[1])) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("atl_", 0) == 0 && name.size() > 8 &&
        name.compare(name.size() - 8, 8, "_dat.xml") == 0) {
      std::ifstream in(entry.path(), std::ios::binary);
      std::stringstream text;
      text << in.rdbuf();
      atlases[name] = text.str();
    }
  }
  std::string report;
  const auto changed = warden::glyphs::PatchAtlases(atlases, *set, &report);
  fs::create_directories(argv[3]);
  for (const auto& [name, text] : changed) {
    std::ofstream out(fs::path(argv[3]) / name, std::ios::binary);
    out << text;
  }
  std::cout << atlases.size() << " atlas descriptions, " << changed.size() << " changed\n"
            << report;
  return 0;
}
