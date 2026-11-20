#ifndef INCLUDED_HANDLEBAR_GRIP_H
#define INCLUDED_HANDLEBAR_GRIP_H

#include <array>

// Input-edge policy only. The renderer supplies a fresh, validated hand pose
// and native handlebar proximity before consuming a press. No timers, geometry,
// eye-dependent state, emulation, or steering inputs are changed here.
namespace New3D { namespace HandlebarGrip {
class Controller {
public:
  void Buttons(bool left, bool right, bool active)
  {
    if (!active) { Reset(); return; }
    const bool buttons[2] = {left,right};
    for (unsigned hand=0; hand<2; ++hand) {
      auto& s=m_hand[hand];
      if (!buttons[hand]) s.armed=true;
      else if (!s.held && s.armed) s.pending=true;
      s.held=buttons[hand];
    }
  }
  bool Prepare(unsigned hand, bool tracked, bool nearGrip, bool canLatch)
  {
    if (hand>=2) return false;
    auto& s=m_hand[hand];
    if (!canLatch) { s=Hand{}; return false; }
    // Releasing an existing toggle never requires proximity or tracking.
    if (s.pending && s.latched) s.latched=false;
    else if (s.pending && tracked && nearGrip) s.latched=true;
    s.pending=false;
    // Tracking recovery must see a fresh released input sample. A previously
    // held button cannot attach merely because the hand became visible again.
    if (!tracked && !s.latched) s.armed=false;
    return s.latched;
  }
  bool Latched(unsigned hand) const { return hand<2 && m_hand[hand].latched; }
  void Reset() { m_hand={}; }
private:
  struct Hand { bool held=false,armed=false,pending=false,latched=false; };
  std::array<Hand,2> m_hand{};
};
}}
#endif
