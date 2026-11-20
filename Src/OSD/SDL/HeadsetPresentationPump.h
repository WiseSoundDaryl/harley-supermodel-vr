#ifndef INCLUDED_HEADSET_PRESENTATION_PUMP_H
#define INCLUDED_HEADSET_PRESENTATION_PUMP_H
#include "HeadsetControlsLatch.h"
#include "HeadsetPresentBudget.h"
#include "HeadsetCadenceStats.h"
#include "LiveFrameTrace.h"
#include <chrono>

class IRender3D;

// All of this code runs on the GL owner, including WorkerWait. The runtime's
// blocking timing wait runs separately inside Session; no emulator state is
// read or changed here. Template injection allows testing without a headset.
template<class Session, class Clock = std::chrono::steady_clock>
class HeadsetPresentationPump
{
public:
  struct PollStats {
    uint64_t attempted = 0, begun = 0, failed = 0;
    uint64_t pendingFocused = 0, pendingOther = 0;
  };
  HeadsetPresentationPump(Session& session, bool& paused)
    : m_session(session), m_paused(paused) {}
  void SetRenderer(IRender3D* renderer) { m_renderer = renderer; }
  void SetBudget(HeadsetPresentBudget* budget) { m_budget = budget; }
  void SetLateWorkerEnabled(bool enabled) { m_lateWorkerEnabled = enabled; }
  static double NowMs()
  {
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
  }
  double LastFrameMs() const { return m_lastFrameMs; }
  void ObserveActivity(bool active) {
    m_cadence.SetActive(active);
    if (!active) {
      m_displayPeriodMs = 0;
      if (m_budget) m_budget->ResetWorkerPrediction();
      if (m_budget) m_budget->ResetLateWorkerHistory();
    }
  }
  void BeginNative(double nowMs) { m_cadence.BeginNative(nowMs); }
  void EndNative() { m_cadence.EndNative(); }
  const HeadsetCadenceStats& Cadence() const { return m_cadence; }
  const PollStats& Polls() const { return m_polls; }
  bool Pump(bool mirror = false, int width = 0, int height = 0)
  {
    if (Failed() || m_session.WantsExit()) return false;
    const auto traceStart = LiveFrameTrace::Now();
    const double started = NowMs();
    OpenXRSession::Frame next;
    ++m_polls.attempted;
    if (!m_session.TryBegin(next)) {
      ++m_polls.failed;
      // A locate/input failure may occur after xrBeginFrame. Balance it now.
      m_session.End(nullptr, false, 0, 0);
      m_failed = true;
      m_controls.Suspend();
      LiveFrameTrace::Event("pump.failed", traceStart, LiveFrameTrace::Now());
      return false;
    }
    const bool focused = m_session.Focused();
    if (!focused) { m_controls.Suspend(); m_cadence.SetActive(false); }
    if (!next.begun) {
      // Pending timing is NOT tracking/focus loss. An unfocused/not-running
      // session can also return no begun frame; keep that diagnostic separate.
      if (focused) ++m_polls.pendingFocused; else ++m_polls.pendingOther;
      const auto traceEnd = LiveFrameTrace::Now();
      if (traceStart && traceEnd - traceStart > LiveFrameTrace::Get().frequency / 2000)
        LiveFrameTrace::Event("pump.slow_pending", traceStart, traceEnd, 0, focused);
      return false;
    }
    ++m_polls.begun;
    if (next.period > 0) m_displayPeriodMs = double(next.period) / 1.0e6;
    m_controls.Observe(next, m_session.GetControls(), m_paused);
    const uint64_t before = m_session.Presents();
    if (!m_session.End(m_renderer, mirror, width, height)) m_failed = true;
    // End refreshes the pose after acquiring images. A later tracking loss
    // must stop native progression immediately, even when submission succeeds
    // with zero layers and the next runtime timing result is still pending.
    if (!m_session.Tracked()) m_controls.Suspend();
    if (!m_failed && !m_session.WantsExit()) m_session.Prefetch();
    m_lastFrameMs = NowMs();
    m_cadence.SetActive(m_controls.Active() && !Failed());
    if (m_session.Presents() > before)
      m_cadence.Submitted(m_lastFrameMs, next.predictedTime, next.period);
    if (m_budget) {
      const uint64_t lateBefore = m_budget->LateWorkerPresents();
      m_budget->RecordPresent(m_lastFrameMs - started, m_controls.Active(), m_lastFrameMs);
      // A balanced zero-layer/failing frame is not an active completed stereo
      // presentation. It still consumes ordinary admission exactly as before.
      if (m_session.Presents() == before || Failed() || m_session.WantsExit())
        m_budget->ResetLateWorkerHistory();
      if (m_budget->LateWorkerPresents() > lateBefore)
        LiveFrameTrace::Event("worker.late_present", traceStart, LiveFrameTrace::Now(), next.traceId,
          m_budget->NativePresents(), int64_t((m_lastFrameMs - started) * 1000.0),
          next.period, m_session.Presents() - before);
    }
    LiveFrameTrace::Event("pump.frame", traceStart, LiveFrameTrace::Now(), next.traceId,
      m_controls.Active(), Failed(), m_session.Presents(), m_budget ? int64_t(m_budget->EstimatedCostMs() * 1000.0) : 0);
    return !Failed() && !m_session.WantsExit();
  }
  static bool WorkerWait(void* context)
  {
    auto& self = *static_cast<HeadsetPresentationPump*>(context);
    const double now = NowMs();
    const double rescueAge = self.m_displayPeriodMs > 0 ?
      std::min(double(HeadsetFrameClock::NativePeriod) / 1.0e6, 1.5 * self.m_displayPeriodMs) :
      double(HeadsetFrameClock::NativePeriod) / 1.0e6;
    const bool rescueDue = self.m_controls.Active() && self.m_lastFrameMs > 0 && now - self.m_lastFrameMs >= rescueAge;
    if (self.m_budget) {
      const uint64_t traceBegin = LiveFrameTrace::OwnerNow();
      const uint64_t capBefore = traceBegin ? self.m_budget->Admissions().nativeDeniedCap : 0;
      const bool lateWorkerActive = self.m_lateWorkerEnabled && !self.m_paused &&
        self.m_controls.Active() && self.m_controls.LatestFrame().period > 0 &&
        self.m_session.Focused() && self.m_session.Tracked() &&
        !self.Failed() && !self.m_session.WantsExit();
      if (!self.m_budget->CanPresentInNative(now, rescueDue, lateWorkerActive, self.m_displayPeriodMs)) {
        if (traceBegin && self.m_budget->Admissions().nativeDeniedCap > capBefore)
          LiveFrameTrace::Event("worker.cap_denied", traceBegin, LiveFrameTrace::RawNow(), LiveFrameTrace::Get().ownerFrame,
            self.m_lastFrameMs > 0 ? int64_t((now - self.m_lastFrameMs) * 1000.0) : -1,
            self.m_budget->NativePresents(), int64_t(self.m_budget->EstimatedCostMs() * 1000.0), rescueDue);
        return false;
      }
    }
    const uint64_t before = self.m_session.Presents();
    const bool presented = self.Pump(); // No input polling, emulator calls, config edits or window swap.
    self.m_workerPresents += self.m_session.Presents() - before;
    return presented;
  }
  HeadsetControlsLatch& Controls() { return m_controls; }
  bool Failed() const { return m_failed || m_session.Failed(); }
  uint64_t WorkerPresents() const { return m_workerPresents; }
private:
  Session& m_session;
  bool& m_paused;
  IRender3D* m_renderer = nullptr;
  HeadsetControlsLatch m_controls;
  HeadsetCadenceStats m_cadence;
  PollStats m_polls;
  bool m_failed = false;
  bool m_lateWorkerEnabled = false;
  uint64_t m_workerPresents = 0;
  HeadsetPresentBudget* m_budget = nullptr;
  double m_lastFrameMs = 0;
  double m_displayPeriodMs = 0; // Scheduling input; cadence histograms stay diagnostic-only.
};
#endif
