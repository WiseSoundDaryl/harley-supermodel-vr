#ifndef INCLUDED_BODY_INTERACTION_MODE_H
#define INCLUDED_BODY_INTERACTION_MODE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

// Presentation-only policy. The caller supplies held keyboard/controller
// buttons and owns the frozen scene's local floor/bounds. Head poses and native
// game state are deliberately absent. Movement uses the presentation clock.
namespace BodyInteractionMode {

struct Sample {
  double nowMs = 0;
  bool focused = false;
  bool handToggle = false; // Compatibility only; hands are always free by default.
  bool exploreToggle = false; // One R3 click; caller owns keyboard equivalent.
  bool returnToSeat = false;
  float moveX = 0, moveY = 0, turnX = 0;
  bool eligible = false;
  bool paused = false;
  uint64_t sceneEpoch = 0, sceneFrame = 0;
};

struct Request {
  bool armsEnabled = true;
  bool exploring = false;
  bool entered = false, exited = false, cancelled = false;
  bool pauseChanged = false, pauseValue = false;
  float moveRightMeters = 0, moveForwardMeters = 0;
  // One fixed turn per right-stick deflection; release to center to rearm.
  float snapYawDegrees = 0;
  float vignetteStrength = 0;
  bool blackout = false; // Never used for walking, turning or mode changes.
};

class Controller {
public:
  static constexpr float Deadzone = .15f;
  static constexpr float WalkMetersPerSecond = 1.25f;
  static constexpr float TurnDegrees = 30.f;
  static constexpr float TurnActivateThreshold = .7f;
  static constexpr float TurnReleaseThreshold = .3f;
  static constexpr float MaximumVignette = .9f;
  static constexpr double MaximumDeltaSeconds = .05;
  static constexpr double ReleaseSeconds = .15;
  static constexpr double VignetteAttackSeconds = .025;
  static constexpr double VignetteReleaseSeconds = .040;

  Request Update(const Sample& sample)
  {
    Request out;
    if (!std::isfinite(sample.nowMs) || sample.nowMs < 0 ||
        (m_clockValid && sample.nowMs < m_lastMs)) {
      Cancel(out);
      m_clockValid = false; m_wasFocused = false;
      m_coverNeedsClock = true; m_motionVignette = 0;
      DisarmButtons();
      m_vignette = out.vignetteStrength = MaximumVignette;
      out.exploring = m_exploring;
      return out;
    }
    double delta = m_clockValid ? std::min((sample.nowMs - m_lastMs) * .001, MaximumDeltaSeconds) : 0;
    m_clockValid = true; m_lastMs = sample.nowMs;
    if (m_coverNeedsClock) { m_coverNeedsClock = false; Cover(sample.nowMs); }
    if (!m_wasFocused || !sample.focused) delta = 0;

    // Freeze means this exact completed native scene. Reset/load/frame advance
    // cancels inspection paused; it never silently resumes an altered game.
    if (m_exploring && (sample.sceneEpoch != m_epoch || sample.sceneFrame != m_frame)) {
      Cancel(out); Cover(sample.nowMs); DisarmButtons();
      m_wasFocused = sample.focused;
      return Finish(out, sample.nowMs);
    }
    if (m_exploring && !sample.paused) { out.pauseChanged = true; out.pauseValue = true; }
    if (!sample.focused) {
      DisarmButtons(); m_wasFocused = false; m_motionVignette = 0;
      return Finish(out, sample.nowMs);
    }
    m_wasFocused = true;

    // Buttons arm independently of stick position. Held buttons at startup or
    // focus recovery cannot activate; one released sample then one press can.
    const bool returnEdge = m_return.Update(sample.returnToSeat);
    const bool exploreEdge = m_explore.Update(sample.exploreToggle);
    if (returnEdge) {
      if (m_exploring) Leave(out, sample.nowMs);
      return Finish(out, sample.nowMs);
    }
    if (exploreEdge) {
      if (m_exploring) Leave(out, sample.nowMs);
      else if (sample.eligible) {
        m_exploring = true; m_wasPaused = sample.paused;
        m_epoch = sample.sceneEpoch; m_frame = sample.sceneFrame;
        m_turnHeld = false;
        m_turnArmed = std::isfinite(sample.turnX) && std::abs(sample.turnX) <= TurnReleaseThreshold;
        out.entered = true; out.pauseChanged = true; out.pauseValue = true;
      }
      // Entry freezes without moving. Movement starts after pause is honored.
      return Finish(out, sample.nowMs);
    }

    float intensity = 0;
    if (m_exploring && std::isfinite(sample.moveX) &&
        std::isfinite(sample.moveY) && std::isfinite(sample.turnX)) {
      const float x = std::clamp(sample.moveX, -1.f, 1.f);
      const float y = std::clamp(sample.moveY, -1.f, 1.f);
      const float turn = std::clamp(sample.turnX, -1.f, 1.f);
      const float length = std::hypot(x, y);
      const float move = Magnitude(std::min(length, 1.f));
      if (length > Deadzone) {
        out.moveRightMeters = x / length * move * WalkMetersPerSecond * float(delta);
        out.moveForwardMeters = y / length * move * WalkMetersPerSecond * float(delta);
      }
      out.snapYawDegrees = Turn(turn);
      intensity = move; // Turning alone never adds a vignette.
    } else if (m_exploring) {
      m_turnArmed = m_turnHeld = false;
    }
    // Continuous stick response, with no trigger wait or timed hold. The first
    // movement sample begins the mask immediately; short time-based attack and
    // release avoid visible jumps when the player changes stick pressure.
    const float target = MaximumVignette * intensity * intensity * (3.f - 2.f * intensity);
    const double tau = target > m_motionVignette ? VignetteAttackSeconds : VignetteReleaseSeconds;
    m_motionVignette += (target - m_motionVignette) * float(-std::expm1(-delta / tau));
    if (target == 0 && m_motionVignette < .0001f) m_motionVignette = 0;
    return Finish(out, sample.nowMs);
  }

  bool ArmsEnabled() const { return true; }
  bool Exploring() const { return m_exploring; }
  bool Blackout() const { return false; }
  float VignetteStrength() const { return m_vignette; }
  uint64_t FrozenEpoch() const { return m_epoch; }
  uint64_t FrozenFrame() const { return m_frame; }

private:
  struct Button {
    bool armed = false, held = false;
    bool Update(bool down) {
      if (!down) { armed = true; held = false; return false; }
      const bool edge = armed && !held; held = true; return edge;
    }
  };
  static float Magnitude(float magnitude) {
    return magnitude <= Deadzone ? 0.f : (magnitude - Deadzone) / (1.f - Deadzone);
  }
  float Turn(float axis) {
    const float magnitude = std::abs(axis);
    if (magnitude <= TurnReleaseThreshold) {
      m_turnArmed = true; m_turnHeld = false; return 0;
    }
    if (m_turnArmed && !m_turnHeld && magnitude >= TurnActivateThreshold) {
      m_turnHeld = true; return std::copysign(TurnDegrees, axis);
    }
    return 0;
  }
  void DisarmButtons() { m_explore = {}; m_return = {}; m_turnArmed = m_turnHeld = false; }
  void Cover(double nowMs) { m_coverStart = nowMs; m_coverActive = true; }
  void Cancel(Request& out) {
    m_motionVignette = 0;
    if (!m_exploring) return;
    m_exploring = false; out.cancelled = true;
    out.pauseChanged = true; out.pauseValue = true;
  }
  void Leave(Request& out, double nowMs) {
    m_exploring = false; out.exited = true;
    out.pauseChanged = true; out.pauseValue = m_wasPaused;
    m_motionVignette = 0; Cover(nowMs);
  }
  Request Finish(Request out, double nowMs) {
    float transition = 0;
    if (m_coverActive) {
      const double remaining = 1.0 - (nowMs - m_coverStart) / (ReleaseSeconds * 1000.0);
      if (remaining <= 0) m_coverActive = false;
      else transition = MaximumVignette * float(std::min(remaining, 1.0));
    }
    m_vignette = std::max(m_motionVignette, transition);
    out.exploring = m_exploring; out.vignetteStrength = m_vignette;
    return out;
  }

  bool m_exploring = false, m_wasPaused = false;
  bool m_clockValid = false, m_wasFocused = false;
  bool m_coverActive = false, m_coverNeedsClock = false;
  double m_lastMs = 0, m_coverStart = 0;
  float m_motionVignette = 0, m_vignette = 0;
  Button m_explore, m_return;
  bool m_turnArmed = false, m_turnHeld = false;
  uint64_t m_epoch = 0, m_frame = 0;
};

} // namespace BodyInteractionMode
#endif
