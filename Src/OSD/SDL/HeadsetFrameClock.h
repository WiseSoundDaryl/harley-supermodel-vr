#ifndef INCLUDED_HEADSET_FRAME_CLOCK_H
#define INCLUDED_HEADSET_FRAME_CLOCK_H
#include <cstdint>

// The runtime owns display cadence; the arcade simulation retains its own
// cadence. The caller supplies monotonic nanoseconds (wall time in the live
// independent loop). No catch-up burst follows a removed headset or a late frame.
class HeadsetFrameClock
{
public:
  static constexpr int64_t NativePeriod = 17384000; // round(1e9 / 57.524160)
  int64_t Remaining(int64_t now) const
  {
    return !m_started || now < m_previous || now >= m_next ? 0 : m_next - now;
  }
  bool Tick(int64_t now, bool active)
  {
    if (!active || now <= 0) { Reset(); return false; }
    if (!m_started || now < m_previous) {
      m_started = true;
      m_next = now;
    }
    m_previous = now;
    if (now < m_next) return false;
    m_next += NativePeriod;
    if (m_next <= now) m_next = now + NativePeriod;
    return true;
  }
  void Reset() { m_started = false; m_next = m_previous = 0; }
private:
  bool m_started = false;
  int64_t m_next = 0, m_previous = 0;
};
#endif
