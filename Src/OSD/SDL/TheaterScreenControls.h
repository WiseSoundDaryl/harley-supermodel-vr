#ifndef INCLUDED_THEATERSCREENCONTROLS_H
#define INCLUDED_THEATERSCREENCONTROLS_H

#include <algorithm>
#include <cmath>

// Desktop-only screen sizing. One deliberate press changes angular width;
// key repeat, a held startup key, and focus return cannot resize the screen.
class TheaterScreenControls
{
public:
  static constexpr float MinimumAngle = 40.0f;
  static constexpr float MaximumAngle = 100.0f;
  static constexpr float DefaultAngle = 70.0f;

  explicit TheaterScreenControls(float angle = DefaultAngle)
    : m_angle(std::isfinite(angle) ? std::max(MinimumAngle, std::min(MaximumAngle, angle)) : DefaultAngle) {}

  float Angle() const { return m_angle; }

  bool Update(bool focused, bool largerDown, bool smallerDown)
  {
    if (!focused)
    {
      m_requireRelease = true;
      return false;
    }
    if (!largerDown && !smallerDown)
    {
      m_requireRelease = false;
      return false;
    }
    if (m_requireRelease)
      return false;
    m_requireRelease = true;
    if (largerDown == smallerDown)
      return false;
    const float next = std::max(MinimumAngle, std::min(MaximumAngle, m_angle + (largerDown ? 5.0f : -5.0f)));
    if (next == m_angle)
      return false;
    m_angle = next;
    return true;
  }

private:
  float m_angle;
  bool m_requireRelease = true;
};

#endif // INCLUDED_THEATERSCREENCONTROLS_H
