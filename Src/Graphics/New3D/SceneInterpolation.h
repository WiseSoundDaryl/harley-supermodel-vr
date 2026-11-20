#ifndef INCLUDED_SCENE_INTERPOLATION_H
#define INCLUDED_SCENE_INTERPOLATION_H

// Presentation-only rigid transforms. No emulator or GL dependency. Runtime
// display timestamp differences pace the scene; head pose is never delayed.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace New3D { namespace SceneInterpolation {
constexpr int64_t IntervalNs = 17384000;
constexpr size_t MaxModels = 8192, MaxViewports = 32;
using Matrix = std::array<float, 16>;
struct Pose { std::array<double,3> position{}; std::array<double,4> rotation{{0,0,0,1}}; };
inline Matrix Identity() { Matrix m{}; m[0]=m[5]=m[10]=m[15]=1; return m; }
inline Matrix Multiply(const float* a, const float* b) {
  Matrix out{};
  for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];
  return out;
}
inline bool ReadRigid(const float* m, Pose& out) {
  for(int i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
  if(std::abs(m[3])>1e-5||std::abs(m[7])>1e-5||std::abs(m[11])>1e-5||std::abs(m[15]-1)>1e-5)return false;
  for(int c=0;c<3;++c)for(int d=c;d<3;++d){double dot=0;for(int r=0;r<3;++r)dot+=double(m[c*4+r])*m[d*4+r];if(std::abs(dot-(c==d?1:0))>.003)return false;}
  const double det=m[0]*(double(m[5])*m[10]-double(m[9])*m[6])-m[4]*(double(m[1])*m[10]-double(m[9])*m[2])+m[8]*(double(m[1])*m[6]-double(m[5])*m[2]);
  if(det<.997||det>1.003)return false;
  auto& q=out.rotation; const double trace=double(m[0])+m[5]+m[10];
  if(trace>0){const double s=std::sqrt(trace+1)*2;q={{(m[6]-m[9])/s,(m[8]-m[2])/s,(m[1]-m[4])/s,.25*s}};}
  else if(m[0]>m[5]&&m[0]>m[10]){const double s=std::sqrt(1.0+m[0]-m[5]-m[10])*2;q={{.25*s,(m[4]+m[1])/s,(m[8]+m[2])/s,(m[6]-m[9])/s}};}
  else if(m[5]>m[10]){const double s=std::sqrt(1.0+m[5]-m[0]-m[10])*2;q={{(m[4]+m[1])/s,.25*s,(m[9]+m[6])/s,(m[8]-m[2])/s}};}
  else {const double s=std::sqrt(1.0+m[10]-m[0]-m[5])*2;q={{(m[8]+m[2])/s,(m[9]+m[6])/s,.25*s,(m[1]-m[4])/s}};}
  double norm=0;for(double x:q)norm+=x*x;if(!std::isfinite(norm)||norm<.5)return false;
  for(double& x:q)x/=std::sqrt(norm);
  out.position={{m[12],m[13],m[14]}};return true;
}
// Camera extraction remains strictly rigid. ROM model matrices may also carry
// a constant positive uniform scale (the retained airborne assembly uses1.2).
// Separate that size from the moving rigid pose; do not normalize shears,
// reflections, nonuniform scale or degenerate matrices into invented rotations.
inline bool ReadModelRigid(const float* m,Pose& out,float& scale) {
  scale=1;
  if(ReadRigid(m,out))return true; // Preserve the established unit-basis path.
  for(int i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
  if(std::abs(m[3])>1e-5||std::abs(m[7])>1e-5||std::abs(m[11])>1e-5||std::abs(m[15]-1)>1e-5)return false;
  double lengths[3]{};
  for(int c=0;c<3;++c){for(int r=0;r<3;++r)lengths[c]+=double(m[c*4+r])*m[c*4+r];lengths[c]=std::sqrt(lengths[c]);}
  const double size=(lengths[0]+lengths[1]+lengths[2])/3;
  if(!std::isfinite(size)||size<1.0/1024||size>1024)return false;
  for(double length:lengths)if(std::abs(length-size)>size*1e-5)return false;
  Matrix normalized;std::copy(m,m+16,normalized.begin());
  for(int c=0;c<3;++c)for(int r=0;r<3;++r)normalized[c*4+r]=float(m[c*4+r]/size);
  for(int c=0;c<3;++c)for(int d=c+1;d<3;++d) {
    double dot=0;for(int r=0;r<3;++r)dot+=double(normalized[c*4+r])*normalized[d*4+r];
    if(std::abs(dot)>1e-5)return false;
  }
  if(!ReadRigid(normalized.data(),out))return false;
  scale=float(size);return true;
}
inline bool SameModelScale(float a,float b) {
  return std::isfinite(a)&&std::isfinite(b)&&a>0&&b>0&&std::abs(double(a)-b)<=std::max(double(a),double(b))*1e-5;
}
inline Matrix ToMatrix(const Pose& p) {
  const auto& q=p.rotation; const double x=q[0],y=q[1],z=q[2],w=q[3]; Matrix m=Identity();
  m[0]=float(1-2*(y*y+z*z));m[1]=float(2*(x*y+w*z));m[2]=float(2*(x*z-w*y));
  m[4]=float(2*(x*y-w*z));m[5]=float(1-2*(x*x+z*z));m[6]=float(2*(y*z+w*x));
  m[8]=float(2*(x*z+w*y));m[9]=float(2*(y*z-w*x));m[10]=float(1-2*(x*x+y*y));
  for(int i=0;i<3;++i)m[12+i]=float(p.position[i]);return m;
}
inline Matrix Inverse(const float* m) {
  Matrix out=Identity();for(int c=0;c<3;++c)for(int r=0;r<3;++r)out[c*4+r]=m[r*4+c];
  for(int r=0;r<3;++r)out[12+r]=-(out[r]*m[12]+out[4+r]*m[13]+out[8+r]*m[14]);return out;
}
inline Pose Blend(const Pose& a,const Pose& b,double alpha) {
  if(alpha<=0)return a;if(alpha>=1)return b;
  Pose out;auto q=b.rotation;double dot=0;for(int i=0;i<4;++i)dot+=a.rotation[i]*q[i];
  if(dot<0){dot=-dot;for(double& x:q)x=-x;}dot=std::clamp(dot,0.0,1.0);
  double wa=1-alpha,wb=alpha;if(dot<.9995){const double angle=std::acos(dot),s=std::sin(angle);wa=std::sin((1-alpha)*angle)/s;wb=std::sin(alpha*angle)/s;}
  double norm=0;for(int i=0;i<4;++i){out.rotation[i]=wa*a.rotation[i]+wb*q[i];norm+=out.rotation[i]*out.rotation[i];}
  for(double& x:out.rotation)x/=std::sqrt(norm);
  for(int i=0;i<3;++i)out.position[i]=(1-alpha)*a.position[i]+alpha*b.position[i];return out;
}
inline bool Nearby(const Pose& a,const Pose& b,double maxDistance,double maxDegrees) {
  double distance=0,dot=0;for(int i=0;i<3;++i){double d=a.position[i]-b.position[i];distance+=d*d;}
  for(int i=0;i<4;++i)dot+=a.rotation[i]*b.rotation[i];
  return std::isfinite(distance)&&distance<=maxDistance*maxDistance&&std::abs(dot)>=std::cos(maxDegrees*3.14159265358979323846/360.0);
}
// ROM assets can be submitted by several cars or repeated by the native draw
// list. Source identity alone cannot identify an instance. Accept a repeated
// asset only when its bounded motion neighborhood identifies one pose cohort
// in BOTH scenes; crossing/overlapping neighborhoods use the native transform.
// An identical pose/material repeated by a draw pass is one cohort, not a new
// entity. Scene-node addresses and traversal order are deliberately irrelevant.
constexpr size_t MaxSourceCopies = 32;
constexpr size_t NoPoseMatch = std::numeric_limits<size_t>::max();
inline bool SamePoseCohort(const Pose& a,const Pose& b) {
  if(a.position!=b.position)return false;
  if(a.rotation==b.rotation)return true;
  // q and -q describe the same rotation. Within one native scene, exact
  // repeated draw matrices yield identical poses. Do not merge near poses:
  // tolerance chains can otherwise make association depend on draw order.
  for(size_t i=0;i<4;++i)if(a.rotation[i]!=-b.rotation[i])return false;
  return true;
}
template<class CurrentAt,class PreviousAt>
size_t FindMutualPoseMatch(size_t index,size_t currentCount,size_t previousCount,
                          CurrentAt currentAt,PreviousAt previousAt) {
  if(index>=currentCount||!currentCount||!previousCount||
     currentCount>MaxSourceCopies||previousCount>MaxSourceCopies)return NoPoseMatch;
  const auto& current=currentAt(index);
  if(!current.rigid)return NoPoseMatch;
  if(currentCount==1&&previousCount==1) {
    const auto& previous=previousAt(0);
    return previous.rigid&&previous.signature==current.signature&&SameModelScale(previous.scale,current.scale)&&Nearby(previous.pose,current.pose,16.0,30.0)?0:NoPoseMatch;
  }
  size_t selected=NoPoseMatch;
  for(size_t i=0;i<previousCount;++i) {
    const auto& previous=previousAt(i);
    if(!previous.rigid||previous.signature!=current.signature||!SameModelScale(previous.scale,current.scale)||
       !Nearby(previous.pose,current.pose,16.0,30.0))continue;
    if(selected!=NoPoseMatch&&!SamePoseCohort(previousAt(selected).pose,previous.pose))return NoPoseMatch;
    if(selected==NoPoseMatch)selected=i;
  }
  if(selected==NoPoseMatch)return NoPoseMatch;
  const auto& previous=previousAt(selected);
  for(size_t i=0;i<currentCount;++i) {
    const auto& other=currentAt(i);
    if(other.rigid&&other.signature==previous.signature&&SameModelScale(other.scale,previous.scale)&&
       Nearby(previous.pose,other.pose,16.0,30.0)&&!SamePoseCohort(current.pose,other.pose))return NoPoseMatch;
  }
  return selected;
}
// Camera is represented as world-space camera POSE. Head/IPD remains an
// external left multiplier and is never part of either interpolated endpoint.
inline Matrix CameraCorrection(const Pose& previous,const Pose& current,double alpha,const float* inverseCurrentGameCamera) {
  const auto camera=ToMatrix(Blend(previous,current,alpha));const auto view=Inverse(camera.data());
  return Multiply(view.data(),inverseCurrentGameCamera);
}
inline Matrix ObjectModelView(const Pose& previous,const Pose& current,double alpha,const float* currentGameCamera,float modelScale=1) {
  auto world=ToMatrix(Blend(previous,current,alpha));
  if(modelScale!=1)for(int c=0;c<3;++c)for(int r=0;r<3;++r)world[c*4+r]*=modelScale;
  return Multiply(currentGameCamera,world.data());
}

enum class SampleReason : int64_t {
  Advanced = 0, Seeded = 1, SceneDiscontinuity = 2, MissingDisplayTime = 3,
  NonIncreasingDisplayTime = 4, DisplayGap = 5, InvalidLocalClock = 6,
  ClampedPrevious = 7, ClampedCurrent = 8, EyeTimeMismatch = 9
};
struct Sample {
  double alpha=1;
  int64_t sceneTime=0;
  bool continuous=false;
  int64_t displayDelta=0;
  SampleReason reason=SampleReason::SceneDiscontinuity;
};
class Clock {
public:
  void Reset(){*this=Clock();}
  bool Publish(uint64_t frame,uint64_t epoch,int64_t now,bool compatible) {
    if(frame>uint64_t(std::numeric_limits<int64_t>::max()/IntervalNs)-1||now<=0){Reset();return false;}
    if(m_have&&frame==m_frame&&epoch==m_epoch)return m_continuous;
    const int64_t boundary=int64_t(frame+1)*IntervalNs;
    m_continuous=compatible&&m_have&&epoch==m_epoch&&frame==m_frame+1&&now>=m_published&&now-m_published<=4*IntervalNs;
    if(!m_continuous)ClearDisplay();
    if(!m_continuous||now<m_originTime||m_originBoundary+(now-m_originTime)<boundary){m_originBoundary=boundary;m_originTime=now;}
    m_frame=frame;m_epoch=epoch;m_boundary=boundary;m_published=now;m_have=true;return m_continuous;
  }
  Sample At(int64_t now) {
    Sample out;out.sceneTime=m_boundary;
    if(!m_have||now<m_published||now<m_lastSample){m_continuous=false;return out;}
    m_lastSample=now;out.continuous=m_continuous;
    if(m_continuous){const int64_t elapsed=std::max<int64_t>(0,m_originBoundary+(now-m_originTime)-m_boundary);out.alpha=std::min(1.0,double(elapsed)/IntervalNs);out.sceneTime=m_boundary-IntervalNs+std::min(elapsed,IntervalNs);}
    return out;
  }
  // The runtime clock has its own opaque epoch. Only positive consecutive
  // differences enter the scene timeline; CPU publication/sample times remain
  // in their own domain. Uniform display periods therefore produce uniform
  // world steps even when owner-thread rendering starts in uneven bursts.
  // The two available native endpoints are hard bounds: never extrapolate,
  // wait for the worker, or accumulate an unbounded playback backlog.
  Sample AtDisplay(int64_t now,int64_t displayTime,bool matchingEyes=true) {
    Sample out;out.sceneTime=m_boundary;
    auto fail=[&](SampleReason reason) { ClearDisplay();out.reason=reason;return out; };
    if(!matchingEyes)return fail(SampleReason::EyeTimeMismatch);
    if(displayTime<=0)return fail(SampleReason::MissingDisplayTime);
    if(!m_have||now<=0||now<m_published||now<m_lastSample)return fail(SampleReason::InvalidLocalClock);
    m_lastSample=now;
    if(m_haveDisplay) {
      if(displayTime<=m_displayTime)return fail(SampleReason::NonIncreasingDisplayTime);
      out.displayDelta=displayTime-m_displayTime;
      if(out.displayDelta>4*IntervalNs)return fail(SampleReason::DisplayGap);
    }
    if(!m_continuous||!m_haveDisplay) {
      m_haveDisplay=true;m_displayTime=displayTime;m_displaySceneTime=m_boundary;
      out.reason=m_continuous?SampleReason::Seeded:SampleReason::SceneDiscontinuity;return out;
    }
    const int64_t lower=m_boundary-IntervalNs;
    if(m_displaySceneTime>m_boundary)return fail(SampleReason::SceneDiscontinuity);
    // Subtract before adding to avoid overflow near the largest valid scene ID.
    if(out.displayDelta>m_boundary-m_displaySceneTime) {
      out.sceneTime=m_boundary;out.reason=SampleReason::ClampedCurrent;
    } else {
      out.sceneTime=m_displaySceneTime+out.displayDelta;
      if(out.sceneTime<lower){out.sceneTime=lower;out.reason=SampleReason::ClampedPrevious;}
      else out.reason=SampleReason::Advanced;
    }
    out.alpha=double(out.sceneTime-lower)/IntervalNs;out.continuous=true;
    m_displayTime=displayTime;m_displaySceneTime=out.sceneTime;
    return out;
  }
private:
  void ClearDisplay(){m_haveDisplay=false;m_displayTime=m_displaySceneTime=0;}
  bool m_have=false,m_continuous=false;
  bool m_haveDisplay=false;
  uint64_t m_frame=0,m_epoch=0;
  int64_t m_boundary=0,m_originBoundary=0,m_originTime=0,m_published=0,m_lastSample=0;
  int64_t m_displayTime=0,m_displaySceneTime=0;
};
}} // namespace New3D::SceneInterpolation
#endif
