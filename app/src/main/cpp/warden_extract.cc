#include "warden_extract.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>

#include "warden_affinity.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

// 4 MiB. Large enough that the per-read overhead disappears against the transfer,
// small enough that cancellation is responsive and the buffer is not a notable
// allocation on a device with 16 GiB.
constexpr size_t kChunkSize = 4u * 1024 * 1024;

// Written only after every file has been extracted, so its presence means the whole
// job finished - not that it started.
constexpr const char* kStampName = "extraction-complete.txt";

// The console's own system-update payload. Not game data, and extracting it would be
// 7.9 MB of files the runtime has no use for.
constexpr const char* kSkipRoot = "$SystemUpdate";

bool MakeDirectories(const std::string& path) {
  // Created one component at a time; EEXIST is success, which also makes this safe to
  // call for a directory that a previous run already made.
  std::string partial;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (!partial.empty() && mkdir(partial.c_str(), 0775) != 0 && errno != EEXIST) {
        return false;
      }
    }
    if (i < path.size()) {
      partial.push_back(path[i]);
    }
  }
  return true;
}

bool FileSize(const std::string& path, uint64_t& size_out) {
  struct stat info;
  if (stat(path.c_str(), &info) != 0) {
    return false;
  }
  size_out = uint64_t(info.st_size);
  return true;
}

std::string Errno() { return std::strerror(errno); }

}  // namespace

Extractor& Extractor::Get() {
  static Extractor instance;
  return instance;
}

namespace {
// Remembered from the last IsComplete call. A plain bool: written on the main thread
// at startup and read on the main thread for the report.
bool g_known_complete = false;
}  // namespace

bool Extractor::KnownComplete() { return g_known_complete; }

bool Extractor::IsComplete(const std::string& destination, std::string& detail) {
  const std::string stamp = destination + "/" + kStampName;
  std::FILE* file = std::fopen(stamp.c_str(), "r");
  if (!file) {
    detail.clear();
    return false;
  }
  char buffer[256] = {};
  const size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
  std::fclose(file);
  detail.assign(buffer, read);
  // Trim the trailing newline so the detail reads cleanly in a report.
  while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r')) {
    detail.pop_back();
  }
  g_known_complete = true;
  return true;
}

bool Extractor::Begin(int iso_fd, const std::string& destination,
                      const std::string& source) {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    XELOGW("extraction already running");
    return false;
  }
  if (thread_.joinable()) {
    // A previous run that finished but was never joined.
    thread_.join();
  }
  cancel_.store(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = Progress{};
    progress_.state = State::kScanning;
  }
  thread_ = std::thread(&Extractor::Run, this, iso_fd, destination, source);
  return true;
}

void Extractor::Cancel() { cancel_.store(true); }

void Extractor::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Extractor::Fail(const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  progress_.state = State::kFailed;
  progress_.error = message;
  XELOGE("extraction failed: {}", message);
}

void Extractor::Run(int iso_fd, std::string destination, std::string source) {
  // Performance cluster, not the prime core: the prime core is reserved for the
  // thread that will run guest code, and an extraction competing with it would be
  // the wrong trade. Pinning off the efficiency cores still matters - a 6.4 GiB copy
  // on an A510 is several times slower.
  Affinity::Get().PinCurrentThreadToPerformance();

  // pread, so there is no shared seek position and this thread's reads cannot be
  // disturbed by anything else holding the same descriptor.
  Xdvdfs iso([iso_fd](uint64_t offset, void* out, size_t size) {
    uint8_t* cursor = static_cast<uint8_t*>(out);
    size_t remaining = size;
    while (remaining) {
      const ssize_t got = pread(iso_fd, cursor, remaining, off_t(offset));
      if (got <= 0) {
        if (got < 0 && errno == EINTR) {
          continue;
        }
        return false;
      }
      cursor += got;
      offset += uint64_t(got);
      remaining -= size_t(got);
    }
    return true;
  });

  if (!iso.Open()) {
    Fail("not a readable Xbox 360 disc image: " + iso.error());
    close(iso_fd);
    running_.store(false);
    return;
  }

  // Scan first, so the progress bar means something from the start rather than
  // discovering the total as it goes.
  struct Job {
    std::string path;
    Xdvdfs::Entry entry;
  };
  std::vector<Job> jobs;
  uint64_t total_bytes = 0;

  std::function<bool(const std::string&)> scan = [&](const std::string& path) {
    std::vector<Xdvdfs::Entry> entries;
    if (!iso.List(path, entries)) {
      Fail(path == source && !source.empty()
               ? "this disc image has no " + source + " folder"
               : "cannot list '" + path + "': " + iso.error());
      return false;
    }
    for (const Xdvdfs::Entry& entry : entries) {
      if (cancel_.load()) {
        return false;
      }
      if (path.empty() && entry.name == kSkipRoot) {
        continue;
      }
      const std::string child = path.empty() ? entry.name : path + "/" + entry.name;
      if (entry.is_directory()) {
        if (!scan(child)) {
          return false;
        }
      } else {
        total_bytes += entry.size;
        // Within `source`, the paths below it: its contents go into the destination.
        jobs.push_back(Job{source.empty() ? child : child.substr(source.size() + 1), entry});
      }
    }
    return true;
  };

  if (!scan(source)) {
    if (cancel_.load()) {
      std::lock_guard<std::mutex> lock(mutex_);
      progress_.state = State::kCancelled;
    }
    close(iso_fd);
    running_.store(false);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_.state = State::kExtracting;
    progress_.bytes_total = total_bytes;
    progress_.files_total = uint32_t(jobs.size());
  }
  XELOGI("extracting {} files, {} bytes to {}", jobs.size(), total_bytes, destination);

  if (!MakeDirectories(destination)) {
    Fail("cannot create " + destination + ": " + Errno());
    close(iso_fd);
    running_.store(false);
    return;
  }

  for (const Job& job : jobs) {
    if (cancel_.load()) {
      std::lock_guard<std::mutex> lock(mutex_);
      progress_.state = State::kCancelled;
      close(iso_fd);
      running_.store(false);
      XELOGI("extraction cancelled after {} files", progress_.files_done);
      return;
    }
    const std::string output = destination + "/" + job.path;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      progress_.current = job.path;
    }

    // Resume: a destination file of exactly the right size was renamed into place by
    // a previous run, so it is whole. Partial writes live under a .part name and
    // never match.
    uint64_t existing = 0;
    if (FileSize(output, existing) && existing == job.entry.size) {
      std::lock_guard<std::mutex> lock(mutex_);
      ++progress_.files_done;
      ++progress_.files_skipped;
      progress_.bytes_done += job.entry.size;
      continue;
    }

    const size_t slash = output.rfind('/');
    if (slash != std::string::npos && !MakeDirectories(output.substr(0, slash))) {
      Fail("cannot create directory for " + job.path + ": " + Errno());
      break;
    }
    if (!ExtractFile(iso, job.entry, output)) {
      break;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ++progress_.files_done;
  }

  close(iso_fd);

  State final_state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // A cancellation noticed inside ExtractFile returns false without touching the
    // state - it is not a failure. Without recording it here, the state would still
    // read kExtracting below and a cancelled run would write the completion stamp,
    // so the next launch would skip extraction and the game would be missing files.
    if (cancel_.load() && progress_.state == State::kExtracting) {
      progress_.state = State::kCancelled;
      XELOGI("extraction cancelled after {} of {} files", progress_.files_done,
             progress_.files_total);
    }
    final_state = progress_.state;
  }
  if (final_state == State::kExtracting) {
    const std::string stamp = destination + "/" + kStampName;
    std::FILE* file = std::fopen(stamp.c_str(), "w");
    if (file) {
      std::lock_guard<std::mutex> lock(mutex_);
      std::fprintf(file, "%u files, %llu bytes\n", progress_.files_total,
                   (unsigned long long)progress_.bytes_total);
      std::fclose(file);
      progress_.state = State::kDone;
      progress_.current.clear();
      XELOGI("extraction complete: {} files, {} bytes ({} already present)",
             progress_.files_total, progress_.bytes_total, progress_.files_skipped);
    } else {
      Fail("cannot write the completion stamp: " + Errno());
    }
  }
  running_.store(false);
}

bool Extractor::ExtractFile(Xdvdfs& iso, const Xdvdfs::Entry& entry,
                            const std::string& destination_path) {
  // Written under a temporary name and renamed on success. Without that, a run
  // interrupted mid-file leaves a short file at the real name, and the resume check
  // would have to distinguish "short because interrupted" from "short because that
  // is its size" - which it cannot, for a file whose size it is trying to confirm.
  const std::string temporary = destination_path + ".part";
  std::FILE* out = std::fopen(temporary.c_str(), "wb");
  if (!out) {
    Fail("cannot create " + temporary + ": " + Errno());
    return false;
  }

  std::vector<uint8_t> buffer(kChunkSize);
  uint64_t written = 0;
  while (written < entry.size) {
    if (cancel_.load()) {
      std::fclose(out);
      ::remove(temporary.c_str());
      return false;
    }
    const size_t want = size_t(std::min<uint64_t>(kChunkSize, entry.size - written));
    if (!iso.Read(entry, written, buffer.data(), want)) {
      std::fclose(out);
      ::remove(temporary.c_str());
      Fail("read error in the image at " + destination_path);
      return false;
    }
    if (std::fwrite(buffer.data(), 1, want, out) != want) {
      std::fclose(out);
      ::remove(temporary.c_str());
      Fail("write error on " + destination_path + ": " + Errno());
      return false;
    }
    written += want;
    std::lock_guard<std::mutex> lock(mutex_);
    progress_.bytes_done += want;
  }

  if (std::fclose(out) != 0) {
    ::remove(temporary.c_str());
    Fail("could not flush " + destination_path + ": " + Errno());
    return false;
  }
  if (::rename(temporary.c_str(), destination_path.c_str()) != 0) {
    ::remove(temporary.c_str());
    Fail("could not rename " + temporary + ": " + Errno());
    return false;
  }
  return true;
}

Extractor::Progress Extractor::Poll() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return progress_;
}

std::string Extractor::Report() const {
  const Progress progress = Poll();
  static const char* kStateNames[] = {"idle",      "scanning", "extracting",
                                      "complete",  "FAILED",   "cancelled"};
  char line[512];
  std::string out = "GAME DATA\n";
  std::snprintf(line, sizeof(line), "  %-30s %s\n", "state",
                kStateNames[int(progress.state)]);
  out += line;
  if (progress.files_total) {
    const double percent = progress.bytes_total
                               ? 100.0 * double(progress.bytes_done) /
                                     double(progress.bytes_total)
                               : 0.0;
    std::snprintf(line, sizeof(line), "  %-30s %u / %u\n", "files",
                  progress.files_done, progress.files_total);
    out += line;
    std::snprintf(line, sizeof(line), "  %-30s %.2f / %.2f GiB  (%.1f%%)\n", "bytes",
                  double(progress.bytes_done) / double(1u << 30),
                  double(progress.bytes_total) / double(1u << 30), percent);
    out += line;
    if (progress.files_skipped) {
      std::snprintf(line, sizeof(line), "  %-30s %u\n", "already present",
                    progress.files_skipped);
      out += line;
    }
  }
  if (!progress.current.empty()) {
    std::snprintf(line, sizeof(line), "  %-30s %s\n", "current", progress.current.c_str());
    out += line;
  }
  if (!progress.error.empty()) {
    std::snprintf(line, sizeof(line), "  %-30s %s\n", "error", progress.error.c_str());
    out += line;
  }
  return out;
}

}  // namespace warden
