#ifndef INCLUDED_BIKE_HUD_H
#define INCLUDED_BIKE_HUD_H

#include <array>

namespace New3D { namespace BikeHUD {

// Native HUD coordinates use a top-left origin. These five exhaustive crops
// retain route/map, timer, score, speed, gear, music and transient messages.
// Physical positions are in metres in a forward-facing dashboard frame at
// the rider's chassis origin (not the independently tracked head).
struct Instrument {
  float left, top, right, bottom;
  float x, y, distance, halfWidth;
  float HalfHeight() const { return halfWidth * (bottom-top)/(right-left); }
};
static constexpr std::array<Instrument,5> Instruments = {{
  {0,   0,496, 96,  0.0f, .48f, .90f, .34f}, // compact status / timer rail, lowered toward bars
  {0,  96,496,176,  0.0f, .71f,1.10f, .38f}, // transient messages; empty pixels stay transparent
  {0, 176,145,384, -.30f, .32f, .72f, .13f}, // left navigation display
  {350,176,496,384, .30f, .32f, .72f, .13f}, // right speed / gear display
  {145,176,350,384, 0.0f, .31f, .68f, .13f}  // tank music / message display
}};

} }
#endif
