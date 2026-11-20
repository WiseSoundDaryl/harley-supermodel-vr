#ifndef INCLUDED_HEADSET_PRESENT_BUDGET_H
#define INCLUDED_HEADSET_PRESENT_BUDGET_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "HeadsetFrameClock.h"

// Owner-thread headset admission. Outside a native update, the native deadline
// retains priority. Inside the worker wait, CPU work proceeds in parallel with
// the immutable redraw: a late game deadline must not turn that overlap into
// several whole game updates without a headset submission.
class HeadsetPresentBudget
{
public:
  // Owner-thread probe counters only. Allowed probes can still find runtime
  // timing pending; denied probes are attempts, not unique missed displays.
  // Inactive/freshness outer-loop overrides are deliberately not counted as
  // admissions here, and the counters never participate in scheduling.
  struct AdmissionStats {
    uint64_t nativeAllowed = 0, nativeDeniedCap = 0;
    uint64_t nativeDeniedInvalid = 0, nativeDeniedRemaining = 0;
    uint64_t outsideAllowed = 0, outsideDeniedDeadline = 0;
    uint64_t outsideDeniedRemaining = 0, outsideDeniedInvalid = 0;
    uint64_t nativePredictedAllowed = 0, nativeRescueAllowed = 0;
    uint64_t nativeBorrowedAllowed = 0;
    uint64_t nativeLateWorkerAllowed = 0;
  };

  // Existing PPC timings are whole milliseconds, independent of owner-thread
  // XR work. A recent sample predicts useful overlap, never the game clock.
  void RecordNativeWork(double nowMs, double ppcMs)
  {
    if (!std::isfinite(nowMs) || !std::isfinite(ppcMs) || ppcMs <= 0) {
      ResetWorkerPrediction(); return;
    }
    m_workerDuration = std::clamp(ppcMs - 1.0, 0.0, RecoveryWindowMs);
    m_workerSampleTime = nowMs;
    m_haveWorkerSample = true;
  }
  void ResetWorkerPrediction() { m_haveWorkerSample = false; }

  void BeginNative(double nowMs, double availableWindowMs)
  {
    m_native = true;
    m_nativePresents = 0;
    m_rescueUsed = m_pendingRescue = m_pendingBorrow = false;
    m_pendingLateWorker = false;
    // Zero means an already-due native deadline, not an invalid worker wait.
    // Normal admission has no slack, but prediction/rescue may still use CPU
    // work running in parallel. Negative or nonfinite arguments stay invalid.
    m_validNative = std::isfinite(nowMs) && std::isfinite(availableWindowMs) && availableWindowMs >= 0;
    m_nativeStart = nowMs;
    m_nativeEnd = m_validNative ? nowMs + std::min(availableWindowMs, 50.0) : nowMs;
    m_workerEnd = m_nativeEnd;
    if (m_validNative && m_haveWorkerSample && nowMs >= m_workerSampleTime &&
        nowMs - m_workerSampleTime <= RecoveryWindowMs)
      m_workerEnd = std::max(m_workerEnd, nowMs + m_workerDuration);
    m_lastProbe = nowMs;
  }

  bool CanPresentInNative(double nowMs, bool rescueDue = false,
    bool lateWorkerActive = false, double displayPeriodMs = 0) const
  {
    m_lastProbe = nowMs;
    m_pendingRescue = m_pendingBorrow = false;
    m_pendingLateWorker = false;
    const bool eligible = m_native && m_validNative && m_nativePresents < 2 && std::isfinite(nowMs);
    const double cost = eligible ? CostAt(nowMs) : 0;
    const bool normal = eligible && m_nativeEnd - nowMs >= cost;
    const bool predicted = eligible && !normal && m_workerEnd - nowMs >= cost;
    // Two seven-millisecond presentations plus preparation can narrowly cross
    // one native deadline. Requiring both to fit exactly quantizes headset work
    // to one presentation per game update even when average throughput permits
    // more. Only the second call may borrow two milliseconds against this
    // absolute deadline, retaining the conservative whole-call estimate. The
    // unchanged native clock carries that lateness into the next window, which
    // denies another second call once the allowance is spent. This is an
    // admission bound, not a guarantee against an unexpected runtime stall.
    const bool borrowed = eligible && !normal && !predicted &&
      m_nativePresents == 1 && PenaltyAt(nowMs) <= 0 &&
      m_nativeEnd + SecondPresentBorrowMs - nowMs >= cost;
    const bool rescue = eligible && !normal && !predicted && !borrowed && rescueDue && !m_rescueUsed;
    // Opt-in recovery only after the ordinary two-frame allowance is spent.
    // A late native worker can otherwise strand an already-decoded scene for
    // many display periods. Redraw that immutable scene without changing the
    // native clock or admitting work in the next owner preparation interval.
    // Cooldown starts at actual call completion, never the admission probe;
    // pending TryBegin polls neither spend a frame nor restart this cooldown.
    // Keep the last actual full-call cost as a floor even if a spike penalty
    // has decayed. A worker finishing during a call still waits for that call.
    const double nativeAge = nowMs - m_nativeStart;
    const double completedAge = nowMs - m_lastCompletedPresent;
    const bool lateWorker = lateWorkerActive && m_native && m_validNative && m_nativePresents >= 2 &&
      std::isfinite(nowMs) && std::isfinite(nativeAge) && nativeAge >= RecoveryWindowMs &&
      std::isfinite(displayPeriodMs) && displayPeriodMs > 0 && m_haveCompletedPresent &&
      std::isfinite(completedAge) && completedAge >= 0 &&
      completedAge >= std::max(displayPeriodMs, 2.0 * std::max(CostAt(nowMs), m_lastCompletedCost));
    const bool allowed = normal || predicted || borrowed || rescue || lateWorker;
    if (allowed) {
      ++m_admissions.nativeAllowed;
      m_admissions.nativePredictedAllowed += predicted;
      m_admissions.nativeRescueAllowed += rescue;
      m_admissions.nativeBorrowedAllowed += borrowed;
      m_admissions.nativeLateWorkerAllowed += lateWorker;
      m_pendingRescue = rescue;
      m_pendingBorrow = borrowed;
      m_pendingLateWorker = lateWorker;
    }
    else if (!m_native || !m_validNative || !std::isfinite(nowMs)) ++m_admissions.nativeDeniedInvalid;
    else if (m_nativePresents >= 2) ++m_admissions.nativeDeniedCap;
    else ++m_admissions.nativeDeniedRemaining;
    return allowed;
  }

  void EndNative() { m_native = m_validNative = m_pendingRescue = m_pendingBorrow = m_pendingLateWorker = false; }
  void ResetLateWorkerHistory() { m_haveCompletedPresent = false; }

  bool CanPresentBeforeNative(double nowMs, double remainingMs) const
  {
    m_lastProbe = nowMs;
    const bool allowed = !m_native && std::isfinite(nowMs) && std::isfinite(remainingMs) &&
      remainingMs > 0 && remainingMs >= CostAt(nowMs);
    if (allowed) ++m_admissions.outsideAllowed;
    else if (m_native || !std::isfinite(nowMs) || !std::isfinite(remainingMs)) ++m_admissions.outsideDeniedInvalid;
    else if (remainingMs <= 0) ++m_admissions.outsideDeniedDeadline;
    else ++m_admissions.outsideDeniedRemaining;
    return allowed;
  }

  // Call only if an XR frame actually began. A TryBegin that reports "not
  // ready" did not perform a frame and must not consume admission or teach a
  // near-zero cost. Include Begin, eye work, image waits and End in elapsedMs.
  // Paused/unfocused presentation is independent and need not train this model.
  void RecordPresent(double elapsedMs, bool train = true, double completedAtMs = -1)
  {
    if (!std::isfinite(elapsedMs) || elapsedMs < 0 || !std::isfinite(m_lastProbe)) {
      m_haveCompletedPresent = false;
      ResetSustainedCostEvidence();
      return;
    }
    // The pump supplies its real completion clock. The default preserves
    // existing callers, whose modeled completion is probe + whole-call cost.
    // Invalid/inactive observations cannot arm the separate late-worker lane;
    // ordinary cost, cap and overrun accounting below remains unchanged.
    const double completed = completedAtMs == -1 ? m_lastProbe + elapsedMs : completedAtMs;
    m_haveCompletedPresent = train && std::isfinite(completed) && completed >= m_lastProbe &&
      (completedAtMs == -1 || completed - m_lastProbe >= elapsedMs);
    if (m_haveCompletedPresent) {
      m_lastCompletedPresent = completed;
      m_lastCompletedCost = elapsedMs;
    }
    if (m_native) {
      ++m_nativePresents;
      if (m_lastProbe + elapsedMs > m_nativeEnd) ++m_overruns;
      if (m_lastProbe + elapsedMs > m_workerEnd) ++m_workerOverruns;
      if (m_pendingRescue) { m_rescueUsed = true; ++m_rescuedPresents; }
      if (m_pendingBorrow) ++m_borrowedPresents;
      if (m_pendingLateWorker) ++m_lateWorkerPresents;
      m_pendingRescue = m_pendingBorrow = false;
      m_pendingLateWorker = false;
    }
    if (!train) { // Still count admitted frames; ignore inactive rendering costs.
      ResetSustainedCostEvidence();
      return;
    }
    // Only ordered, actually completed active calls may establish a new cost
    // level. Invalid/backward observations cannot connect two partial clusters.
    const bool orderedCost = m_haveCompletedPresent &&
      (!m_haveCostObservation || completed > m_costObservationTime);
    if (orderedCost) {
      m_haveCostObservation = true;
      m_costObservationTime = completed;
    } else ResetSustainedCostEvidence();
    ++m_samples;
    const double baseCost = BaseCostAt(m_lastProbe);
    const double normalLimit = std::max(baseCost * 1.75, baseCost + 4.0);
    const double decayedPenalty = PenaltyAt(m_lastProbe + elapsedMs);
    if (elapsedMs > normalLimit) {
      // A rare long submission must reduce optional work, but cannot poison
      // the stable estimate forever: no new samples arrive while it is denied.
      m_penalty = std::max(decayedPenalty, std::min(50.0, elapsedMs) - baseCost);
      m_penaltyTime = m_lastProbe + elapsedMs;
      m_penaltyExpires = m_penaltyTime + RecoveryWindowMs;
      ++m_spikes;
      // An isolated exceptional call keeps the exact existing backoff. Four
      // comparable moderate observations can instead establish sustained work:
      // a cheap theater must not classify every driving call as a rare stall.
      if (!orderedCost || !ObserveSustainedCost(elapsedMs, completed)) return;
      const double protectedCost = CostAt(m_penaltyTime);
      const double sustained = m_sustainedMaximum;
      m_mean = std::max(m_mean, sustained);
      m_costs[m_costCursor] = sustained;
      m_costTimes[m_costCursor] = m_penaltyTime;
      m_costCursor = (m_costCursor + 1) % m_costs.size();
      m_costCount = std::min(m_costCount + 1, m_costs.size());
      // Transfer only the part now covered by the ordinary estimate. Preserve
      // any larger rare-stall penalty and its original decay clock/expiry.
      m_penalty = std::max(0.0, protectedCost - BaseCostAt(m_penaltyTime));
      ResetSustainedCostEvidence();
      return;
    }
    ResetSustainedCostEvidence();
    m_costs[m_costCursor] = elapsedMs;
    m_costTimes[m_costCursor] = m_lastProbe + elapsedMs;
    m_costCursor = (m_costCursor + 1) % m_costs.size();
    m_costCount = std::min(m_costCount + 1, m_costs.size());
    m_mean = .8 * m_mean + .2 * elapsedMs;
    m_penalty = decayedPenalty * .5;
    m_penaltyTime = m_lastProbe + elapsedMs;
  }

  double EstimatedCostMs() const { return CostAt(m_lastProbe); }
  uint64_t Samples() const { return m_samples; }
  uint64_t Spikes() const { return m_spikes; }
  uint64_t NativeOverruns() const { return m_overruns; }
  unsigned NativePresents() const { return m_nativePresents; }
  uint64_t RescuedPresents() const { return m_rescuedPresents; }
  uint64_t BorrowedPresents() const { return m_borrowedPresents; }
  uint64_t LateWorkerPresents() const { return m_lateWorkerPresents; }
  uint64_t WorkerOverruns() const { return m_workerOverruns; }
  const AdmissionStats& Admissions() const { return m_admissions; }

private:
  // A maximum over sixteen samples cannot recover while its own estimate
  // denies the next sample. In a regression reproducing the admission
  // failure, an accepted 11ms call excluded both available owner windows;
  // only the 50ms fallback could
  // evict it, taking about 800ms. Retain the conservative recent high-water
  // estimate for two native periods, then fall back to the full-call EWMA.
  // Elapsed time, not successful submissions, bounds exceptional backoff too.
  // Sustained expensive calls remain represented by the EWMA; nothing here
  // changes the native clock, ordinary two-frame cap, or whole-call overrun accounting.
  static constexpr double RecoveryWindowMs = 2.0 * HeadsetFrameClock::NativePeriod / 1.0e6;
  static constexpr double SecondPresentBorrowMs = 2.0;

  void ResetSustainedCostEvidence() { m_sustainedCount = 0; }

  bool ObserveSustainedCost(double elapsedMs, double completedMs)
  {
    // Deliberately do not promote repeated long runtime stalls. This path is
    // bounded to sub-native-period work; the ordinary fallback/backoff remains
    // responsible for more expensive calls.
    if (elapsedMs >= HeadsetFrameClock::NativePeriod / 1.0e6) {
      ResetSustainedCostEvidence(); return false;
    }
    if (m_sustainedCount &&
        (completedMs - m_sustainedFirst > 2.0 * RecoveryWindowMs ||
         std::max(m_sustainedMaximum, elapsedMs) >
           1.25 * std::min(m_sustainedMinimum, elapsedMs) + .25))
      ResetSustainedCostEvidence();
    if (!m_sustainedCount) {
      m_sustainedFirst = completedMs;
      m_sustainedMinimum = m_sustainedMaximum = elapsedMs;
    } else {
      m_sustainedMinimum = std::min(m_sustainedMinimum, elapsedMs);
      m_sustainedMaximum = std::max(m_sustainedMaximum, elapsedMs);
    }
    return ++m_sustainedCount == 4;
  }

  double BaseCostAt(double nowMs) const
  {
    double recent = 0;
    for (size_t i = 0; i < m_costCount; ++i) {
      if (!std::isfinite(nowMs) || nowMs - m_costTimes[i] <= RecoveryWindowMs)
        recent = std::max(recent, m_costs[i]);
    }
    return std::clamp(std::max(m_mean, recent) + .10, .25, 32.0);
  }

  double PenaltyAt(double nowMs) const
  {
    if (!std::isfinite(nowMs)) return m_penalty;
    if (nowMs >= m_penaltyExpires) return 0;
    const double age = std::max(0.0, nowMs - m_penaltyTime);
    return m_penalty * std::exp2(-age / 250.0);
  }
  double CostAt(double nowMs) const { return BaseCostAt(nowMs) + PenaltyAt(nowMs); }

  bool m_native = false, m_validNative = false;
  bool m_haveWorkerSample = false, m_rescueUsed = false;
  mutable bool m_pendingRescue = false, m_pendingBorrow = false;
  mutable bool m_pendingLateWorker = false;
  bool m_haveCompletedPresent = false;
  unsigned m_nativePresents = 0;
  double m_nativeEnd = 0, m_workerEnd = 0, m_workerDuration = 0, m_workerSampleTime = 0;
  double m_nativeStart = 0, m_lastCompletedPresent = 0, m_lastCompletedCost = 0;
  mutable double m_lastProbe = 0;
  double m_mean = 7.4, m_penalty = 0, m_penaltyTime = 0, m_penaltyExpires = 0;
  bool m_haveCostObservation = false;
  double m_costObservationTime = 0;
  unsigned m_sustainedCount = 0;
  double m_sustainedFirst = 0, m_sustainedMinimum = 0, m_sustainedMaximum = 0;
  std::array<double, 16> m_costs{};
  std::array<double, 16> m_costTimes{};
  size_t m_costCursor = 0, m_costCount = 0;
  uint64_t m_samples = 0, m_spikes = 0, m_overruns = 0;
  uint64_t m_rescuedPresents = 0, m_workerOverruns = 0;
  uint64_t m_borrowedPresents = 0;
  uint64_t m_lateWorkerPresents = 0;
  mutable AdmissionStats m_admissions;
};

#endif
