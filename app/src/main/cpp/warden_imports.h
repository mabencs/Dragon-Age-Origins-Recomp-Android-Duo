// The kernel import boundary. Task 5.1.
//
// The recompiled title calls 251 distinct Xbox 360 kernel, XAM, video-driver and audio
// imports. tools/gen_import_stubs.py emits a weak stub for each from the list
// XenonRecomp declares, so the library links; implementing one means writing it
// strongly in warden_imports.cc, where it overrides the stub with no list to maintain.
//
// The counting is the point. A stub records that it was called and leaves a trail, and
// it returns its import's *failure* value - read from Xenia's kernel by the generator -
// so the title takes its own error path rather than acting on outputs nobody wrote. After a boot attempt the report names every import the game actually
// reached and how often, in the order first seen - which turns "implement 251 kernel
// functions" into a short list of the ones that are in the way.
#ifndef WARDEN_IMPORTS_H_
#define WARDEN_IMPORTS_H_

#include <cstddef>
#include <string>

namespace warden {

// Generated alongside the stubs.
extern const char* const kImportNames[];
extern const size_t kImportCount;
// What each stub returns - its import's failure value, named - indexed as
// kImportNames is. A stub never reports success (BUG-030).
extern const char* const kImportFailures[];

// Called by a stub on every call. Must be cheap and must not allocate: some of these
// are called thousands of times per frame once the game runs.
void NoteImportCall(size_t index);

// Every stub that has been reached, in the order first seen, with call counts.
std::string ImportReport();

}  // namespace warden

#endif  // WARDEN_IMPORTS_H_
