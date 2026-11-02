// PLAN 10.19: a PC add-in (a .dazip, or its unpacked addins/<id>/ with its Manifest.xml)
// installed as the Xbox 360 version's downloadable content (dao_addin_android.cc).
#ifndef DAO_ADDIN_ANDROID_H_
#define DAO_ADDIN_ANDROID_H_

#include <filesystem>
#include <string>
#include <vector>

namespace warden::addins {

struct Installed {
  std::string package;  // its folder in the content folder
  std::string title;
  size_t files = 0;
};

struct Report {
  std::vector<Installed> installed;
  std::vector<std::string> problems;
  std::vector<std::string> left_out;
  std::vector<std::string> notes;
};

// Finds the add-ins in a mod folder, converts each one's files to the 360's formats
// (DAORecomp's converter), packs them as the 360's add-ins are packed and installs each
// as a package of downloadable content in content_dir (files/content/00000002) - the
// game lists that folder and loads the add-ins in it. What was converted is set aside in
// mods/.originals/<mod>/; a package's other parts (Contents/packages/) are put in the mod
// for the usual conversion. The mod's addins.txt lists its packages ("<folder>\t<title>").
void Install(const std::filesystem::path& mod_folder, const std::filesystem::path& game_root,
             const std::filesystem::path& content_dir, Report& report);

// The package folder name for an add-in's id.
std::string PackageName(const std::string& uid);

}  // namespace warden::addins

#endif  // DAO_ADDIN_ANDROID_H_
