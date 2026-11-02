#include "warden_affinity.h"

#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "xenia/base/logging.h"

namespace warden {

namespace {

// sysfs rather than sysconf(_SC_NPROCESSORS_CONF): cores can be hotplugged offline on
// Android, and an offline core still has its cpufreq node, so this sees the whole
// topology rather than whatever happens to be awake.
uint64_t ReadMaxFreqKhz(int cpu) {
  char path[128];
  std::snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
  std::FILE* file = std::fopen(path, "r");
  if (!file) {
    return 0;
  }
  unsigned long long value = 0;
  const int read = std::fscanf(file, "%llu", &value);
  std::fclose(file);
  return read == 1 ? uint64_t(value) : 0;
}

std::string JoinCores(const std::vector<int>& cores) {
  if (cores.empty()) {
    return "none";
  }
  std::string out;
  for (int core : cores) {
    if (!out.empty()) {
      out += ",";
    }
    out += std::to_string(core);
  }
  return out;
}

const char* ClusterName(Affinity::Cluster cluster) {
  switch (cluster) {
    case Affinity::Cluster::kPrime:
      return "prime";
    case Affinity::Cluster::kPerformance:
      return "performance";
    default:
      return "efficiency";
  }
}

}  // namespace

Affinity& Affinity::Get() {
  static Affinity instance;
  return instance;
}

void Affinity::Detect() {
  if (detected_) {
    return;
  }
  detected_ = true;

  // CPU_SETSIZE is the ceiling on what sched_setaffinity can address anyway, so there
  // is no point looking further than that.
  for (int cpu = 0; cpu < CPU_SETSIZE && cpu < 64; ++cpu) {
    uint64_t freq = ReadMaxFreqKhz(cpu);
    if (!freq) {
      continue;
    }
    cores_.push_back(Core{cpu, freq, Cluster::kEfficiency});
  }
  if (cores_.empty()) {
    last_error_ = "no cpufreq nodes readable";
    XELOGW("CPU topology unknown: {}", last_error_);
    return;
  }

  uint64_t highest = 0;
  uint64_t lowest = UINT64_MAX;
  for (const Core& core : cores_) {
    highest = std::max(highest, core.max_freq_khz);
    lowest = std::min(lowest, core.max_freq_khz);
  }

  for (Core& core : cores_) {
    if (core.max_freq_khz == highest) {
      core.cluster = Cluster::kPrime;
      prime_.push_back(core.index);
    } else if (core.max_freq_khz == lowest) {
      core.cluster = Cluster::kEfficiency;
      efficiency_.push_back(core.index);
    } else {
      core.cluster = Cluster::kPerformance;
      performance_.push_back(core.index);
    }
  }

  // A machine whose cores all share one frequency has no clusters to speak of, and
  // everything would land in "prime". Say so rather than pinning the guest thread to
  // all eight cores and calling it an optimisation.
  if (highest == lowest) {
    last_error_ = "all cores report the same maximum frequency; no clusters";
    XELOGI("CPU topology: {} uniform cores, pinning disabled", cores_.size());
    prime_.clear();
    performance_.clear();
    efficiency_.clear();
    return;
  }

  XELOGI("CPU topology: prime {} / performance {} / efficiency {}", JoinCores(prime_),
         JoinCores(performance_), JoinCores(efficiency_));
}

bool Affinity::PinTo(Cluster minimum_cluster) {
  Detect();
  std::vector<int> allowed = prime_;
  if (minimum_cluster == Cluster::kPerformance) {
    allowed.insert(allowed.end(), performance_.begin(), performance_.end());
  }
  if (allowed.empty()) {
    // Without a known topology, do nothing. A guess here is worse than the
    // scheduler's own judgement.
    return false;
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  for (int core : allowed) {
    CPU_SET(core, &set);
  }
  // pid 0 means the calling thread, not the process - which is the whole point.
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    last_error_ = std::string("sched_setaffinity: ") + std::strerror(errno);
    XELOGW("could not pin thread to {}: {}", ClusterName(minimum_cluster),
           last_error_);
    return false;
  }
  return true;
}

bool Affinity::PinCurrentThreadToPrime() { return PinTo(Cluster::kPrime); }

bool Affinity::PinCurrentThreadToPerformance() {
  return PinTo(Cluster::kPerformance);
}

std::string Affinity::DescribeCurrentThread() const {
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) != 0) {
    return std::string("sched_getaffinity failed: ") + std::strerror(errno);
  }
  std::vector<int> allowed;
  for (const Core& core : cores_) {
    if (CPU_ISSET(core.index, &set)) {
      allowed.push_back(core.index);
    }
  }
  return JoinCores(allowed);
}

std::string Affinity::Report() {
  Detect();
  std::string out = "CPU TOPOLOGY\n";
  char line[256];
  for (const Core& core : cores_) {
    std::snprintf(line, sizeof(line), "  cpu%-2d  %7llu kHz  %s\n", core.index,
                  (unsigned long long)core.max_freq_khz, ClusterName(core.cluster));
    out += line;
  }
  std::snprintf(line, sizeof(line), "  %-32s %s\n", "prime cores", JoinCores(prime_).c_str());
  out += line;
  std::snprintf(line, sizeof(line), "  %-32s %s\n", "performance cores",
                JoinCores(performance_).c_str());
  out += line;
  std::snprintf(line, sizeof(line), "  %-32s %s\n", "efficiency cores",
                JoinCores(efficiency_).c_str());
  out += line;

  // Whether pinning works at all on this device, checked by doing it on a throwaway
  // thread and reading the mask back. Some vendors silently refuse
  // sched_setaffinity for app processes, and finding that out in Phase 5 - with a
  // slow game and no idea why - would be considerably worse than finding out here.
  //
  // The UI thread is deliberately not the one tested: it is not the thread that will
  // run guest code, and pinning it would fight the system for nothing.
  cpu_set_t original;
  CPU_ZERO(&original);
  const bool saved = sched_getaffinity(0, sizeof(original), &original) == 0;
  const bool pinned = PinCurrentThreadToPrime();
  std::string after = DescribeCurrentThread();
  if (saved) {
    sched_setaffinity(0, sizeof(original), &original);
  }
  std::snprintf(line, sizeof(line), "  %-32s %s\n", "sched_setaffinity accepted",
                pinned ? "yes" : "no");
  out += line;
  std::snprintf(line, sizeof(line), "  %-32s %s\n", "mask read back after pinning",
                after.c_str());
  out += line;
  std::snprintf(line, sizeof(line), "  %-32s %s\n", "restored",
                saved ? DescribeCurrentThread().c_str() : "could not save original");
  out += line;
  if (!last_error_.empty()) {
    std::snprintf(line, sizeof(line), "  %-32s %s\n", "note", last_error_.c_str());
    out += line;
  }
  return out;
}

}  // namespace warden
