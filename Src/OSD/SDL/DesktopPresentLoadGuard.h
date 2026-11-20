#ifndef INCLUDED_DESKTOP_PRESENT_LOAD_GUARD_H
#define INCLUDED_DESKTOP_PRESENT_LOAD_GUARD_H

#include <algorithm>
#include <cstdint>

// Optional desktop redraws must not keep the emulated game below its native
// cadence when the window driver blocks swaps. This does not govern OpenXR.
// Owner-thread only. Paused redraws are allowed, but their swaps should not be
// observed: there is no corresponding running native frame to assess.
class DesktopPresentLoadGuard
{
public:
  void ObserveSwap(uint64_t ticks, uint64_t frequency, bool frozen)
  {
    if (!frequency) return;
    const bool slow = Milliseconds(ticks, frequency) > SlowSwapMs;
    m_slowSwaps = uint8_t((unsigned(m_slowSwaps) << 1) | unsigned(slow));
    m_swapSamples = std::min(8u, m_swapSamples + 1);
    if (m_state == State::Probe && frozen)
    {
      m_probeSwapObserved = true;
      if (slow) EnterCooldown();
    }
  }

  void ObserveNativeFrame(uint64_t ticks, uint64_t frequency)
  {
    if (!frequency) return;
    // A long debugger/OS interruption must not poison the moving estimate for
    // minutes after ordinary work resumes. Even the cap is decisively slow.
    const double work = std::min(250.0, Milliseconds(ticks, frequency));
    m_nativeMs = m_haveNative ? m_nativeMs + (work - m_nativeMs) * 0.125 : work;
    m_haveNative = true;
    if (m_state == State::Normal)
    {
      if (SlowSwapCount() >= 4 && m_nativeMs > SlowNativeMs)
        EnterCooldown();
      return;
    }
    if (m_state == State::Probe)
    {
      // A fast swap alone is insufficient: its extra drawing can still delay
      // the native tick. Wait for that tick to finish before accepting recovery.
      if (work > HealthyNativeMs || m_nativeMs > HealthyNativeMs || SlowSwapCount() != 0)
        EnterCooldown();
      else if (m_probeSwapObserved)
      {
        m_state = State::Normal;
        m_probeSwapObserved = false;
      }
      return;
    }
    if (m_cooldownFrames)
    {
      --m_cooldownFrames;
      m_healthyFrames = 0;
      return;
    }
    const bool healthy = m_swapSamples == 8 && SlowSwapCount() == 0 &&
      work <= HealthyNativeMs && m_nativeMs <= HealthyNativeMs;
    m_healthyFrames = healthy ? m_healthyFrames + 1 : 0;
    if (m_healthyFrames >= RecoveryFrames)
    {
      m_state = State::Probe;
      m_probeSwapObserved = false;
    }
  }

  // Check before starting optional work. Probe allowance is consumed by an
  // actual frozen swap, not by repeated polls or a rejected prepared scene.
  bool AllowFrozen(bool paused) const
  {
    return paused || m_state == State::Normal ||
      (m_state == State::Probe && !m_probeSwapObserved);
  }
  bool Guarded() const { return m_state != State::Normal; }
  uint64_t Activations() const { return m_activations; }

private:
  enum class State { Normal, Cooldown, Probe };
  static constexpr double SlowSwapMs = 1.0;
  static constexpr double NativePeriodMs = 1000.0 / 57.524160;
  static constexpr double SlowNativeMs = NativePeriodMs * 1.10;
  static constexpr double HealthyNativeMs = NativePeriodMs * 1.06;
  static constexpr unsigned CooldownFrames = 60;
  static constexpr unsigned RecoveryFrames = 30;

  static double Milliseconds(uint64_t ticks, uint64_t frequency)
  {
    return double(ticks) * 1000.0 / double(frequency);
  }
  unsigned SlowSwapCount() const
  {
    unsigned count = 0;
    for (unsigned bits = m_slowSwaps; bits; bits >>= 1) count += bits & 1u;
    return count;
  }
  void EnterCooldown()
  {
    m_state = State::Cooldown;
    m_cooldownFrames = CooldownFrames;
    m_healthyFrames = 0;
    m_probeSwapObserved = false;
    ++m_activations;
  }

  State m_state = State::Normal;
  uint8_t m_slowSwaps = 0;
  unsigned m_swapSamples = 0, m_cooldownFrames = 0, m_healthyFrames = 0;
  uint64_t m_activations = 0;
  double m_nativeMs = 0;
  bool m_haveNative = false, m_probeSwapObserved = false;
};

#endif
