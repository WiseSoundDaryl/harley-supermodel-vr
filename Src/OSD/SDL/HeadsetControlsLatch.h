#ifndef INCLUDED_HEADSET_CONTROLS_LATCH_H
#define INCLUDED_HEADSET_CONTROLS_LATCH_H

#include "../Windows/OpenXRSession.h"
#include <cstdint>

// Owner-thread-only input mailbox. Observe only successfully begun XR frames;
// a nonblocking TryBegin that has no frame must not manufacture a neutral sample.
// No emulator, configuration, renderer, input-system or OpenXR calls occur here.
class HeadsetControlsLatch
{
public:
  using Frame = OpenXRSession::Frame;
  using Controls = OpenXRSession::Controls;

  void Observe(const Frame& frame, const Controls& controls, bool paused)
  {
    if (!frame.begun) return;
    SetPaused(paused);
    m_frame = frame;
    m_controls = controls;

    const unsigned ui = (controls.pause ? Pause : 0u) |
                        (controls.recenter ? Recenter : 0u);
    const unsigned uiAvailable = frame.focused ?
      ((controls.leftActive ? Pause : 0u) | (controls.rightActive ? Recenter : 0u)) : 0u;
    const unsigned uiEdges = Edges(ui, uiAvailable, m_previousUI, m_rearmUI, m_blockedUI);
    m_pausePending |= (uiEdges & Pause) != 0;
    m_recenterPending |= (uiEdges & Recenter) != 0;

    const unsigned game = GameMask(controls);
    const bool playable = frame.focused && frame.tracked && frame.render;
    const unsigned gameAvailable = playable ?
      ((controls.leftActive ? LeftGame : 0u) | (controls.rightActive ? RightGame : 0u)) : 0u;
    const unsigned gameEdges = Edges(game, gameAvailable, m_previousGame, m_rearmGame, m_blockedGame);
    m_gamePending &= gameAvailable;
    if (playable && !m_paused && !m_pausePending && !m_exitRequested)
      m_gamePending |= gameEdges;
    else
      m_gamePending = 0;

    // Runtime prediction is not assumed monotonic across restarts/recentering.
    // An accepted exit stays latched; focus loss cannot revoke a user's request.
    const bool exitHeld = frame.focused && controls.leftActive && controls.rightActive && controls.exitCombo;
    if (!exitHeld || frame.predictedTime <= 0 || frame.predictedTime < m_previousTime)
    {
      m_exitTiming = false;
      m_exitSince = 0;
    }
    else if (!m_exitTiming)
    {
      m_exitTiming = true;
      m_exitSince = frame.predictedTime;
    }
    else if (frame.predictedTime - m_exitSince >= 2000000000LL)
    {
      m_exitRequested = true;
      m_gamePending = 0;
    }
    m_previousTime = frame.predictedTime;
  }

  // Call immediately after handling TakePause, and on keyboard pause changes.
  // Held game buttons must be released before acting after a pause transition.
  void SetPaused(bool paused)
  {
    if (m_paused == paused) return;
    m_paused = paused;
    m_gamePending = 0;
    m_rearmGame = AllGame;
  }

  // Explicit session loss, not a TryBegin "not ready" result. Previously
  // accepted pause/recenter/exit requests remain available to the outer loop.
  void Suspend()
  {
    m_frame = {};
    m_controls = {};
    m_gamePending = m_previousGame = m_blockedGame = 0;
    m_previousUI = m_blockedUI = 0;
    m_rearmGame = AllGame;
    m_rearmUI = AllUI;
    m_exitTiming = false;
    m_exitSince = m_previousTime = 0;
  }

  // Requests coalesce until drained. A pause request gates gameplay immediately
  // even though the caller must wait for native workers before actually pausing.
  bool TakePause() { const bool value = m_pausePending; m_pausePending = false; return value; }
  bool TakeRecenter() { const bool value = m_recenterPending; m_recenterPending = false; return value; }
  bool ExitRequested() const { return m_exitRequested; }
  bool Active() const
  {
    return m_frame.begun && m_frame.focused && m_frame.tracked && m_frame.render &&
           !m_paused && !m_pausePending && !m_exitRequested;
  }
  const Frame& LatestFrame() const { return m_frame; }
  const Controls& LatestControls() const { return m_controls; }

  // Exactly one native-frame pulse per queued button. Do not OR raw held game
  // buttons back in afterward. Analog controls retain the newest sample.
  // Multiple presses of the same button before a native tick coalesce.
  Controls ConsumeGame()
  {
    Controls value{};
    if (Active())
    {
      value.active = m_controls.active;
      value.leftActive = m_controls.leftActive;
      value.rightActive = m_controls.rightActive;
      if (value.leftActive) { value.steering = m_controls.steering; value.brake = m_controls.brake; }
      if (value.rightActive) value.throttle = m_controls.throttle;
      value.coin = (m_gamePending & Coin) != 0;
      value.select = (m_gamePending & Select) != 0;
      value.view = (m_gamePending & View) != 0;
      value.music = (m_gamePending & Music) != 0;
      value.gearShiftUp = (m_gamePending & GearUp) != 0;
      value.gearShiftDown = (m_gamePending & GearDown) != 0;
    }
    m_gamePending = 0;
    return value;
  }

private:
  enum : unsigned {
    Coin = 1u << 0, Select = 1u << 1, View = 1u << 2, Music = 1u << 3,
    GearUp = 1u << 4, GearDown = 1u << 5,
    LeftGame = Coin | View, RightGame = Select | Music | GearUp | GearDown,
    AllGame = LeftGame | RightGame, Pause = 1u << 0, Recenter = 1u << 1,
    AllUI = Pause | Recenter
  };
  static unsigned GameMask(const Controls& controls)
  {
    return (controls.coin ? Coin : 0u) | (controls.select ? Select : 0u) |
           (controls.view ? View : 0u) | (controls.music ? Music : 0u) |
           (controls.gearShiftUp ? GearUp : 0u) | (controls.gearShiftDown ? GearDown : 0u);
  }
  static unsigned Edges(unsigned down, unsigned available, unsigned& previous,
                        unsigned& rearm, unsigned& blocked)
  {
    // A disconnected/unfocused hand can reappear with buttons held. Baseline
    // those buttons and require a release instead of synthesizing a press.
    rearm |= ~available;
    blocked |= down & rearm & available;
    rearm &= ~available;
    blocked &= down;
    const unsigned edges = down & ~previous & available & ~blocked;
    previous = down;
    return edges;
  }

  Frame m_frame{};
  Controls m_controls{};
  unsigned m_gamePending = 0, m_previousGame = 0, m_blockedGame = 0, m_rearmGame = AllGame;
  unsigned m_previousUI = 0, m_blockedUI = 0, m_rearmUI = AllUI;
  bool m_paused = false, m_pausePending = false, m_recenterPending = false;
  bool m_exitRequested = false, m_exitTiming = false;
  int64_t m_exitSince = 0, m_previousTime = 0;
};

#endif
