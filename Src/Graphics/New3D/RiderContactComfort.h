#ifndef INCLUDED_RIDER_CONTACT_COMFORT_H
#define INCLUDED_RIDER_CONTACT_COMFORT_H

#include "../HarleyPresentation.h"
#include <algorithm>
#include <cstdint>

namespace New3D {

// Consumes verified game-physics metadata for completed native snapshots.
// No geometry, head pose, throttle or road-height estimate enters this policy.
class RiderContactComfort
{
public:
  static constexpr double SceneHz = 57.524160;
  static constexpr double ReleaseSeconds = 0.12;
  struct Result {
    bool valid = false, airborne = false, impactActive = false;
    double jumpStrength = 0.0, crashStrength = 0.0, cutStrength = 0.0;
    double strength = 0.0;
    float crashRelief = 0.0f;
  };

  const Result& Update(uint64_t frame, uint64_t epoch,
                       const HarleyPresentation::MotionState& motion,
                       bool discontinuity = false)
  {
    if (m_seen && frame == m_frame && epoch == m_epoch) return m_result;
    const bool consecutive = m_seen && epoch == m_epoch && frame > m_frame && frame - m_frame == 1;
    const bool previousKnown = consecutive && m_result.valid;
    const bool previouslyAirborne = m_result.airborne;
    m_seen = true;
    m_frame = frame;
    m_epoch = epoch;
    if (!consecutive || !motion.valid) m_result = {};
    if (!motion.valid) return m_result; // Unknown is not a grounded observation.

    m_result.valid = true;
    m_result.impactActive = motion.frontImpactFrames != 0 || motion.rearImpactFrames != 0;
    // The game's impact response can remove both support flags itself. A
    // grounded impact therefore gets the crash profile, not a newly invented
    // jump. Never soften an established flight; after a reset an unsupported
    // impact has unknown origin and conservatively receives jump protection.
    m_result.airborne = !motion.frontSupported && !motion.rearSupported &&
      (!m_result.impactActive || previouslyAirborne || !previousKnown);
    m_result.jumpStrength = Envelope(m_result.jumpStrength, m_result.airborne);
    m_result.crashStrength = Envelope(m_result.crashStrength, m_result.impactActive);
    m_result.cutStrength = Envelope(m_result.cutStrength, discontinuity);
    m_result.strength = std::max({m_result.jumpStrength, m_result.crashStrength, m_result.cutStrength});
    // Strong jump/cut protection retains its aperture throughout its release,
    // including a simultaneous crash or a landing that also hits a wall.
    m_result.crashRelief = m_result.crashStrength > 0.0 &&
      m_result.jumpStrength == 0.0 && m_result.cutStrength == 0.0 ? 1.0f : 0.0f;
    return m_result;
  }

  const Result& State() const { return m_result; }

private:
  static double Envelope(double current, bool active)
  {
    return active ? 1.0 : std::max(0.0, current - 1.0 / (SceneHz * ReleaseSeconds));
  }
  bool m_seen = false;
  uint64_t m_frame = 0, m_epoch = 0;
  Result m_result;
};

} // namespace New3D
#endif
