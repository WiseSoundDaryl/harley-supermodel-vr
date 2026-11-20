#ifndef INCLUDED_HARLEY_PRESENTATION_H
#define INCLUDED_HARLEY_PRESENTATION_H

#include <cstdint>

// Immutable metadata for the same native frame as the GPU snapshot. No game
// RAM pointer crosses into the renderer or its independent head-pose redraws.
namespace HarleyPresentation
{
// Completed native physics, copied while the PPC is parked. Support comes from
// Harley's wheel collision solver; impact timers come from its common response.
// No live RAM access is required by the renderer or either headset eye.
struct MotionState
{
  bool valid = false;
  bool frontSupported = false;
  bool rearSupported = false;
  uint32_t frontImpactFrames = 0;
  uint32_t rearImpactFrames = 0;
};

struct State
{
  bool revisionB = false;
  uint32_t mainPhase = 0;
  uint32_t racePhase = 0;
  uint32_t attractFlag = 0;
  uint32_t introComplete = 0;
  uint32_t riderConfirmed = 0;
  uint32_t selectionStage = 0;
  MotionState motion;
};

enum class Mode { Theater, RiderSelection, Driving, Ending };

// Deliberate allowlist. Boot, attract, intro, AT/MT, route/race-start fly-in,
// results, name entry, service screens and unknown states all remain theater.
// Selection stage 1 spans the whole building intro: introComplete is essential.
inline Mode Classify(const State& state)
{
  if (!state.revisionB || state.mainPhase != 5)
    return Mode::Theater;
  // The original ending initializer sets attractFlag itself, so that flag
  // cannot reject an ending that follows the player's completed ride.
  if (state.racePhase == 16 || state.racePhase == 17)
    return Mode::Ending;
  if (state.attractFlag != 0)
    return Mode::Theater;
  if (state.racePhase == 8)
    return Mode::Driving;
  if (state.racePhase == 2 && state.introComplete == 1 &&
      state.riderConfirmed == 0 && state.selectionStage == 1)
    return Mode::RiderSelection;
  return Mode::Theater;
}

inline const char* Name(Mode scene)
{
  switch (scene)
  {
  case Mode::RiderSelection: return "rider_selection";
  case Mode::Driving: return "driving";
  case Mode::Ending: return "ending";
  default: return "theater";
  }
}
}

#endif
