#ifndef INCLUDED_STEERING_VIEW_COMFORT_H
#define INCLUDED_STEERING_VIEW_COMFORT_H

#include <cmath>
#include <cstdint>

// Presentation-only native-camera yaw policy. Head poses, eye offsets, world
// translation and emulation are intentionally absent. Call once per completed
// native scene; repeated presentations must reuse State(). Both eyes therefore
// use one camera heading. This softens automatic turning; it does not remove it.
namespace New3D { namespace SteeringViewComfort {

enum class Reason {
  Uninitialized, Ineligible, NonFinite, Seeded, Recovered, Advanced,
  EpochChanged, FrameGap, FrameRollback, CameraCut
};

inline const char* ReasonName(Reason reason)
{
  switch (reason) {
  case Reason::Uninitialized: return "uninitialized";
  case Reason::Ineligible: return "ineligible";
  case Reason::NonFinite: return "nonfinite";
  case Reason::Seeded: return "seeded";
  case Reason::Recovered: return "recovered";
  case Reason::Advanced: return "advanced";
  case Reason::EpochChanged: return "epoch_changed";
  case Reason::FrameGap: return "frame_gap";
  case Reason::FrameRollback: return "frame_rollback";
  case Reason::CameraCut: return "camera_cut";
  }
  return "unknown";
}

struct Result {
  bool valid = false;
  bool continuous = false;
  double yawDeg = 0;
  double nativeYawDeg = 0;
  double previousYawDeg = 0;
  // Filtered minus native heading, wrapped to the shortest signed angle.
  // This is zero on every seed/recovery/cut; no compensating turn is invented.
  double deltaDeg = 0;
  Reason reason = Reason::Uninitialized;
};

class Controller {
public:
  static constexpr double SceneHz = 57.524160;
  static constexpr double TimeConstantSeconds = 0.12;
  static constexpr double MaximumLagDegrees = 5.0;
  static constexpr double CutDegrees = 45.0;

  static double Wrap(double degrees) { return std::remainder(degrees, 360.0); }

  const Result& Update(uint64_t frame, uint64_t epoch, bool valid, double nativeYawDeg)
  {
    // Explicit invalidation takes priority even if a scene was invalidated and
    // rebuilt under the same ID. Never retain a previous valid compensation on
    // an ineligible scene or turn a nonfinite input into a camera transform.
    if (!valid || !std::isfinite(nativeYawDeg)) {
      m_seen = true; m_have = false; m_frame = frame; m_epoch = epoch;
      m_result = Result{};
      m_result.reason = valid ? Reason::NonFinite : Reason::Ineligible;
      return m_result;
    }
    if (m_have && frame == m_frame && epoch == m_epoch) return m_result;

    const double native = Wrap(nativeYawDeg);
    Reason reason = Reason::Advanced;
    if (!m_have) reason = m_seen ? Reason::Recovered : Reason::Seeded;
    else if (epoch != m_epoch) reason = Reason::EpochChanged;
    else if (frame < m_frame) reason = Reason::FrameRollback;
    else if (frame - m_frame != 1) reason = Reason::FrameGap;
    else if (std::abs(Wrap(native - m_result.nativeYawDeg)) > CutDegrees)
      reason = Reason::CameraCut;

    const double previous = m_result.yawDeg;
    m_seen = m_have = true; m_frame = frame; m_epoch = epoch;
    m_result = Result{};
    m_result.valid = true; m_result.nativeYawDeg = native; m_result.reason = reason;
    if (reason != Reason::Advanced) {
      m_result.yawDeg = m_result.previousYawDeg = native;
      return m_result;
    }

    // Smoothly bound lag instead of clipping a filtered camera to a hard angle
    // limit. A hard limit caused sharper catch-up acceleration in retained game
    // traces. A deadband was also rejected: its threshold crossings sharpened
    // otherwise smooth steering. Sustained turns still follow the native view.
    const double error = Wrap(native - previous);
    const double decay = std::exp(-1.0 / (SceneHz * TimeConstantSeconds));
    const double residual = MaximumLagDegrees * std::tanh(decay * error / MaximumLagDegrees);
    m_result.continuous = true;
    m_result.previousYawDeg = previous;
    m_result.deltaDeg = -residual;
    m_result.yawDeg = Wrap(native - residual);
    return m_result;
  }

  const Result& State() const { return m_result; }
  void Reset() { *this = Controller{}; }

private:
  bool m_seen = false, m_have = false;
  uint64_t m_frame = 0, m_epoch = 0;
  Result m_result;
};

}} // namespace New3D::SteeringViewComfort
#endif
