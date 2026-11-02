// Button prompts for the controller in hand: Xbox, Nintendo or PlayStation (PLAN
// Phase 7, docs/GLYPHS.md). See warden_glyphs.cc.
#ifndef WARDEN_GLYPHS_H_
#define WARDEN_GLYPHS_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace warden::glyphs {

enum class Set { kXbox, kNintendo, kPlayStation };

// "xbox", "nintendo", "playstation" - the marker file's words; nullopt otherwise.
std::optional<Set> ParseSet(std::string_view text);
const char* SetName(Set set);

// What a set changes: the image `image` is drawn from the atlas cell of `source`.
struct Redirect {
  const char* image;
  const char* source;
};
std::vector<Redirect> Redirects(Set set);

// Applies a set's redirects to the UI's atlas descriptions. `atlases` holds each atlas
// XML (atl_*_dat.xml) by file name; the result holds the ones that changed, by file
// name. A redirect within a page rewrites the cell's rectangle; one to another page
// moves the cell's entry to that page's description. `report` gets a line per redirect.
std::map<std::string, std::string> PatchAtlases(
    const std::map<std::string, std::string>& atlases, Set set, std::string* report);

// Writes the set's changed atlas descriptions into the loose-file tree that overrides
// the disc (warden_fs.h, SetGuestOverrideRoot) - as packages/core/patch/<name>, where
// the title looks for files that replace the ones in its archives - read from the
// extracted guiexport.erf; the Xbox set, the game's own, writes none. Before the title
// starts, never while it runs: it reads the descriptions again at every load, and
// rewriting them under it crashed the next one (BUG-061). `record` (outside the tree,
// which the title sees) lists what was written, for the next start: files an earlier
// set wrote are removed first, and the same set again is left as it is. False, and the
// reason in `error`, if the archive could not be read; the title then shows its own
// prompts.
bool Apply(const std::string& game_data_dir, const std::string& override_root,
           const std::string& record, Set set, std::string* error);

}  // namespace warden::glyphs

#endif  // WARDEN_GLYPHS_H_
