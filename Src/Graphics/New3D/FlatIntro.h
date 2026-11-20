#ifndef INCLUDED_FLAT_INTRO_H
#define INCLUDED_FLAT_INTRO_H

#include "MenuComfort.h"

namespace New3D {

// Native-frame presentation policy. The opening zoom continues after the menu
// layout changes from Intro to Select, so keep its original camera until the
// same validated camera sample used by MenuComfort has finished settling.
// Render-only head-pose updates read Active(); they must never call Tick().
class FlatIntro
{
public:
  void Reset() { m_active = m_opening = false; m_stationarySelect = 0; }
  bool Active() const { return m_active; }

  bool Tick(MenuComfort::Phase phase, const MenuComfort::Result& comfort)
  {
    switch (phase) {
    case MenuComfort::Phase::Intro:
      m_opening = m_active = true;
      m_stationarySelect = 0;
      break;
    case MenuComfort::Phase::Select:
      // Direct selection loads and subsequent rider-navigation inputs do not
      // arm a new flat sequence. Missing camera data cannot expose the zoom.
      if (m_opening && comfort.cameraValid && !comfort.motionActive && !comfort.cameraMoved)
        m_stationarySelect = std::min(MenuComfort::SettledFrames, m_stationarySelect + 1);
      else m_stationarySelect = 0;
      // A state loaded after the camera has already stopped can have no prior
      // motion in MenuComfort's history. Consecutive validated stationary
      // selection samples also establish settlement for that recovery case.
      if (m_opening && comfort.cameraValid && !comfort.motionActive && !comfort.cameraMoved &&
          (comfort.stableFrames >= MenuComfort::SettledFrames ||
           m_stationarySelect >= MenuComfort::SettledFrames))
        m_opening = false;
      m_active = m_opening;
      break;
    case MenuComfort::Phase::RaceIntro:
      m_opening = false;
      m_stationarySelect = 0;
      m_active = true;
      break;
    case MenuComfort::Phase::Transmission:
    case MenuComfort::Phase::Other:
      Reset();
      break;
    }
    return m_active;
  }

private:
  bool m_active = false;
  bool m_opening = false;
  unsigned m_stationarySelect = 0;
};

} // namespace New3D

#endif
