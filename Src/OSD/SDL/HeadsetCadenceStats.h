#ifndef INCLUDED_HEADSET_CADENCE_STATS_H
#define INCLUDED_HEADSET_CADENCE_STATS_H
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Owner-thread diagnostics only. Fixed 0.25ms histogram bins, capped at 256ms;
// sums/maxima remain exact. No allocation, logging or scheduling in Record calls.
struct HeadsetCadenceStats
{
  struct Distribution {
    uint64_t samples = 0;
    double sum = 0, maximum = 0;
    std::array<uint64_t, 1025> bins{};
    void Record(double ms) {
      if (!std::isfinite(ms) || ms < 0) return;
      ++samples; sum += ms; maximum = std::max(maximum, ms);
      ++bins[size_t(std::min(1024.0, std::ceil(ms * 4.0)))];
    }
    double Mean() const { return samples ? sum / double(samples) : 0; }
    double Percentile(double fraction) const {
      if (!samples) return 0;
      const uint64_t rank = std::max<uint64_t>(1, uint64_t(std::ceil(std::clamp(fraction, 0.0, 1.0) * samples)));
      uint64_t total = 0;
      for (size_t i = 0; i < bins.size(); ++i) {
        total += bins[i];
        if (total >= rank) return std::min(maximum, double(i) * .25);
      }
      return 256;
    }
  };
  Distribution submissionGaps, nativeGaps, nativePhase;
  uint64_t submitted = 0, insideNative = 0, outsideNative = 0;
  uint64_t shortGaps = 0, longGaps = 0, predictedSkipped = 0, predictedNonIncreasing = 0;
  double periodMs = 0;

  void SetActive(bool active) {
    m_active = active;
    if (!active) m_haveSubmission = m_haveNative = false;
  }
  void BeginNative(double nowMs) {
    m_inNative = true; m_nativeStart = nowMs;
    if (!m_active || !std::isfinite(nowMs)) return;
    if (m_haveNative) nativeGaps.Record(nowMs - m_previousNative);
    m_previousNative = nowMs; m_haveNative = true;
  }
  void EndNative() { m_inNative = false; }
  void Submitted(double nowMs, int64_t predictedTime, int64_t period) {
    if (!m_active || !std::isfinite(nowMs)) return;
    ++submitted;
    if (m_inNative) { ++insideNative; nativePhase.Record(nowMs - m_nativeStart); }
    else ++outsideNative;
    if (period > 0) periodMs = double(period) / 1.0e6;
    if (m_haveSubmission && nowMs >= m_previousSubmission) {
      const double gap = nowMs - m_previousSubmission;
      submissionGaps.Record(gap);
      if (period > 0) {
        shortGaps += gap < .5 * periodMs;
        longGaps += gap > 1.5 * periodMs;
        if (predictedTime > 0 && m_previousPredicted > 0) {
          if (predictedTime <= m_previousPredicted) ++predictedNonIncreasing;
          else {
            const double periods = double(predictedTime - m_previousPredicted) / double(period);
            if (periods > 1.5) predictedSkipped += uint64_t(std::floor(periods + .5)) - 1;
          }
        }
      }
    }
    m_previousSubmission = nowMs; m_previousPredicted = predictedTime; m_haveSubmission = true;
  }
private:
  bool m_active = false, m_inNative = false, m_haveSubmission = false, m_haveNative = false;
  double m_previousSubmission = 0, m_previousNative = 0, m_nativeStart = 0;
  int64_t m_previousPredicted = 0;
};
#endif
