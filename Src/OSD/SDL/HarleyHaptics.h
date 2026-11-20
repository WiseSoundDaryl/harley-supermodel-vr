#ifndef INCLUDED_HARLEY_HAPTICS_H
#define INCLUDED_HARLEY_HAPTICS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "Graphics/HarleyPresentation.h"

// Presentation-only feedback. Consume immutable completed-scene rider anchors,
// never live game RAM or a headset/eye transform. Only impacts and hard landings
// produce pulses; engine/idle riding is silent. Prefer authenticated completed
// collision-response timers; unsupported metadata retains the motion heuristic.
// All timing uses owner steady time.
class HarleyHaptics
{
public:
  struct Sample {
    uint64_t frame = 0, epoch = 0;
    bool driving = false, anchorValid = false;
    std::array<double, 3> body{};
    HarleyPresentation::MotionState motion;
  };
  // Keep trace IDs stable. Engine is a reserved legacy value, never emitted.
  enum class Kind { None = 0, Engine = 1, Impact = 2, Stop = 3 };
  struct Output {
    Kind kind = Kind::None;
    float amplitude = 0, seconds = 0, frequency = 0;
  };

  Output Update(const Sample& sample, float throttle, float brake,
                bool allowed, double nowMs)
  {
    if (!std::isfinite(nowMs) || nowMs < 0 ||
        (m_clockSeen && nowMs < m_lastUpdate) || !allowed || !sample.driving ||
        !std::isfinite(throttle) || !std::isfinite(brake))
      return Suspend();
    m_clockSeen = true;
    m_lastUpdate = nowMs;
    brake = std::clamp(brake, 0.0f, 1.0f);

    const bool fresh = !m_seen || sample.frame != m_sample.frame || sample.epoch != m_sample.epoch;
    bool impact = false, discontinuity = false;
    if (fresh) {
      const bool consecutive = m_seen && sample.epoch == m_sample.epoch &&
        sample.frame > m_sample.frame && sample.frame - m_sample.frame == 1;
      const bool valid = ValidAnchor(sample);
      if (consecutive && valid && ValidAnchor(m_sample)) {
        const double dx = sample.body[0] - m_sample.body[0];
        const double dy = sample.body[1] - m_sample.body[1];
        const double dz = sample.body[2] - m_sample.body[2];
        const double speed = std::hypot(dx, dz);
        // Use the same gross-discontinuity bounds as RiderComfort. Respawns,
        // state loads and invalid anchor reacquisition must not kick the hands.
        if (speed <= 24.0 && std::abs(dy) <= 12.0) {
          const bool contactKnown = ValidMotion(sample.motion);
          // Trigger only a new, observed collision-response interval. Initial
          // acquisition, focus return, missing metadata and continuous/renewed
          // timers cannot replay a collision already in progress. Braking does
          // not veto an authenticated impact. Both wheels share one interval.
          const bool contactImpact = contactKnown && m_contactKnown &&
            nowMs - m_lastScene <= 250.0 &&
            ImpactActive(sample.motion) && !m_contactActive;
          const bool abruptStop = !contactKnown && m_steps >= 3 && m_speed >= 1.5 &&
            m_speed - speed >= 1.0 && speed < m_speed * 0.45 && brake < 0.25f;
          const bool hardLanding = m_steps >= 3 && m_verticalStep < -0.75 &&
            dy >= -0.10 && dy - m_verticalStep > 0.9;
          // A verified new response needs only a short non-overlap guard;
          // distinct crashes can occur inside the heuristic's 650 ms window.
          impact = (contactImpact && nowMs >= m_nextContactImpact) ||
            ((abruptStop || hardLanding) && nowMs >= m_nextImpact);
          m_speed = speed;
          m_verticalStep = dy;
          m_steps = std::min(3u, m_steps + 1);
        }
        else { ResetMotion(); discontinuity = true; }
      }
      else { ResetMotion(); discontinuity = true; }
      m_sample = sample;
      m_contactKnown = valid && ValidMotion(sample.motion);
      m_contactActive = m_contactKnown && ImpactActive(sample.motion);
      m_seen = true;
      m_lastScene = nowMs;
    }
    // A cut, lost anchor or discontinuous native sequence cancels a pulse
    // already in flight (or waiting for the next presentation).
    if (discontinuity && m_active) {
      m_active = false;
      return {Kind::Stop, 0, 0, 0};
    }
    // An old frozen scene cannot sustain vibration indefinitely during a stall.
    // This does not alter native clocks or schedule any extra rendering work.
    if (nowMs - m_lastScene > 250.0) {
      // Retain the scene identity while stale and discard motion history;
      // repeated draws must not reacquire a frozen scene as fresh movement.
      const bool stop = m_active;
      m_active = false;
      ResetMotion();
      return {stop ? Kind::Stop : Kind::None, 0, 0, 0};
    }
    if (impact) {
      m_active = true;
      m_nextImpact = nowMs + 650.0;
      m_nextContactImpact = nowMs + 200.0;
      return {Kind::Impact, 0.75f, 0.16f, 65.0f};
    }
    return {};
  }

  // Presentation may be slower OR faster than native emulation. Poll only the
  // retained native sample: rendering must never advance detection history.
  Output Poll(bool allowed, double nowMs)
  {
    return Update(m_sample, 0, 0, allowed, nowMs);
  }

  Output Suspend()
  {
    const bool stop = m_active;
    *this = HarleyHaptics{};
    return {stop ? Kind::Stop : Kind::None, 0, 0, 0};
  }

private:
  static bool ValidMotion(const HarleyPresentation::MotionState& motion)
  {
    return motion.valid && motion.frontImpactFrames <= 30 && motion.rearImpactFrames <= 30;
  }
  static bool ImpactActive(const HarleyPresentation::MotionState& motion)
  {
    return motion.frontImpactFrames != 0 || motion.rearImpactFrames != 0;
  }
  static bool ValidAnchor(const Sample& sample)
  {
    return sample.anchorValid && std::isfinite(sample.body[0]) &&
      std::isfinite(sample.body[1]) && std::isfinite(sample.body[2]);
  }
  void ResetMotion() {
    m_steps = 0; m_speed = m_verticalStep = 0;
    m_contactKnown = m_contactActive = false;
  }
  Sample m_sample{};
  bool m_seen = false, m_active = false, m_clockSeen = false;
  bool m_contactKnown = false, m_contactActive = false;
  unsigned m_steps = 0;
  double m_speed = 0, m_verticalStep = 0, m_lastUpdate = 0, m_lastScene = 0;
  double m_nextImpact = 0, m_nextContactImpact = 0;
};

#endif
