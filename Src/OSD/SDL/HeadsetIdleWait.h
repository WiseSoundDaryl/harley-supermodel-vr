#ifndef INCLUDED_HEADSET_IDLE_WAIT_H
#define INCLUDED_HEADSET_IDLE_WAIT_H

#include <algorithm>
#include <cstdint>
#ifdef SUPERMODEL_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Only the idle owner loop uses this timer. Never sleep past a native deadline
// merely to complete a fixed polling interval. This does not change the game
// clock, XR timing thread, worker completion waits, or system timer resolution.
// At a native deadline only, a final <=300us CPU-pause tail avoids the kernel
// wakeup rounding consuming the narrow second-eye-pair admission window.
class HeadsetIdleWait
{
public:
  HeadsetIdleWait() = default;
  HeadsetIdleWait(const HeadsetIdleWait&) = delete;
  HeadsetIdleWait& operator=(const HeadsetIdleWait&) = delete;
  ~HeadsetIdleWait() {
#ifdef SUPERMODEL_WIN32
    if (m_timer) CloseHandle(m_timer);
#endif
  }
  bool Initialize() {
#ifdef SUPERMODEL_WIN32
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return false;
    m_frequency = uint64_t(frequency.QuadPart);
    if (!m_timer) m_timer = CreateWaitableTimerExW(nullptr, nullptr,
      0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
      TIMER_MODIFY_STATE | SYNCHRONIZE);
    return m_timer != nullptr;
#else
    return false;
#endif
  }
  static int64_t Duration(int64_t remainingNs) {
    return std::clamp<int64_t>(remainingNs, 0, 1000000);
  }
  bool Wait(int64_t remainingNs) {
#ifdef SUPERMODEL_WIN32
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return false;
    return WaitFrom(uint64_t(now.QuadPart), m_frequency, remainingNs);
#else
    return remainingNs <= 0;
#endif
  }
  bool WaitFrom(uint64_t sampledCounter, uint64_t frequency, int64_t remainingNs,
    void* readySignal = nullptr, bool* wokeReady = nullptr) {
    if (wokeReady) *wokeReady = false;
    const int64_t duration = Duration(remainingNs);
    if (duration < 100) return true;
#ifdef SUPERMODEL_WIN32
    if (!m_timer || !frequency || frequency != m_frequency) return false;
    const uint64_t target = sampledCounter + uint64_t(double(duration) * double(frequency) / 1.0e9);
    if (target < sampledCounter) return false;
    const bool deadlineTail = remainingNs <= 1000000;
    // Borrowed session-owned event: never close, reset or consume this handle.
    HANDLE ready = static_cast<HANDLE>(readySignal);
    bool checkReady = true;
    for (;;) {
      if (ready && checkReady) {
        const DWORD status = WaitForSingleObject(ready, 0);
        if (status == WAIT_OBJECT_0) { if (wokeReady) *wokeReady = true; return true; }
        if (status != WAIT_TIMEOUT) ready = nullptr;
        checkReady = false; // Keep kernel calls out of the <=300us pause tail.
      }
      LARGE_INTEGER now{};
      if (!QueryPerformanceCounter(&now)) break;
      if (uint64_t(now.QuadPart) >= target) return true;
      const int64_t left = int64_t(double(target - uint64_t(now.QuadPart)) * 1.0e9 / double(frequency));
      if (deadlineTail && left <= 300000) { YieldProcessor(); continue; }
      const int64_t sleep = deadlineTail ? left - 300000 : left;
      LARGE_INTEGER due{};
      due.QuadPart = -std::max<int64_t>(1, sleep / 100);
      if (!SetWaitableTimerEx(m_timer, &due, 0, nullptr, nullptr, nullptr, 0)) break;
      DWORD waited;
      if (ready) {
        const HANDLE handles[2] = {ready, m_timer};
        waited = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (waited == WAIT_OBJECT_0) { if (wokeReady) *wokeReady = true; return true; }
        if (waited == WAIT_OBJECT_0 + 1) waited = WAIT_OBJECT_0;
        else {
          // A failed optional hint must not retire a working native timer.
          ready = nullptr;
          waited = WaitForSingleObject(m_timer, INFINITE);
        }
      } else waited = WaitForSingleObject(m_timer, INFINITE);
      if (waited != WAIT_OBJECT_0) break;
      if (!deadlineTail) return true; // An early wake is just an earlier XR poll.
      checkReady = true;
    }
    CloseHandle(m_timer); m_timer = nullptr;
#endif
    return false;
  }
private:
#ifdef SUPERMODEL_WIN32
  HANDLE m_timer = nullptr;
  uint64_t m_frequency = 0;
#endif
};
#endif
