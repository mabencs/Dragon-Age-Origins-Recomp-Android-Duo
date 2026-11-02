// PLAN 10.19: .7z and .rar archives unpacked for the Mods page (dao_archive_android.cc).
#ifndef DAO_ARCHIVE_ANDROID_H_
#define DAO_ARCHIVE_ANDROID_H_

#include <string>

namespace warden::archives {

// Unpacks a .7z or .rar archive (told apart by its first bytes) into a folder: "" when
// done, else why not, for the player. Slow: never on the UI thread.
std::string Unpack(const std::string& archive, const std::string& into);

}  // namespace warden::archives

#endif  // DAO_ARCHIVE_ANDROID_H_
