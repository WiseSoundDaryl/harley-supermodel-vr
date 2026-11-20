#ifndef INCLUDED_OPENXR_TIMING_READY_SIGNAL_H
#define INCLUDED_OPENXR_TIMING_READY_SIGNAL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>

// Optional wake hint only. The timing mutex protects publication/consumption;
// this manual-reset event never owns a frame or grants presentation admission.
// Initialize/Get/Close run on the GL owner. Publish/Reset run under waitMutex
// (or after its worker has joined). The handle stays open until that join.
class OpenXRTimingReadySignal
{
public:
  OpenXRTimingReadySignal() = default;
  OpenXRTimingReadySignal(const OpenXRTimingReadySignal&) = delete;
  OpenXRTimingReadySignal& operator=(const OpenXRTimingReadySignal&) = delete;
  ~OpenXRTimingReadySignal() { CloseAfterWorkerJoin(); }

  bool Initialize()
  {
    CloseAfterWorkerJoin();
    m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_healthy.store(m_event != nullptr, std::memory_order_release);
    return m_event != nullptr;
  }

  void* Get() const
  {
    return m_healthy.load(std::memory_order_acquire) ? m_event : nullptr;
  }

  void Publish() { Update(true); }
  void Reset() { Update(false); }

  void CloseAfterWorkerJoin()
  {
    m_healthy.store(false, std::memory_order_release);
    if (m_event) CloseHandle(m_event);
    m_event = nullptr;
  }

private:
  void Update(bool ready)
  {
    if (!m_healthy.load(std::memory_order_acquire)) return;
    if (!(ready ? SetEvent(m_event) : ResetEvent(m_event))) {
      // Do not close a handle borrowed by the owner's current wait. Disable
      // future hint waits instead; the existing timer/poll path remains valid.
      m_healthy.store(false, std::memory_order_release);
    }
  }

  HANDLE m_event = nullptr;
  std::atomic<bool> m_healthy{false};
};

#endif
