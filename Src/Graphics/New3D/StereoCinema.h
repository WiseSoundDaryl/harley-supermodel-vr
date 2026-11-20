#ifndef INCLUDED_STEREO_CINEMA_H
#define INCLUDED_STEREO_CINEMA_H

#include <cmath>

namespace New3D { namespace StereoCinema {

// An optional stereoscopic movie, not a head-tracked camera into the game.
// The real eye poses still place its screen in a fixed black virtual room.
// Capture uses a conservative 64 mm virtual camera baseline, independent of
// headset pose; flat artwork is never offset or assigned invented depth.
inline int DepthMode(int value) { return value >= 0 && value <= 2 ? value : 0; }
inline float DepthScale(int mode) { return mode == 1 ? .5f : mode == 2 ? 1.0f : 0.0f; }
inline float Units(float value) { return std::isfinite(value) && value >= 1.0f && value <= 100.0f ? value : 16.0f; }

struct Eye {
  bool stereo = false;
  float cameraX = 0, viewX = 0, slopeShift = 0, convergence = 0;
};

inline Eye CaptureEye(int mode, int eye, float units, float screenAngle, float left, float right)
{
  Eye result;
  const float strength = DepthScale(DepthMode(mode));
  if (strength == 0 || (eye != 0 && eye != 1) ||
      !std::isfinite(left) || !std::isfinite(right) || right-left < .0001f ||
      right-left > 1000 || !std::isfinite(screenAngle) || screenAngle < 40 || screenAngle > 100)
    return result;
  units = Units(units);
  const float halfScreen = 2.5f * units * std::tan(screenAngle * .00872664626f);
  // Keep the screen's zero-disparity plane fixed between Shallow and Full.
  // Scale the camera baseline, not only the off-axis convergence shift, so
  // Shallow halves disparity at every depth rather than shifting the movie.
  result.convergence = 2 * halfScreen / (right-left);
  result.cameraX = (eye == 0 ? -.5f : .5f) * .064f * units * strength;
  result.viewX = -result.cameraX;
  result.slopeShift = result.viewX / result.convergence;
  result.stereo = std::isfinite(result.convergence) && result.convergence > 0 &&
    std::isfinite(result.slopeShift);
  return result.stereo ? result : Eye{};
}

} }
#endif
