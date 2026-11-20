#ifndef INCLUDED_HEADSET_WORKER_TEMPORAL_H
#define INCLUDED_HEADSET_WORKER_TEMPORAL_H

// Pure diagnostic mathematics, shared with the standalone offline tests.
// Nothing in this header participates in production headset scheduling.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace HeadsetWorkerTemporal {
constexpr double Pi = 3.14159265358979323846;
constexpr int64_t SyntheticPeriodNs = 8333333;
constexpr size_t MaxNativeSamples = 4096;
constexpr size_t MaxStereoSamples = 16384;
constexpr double NativeHz = 57.524;

struct Pose {
  // Yaw, pitch, roll in degrees; head center X, Y, Z in metres.
  std::array<double, 6> values{};
};
constexpr std::array<double, 6> Amplitudes{{25.0, 12.0, 4.0, .10, .04, .08}};
constexpr std::array<double, 6> PeriodsSeconds{{6.0, 5.0, 7.0, 4.0, 9.0, 5.5}};
inline Pose PoseAt(double seconds)
{
  Pose result;
  if (!std::isfinite(seconds)) return result;
  for (size_t i = 0; i < result.values.size(); ++i)
    result.values[i] = Amplitudes[i] * std::sin(2 * Pi * std::remainder(seconds, PeriodsSeconds[i]) / PeriodsSeconds[i]);
  return result;
}

struct Distribution {
  size_t count = 0, invalid = 0;
  double minimum = 0, mean = 0, p50 = 0, p95 = 0, p99 = 0, maximum = 0;
};
// Nearest-rank quantiles, calculated only when the diagnostic has stopped.
inline Distribution Summarize(const std::vector<double>& values, bool allowZero = true)
{
  Distribution result;
  std::vector<double> ordered;
  ordered.reserve(values.size());
  for (double value : values) {
    if (!std::isfinite(value) || value < 0 || (!allowZero && value == 0)) { ++result.invalid; continue; }
    ordered.push_back(value);
    result.mean += value;
  }
  result.count = ordered.size();
  if (ordered.empty()) return result;
  std::sort(ordered.begin(), ordered.end());
  const auto percentile = [&](double q) { return ordered[size_t(std::ceil(q * ordered.size())) - 1]; };
  result.minimum = ordered.front(); result.maximum = ordered.back();
  result.mean /= ordered.size();
  result.p50 = percentile(.50); result.p95 = percentile(.95); result.p99 = percentile(.99);
  return result;
}
inline Distribution IntervalsMs(const std::vector<int64_t>& stamps)
{
  std::vector<double> intervals;
  intervals.reserve(stamps.empty() ? 0 : stamps.size() - 1);
  for (size_t i = 1; i < stamps.size(); ++i)
    intervals.push_back(double(stamps[i] - stamps[i-1]) / 1e6);
  return Summarize(intervals, false);
}
inline double RateHz(const std::vector<int64_t>& stamps)
{
  if (stamps.size() < 2 || stamps.back() <= stamps.front()) return 0;
  // Count intervals, not samples: two samples eight milliseconds apart are
  // 125 Hz, not 250 Hz. Never describe this as physical headset FPS.
  return double(stamps.size()-1) * 1e9 / double(stamps.back() - stamps.front());
}
struct Readiness {
  int64_t nextNs = 0;
  uint64_t skippedSlots = 0;
  bool TryBegin(int64_t nowNs, int64_t& predictedNs)
  {
    if (nowNs < 0 || nowNs < nextNs) return false;
    skippedSlots += uint64_t((nowNs - nextNs) / SyntheticPeriodNs);
    nextNs = (nowNs / SyntheticPeriodNs + 1) * SyntheticPeriodNs;
    predictedNs = nextNs;
    return true;
  }
};
template<class T> inline bool AppendBounded(std::vector<T>& samples, const T& value, size_t limit)
{
  if (samples.size() >= limit) return false;
  samples.push_back(value);
  return true;
}
}
#endif
