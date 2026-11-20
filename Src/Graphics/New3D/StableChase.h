#ifndef INCLUDED_STABLE_CHASE_H
#define INCLUDED_STABLE_CHASE_H

#include <array>
#include <cmath>

namespace New3D {

// Pure presentation math for an already-validated player-bike transform.
// Native camera shake is cancelled; physical chassis translation/yaw remains.
// The existing RiderComfort filter supplies any subsequent vertical smoothing.
struct StableChase
{
  struct Pose {
    std::array<float, 16> correction{};
    std::array<float, 16> view{};
    std::array<double, 3> position{}, forward{};
  };

  static bool Calculate(const float* bodyWorld, const float* bodyView,
                        const float* gameView, double height, double distance,
                        Pose& result)
  {
    if (!bodyWorld || !bodyView || !gameView || !std::isfinite(height) || !std::isfinite(distance)) return false;
    for (unsigned i = 0; i < 16; ++i)
      if (!std::isfinite(bodyWorld[i]) || !std::isfinite(bodyView[i]) || !std::isfinite(gameView[i])) return false;
    const double length = std::hypot(double(bodyWorld[8]), double(bodyWorld[10]));
    // Nearly vertical chassis heading cannot define a stable horizontal yaw.
    if (length < 0.1) return false;
    Pose pose;
    pose.forward = { double(bodyWorld[8]) / length, 0.0, double(bodyWorld[10]) / length };
    const double right[] = { -pose.forward[2], 0.0, pose.forward[0] };
    const double up[] = { 0.0, 1.0, 0.0 };
    const double back[] = { -pose.forward[0], 0.0, -pose.forward[2] };
    const double* rows[] = { right, up, back };
    double delta[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
      pose.position[axis] = double(bodyWorld[12 + axis]) + (axis == 1 ? height : 0.0) - distance * pose.forward[axis];
      // Work with the small body-to-native-camera displacement, avoiding
      // cancellation of large world translations in the actual eye correction.
      delta[axis] = double(gameView[axis * 4]) * bodyView[12] +
        double(gameView[axis * 4 + 1]) * bodyView[13] +
        double(gameView[axis * 4 + 2]) * bodyView[14] +
        (axis == 1 ? height : 0.0) - distance * pose.forward[axis];
    }
    for (unsigned row = 0; row < 3; ++row) {
      for (unsigned column = 0; column < 3; ++column) {
        double value = 0.0;
        for (unsigned axis = 0; axis < 3; ++axis) value += rows[row][axis] * gameView[axis * 4 + column];
        pose.correction[column * 4 + row] = float(value);
      }
      pose.correction[12 + row] = float(-(rows[row][0] * delta[0] + rows[row][1] * delta[1] + rows[row][2] * delta[2]));
    }
    pose.correction[15] = 1.0f;
    for (unsigned column = 0; column < 4; ++column) for (unsigned row = 0; row < 4; ++row) {
      double value = 0.0;
      for (unsigned axis = 0; axis < 4; ++axis) value += double(pose.correction[axis * 4 + row]) * gameView[column * 4 + axis];
      pose.view[column * 4 + row] = float(value);
    }
    result = pose;
    return true;
  }
};

} // namespace New3D

#endif
