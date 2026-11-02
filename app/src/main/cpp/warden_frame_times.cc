// Frame times for the performance baseline (PLAN 6.7). See warden_frame_times.h.
//
// Two streams of timestamps: each present - when the presenter queued a frame for the
// panel, which shows it at the first refresh after the GPU has finished it, or at the
// refresh it is held for (warden_frame_pacing) - and each of the title's VdSwap calls,
// which is how evenly the title produces frames. Frames queued a different number of
// refreshes apart than their neighbours are judder even when the average frame rate is
// right; the histogram in refreshes shows it. When frames actually reached the panel is
// the presentation engine's to say (warden_frame_pacing, VK_GOOGLE_display_timing).
// Writers take a mutex once a frame; the status reporter takes the pending intervals
// every five seconds.
#include "warden_frame_times.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

#include "warden_deck.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

struct Intervals {
  std::mutex mutex;
  int64_t last = 0;
  std::vector<int64_t> pending;  // nanoseconds between consecutive events
  int64_t recent[64] = {};       // the last 64 intervals, a ring
  int recent_count = 0;
  int recent_next = 0;

  void Record(int64_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    if (last && now > last) {
      if (pending.size() < 65536) {
        pending.push_back(now - last);
      }
      recent[recent_next] = now - last;
      recent_next = (recent_next + 1) % 64;
      recent_count = std::min(recent_count + 1, 64);
    }
    last = now;
  }

  std::vector<int64_t> Take() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<int64_t> out;
    out.swap(pending);
    return out;
  }

  // The median of the last 16. The caller holds the mutex.
  int64_t MedianLocked() const {
    const int count = std::min(recent_count, 16);
    if (!count) {
      return 0;
    }
    int64_t sorted[16];
    for (int i = 0; i < count; ++i) {
      sorted[i] = recent[(recent_next + 64 - 1 - i) % 64];
    }
    std::sort(sorted, sorted + count);
    return sorted[count / 2];
  }

  int64_t RecentMedian() {
    std::lock_guard<std::mutex> lock(mutex);
    return MedianLocked();
  }

  int64_t RecentMean() {
    std::lock_guard<std::mutex> lock(mutex);
    const int64_t median = MedianLocked();
    int64_t sum = 0;
    int count = 0;
    for (int i = 0; i < recent_count; ++i) {
      if (recent[i] <= 4 * median) {
        sum += recent[i];
        ++count;
      }
    }
    return count ? sum / count : 0;
  }
};

Intervals g_presents;
Intervals g_guest;
std::mutex g_unpresented_mutex;
std::deque<int64_t> g_unpresented;  // the title's swaps not yet presented, oldest first
std::atomic<int64_t> g_refresh_period{0};

int64_t Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

std::string DescribeIntervals(const char* what, std::vector<int64_t> intervals, int64_t unit,
                              const char* unit_name) {
  if (intervals.empty()) {
    return fmt::format("{}: no frames", what);
  }
  std::sort(intervals.begin(), intervals.end());
  const size_t n = intervals.size();
  double sum = 0.0;
  for (int64_t interval : intervals) {
    sum += double(interval);
  }
  auto percentile = [&](double p) {
    const size_t index = std::min(n - 1, size_t(p * double(n - 1) + 0.5));
    return double(intervals[index]) / 1e6;
  };
  // The 1% low: the frame rate of the slowest 1% of intervals, at least one.
  const size_t worst = std::max<size_t>(1, n / 100);
  double worst_sum = 0.0;
  for (size_t i = n - worst; i < n; ++i) {
    worst_sum += double(intervals[i]);
  }
  std::string text = fmt::format(
      "{}: {} frames, interval ms mean {:.1f} p50 {:.1f} p95 {:.1f} p99 {:.1f} max {:.1f}, "
      "1% low {:.1f} fps",
      what, n, sum / double(n) / 1e6, percentile(0.50), percentile(0.95),
      percentile(0.99), double(intervals.back()) / 1e6, 1e9 * double(worst) / worst_sum);
  if (unit > 0) {
    int counts[8] = {};  // 1..7 units, 8 or more in the last
    for (int64_t interval : intervals) {
      int units = int((interval + unit / 2) / unit);
      units = std::clamp(units, 1, 8);
      ++counts[units - 1];
    }
    text += fmt::format(", in {} ({:.2f} ms):", unit_name, double(unit) / 1e6);
    for (int k = 0; k < 8; ++k) {
      if (counts[k]) {
        text += fmt::format(" {}{}x{}", k + 1, k == 7 ? "+" : "", counts[k]);
      }
    }
  }
  return text;
}

void RecordPresent(int64_t present_nanos) {
  g_presents.Record(present_nanos);
  deck::NotePresent();  // PLAN 8.6: the deck's performance tab
}

void SetDisplayRefreshPeriod(int64_t period_nanos) {
  g_refresh_period.store(period_nanos, std::memory_order_relaxed);
}

void RecordGuestSwap() {
  const int64_t now = Now();
  // BUG-092: a title frame of more than 50 ms logged with its time, to be set beside what
  // the log says happened then (the first 200 a session).
  static int64_t last_swap = 0;
  if (last_swap && now - last_swap > 50'000'000) {
    static int logged = 0;
    if (++logged <= 200) {
      XELOGI("frame times: a title frame of {:.1f} ms", double(now - last_swap) / 1e6);
    }
  }
  last_swap = now;
  g_guest.Record(now);
  deck::NoteGuestSwap(now);  // PLAN 8.6: the deck's performance tab
  std::lock_guard<std::mutex> lock(g_unpresented_mutex);
  g_unpresented.push_back(now);
  if (g_unpresented.size() > 8) {
    g_unpresented.pop_front();
  }
}

int64_t TakeOldestGuestSwap() {
  const int64_t now = Now();
  std::lock_guard<std::mutex> lock(g_unpresented_mutex);
  // A swap the presenter never presented (a swapchain being made again) would pair every
  // later present with the swap before its own: one older than any present lags is
  // dropped instead.
  while (!g_unpresented.empty() && now - g_unpresented.front() > 200000000) {
    g_unpresented.pop_front();
  }
  if (g_unpresented.empty()) {
    return 0;
  }
  const int64_t swap = g_unpresented.front();
  g_unpresented.pop_front();
  return swap;
}

int64_t RecentGuestFrameInterval() { return g_guest.RecentMedian(); }

int64_t RecentGuestMeanInterval() { return g_guest.RecentMean(); }

std::string FrameTimesSummary() {
  return DescribeIntervals("present", g_presents.Take(),
                           g_refresh_period.load(std::memory_order_relaxed), "refreshes") +
         "; " + DescribeIntervals("title", g_guest.Take(), int64_t(1e9 / 60), "60 Hz vblanks");
}

}  // namespace warden

