#ifndef INCLUDED_MENU_COMFORT_H
#define INCLUDED_MENU_COMFORT_H

#include <algorithm>
#include <array>
#include <cmath>

namespace New3D {

// Presentation-only state. Tick once per native frame with the game's camera,
// before applying mouse/head poses. Frozen eye redraws only read State(). Reset
// on load/reset or a discontinuous native-frame timeline; do not serialize it.
class MenuComfort
{
public:
  enum class Phase { Other, Intro, Select, Transmission, RaceIntro };
  static constexpr double TransmissionStrength = 0.55;
  static constexpr unsigned InputGraceFrames = 8;
  static constexpr unsigned SettledFrames = 3;
  static constexpr unsigned ReleaseFrames = 10;
  static constexpr double PositionEpsilon = 0.02;
  static constexpr double RotationEpsilon = 0.0001;

  struct Result {
    double strength = 0.0;
    bool inputEdge = false, cameraMoved = false, cameraValid = false;
    bool motionActive = false;
    unsigned pendingFrames = 0, stableFrames = 0, missingFrames = 0;
    const char* status = "initial";
  };

  void Reset()
  {
    m_result = Result{};
    m_phase = Phase::Other;
    m_seen = m_haveCamera = m_holding = m_motionObserved = false;
    m_direction = 0;
    m_pending = m_stable = m_missing = m_release = 0;
    m_peakStrength = 1.0;
    m_rotation.fill(0.0);
    m_position.fill(0.0);
  }

  const Result& State() const { return m_result; }

  const Result& Tick(Phase phase, int navDirection, bool startDown,
                     bool cameraValid, const float* gameCamera)
  {
    // Reserved for a future independently validated attract-screen trigger.
    // Start does not select a Harley bike, and never arms a driving mask.
    (void)startDown;
    const bool entering = !m_seen || phase != m_phase;
    // Keep the final zoom-to-rider handoff fully covered for its first driving
    // frame, then release without treating ordinary driving as menu motion.
    const bool raceToDriving = m_seen && m_phase == Phase::RaceIntro && phase == Phase::Other;
    if (raceToDriving) {
      Reset();
      m_seen = true;
      m_phase = Phase::Other;
      m_result.strength = m_peakStrength = 1.0;
      m_result.status = "race_handoff";
      return m_result;
    }
    if (phase == Phase::Other && m_seen && m_phase == Phase::Other && m_result.strength > 0.0) {
      m_release = std::min(ReleaseFrames, m_release + 1);
      m_result.strength = double(ReleaseFrames - m_release) / ReleaseFrames;
      m_result.status = m_result.strength > 0.0 ? "race_release" : "other";
      return m_result;
    }
    // The selection overlay reaches its full height while the opening camera
    // is still zooming. Preserve its motion/settling history at that boundary
    // so the classification change cannot expose one unprotected frame.
    const bool introToSelect = m_seen && m_phase == Phase::Intro && phase == Phase::Select;
    // Confirming a rider can briefly restore the selection layout for one
    // frame between two transmission layouts. Keep the entry protection
    // through that menu-only boundary; Other still always clears immediately.
    const bool transmissionToSelect = m_seen && m_phase == Phase::Transmission && phase == Phase::Select;
    if (entering && !introToSelect && !transmissionToSelect) Reset();
    if (introToSelect || transmissionToSelect) m_direction = 0;
    m_seen = true;
    m_phase = phase;
    m_result.inputEdge = m_result.cameraMoved = m_result.cameraValid = false;

    if (phase == Phase::Other) {
      Reset();
      m_seen = true;
      m_phase = Phase::Other;
      m_result.status = "other";
      return m_result;
    }

    const int direction = navDirection > 0 ? 1 : navDirection < 0 ? -1 : 0;
    m_result.inputEdge = (phase == Phase::Select || phase == Phase::Transmission) &&
      direction != 0 && direction != m_direction;
    m_direction = direction;
    SampleCamera(cameraValid, gameCamera);

    if (phase == Phase::RaceIntro) {
      // The measured enlarged/animating race map spans the far-away camera,
      // fast fly-in, and its final approach. Do not release during pauses in
      // that sequence or sample the user's independent head/mouse rotation.
      m_result.strength = m_peakStrength = 1.0;
      m_result.motionActive = true;
      m_result.pendingFrames = m_result.stableFrames = m_result.missingFrames = 0;
      m_result.status = entering ? "race_intro_prearmed" : "race_intro_hold";
      return m_result;
    }

    const double peakStrength = phase == Phase::Transmission ? TransmissionStrength : 1.0;
    const bool prearm = m_result.inputEdge ||
      (entering && (phase == Phase::Intro || phase == Phase::Transmission));
    if (prearm || m_result.cameraMoved) {
      // Attack is deliberately exact and immediate: no easing, including
      // when another rider-switch begins during a previous release.
      m_result.strength = peakStrength;
      m_peakStrength = peakStrength;
      m_holding = true;
      m_motionObserved = m_result.cameraMoved;
      m_pending = m_motionObserved ? 0 : InputGraceFrames - 1;
      m_stable = m_missing = m_release = 0;
      m_result.status = m_result.inputEdge ? "input_prearmed" :
        entering && phase == Phase::Intro ? "intro_prearmed" :
        entering && phase == Phase::Transmission ? "transmission_prearmed" : "camera_motion";
    }
    else if (m_holding && m_pending > 0) {
      --m_pending;
      m_result.status = "waiting_motion";
    }
    else if (m_holding && m_motionObserved) {
      if (m_result.cameraValid) {
        ++m_stable;
        m_missing = 0;
        m_result.status = "settling";
        if (m_stable >= SettledFrames) m_holding = false;
      }
      else {
        // Missing camera evidence cannot hold a menu mask indefinitely.
        m_stable = 0;
        ++m_missing;
        m_result.status = "camera_unavailable";
        if (m_missing >= InputGraceFrames) m_holding = false;
      }
    }
    else {
      m_holding = false;
      if (m_result.strength > 0.0) {
        m_release = std::min(ReleaseFrames, m_release + 1);
        m_result.strength = m_peakStrength * double(ReleaseFrames - m_release) / ReleaseFrames;
        m_result.status = m_result.strength > 0.0 ? "release" :
          IdleStatus(phase);
      }
      else m_result.status = IdleStatus(phase);
    }
    m_result.motionActive = m_holding;
    m_result.pendingFrames = m_pending;
    m_result.stableFrames = m_stable;
    m_result.missingFrames = m_missing;
    return m_result;
  }

private:
  static const char* IdleStatus(Phase phase)
  {
    return phase == Phase::Intro ? "intro_idle" :
      phase == Phase::Transmission ? "transmission_idle" : "selection_idle";
  }

  void SampleCamera(bool valid, const float* camera)
  {
    valid = valid && camera;
    for (unsigned i = 0; valid && i < 16; ++i) valid = std::isfinite(camera[i]);
    m_result.cameraValid = valid;
    if (!valid) { m_haveCamera = false; return; }

    std::array<double, 9> rotation;
    std::array<double, 3> position;
    for (unsigned column = 0; column < 3; ++column) {
      for (unsigned row = 0; row < 3; ++row) rotation[column * 3 + row] = camera[column * 4 + row];
      // The renderer supplies a validated rigid OpenGL view matrix. Compare
      // the camera's inverse position, not raw view translation: a tiny
      // rotation can otherwise amplify large world coordinates into motion.
      position[column] = -(double(camera[column * 4]) * camera[12] +
        double(camera[column * 4 + 1]) * camera[13] +
        double(camera[column * 4 + 2]) * camera[14]);
    }
    if (m_haveCamera) {
      double squaredDistance = 0.0, rotationStep = 0.0;
      for (unsigned i = 0; i < 3; ++i) {
        const double step = position[i] - m_position[i];
        squaredDistance += step * step;
      }
      for (unsigned i = 0; i < 9; ++i) rotationStep = std::max(rotationStep, std::abs(rotation[i] - m_rotation[i]));
      m_result.cameraMoved = squaredDistance > PositionEpsilon * PositionEpsilon || rotationStep > RotationEpsilon;
    }
    m_position = position;
    m_rotation = rotation;
    m_haveCamera = true;
  }

  Result m_result;
  Phase m_phase = Phase::Other;
  bool m_seen = false, m_haveCamera = false, m_holding = false, m_motionObserved = false;
  int m_direction = 0;
  unsigned m_pending = 0, m_stable = 0, m_missing = 0, m_release = 0;
  double m_peakStrength = 1.0;
  std::array<double, 9> m_rotation{};
  std::array<double, 3> m_position{};
};

} // namespace New3D

#endif
