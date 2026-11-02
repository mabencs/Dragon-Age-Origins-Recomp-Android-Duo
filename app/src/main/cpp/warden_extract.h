// First-run extraction of the disc image to app storage. Task 4.4.
//
// The plan's reasoning stands: do not read from the ISO at runtime. Seek latency plus
// Storage Access Framework overhead make it a bad trade against 6.4 GiB of disk, and
// the handheld has 209 GiB free.
//
// Measured, rather than the plan's estimate of "roughly 20 GB":
//
//   modules/        4,025,577,252   1,658 files
//   packages/       2,812,884,639   7,789 files
//   default.xex        19,021,824
//   $SystemUpdate/      7,915,844   skipped - console system update, not game data
//   nxeart, splash.dds  1,661,056
//   -------------------------------------------------
//   extracted       6,859,144,771   9,450 files   (6.39 GiB)
//
// Runs on its own thread. Resumable, because a 6.4 GiB extraction that has to start
// over because the screen turned off is not resumable in any useful sense: each file
// is written to a temporary name and renamed on completion, so a destination file that
// exists with exactly the right size is known to be whole and is skipped.
#ifndef WARDEN_EXTRACT_H_
#define WARDEN_EXTRACT_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "warden_xdvdfs.h"

namespace warden {

class Extractor {
 public:
  enum class State : int32_t {
    kIdle = 0,
    kScanning = 1,
    kExtracting = 2,
    kDone = 3,
    kFailed = 4,
    kCancelled = 5,
  };

  struct Progress {
    State state = State::kIdle;
    uint64_t bytes_done = 0;
    uint64_t bytes_total = 0;
    uint32_t files_done = 0;
    uint32_t files_total = 0;
    uint32_t files_skipped = 0;
    std::string current;
    std::string error;
  };

  static Extractor& Get();

  // Takes ownership of `iso_fd` and closes it when finished. Returns false if an
  // extraction is already running. `source`, when given, is a folder on the disc
  // ("Addins/DAO_PRC_EP_1", PLAN 10.10) whose contents alone are extracted, into
  // `destination` itself.
  bool Begin(int iso_fd, const std::string& destination,
             const std::string& source = std::string());
  void Cancel();
  // Blocks until the thread has finished. Called before the process goes away so the
  // thread is not writing into a destination that is being torn down.
  void Join();

  Progress Poll() const;
  std::string Report() const;

  // True if `destination` already holds a complete extraction, from the stamp written
  // at the end of a successful run.
  //
  // Also records the answer. Java is the side that knows where the extraction lives -
  // it comes from getExternalFilesDir - and it already asks this at startup, so the
  // status block can read the result instead of a new JNI call being added to ask the
  // same question from the side that does not know.
  static bool IsComplete(const std::string& destination, std::string& detail);
  static bool KnownComplete();

 private:
  Extractor() = default;

  void Run(int iso_fd, std::string destination, std::string source);
  bool ExtractFile(Xdvdfs& iso, const Xdvdfs::Entry& entry,
                   const std::string& destination_path);
  void Fail(const std::string& message);

  mutable std::mutex mutex_;
  Progress progress_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace warden

#endif  // WARDEN_EXTRACT_H_
