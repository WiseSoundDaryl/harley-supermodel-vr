#ifndef INCLUDED_RIDER_COMFORT_H
#define INCLUDED_RIDER_COMFORT_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "RiderContactComfort.h"

namespace New3D {

// Presentation state only: never serialized into the emulated hardware. The
// clock identifies a completed GPU snapshot, not an eye draw or wall-clock tick.
class RiderComfort
{
public:
  static constexpr double SceneHz = 57.524160;
  static constexpr double TimeConstant = 0.18;
  static constexpr double MaxLag = 1.5;
  static constexpr double LargeMotionStep = 0.4;
  static constexpr double LargeMotionTravel = 8.0;
  static constexpr unsigned MotionFrames = 6;
  static constexpr unsigned SettledFrames = 12;
  static constexpr double SettledStep = 0.25;
  static constexpr double SettledVelocitySpan = 0.025;
  static constexpr unsigned RoughFrames = 60;
  static constexpr double RoughHeightSpan = 3.0;
  static constexpr double RoughMinimumStep = 0.025;
  static constexpr unsigned RoughReversals = 2;
  static constexpr double AttackSeconds = 0.12;
  static constexpr double ReleaseSeconds = 0.4;

  // A conservative horizontal-stop attribution, not a ROM collision flag.
  // It only widens an already active headset mask; the existing vertical
  // classifier, strength envelope and camera correction remain independent.
  static constexpr unsigned CrashReliefFrames = 46;
  static constexpr unsigned CrashReliefFadeFrames = 12;
  float CrashRelief() const {
    if (m_contactComfort.State().valid) return m_contactComfort.State().crashRelief;
    return std::min(1.0f, float(m_crashFrames) / float(CrashReliefFadeFrames));
  }

  struct Result {
    double verticalOffset = 0.0;
    double vignetteStrength = 0.0;
    double verticalTravel = 0.0;
    double heightSpan = 0.0, velocitySpan = 0.0;
    unsigned fastFrames = 0, roughReversals = 0;
    bool motionQualified = false, settled = false, largeMotion = false;
    bool contactValid = false, airborne = false, impactActive = false;
    double jumpStrength = 0.0, crashStrength = 0.0, cutStrength = 0.0;
    const char* status = "initial";
  };

  const Result& Update(uint64_t frame, uint64_t epoch, bool valid,
                       const std::array<double, 3>& body,
                       const std::array<double, 2>& back,
                       const HarleyPresentation::MotionState& motion = {})
  {
    if (m_seen && frame == m_frame && epoch == m_epoch) return m_result;
    const bool epochChanged = m_seen && epoch != m_epoch;
    const bool consecutive = m_seen && !epochChanged && frame > m_frame && frame - m_frame == 1;
    const bool previouslyValid = m_valid;
    m_seen = true;
    m_frame = frame;
    m_epoch = epoch;
    m_valid = valid;
    m_result.verticalOffset = 0.0;
    m_result.largeMotion = false;
    UpdateCrashAttribution(consecutive, previouslyValid, valid, body, back);
    if (!consecutive) { ResetMotion(); m_result.vignetteStrength = m_legacyStrength = 0.0; }
    if (!valid) {
      ResetMotion();
      // Never blank a scene when a cockpit cannot be identified. The renderer
      // can still show the available camera with a modest peripheral mask.
      ApproachStrength(m_acquired ? 0.35 : 0.0);
      m_result.status = "anchor_unavailable";
      ApplyContactComfort(frame, epoch, motion, false, m_body[1]);
      return m_result;
    }

    const double horizontalStep = std::hypot(body[0] - m_body[0], body[2] - m_body[2]);
    const double dy = body[1] - m_body[1];
    const double headingDot = back[0] * m_back[0] + back[1] * m_back[1];
    // Only large discontinuities reseed the camera. Small bumps continue
    // through the height filter and cannot trigger a one-sample blackout.
    const bool positionJump = previouslyValid && consecutive && (horizontalStep > 24.0 || std::abs(dy) > 12.0);
    const bool headingJump = previouslyValid && consecutive && headingDot < 0.866025403784;
    const bool restart = !previouslyValid || !consecutive || positionJump || headingJump;
    double target = 0.0;
    if (restart) {
      ResetMotion();
      m_filteredY = body[1];
      m_heights.fill(body[1]);
      target = positionJump || headingJump ? 1.0 : 0.0;
      m_result.status = epochChanged ? "scene_epoch" : positionJump ? "position_jump" :
        headingJump ? "heading_jump" : !previouslyValid ? "acquired" : "scene_gap";
    }
    else {
      const double alpha = -std::expm1(-1.0 / (SceneHz * TimeConstant));
      m_filteredY += alpha * (body[1] - m_filteredY);
      m_filteredY = std::clamp(m_filteredY, body[1] - MaxLag, body[1] + MaxLag);
      m_result.verticalOffset = m_filteredY - body[1];

      // Detect sustained vertical travel in either direction. A normal jump
      // slows while climbing, so acceleration in the direction of travel must
      // not be required. Reset travel on small steps or a reversal: repeated
      // little road bumps cannot accumulate into a large event.
      const int direction = dy > 0.0 ? 1 : dy < 0.0 ? -1 : 0;
      if (std::abs(dy) > LargeMotionStep) {
        m_result.fastFrames = direction == m_direction ? std::min(MotionFrames, m_result.fastFrames + 1) : 1;
        m_result.verticalTravel = direction == m_direction ?
          m_result.verticalTravel + std::abs(dy) : std::abs(dy);
      }
      else {
        m_result.fastFrames = 0;
        m_result.verticalTravel = 0.0;
      }
      m_direction = direction;
      m_result.motionQualified = m_result.fastFrames >= MotionFrames &&
        m_result.verticalTravel >= LargeMotionTravel;

      // Slow, stable vertical velocity permits recovery on a gentle grade.
      // A longer velocity window keeps a slowly reversing jump apex active.
      // Small repeated road chatter can prevent exact settling indefinitely:
      // recover after a bounded height window with multiple real reversals.
      // A smooth airborne apex reverses once, so it does not satisfy that test.
      for (unsigned i = 0; i < RoughFrames; ++i) m_heights[i] = m_heights[i + 1];
      m_heights[RoughFrames] = body[1];
      for (unsigned i = 0; i + 1 < SettledFrames; ++i) m_velocities[i] = m_velocities[i + 1];
      m_velocities[SettledFrames - 1] = dy;
      m_motionSamples = std::min(RoughFrames, m_motionSamples + 1);
      const auto heights = std::minmax_element(m_heights.begin(), m_heights.end());
      const auto velocities = std::minmax_element(m_velocities.begin(), m_velocities.end());
      m_result.heightSpan = *heights.second - *heights.first;
      m_result.velocitySpan = *velocities.second - *velocities.first;
      m_result.roughReversals = 0;
      int previousDirection = 0;
      for (unsigned i = 1; i <= RoughFrames; ++i) {
        const double step = m_heights[i] - m_heights[i - 1];
        if (std::abs(step) <= RoughMinimumStep) continue;
        const int currentDirection = step > 0.0 ? 1 : -1;
        if (previousDirection && currentDirection != previousDirection) ++m_result.roughReversals;
        previousDirection = currentDirection;
      }
      const bool steady = m_motionSamples >= SettledFrames &&
        std::max(std::abs(*velocities.first), std::abs(*velocities.second)) <= SettledStep &&
        m_result.velocitySpan <= SettledVelocitySpan;
      const bool smallRoughMotion = m_motionSamples == RoughFrames &&
        m_result.heightSpan <= RoughHeightSpan && m_result.roughReversals >= RoughReversals;
      m_result.settled = steady || smallRoughMotion;
      if (m_result.motionQualified) m_motionActive = true;
      else if (m_result.settled) m_motionActive = false;
      m_result.largeMotion = m_motionActive;
      target = m_result.largeMotion ? 1.0 : 0.0;
      m_result.status = m_result.largeMotion ? "large_vertical_motion" : "tracking";
    }
    ApproachStrength(target);
    ApplyContactComfort(frame, epoch, motion, positionJump || headingJump, body[1]);
    m_acquired = true;
    m_body = body;
    m_back = back;
    return m_result;
  }

private:
  void ApplyContactComfort(uint64_t frame, uint64_t epoch,
                           const HarleyPresentation::MotionState& motion,
                           bool discontinuity, double anchorY)
  {
    const auto& contact = m_contactComfort.Update(frame, epoch, motion, discontinuity);
    m_result.contactValid = contact.valid;
    m_result.airborne = contact.airborne;
    m_result.impactActive = contact.impactActive;
    m_result.jumpStrength = contact.jumpStrength;
    m_result.crashStrength = contact.crashStrength;
    m_result.cutStrength = contact.cutStrength;
    if (!contact.valid) return;
    // Do not keep a contradictory height-only mask hidden under trusted road
    // contact. Previously, a supported uphill ride could build legacy strength
    // to one; a single unknown metadata sample then exposed that stale mask.
    // Start fallback from the actual visible envelope, and reseed only the
    // height-event history on clear support. The camera Y filter is untouched.
    m_legacyStrength = contact.strength;
    if (!contact.airborne && !contact.impactActive && contact.strength == 0.0) {
      ResetMotion();
      m_heights.fill(anchorY);
    }
    m_result.vignetteStrength = contact.strength;
    m_result.largeMotion = contact.airborne;
    m_result.motionQualified = contact.airborne;
    m_result.settled = !contact.airborne;
    m_result.status = contact.airborne ? "contact_airborne" : contact.impactActive ? "contact_impact" :
      contact.cutStrength > 0.0 ? "contact_cut" : contact.strength > 0.0 ? "contact_release" : "contact_supported";
  }

  void UpdateCrashAttribution(bool consecutive, bool previouslyValid, bool valid,
                              const std::array<double, 3>& body,
                              const std::array<double, 2>& back)
  {
    if (!consecutive) m_crashFrames = 0;
    else if (m_crashFrames) --m_crashFrames;
    // A missing anchor may follow an impact, but cannot originate one. Keep an
    // already attributed window briefly while discarding movement history.
    if (!consecutive || !previouslyValid || !valid) {
      m_groundedSteps = 0;
      m_previousHorizontalStep = 0.0;
      return;
    }
    const double speed = std::hypot(body[0] - m_body[0], body[2] - m_body[2]);
    const double dy = body[1] - m_body[1];
    const double headingDot = back[0] * m_back[0] + back[1] * m_back[1];
    if (!std::isfinite(speed) || !std::isfinite(dy) || !std::isfinite(headingDot) ||
        speed > 24.0 || std::abs(dy) > 12.0 || headingDot < 0.866025403784) {
      m_crashFrames = 0;
      m_groundedSteps = 0;
      m_previousHorizontalStep = 0.0;
      return;
    }
    // Require several near-level native steps before AND at the stop. Airborne
    // descent, a hard landing, ordinary braking and coasting must not borrow
    // this softer profile. There is no brake channel here: an exceptionally
    // abrupt grounded deceleration can still be a false positive.
    const bool nearLevel = std::abs(dy) <= 0.25;
    // Stronger protection wins if the post-impact path becomes a jump/fall or
    // another camera discontinuity. The relief cannot carry into that event.
    if (!nearLevel || m_motionActive) m_crashFrames = 0;
    const bool abruptStop = m_groundedSteps >= 3 && nearLevel && !m_motionActive &&
      m_previousHorizontalStep >= 1.5 && m_previousHorizontalStep - speed >= 1.25 &&
      speed < m_previousHorizontalStep * 0.35;
    if (abruptStop) m_crashFrames = CrashReliefFrames;
    m_groundedSteps = nearLevel && !m_motionActive ? std::min(3u, m_groundedSteps + 1) : 0;
    m_previousHorizontalStep = speed;
  }

  void ResetMotion()
  {
    m_result.verticalTravel = m_result.heightSpan = m_result.velocitySpan = 0.0;
    m_result.fastFrames = m_result.roughReversals = 0;
    m_result.motionQualified = m_result.settled = m_motionActive = false;
    m_direction = 0;
    m_heights.fill(0.0);
    m_velocities.fill(0.0);
    m_motionSamples = 0;
  }

  void ApproachStrength(double target)
  {
    const double step = 1.0 / (SceneHz * (target > m_legacyStrength ? AttackSeconds : ReleaseSeconds));
    m_legacyStrength += std::clamp(target - m_legacyStrength, -step, step);
    m_legacyStrength = std::clamp(m_legacyStrength, 0.0, 1.0);
    m_result.vignetteStrength = m_legacyStrength;
  }

  bool m_seen = false, m_valid = false, m_acquired = false;
  uint64_t m_frame = 0, m_epoch = 0;
  double m_filteredY = 0.0;
  int m_direction = 0;
  bool m_motionActive = false;
  unsigned m_crashFrames = 0, m_groundedSteps = 0;
  double m_previousHorizontalStep = 0.0;
  std::array<double, RoughFrames + 1> m_heights{};
  std::array<double, SettledFrames> m_velocities{};
  unsigned m_motionSamples = 0;
  std::array<double, 3> m_body{};
  std::array<double, 2> m_back{};
  Result m_result;
  double m_legacyStrength = 0.0;
  RiderContactComfort m_contactComfort;
};

} // namespace New3D
#endif
