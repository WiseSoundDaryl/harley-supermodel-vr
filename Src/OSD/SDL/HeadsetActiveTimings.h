#ifndef INCLUDED_HEADSET_ACTIVE_TIMINGS_H
#define INCLUDED_HEADSET_ACTIVE_TIMINGS_H
#include <cstdint>

// Sample on the owner at every observed activity boundary. The counters and
// elapsed time use the same intervals; untracked startup, standby and pause
// are excluded. These are active-session intervals, not game-phase filtering;
// gameplay benchmarks must load a driving fixture.
struct HeadsetActiveTimings
{
  uint64_t ticks = 0, native = 0, stereo = 0, segments = 0;
  void Sample(uint64_t now, bool active, uint64_t presents, uint64_t gameFrames)
  {
    if (m_active && now >= m_previous) {
      ticks += now - m_previous;
      stereo += presents >= m_presents ? presents - m_presents : 0;
      native += gameFrames >= m_native ? gameFrames - m_native : 0;
    }
    if (active && !m_active) ++segments;
    m_previous = now; m_presents = presents; m_native = gameFrames; m_active = active;
  }
private:
  bool m_active = false;
  uint64_t m_previous = 0, m_presents = 0, m_native = 0;
};
#endif
