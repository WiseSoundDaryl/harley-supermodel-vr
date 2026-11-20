#ifndef INCLUDED_MOUSELOOK_H
#define INCLUDED_MOUSELOOK_H

#include <algorithm>
#include <cmath>
#include "Graphics/IRender3D.h"

// Pure input state for the optional desktop head-look diagnostic. The SDL
// owner applies capture changes; this class never accesses SDL or game state.
class MouseLookState
{
public:
  explicit MouseLookState(double sensitivity = 0.12)
    : m_sensitivity(std::isfinite(sensitivity) && sensitivity > 0 ? sensitivity : 0.12) {}

  bool Captured() const { return m_captured; }
  double Yaw() const { return m_yaw; }
  double Pitch() const { return m_pitch; }

  void Release()
  {
    m_captured = false;
    m_requireRelease = true;
  }

  void Update(bool focused, bool rightDown, int dx, int dy, bool recenterDown)
  {
    const bool recenter = focused && recenterDown && !m_recenterDown;
    m_recenterDown = recenterDown;
    if (recenter)
      m_yaw = m_pitch = 0;

    if (!focused)
    {
      Release();
      return;
    }
    if (!rightDown)
    {
      m_captured = false;
      m_requireRelease = false;
      return;
    }
    if (m_requireRelease)
      return;
    if (!m_captured)
    {
      // Discard motion accumulated before this press/relative-mode transition.
      m_captured = true;
      return;
    }
    if (recenter)
      return;
    // Right-handed head-to-base rotations: negative yaw looks right and
    // negative pitch looks down. Limit pitch to avoid overturning the view.
    m_yaw = std::fmod(m_yaw - double(dx) * m_sensitivity + 180.0, 360.0);
    if (m_yaw < 0)
      m_yaw += 360.0;
    m_yaw -= 180.0;
    m_pitch = std::max(-80.0, std::min(80.0, m_pitch - double(dy) * m_sensitivity));
  }

  IRender3D::PresentationPose Pose() const
  {
    constexpr double halfRadiansPerDegree = 3.14159265358979323846 / 360.0;
    const double sy = std::sin(m_yaw * halfRadiansPerDegree);
    const double cy = std::cos(m_yaw * halfRadiansPerDegree);
    const double sx = std::sin(m_pitch * halfRadiansPerDegree);
    const double cx = std::cos(m_pitch * halfRadiansPerDegree);
    IRender3D::PresentationPose pose;
    // qYaw * qPitch: yaw about base up, then pitch about the turned head right.
    pose.orientation[0] = float(cy * sx);
    pose.orientation[1] = float(sy * cx);
    pose.orientation[2] = float(-sy * sx);
    pose.orientation[3] = float(cy * cx);
    return pose;
  }

private:
  double m_sensitivity;
  double m_yaw = 0;
  double m_pitch = 0;
  bool m_captured = false;
  bool m_requireRelease = true;
  bool m_recenterDown = false;
};

#endif // INCLUDED_MOUSELOOK_H
