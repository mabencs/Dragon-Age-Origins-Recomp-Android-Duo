// The guest filesystem: the title's paths onto the extracted game data. See
// warden_fs.cc.
#ifndef WARDEN_FS_H_
#define WARDEN_FS_H_

#include <string>
#include <vector>

namespace warden {

// Where D:\ lives on the host - getExternalFilesDir()/gamedata, the extracted disc.
// Set when the title is loaded, before any guest code runs.
void SetGuestFileRoot(const std::string& host_directory);

// A loose-file tree laid over the disc (PLAN 7.3): a file at
// <host_directory>/packages/core/patch/x.xml is D:\packages\core\patch\x.xml - in place
// of the disc's file of that name, or beside the disc's files, listed with them. The
// title itself reads packages/core/patch/ at its start for files that replace the ones
// in its archives (the title update's are there), so a file there replaces any game
// resource by name. Directories only the tree has are seen too. Empty: none. Set before
// any guest code runs.
void SetGuestOverrideRoot(const std::string& host_directory);
// PLAN 10.12: the enabled mods' folders, each mirroring the disc, laid over it under the
// override tree - the highest priority first (the last in the load order).
void SetGuestModRoots(const std::vector<std::string>& host_directories);

// The "skip intro logos" option, as DAORecomp's: the EA, BioWare and Edge of Reality
// logo movies are reported missing - not found when opened, left out of listings - and
// the title skips them. Off unless the app's files directory holds a file named
// `skip_intro_logos` (Runtime::LoadTitle).
void SetSkipIntroLogos(bool skip);

// Content roots (XamContentCreateEx - a save): the title names a root ("save") and
// then opens "save:\..." paths, which land in `host_directory` and, unlike the disc,
// can be created, written, truncated and deleted. Root names are case-insensitive,
// without the colon. Mounting a root again replaces it; files already open keep the
// folder they were opened in.
void MountContentRoot(const std::string& root, const std::string& host_directory);
void UnmountContentRoot(const std::string& root);

}  // namespace warden

#endif  // WARDEN_FS_H_
