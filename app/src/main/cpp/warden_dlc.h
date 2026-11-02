// PLAN 10.10: downloadable content from its Xbox Live packages. See warden_dlc.cc.
#ifndef WARDEN_DLC_H_
#define WARDEN_DLC_H_

#include <cstdint>
#include <string>

namespace warden::dlc {

// Installs the downloadable content read through `fd` (owned, and closed here) into
// `content_dir` (the runtime's folder for content type 2): a Live package of its own, or
// every one inside a disc image (Ultimate Edition's disc 2). A disc image's title update
// for this game's disc (media 186A1115 - disc 2 carries Title Update 6, PLAN 12.3) is
// copied whole to `title_update_path`, where the runtime and the launcher find a title
// update; empty for none. Returns what happened, in words for the launch screen. Slow
// for large packages: call it off the UI thread.
std::string Install(int fd, const std::string& content_dir,
                    const std::string& title_update_path);

// The install running (or the last): bytes of packages done and in all, for the launcher's
// progress (0 and 0 before any).
void Progress(uint64_t& done, uint64_t& total);

}  // namespace warden::dlc

#endif  // WARDEN_DLC_H_
