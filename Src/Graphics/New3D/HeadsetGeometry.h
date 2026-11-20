#ifndef INCLUDED_HEADSET_GEOMETRY_H
#define INCLUDED_HEADSET_GEOMETRY_H

#include <cmath>
#include "Graphics/IRender3D.h"
#include "Mat4.h"

namespace New3D { namespace HeadsetGeometry {

// Pure geometry seam used by rendering and its tests. Per-eye runtime poses
// already contain head-local IPD; translating them again would double it.
inline bool EyeMatrices(const IRender3D::PresentationEye& eye, float unitsPerMeter,
                        Mat4& view, Mat4& projection)
{
  if (!std::isfinite(unitsPerMeter) || unitsPerMeter < 1.0f || unitsPerMeter > 1000.0f ||
      !eye.framebuffer || eye.x < 0 || eye.y < 0 || eye.width <= 0 || eye.height <= 0 ||
      eye.width > 16384 || eye.height > 16384 || eye.x > 16384 - eye.width || eye.y > 16384 - eye.height)
    return false;
  double norm = 0;
  for (float value : eye.pose.orientation) {
    if (!std::isfinite(value)) return false;
    norm += double(value) * value;
  }
  if (std::abs(norm - 1.0) > 0.01) return false;
  for (float value : eye.pose.position)
    if (!std::isfinite(value) || std::abs(value) > 10.0f) return false;
  float slopes[4];
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(eye.fov[i]) || std::abs(eye.fov[i]) >= 1.57069632679f) return false;
    slopes[i] = std::tan(eye.fov[i]);
  }
  if (slopes[0] >= slopes[1] || slopes[2] >= slopes[3]) return false;

  const float inverseNorm = float(1.0 / std::sqrt(norm));
  const float x = eye.pose.orientation[0] * inverseNorm;
  const float y = eye.pose.orientation[1] * inverseNorm;
  const float z = eye.pose.orientation[2] * inverseNorm;
  const float w = eye.pose.orientation[3] * inverseNorm;
  // Transpose eye-to-base rotation, then apply -R^T * eyePosition.
  Mat4 result;
  result[0] = 1 - 2*(y*y + z*z); result[4] = 2*(x*y + z*w); result[8] = 2*(x*z - y*w);
  result[1] = 2*(x*y - z*w); result[5] = 1 - 2*(x*x + z*z); result[9] = 2*(y*z + x*w);
  result[2] = 2*(x*z + y*w); result[6] = 2*(y*z - x*w); result[10] = 1 - 2*(x*x + y*y);
  for (int row = 0; row < 3; ++row)
    result[12 + row] = -unitsPerMeter * (result[row] * eye.pose.position[0] +
      result[4 + row] * eye.pose.position[1] + result[8 + row] * eye.pose.position[2]);
  Mat4 lens;
  // Match the renderer's reverse-Z depth convention; x/y are precisely the
  // asymmetric runtime frustum, without a desktop aspect/FOV adjustment.
  lens.FrustumRZ(slopes[0], slopes[1], slopes[2], slopes[3], 0.001f);
  view = result;
  projection = lens;
  return true;
}

} } // namespace New3D::HeadsetGeometry
#endif
