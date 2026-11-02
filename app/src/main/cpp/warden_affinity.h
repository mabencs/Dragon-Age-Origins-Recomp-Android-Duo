// CPU topology and thread pinning. Task 4.8.
//
// The Thor is a Snapdragon 8 Gen 2: one Cortex-X3 prime core, four performance cores,
// three Cortex-A510 efficiency cores. The device profile puts it plainly - landing the
// game's main thread on an A510 will tank the framerate - and Android's scheduler has
// no idea that one of our threads is worth more than the others.
//
// Clusters are identified by each core's maximum frequency from sysfs rather than by
// hardcoded indices. Core 7 being the prime core is true of this SoC and is not a
// property of ARM, and a wrong hardcoded index fails silently: the thread gets pinned
// to a little core and the only symptom is that the game is slow.
#ifndef WARDEN_AFFINITY_H_
#define WARDEN_AFFINITY_H_

#include <cstdint>
#include <string>
#include <vector>

namespace warden {

class Affinity {
 public:
  enum class Cluster { kEfficiency, kPerformance, kPrime };

  struct Core {
    int index = 0;
    uint64_t max_freq_khz = 0;
    Cluster cluster = Cluster::kEfficiency;
  };

  static Affinity& Get();

  // Reads sysfs. Safe to call more than once; only the first call does the work.
  void Detect();

  // Restricts the calling thread to the prime core. For the thread that runs guest
  // code: the Xenon was an in-order 3.2 GHz triple core, and single-thread throughput
  // is what a static recompilation of it needs.
  bool PinCurrentThreadToPrime();
  // Prime plus the performance cores, excluding the efficiency cluster. For threads
  // that matter but are not the guest thread - the GPU thread, audio - and as the
  // better choice for anything that should keep running while the prime core is
  // thermally throttled.
  bool PinCurrentThreadToPerformance();

  // What the calling thread is allowed to run on, as reported back by the kernel
  // rather than as requested. Some vendors silently refuse sched_setaffinity.
  std::string DescribeCurrentThread() const;
  std::string Report();

 private:
  Affinity() = default;
  bool PinTo(Cluster minimum_cluster);

  bool detected_ = false;
  std::vector<Core> cores_;
  // Empty if detection failed, in which case nothing is pinned - better to let the
  // scheduler do as it likes than to pin to a guess.
  std::vector<int> prime_;
  std::vector<int> performance_;
  std::vector<int> efficiency_;
  std::string last_error_;
};

}  // namespace warden

#endif  // WARDEN_AFFINITY_H_
