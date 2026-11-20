#ifndef INCLUDED_LIVE_FRAME_TRACE_H
#define INCLUDED_LIVE_FRAME_TRACE_H

// Opt-in diagnostic capture. Producers only reserve a preallocated slot and
// copy integers; formatting, compression and filesystem work happen after the
// OpenXR worker has joined. QPC timestamps share an epoch across processes.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <zlib.h>
#endif

namespace LiveFrameTrace {
struct Record {
  const char* stage;
  uint64_t begin, end, frame;
  int64_t a, b, c, d;
  uint32_t thread;
};
struct State {
  static constexpr size_t Capacity = 2000000;
  std::unique_ptr<Record[]> records;
  std::atomic<uint64_t> next{0};
  std::atomic<bool> enabled{false};
  std::string path;
  uint64_t frequency = 0, anchorBefore = 0, anchorAfter = 0, filetime = 0;
  uint64_t ownerFrame = 0;
  uint32_t ownerThread = 0;
  int descriptor = -1;
};
inline State& Get() { static State state; return state; }
inline bool Enabled() { return Get().enabled.load(std::memory_order_acquire); }
// Emulator workers may outlive Capture::Close. The atomic fast-path avoids a
// shutdown race and avoids TLS/API calls entirely when tracing is disabled.
// A worker's local false marker then prevents access to owner-only fields.
inline bool& OwnerThreadMarker() { static thread_local bool owner = false; return owner; }
inline bool OwnerEnabled() { return Enabled() && OwnerThreadMarker(); }
inline uint64_t RawNow() {
#ifdef _WIN32
  LARGE_INTEGER value; QueryPerformanceCounter(&value); return uint64_t(value.QuadPart);
#else
  return 0;
#endif
}
inline uint64_t Now() { return Enabled() ? RawNow() : 0; }
inline uint64_t OwnerNow() { return OwnerEnabled() ? RawNow() : 0; }
inline void Event(const char* stage, uint64_t begin, uint64_t end, uint64_t frame = 0,
                  int64_t a = 0, int64_t b = 0, int64_t c = 0, int64_t d = 0) {
#ifdef _WIN32
  auto& s = Get();
  if (!s.enabled || !begin) return;
  const uint64_t slot = s.next.fetch_add(1, std::memory_order_relaxed);
  if (slot < State::Capacity)
    s.records[slot] = {stage, begin, end, frame, a, b, c, d, GetCurrentThreadId()};
#endif
}

// Only the capture owner can obtain a nonzero begin timestamp. No other
// emulator thread records lock waits or reads capture state on this path.
inline void SlowOwnerEvent(const char* stage, uint64_t begin, int64_t a = 0,
                           int64_t b = 0, int64_t c = 0, int64_t d = 0) {
  if (!begin) return;
  const uint64_t end = RawNow();
  if (end >= begin && end - begin > std::max<uint64_t>(1, Get().frequency / 500))
    Event(stage, begin, end, Get().ownerFrame, a, b, c, d);
}

struct OwnerCpuSample {
  uint64_t cycles = 0, kernel100ns = 0, user100ns = 0;
  bool cyclesValid = false, timesValid = false;
};
inline OwnerCpuSample ReadOwnerCpu() {
  OwnerCpuSample sample;
#ifdef _WIN32
  if (!OwnerEnabled()) return sample;
  ULONG64 cycles = 0;
  sample.cyclesValid = QueryThreadCycleTime(GetCurrentThread(), &cycles) != 0;
  sample.cycles = cycles;
  FILETIME created{}, exited{}, kernel{}, user{};
  sample.timesValid = GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != 0;
  sample.kernel100ns = (uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
  sample.user100ns = (uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
#endif
  return sample;
}

// Native preparation begins and ends through the existing video callbacks;
// no changes to optimized Model3 code are needed. CPU-time counters have OS
// accounting granularity; cycles are not milliseconds or a fixed CPU rate.
struct OwnerWorkSpan {
  uint64_t begin = 0, frame = 0;
  OwnerCpuSample cpu;
  void Begin(uint64_t id) {
    begin = OwnerNow();
    if (!begin) return;
    frame = id; Get().ownerFrame = id;
    cpu = ReadOwnerCpu();
  }
  void End(const char* stage) {
    if (!begin) return;
    const OwnerCpuSample ended = ReadOwnerCpu();
    const uint64_t end = RawNow();
    const bool cyclesValid = cpu.cyclesValid && ended.cyclesValid && ended.cycles >= cpu.cycles;
    const bool timesValid = cpu.timesValid && ended.timesValid &&
      ended.kernel100ns >= cpu.kernel100ns && ended.user100ns >= cpu.user100ns;
    Event(stage, begin, end, frame,
      cyclesValid ? int64_t(ended.cycles - cpu.cycles) : -1,
      timesValid ? int64_t(ended.kernel100ns - cpu.kernel100ns) : -1,
      timesValid ? int64_t(ended.user100ns - cpu.user100ns) : -1,
      int64_t(cyclesValid) | (int64_t(timesValid) << 1));
    begin = 0;
  }
};

// Must outlive OpenXRSession. Its destructor then follows the session's worker
// join even on an initialization error. Explicit Close is also idempotent.
class Capture {
  bool owned = false;
public:
  bool Start(bool openxr) {
    const char* requested = std::getenv("SMVR_LIVE_TRACE_PATH");
    if (!requested || !*requested || !openxr) return true;
#ifdef _WIN32
    auto& s = Get();
    if (s.enabled || s.descriptor >= 0) return false;
    s.path = requested;
    // The launcher supplies a fresh absolute filename; never overwrite a trace.
    if (s.path.size() < 8 || s.path[1] != ':' || (s.path[2] != '\\' && s.path[2] != '/') ||
        s.path.substr(s.path.size()-7) != ".csv.gz") return false;
    if (GetFileAttributesA((s.path + ".meta.json").c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    HANDLE file = CreateFileA(s.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    s.descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(file), _O_BINARY | _O_WRONLY);
    if (s.descriptor < 0) { CloseHandle(file); return false; }
    owned = true;
    try { s.records.reset(new Record[State::Capacity]{}); }
    catch (...) { _close(s.descriptor); s.descriptor = -1; owned = false; return false; }
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency); s.frequency = frequency.QuadPart;
    FILETIME utc{}; s.anchorBefore = RawNow(); GetSystemTimePreciseAsFileTime(&utc); s.anchorAfter = RawNow();
    s.filetime = (uint64_t(utc.dwHighDateTime) << 32) | utc.dwLowDateTime;
    s.ownerThread = GetCurrentThreadId(); s.ownerFrame = 0; OwnerThreadMarker() = true;
    s.next.store(0, std::memory_order_relaxed); s.enabled.store(true, std::memory_order_release);
    Event("capture.start", s.anchorBefore, s.anchorAfter);
    return true;
#else
    return false;
#endif
  }
  bool Close() {
#ifdef _WIN32
    auto& s = Get();
    if (!owned) return true;
    owned = false;
    if (s.descriptor < 0) return true;
    // Caller must have joined all producers before this point.
    s.enabled.store(false, std::memory_order_release);
    OwnerThreadMarker() = false;
    const uint64_t attempted = s.next.load(std::memory_order_relaxed);
    const uint64_t kept = std::min<uint64_t>(attempted, State::Capacity);
    gzFile out = gzdopen(s.descriptor, "wb1");
    if (!out) { _close(s.descriptor); s.descriptor = -1; return false; }
    gzbuffer(out, 256 * 1024);
    bool ok = gzputs(out, "stage,start_qpc,end_qpc,thread_id,frame,a,b,c,d\n") > 0;
    uint64_t written = 0, firstQpc = UINT64_MAX, lastQpc = 0; bool diskCap = false;
    char row[384];
    for (; ok && written < kept; ++written) {
      if ((written & 4095) == 0 && _telli64(s.descriptor) >= 126LL * 1024 * 1024) { diskCap = true; break; }
      const auto& r = s.records[written];
      firstQpc = std::min(firstQpc, r.begin); lastQpc = std::max(lastQpc, r.end);
      const int n = std::snprintf(row, sizeof row, "%s,%llu,%llu,%lu,%llu,%lld,%lld,%lld,%lld\n",
        r.stage, (unsigned long long)r.begin, (unsigned long long)r.end, (unsigned long)r.thread,
        (unsigned long long)r.frame, (long long)r.a, (long long)r.b, (long long)r.c, (long long)r.d);
      ok = n > 0 && n < int(sizeof row) && gzwrite(out, row, unsigned(n)) == n;
    }
    ok = gzclose(out) == Z_OK && ok; s.descriptor = -1;
    const std::string metaPath = s.path + ".meta.json";
    HANDLE meta = CreateFileA(metaPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (meta == INVALID_HANDLE_VALUE) return false;
    char metadata[1400];
    const int length = std::snprintf(metadata, sizeof metadata,
      "{\"schema\":1,\"clock\":\"Windows QPC\",\"qpc_frequency\":%llu,\"utc_filetime_100ns\":%llu,"
      "\"utc_anchor_before_qpc\":%llu,\"utc_anchor_after_qpc\":%llu,\"attempted_records\":%llu,"
      "\"written_records\":%llu,\"record_capacity\":%llu,\"capacity_reached\":%s,\"disk_capacity_reached\":%s,"
      "\"complete\":%s,\"first_qpc\":%llu,\"last_qpc\":%llu,\"owner_thread_id\":%lu,"
      "\"scope\":\"CPU spans; overlapping spans are not additive. Runtime/compositor timing and perceived comfort require separate evidence. Buffered until graceful shutdown; no trace disk writes during gameplay.\"}\n",
      (unsigned long long)s.frequency, (unsigned long long)s.filetime,
      (unsigned long long)s.anchorBefore, (unsigned long long)s.anchorAfter, (unsigned long long)attempted,
      (unsigned long long)written, (unsigned long long)State::Capacity, attempted > State::Capacity ? "true" : "false",
      diskCap ? "true" : "false", ok && !diskCap && attempted <= State::Capacity ? "true" : "false",
      (unsigned long long)(written ? firstQpc : 0), (unsigned long long)(written ? lastQpc : 0),
      (unsigned long)s.ownerThread);
    DWORD bytes = 0;
    ok = length > 0 && length < int(sizeof metadata) && WriteFile(meta, metadata, DWORD(length), &bytes, nullptr) && bytes == DWORD(length) && ok;
    CloseHandle(meta); s.records.reset();
    return ok;
#else
    return true;
#endif
  }
  ~Capture() { Close(); }
};
// Logs even early loop exits. Callers may update payloads before scope exit.
struct Scope {
  const char* stage; uint64_t begin, frame; int64_t a = 0, b = 0, c = 0, d = 0;
  Scope(const char* name, uint64_t id = 0) : stage(name), begin(Now()), frame(id) {}
  ~Scope() { if (begin) Event(stage, begin, Now(), frame, a, b, c, d); }
};
}
#endif
