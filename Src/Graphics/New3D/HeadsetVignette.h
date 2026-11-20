#ifndef INCLUDED_HEADSET_VIGNETTE_H
#define INCLUDED_HEADSET_VIGNETTE_H

#include <algorithm>
#include <cmath>
#include "Graphics/IRender3D.h"

namespace New3D { namespace HeadsetVignette {

// Both eyes sample one circular aperture in a head-relative plane, rather than
// unrelated circles centered on their texture rectangles. At 2.5 metres, IPD
// gives this shared aperture the same comfortable disparity as the HUD panel.
// Radius 1 is a 45-degree half-angle from the midpoint between the eyes.
constexpr float DistanceMeters = 2.5f;
struct Transform { float fromNdc[9]{}; }; // column-major homogeneous 2D mapping

struct Profile { float clearRadius, darkRadius, opacity; };

inline Profile ProfileFor(bool headset, bool cinematic, float strength, float crashRelief = 0.0f)
{
  const float s = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
  // Desktop previews retain their established screen-relative mask. Runtime
  // eyes share a much smaller head-relative porthole. Radii are tan(half-angle).
  if (!headset) return {cinematic ? 0.30f : 0.58f, 1.0f, (cinematic ? 0.97f : 0.90f) * s};
  const float tightness = std::clamp((s - 0.55f) / 0.45f, 0.0f, 1.0f);
  // Medium transmission movement: clear +/-21 degrees, 90% dark by +/-31.
  // Full movement: clear +/-15 degrees, fully black outside +/-25. The common
  // central opening always remains clear; this never blacks out the whole view.
  const float inner = 0.3838640350f + (0.2679491924f - 0.3838640350f) * tightness;
  const float outer = 0.6008606190f + (0.4663076582f - 0.6008606190f) * tightness;
  const float opacity = s <= 0.55f ? 0.90f * (s / 0.55f) : 0.90f + 0.10f * tightness;
  // Only the independently attributed riding crash window gets a modestly
  // wider aperture. Keep opacity, ordinary jumps and cinematic masks unchanged.
  const float relief = !cinematic && std::isfinite(crashRelief) ? std::clamp(crashRelief, 0.0f, 1.0f) : 0.0f;
  const float radiusScale = 1.0f + 0.15f * relief;
  return {inner * radiusScale, outer * radiusScale, opacity};
}

// Preserve the existing desktop GPU arithmetic. Pre-multiplying opacity
// on the CPU can change a panel texel by one channel level after filtering.
inline const char* FragmentShader(bool cinematic)
{
  return cinematic ? R"glsl(#version 410 core
in vec2 localPosition;
uniform float strength;
uniform vec3 apertureProfile;
uniform bool sharedAperture;
uniform mat3 apertureFromNdc;
out vec4 colour;
void main() {
  float radius = length(localPosition);
  if (sharedAperture) {
    vec3 aperture = apertureFromNdc * vec3(localPosition, 1);
    radius = aperture.z > 0.000001 ? length(aperture.xy) / aperture.z : 2.0;
    float edge = smoothstep(apertureProfile.x, apertureProfile.y, radius);
    colour = vec4(0, 0, 0, apertureProfile.z * edge);
  } else {
    float edge = smoothstep(0.30, 1.0, radius);
    colour = vec4(0, 0, 0, 0.97 * clamp(strength, 0.0, 1.0) * edge);
  }
})glsl" : R"glsl(#version 410 core
in vec2 localPosition;
uniform float strength;
uniform vec3 apertureProfile;
uniform bool sharedAperture;
uniform mat3 apertureFromNdc;
out vec4 colour;
void main() {
  float radius = length(localPosition);
  if (sharedAperture) {
    vec3 aperture = apertureFromNdc * vec3(localPosition, 1);
    radius = aperture.z > 0.000001 ? length(aperture.xy) / aperture.z : 2.0;
    float edge = smoothstep(apertureProfile.x, apertureProfile.y, radius);
    colour = vec4(0, 0, 0, apertureProfile.z * edge);
  } else {
    float edge = smoothstep(0.58, 1.0, radius);
    colour = vec4(0, 0, 0, 0.9 * clamp(strength, 0.0, 1.0) * edge);
  }
})glsl";
}

inline void Rotation(const float* q, float* r)
{
  const float x=q[0], y=q[1], z=q[2], w=q[3];
  r[0]=1-2*(y*y+z*z); r[3]=2*(x*y-z*w); r[6]=2*(x*z+y*w);
  r[1]=2*(x*y+z*w); r[4]=1-2*(x*x+z*z); r[7]=2*(y*z-x*w);
  r[2]=2*(x*z-y*w); r[5]=2*(y*z+x*w); r[8]=1-2*(x*x+y*y);
}

inline bool Build(const IRender3D::PresentationEye (&eyes)[2], Transform (&result)[2])
{
  float q[2][4], center[3]{}, slopes[2][4];
  for (int eye=0; eye<2; ++eye) {
    double norm=0;
    for (int j=0; j<4; ++j) {
      const float value=eyes[eye].pose.orientation[j];
      if (!std::isfinite(value)) return false;
      norm+=double(value)*value;
      const float angle=eyes[eye].fov[j];
      if (!std::isfinite(angle) || std::abs(angle)>=1.57069632679f) return false;
      slopes[eye][j]=std::tan(angle);
    }
    if (std::abs(norm-1)>0.01 || slopes[eye][0]>=slopes[eye][1] || slopes[eye][2]>=slopes[eye][3]) return false;
    for (int j=0; j<4; ++j) q[eye][j]=eyes[eye].pose.orientation[j]/float(std::sqrt(norm));
    for (int j=0; j<3; ++j) {
      const float value=eyes[eye].pose.position[j];
      if (!std::isfinite(value) || std::abs(value)>10) return false;
      center[j]+=value*0.5f;
    }
  }
  // Hemisphere-correct quaternion midpoint removes symmetric runtime eye cant
  // while preserving the common tracked head orientation. q and -q agree.
  float dot=0, head[4], norm=0;
  for (int j=0; j<4; ++j) dot+=q[0][j]*q[1][j];
  for (int j=0; j<4; ++j) {
    head[j]=q[0][j]+(dot<0 ? -q[1][j] : q[1][j]);
    norm+=head[j]*head[j];
  }
  if (!std::isfinite(norm) || norm<0.0001f) return false;
  for (float& value:head) value/=std::sqrt(norm);
  float headRotation[9]; Rotation(head,headRotation);
  Transform prepared[2];
  for (int eye=0; eye<2; ++eye) {
    float eyeRotation[9], relative[9]{}, origin[3]{};
    Rotation(q[eye],eyeRotation);
    for (int row=0; row<3; ++row) {
      for (int k=0; k<3; ++k)
        origin[row]+=headRotation[row*3+k]*(eyes[eye].pose.position[k]-center[k])/DistanceMeters;
      for (int col=0; col<3; ++col) for (int k=0; k<3; ++k)
        relative[col*3+row]+=headRotation[row*3+k]*eyeRotation[col*3+k];
    }
    // Intersect each eye ray with the SAME head-plane z=-1. Absorb asymmetric
    // lens unprojection, cant and eye origin into one cheap fragment mat3.
    const float sx=(slopes[eye][1]-slopes[eye][0])*0.5f, ox=(slopes[eye][1]+slopes[eye][0])*0.5f;
    const float sy=(slopes[eye][3]-slopes[eye][2])*0.5f, oy=(slopes[eye][3]+slopes[eye][2])*0.5f;
    for (int col=0; col<3; ++col) {
      float ray[3];
      for (int row=0; row<3; ++row)
        ray[row]=col==0 ? relative[row]*sx : col==1 ? relative[3+row]*sy :
          relative[row]*ox+relative[3+row]*oy-relative[6+row];
      prepared[eye].fromNdc[col*3]=(1+origin[2])*ray[0]-origin[0]*ray[2];
      prepared[eye].fromNdc[col*3+1]=(1+origin[2])*ray[1]-origin[1]*ray[2];
      prepared[eye].fromNdc[col*3+2]=-ray[2];
    }
    // A malformed pair must not create an aperture behind either eye.
    if (1+origin[2]<=0) return false;
    for (float value:prepared[eye].fromNdc) if (!std::isfinite(value)) return false;
  }
  result[0]=prepared[0]; result[1]=prepared[1];
  return true;
}

inline float Radius(const Transform& transform, float x, float y)
{
  const auto& h=transform.fromNdc;
  const float z=h[2]*x+h[5]*y+h[8];
  if (z<=0.000001f) return 2.0f;
  const float px=h[0]*x+h[3]*y+h[6], py=h[1]*x+h[4]*y+h[7];
  return std::sqrt(px*px+py*py)/z;
}

} }
#endif
