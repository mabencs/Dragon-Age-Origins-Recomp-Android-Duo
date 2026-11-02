// PLAN 10.13: DAORecomp's PC-to-360 mod converter on Android (dao_convert_android.cc).
#ifndef DAO_CONVERT_ANDROID_H_
#define DAO_CONVERT_ANDROID_H_

#include <string>

namespace warden {

// Converts a mod made for the PC version, in place (its PC files kept in
// mods/.originals/<folder>/), its add-ins installed as downloadable content in
// content_dir (PLAN 10.19): "result\t<converted 0|1>\t<files written>", then
// "addin\t<title>\t<package>\t<files>" for each add-in installed, then "problem\t...",
// "left-out\t..." and "note\t..." lines. Slow (seconds to minutes): never on the UI
// thread.
std::string ConvertMod(const std::string& mod_folder, const std::string& game_root,
                       const std::string& content_dir);

}  // namespace warden

#endif  // DAO_CONVERT_ANDROID_H_
