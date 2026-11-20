#ifndef SUPERMODEL_NEW3D_TRACKED_ARMS_H
#define SUPERMODEL_NEW3D_TRACKED_ARMS_H

// Presentation-only rigid two-bone posing. Never changes emulated game state.
// All inputs use ONE shared rider/seat coordinate frame, never separate eyes.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace New3D { namespace TrackedArms {

struct Vec3 { double x=0, y=0, z=0; };
struct Quaternion { double x=0, y=0, z=0, w=1; }; // XYZW, active rotation
using Matrix = std::array<float, 16>; // column-major, affine
using Models = std::array<Matrix, 3>; // upper arm, forearm, hand
enum class Hand { Left, Right };

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x*s,a.y*s,a.z*s}; }
inline double Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline double Length(Vec3 v) { return std::sqrt(Dot(v,v)); }
inline bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool Unit(Vec3 v, Vec3& out) {
  const double n=Length(v);
  if (!Finite(v)||!std::isfinite(n)||n<1e-9) return false;
  out=v*(1.0/n); return true;
}
inline bool Normalize(Quaternion q, Quaternion& out) {
  const double n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
  if (!std::isfinite(n)||n<1e-9) return false;
  out={q.x/n,q.y/n,q.z/n,q.w/n}; return true;
}
inline Quaternion Conjugate(Quaternion q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Quaternion Multiply(Quaternion a, Quaternion b) {
  return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
          a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
          a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Vec3 Rotate(Quaternion q, Vec3 v) {
  const Vec3 u{q.x,q.y,q.z};
  return v+Cross(u,v)*(2*q.w)+Cross(u,Cross(u,v))*2;
}
inline Vec3 TransformPoint(const Matrix& m, Vec3 v) {
  return {m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],
          m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],
          m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]};
}
inline bool Rigid(const Matrix& m) {
  for (float v:m) if (!std::isfinite(v)||std::abs(v)>1e6f) return false;
  if (std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-5) return false;
  const Vec3 x{m[0],m[1],m[2]},y{m[4],m[5],m[6]},z{m[8],m[9],m[10]};
  return std::abs(Dot(x,x)-1)<.005&&std::abs(Dot(y,y)-1)<.005&&std::abs(Dot(z,z)-1)<.005&&
    std::abs(Dot(x,y))<.005&&std::abs(Dot(x,z))<.005&&std::abs(Dot(y,z))<.005&&
    std::abs(Dot(Cross(x,y),z)-1)<.005;
}

// Origin of the original upstream joint matrix, expressed in its centered mesh.
// Verified from ROM translation nodes and six original Harley B saved scenes.
// Provenance: reports/body-exploration-20261027/fatboy-arm-provenance.json.
// Rider base +X is left (+X projects to screen-left); first original profile
// triplet is RIGHT, not left. Do not apply this mapping to other riders.
inline std::array<uint32_t,3> FatBoySources(Hand hand) {
  return hand==Hand::Left ? std::array<uint32_t,3>{{0x85F8CE,0x860AB7,0x8606AB}}
                         : std::array<uint32_t,3>{{0x85FA4D,0x860C53,0x8608B1}};
}
inline std::array<Vec3,3> FatBoyLocalPivots(Hand hand) {
  if (hand==Hand::Left) return {{{-1.38427734375,-.10302734375,-.04443359375},
    {-1.22998046875,.0234375,-.0673828125},{-.56201171875,.12890625,.29541015625}}};
  return {{{-1.38427734375,-.1025390625,.0439453125},
    {-1.22802734375,.0185546875,.06689453125},{-.56201171875,.12890625,-.2958984375}}};
}

struct BindPose {
  Models models{};
  Vec3 shoulder{},elbow{},wrist{};
  bool valid=false;
};
struct Target {
  Vec3 wrist{};
  // Absolute desired mesh-to-rider rotation. The controller-to-hand calibration
  // must be applied by the caller; raw grip orientation is NOT this rotation.
  Quaternion handRotation{};
};
struct Result {
  Models models{};
  Vec3 elbow{},wrist{};
  Vec3 rootShift{}; // common rigid shoulder/chain translation, in input units
  bool rootShiftClamped=false; // requested root assistance exceeded its limit
  bool reachClamped=false;
  bool valid=false;
};

inline bool BuildFatBoyBind(Hand hand, const Models& models, BindPose& out) {
  BindPose b; b.models=models;
  for (const Matrix& m:models) if (!Rigid(m)) { out=BindPose{}; return false; }
  const auto p=FatBoyLocalPivots(hand);
  b.shoulder=TransformPoint(models[0],p[0]);
  b.elbow=TransformPoint(models[1],p[1]);
  b.wrist=TransformPoint(models[2],p[2]);
  const double upper=Length(b.elbow-b.shoulder),fore=Length(b.wrist-b.elbow);
  // The six retained poses measure 2.544..2.547 and 2.689..2.691. A small
  // conservative tolerance admits float quantization, not another topology.
  if (upper<2.50||upper>2.60||fore<2.65||fore>2.73) { out=BindPose{}; return false; }
  b.valid=true; out=b; return true;
}

inline bool Rotation(const Matrix& m, Quaternion& out) {
  if (!Rigid(m)) return false;
  Quaternion q; const double tr=m[0]+m[5]+m[10];
  if (tr>0) {
    const double s=2*std::sqrt(tr+1); q={(m[6]-m[9])/s,(m[8]-m[2])/s,(m[1]-m[4])/s,s*.25};
  } else if (m[0]>m[5]&&m[0]>m[10]) {
    const double s=2*std::sqrt(1+m[0]-m[5]-m[10]); q={s*.25,(m[4]+m[1])/s,(m[8]+m[2])/s,(m[6]-m[9])/s};
  } else if (m[5]>m[10]) {
    const double s=2*std::sqrt(1+m[5]-m[0]-m[10]); q={(m[4]+m[1])/s,s*.25,(m[9]+m[6])/s,(m[8]-m[2])/s};
  } else {
    const double s=2*std::sqrt(1+m[10]-m[0]-m[5]); q={(m[8]+m[2])/s,(m[9]+m[6])/s,s*.25,(m[1]-m[4])/s};
  }
  return Normalize(q,out);
}

inline Vec3 Perpendicular(Vec3 axis) {
  const Vec3 seed=std::abs(axis.x)<std::abs(axis.y)
    ? (std::abs(axis.x)<std::abs(axis.z)?Vec3{1,0,0}:Vec3{0,0,1})
    : (std::abs(axis.y)<std::abs(axis.z)?Vec3{0,1,0}:Vec3{0,0,1});
  Vec3 out; Unit(Cross(axis,seed),out); return out;
}
inline bool Between(Vec3 from, Vec3 to, Vec3 antiParallelHint, Quaternion& out) {
  Vec3 a,b; if (!Unit(from,a)||!Unit(to,b)) return false;
  const double d=std::max(-1.0,std::min(1.0,Dot(a,b)));
  const Vec3 c=Cross(a,b); const double sine=Length(c);
  if (sine>1e-10) {
    // atan2 remains accurate near 180 degrees where (1+dot) loses precision.
    // Do not snap a merely near-antipodal segment to exactly 180 degrees:
    // that separates the original mesh's distal pivot from the solved joint.
    const double half=.5*std::atan2(sine,d),scale=std::sin(half)/sine;
    out={c.x*scale,c.y*scale,c.z*scale,std::cos(half)}; return true;
  }
  if (d>=0) { out={}; return true; }
  Vec3 axis;
  if (!Unit(antiParallelHint-a*Dot(antiParallelHint,a),axis)) axis=Perpendicular(a);
  out={axis.x,axis.y,axis.z,0}; return true;
}

// Rigid left-composition about an actual joint: no scaling of original meshes.
inline Matrix PoseModel(const Matrix& native, Quaternion turn, Vec3 oldPivot, Vec3 newPivot) {
  Matrix out=native;
  for (int col=0;col<3;++col) {
    const Vec3 v=Rotate(turn,{native[col*4],native[col*4+1],native[col*4+2]});
    out[col*4]=static_cast<float>(v.x);out[col*4+1]=static_cast<float>(v.y);out[col*4+2]=static_cast<float>(v.z);
  }
  const Vec3 t=Rotate(turn,Vec3{native[12],native[13],native[14]}-oldPivot)+newPivot;
  out[12]=static_cast<float>(t.x);out[13]=static_cast<float>(t.y);out[14]=static_cast<float>(t.z);
  return out;
}

inline bool Solve(const BindPose& bind, const Target& target, Result& out) {
  // Explicit all-or-nothing fallback; caller must not draw out.models on false.
  out=Result{};
  if (!bind.valid||!Finite(bind.shoulder)||!Finite(bind.elbow)||!Finite(bind.wrist)||!Finite(target.wrist)) return false;
  for (const Matrix& m:bind.models) if (!Rigid(m)) return false;
  Quaternion desired,nativeHand;
  if (!Normalize(target.handRotation,desired)||!Rotation(bind.models[2],nativeHand)) return false;
  const Vec3 upper=bind.elbow-bind.shoulder,fore=bind.wrist-bind.elbow;
  const double a=Length(upper),b=Length(fore);
  if (!std::isfinite(a+b)||a<1e-5||b<1e-5||a+b>1000) return false;
  Vec3 bindDirection;
  if (!Unit(bind.wrist-bind.shoulder,bindDirection)) return false;
  Vec3 bend;
  if (!Unit(upper-bindDirection*Dot(upper,bindDirection),bend)) bend=Perpendicular(bindDirection);
  // Carry the native bend plane with the target direction. This preserves the
  // elbow side through normal reach; an antipodal target uses the same bind
  // plane deterministically rather than a noisy cross-product near zero.
  Vec3 direction; const Vec3 reach=target.wrist-bind.shoulder;
  const double requested=Length(reach);
  if (!std::isfinite(requested)||requested>1e6) return false;
  if (!Unit(reach,direction)) direction=bindDirection;
  Quaternion carry;
  if (!Between(bindDirection,direction,Cross(bindDirection,bend),carry)) return false;
  Vec3 pole=Rotate(carry,bend);
  if (!Unit(pole-direction*Dot(pole,direction),pole)) return false;
  const double margin=(a+b)*1e-5;
  const double minimum=std::abs(a-b)+margin,maximum=a+b-margin;
  const double distance=std::max(minimum,std::min(maximum,requested));
  const double along=(a*a-b*b+distance*distance)/(2*distance);
  const double height=std::sqrt(std::max(0.0,a*a-along*along));
  Result result;
  result.wrist=bind.shoulder+direction*distance;
  result.elbow=bind.shoulder+direction*along+pole*height;
  result.reachClamped=std::abs(distance-requested)>1e-9;
  Quaternion upperTurn,foreTurn;
  const Vec3 normal=Cross(bindDirection,bend);
  if (!Between(upper,result.elbow-bind.shoulder,normal,upperTurn)||
      !Between(fore,result.wrist-result.elbow,normal,foreTurn)) return false;
  result.models[0]=PoseModel(bind.models[0],upperTurn,bind.shoulder,bind.shoulder);
  result.models[1]=PoseModel(bind.models[1],foreTurn,bind.elbow,result.elbow);
  result.models[2]=PoseModel(bind.models[2],Multiply(desired,Conjugate(nativeHand)),bind.wrist,result.wrist);
  if (!Finite(result.elbow)||!Finite(result.wrist)) return false;
  for (const Matrix& m:result.models) if (!Rigid(m)) return false;
  result.valid=true; out=result; return true;
}

// Optional presentation-only reach assistance for the torso-free arm display.
// The original game limbs are only ~0.327m long at 16 units/metre. Translate
// their common virtual root instead of stretching meshes or moving the head.
// A valid reachable original pose follows the exact ordinary Solve path.
// maxRootShiftMetres is explicit, never greater than 0.5m; distant targets can
// still be unreachable, which remains visible through both result flags.
inline bool SolveWithRootReach(const BindPose& bind,const Target& target,
  double unitsPerMetre,double maxRootShiftMetres,Result& out)
{
  out=Result{};
  if (!std::isfinite(unitsPerMetre)||unitsPerMetre<1||unitsPerMetre>100||
      !std::isfinite(maxRootShiftMetres)||maxRootShiftMetres<0||maxRootShiftMetres>.5) return false;
  Result native;
  if (!Solve(bind,target,native)) return false;
  const Vec3 reach=target.wrist-bind.shoulder;
  const double distance=Length(reach);
  const double length=Length(bind.elbow-bind.shoulder)+Length(bind.wrist-bind.elbow);
  // Match Solve's existing small singularity margin exactly. No new scaling,
  // extra bend allowance or shoulder movement inside the original reach.
  const double maximum=length-length*1e-5;
  if (distance<=maximum) { out=native; return true; }
  const double needed=distance-maximum,limit=unitsPerMetre*maxRootShiftMetres;
  const double amount=std::min(needed,limit);
  if (amount==0) { native.rootShiftClamped=needed>0;out=native;return true; }
  const Vec3 shift=reach*(amount/distance);
  BindPose shifted=bind;
  shifted.shoulder=shifted.shoulder+shift;
  shifted.elbow=shifted.elbow+shift;
  shifted.wrist=shifted.wrist+shift;
  for (Matrix& model:shifted.models) {
    model[12]=static_cast<float>(model[12]+shift.x);
    model[13]=static_cast<float>(model[13]+shift.y);
    model[14]=static_cast<float>(model[14]+shift.z);
  }
  Result result;
  if (!Solve(shifted,target,result)) return false;
  result.rootShift=shift;result.rootShiftClamped=needed>limit+1e-9;
  out=result; return true;
}

}} // namespace New3D::TrackedArms
#endif
