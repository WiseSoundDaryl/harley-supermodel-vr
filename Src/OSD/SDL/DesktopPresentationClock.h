#ifndef INCLUDED_DESKTOP_PRESENTATION_CLOCK_H
#define INCLUDED_DESKTOP_PRESENTATION_CLOCK_H

#include <cmath>
#include <cstdint>
#include <limits>

// An owner-thread presentation grid, independent of the emulator clock. A late
// draw skips all expired slots in one step; missed presentations never queue.
class DesktopPresentationClock
{
public:
  static constexpr double NativeHz = 57.524160;

  bool Reset(uint64_t frequency, double hz, uint64_t now)
  {
    m_period = 0;
    m_next = now;
    m_exhausted = false;
    if (!frequency || !std::isfinite(hz) || hz <= 0) return false;
    const double period = std::ceil(double(frequency) / hz);
    if (period < 1 || period >= double(std::numeric_limits<uint64_t>::max())) return false;
    m_period = uint64_t(period);
    return true;
  }

  bool Due(uint64_t now) const { return m_period && !m_exhausted && now >= m_next; }

  bool Take(uint64_t now)
  {
    if (!Due(now)) return false;
    // Preserve the original grid through ordinary wake-up jitter, advancing
    // directly to its first strictly future slot even after a long stall.
    const uint64_t untilNext = m_period - (now - m_next) % m_period;
    m_exhausted = now > std::numeric_limits<uint64_t>::max() - untilNext;
    m_next = m_exhausted ? std::numeric_limits<uint64_t>::max() : now + untilNext;
    return true;
  }

  uint64_t NextTick() const { return m_next; }
  uint64_t PeriodTicks() const { return m_period; }

private:
  uint64_t m_period = 0, m_next = 0;
  bool m_exhausted = false;
};

#endif
