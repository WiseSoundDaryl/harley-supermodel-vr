#ifndef INCLUDED_INSPECTION_GEOMETRY_H
#define INCLUDED_INSPECTION_GEOMETRY_H
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

// Bounded, frozen visual-geometry navigation. This is not the game's collision
// system. Only explicitly retained triangles are considered; missing geometry
// is never treated as a walkable floor. Owner thread only, no renderer/GL calls.
namespace New3D { namespace InspectionGeometry {
using Point = std::array<float, 3>;
using Triangle = std::array<Point, 3>;
inline Point Sub(const Point& a, const Point& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
inline Point Cross(const Point& a, const Point& b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
inline float Dot(const Point& a, const Point& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline bool Finite(const Point& p) { return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]); }
inline bool Height(const Triangle& t, float x, float z, float& y)
{
  const Point u=Sub(t[1],t[0]), v=Sub(t[2],t[0]);
  const float d=u[0]*v[2]-v[0]*u[2];
  if (std::abs(d)<1e-6f) return false;
  const float a=((x-t[0][0])*v[2]-(z-t[0][2])*v[0])/d;
  const float b=(u[0]*(z-t[0][2])-u[2]*(x-t[0][0]))/d;
  if (a<-.0001f||b<-.0001f||a+b>1.0001f) return false;
  // Most retained floor triangles do not contain this query. Keep the exact
  // original barycentric arithmetic/tolerance, but reject those triangles
  // before calculating a 3D normal and square root. No bounding-box epsilon
  // or approximate surface test is introduced.
  const Point n=Cross(u,v);
  const float length=std::sqrt(Dot(n,n));
  if (!std::isfinite(length)||length<1e-5f||std::abs(n[1])<.80f*length) return false;
  y=t[0][1]+a*u[1]+b*v[1];
  return std::isfinite(y);
}
inline bool Ground(const std::vector<Triangle>& triangles, float x, float z,
                   float minimum, float maximum, float& height)
{
  if (!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(minimum)||!std::isfinite(maximum)||minimum>maximum) return false;
  bool found=false;
  for (const auto& triangle:triangles) {
    float y;
    if (Height(triangle,x,z,y)&&y>=minimum&&y<=maximum&&(!found||y>height)) { height=y; found=true; }
  }
  return found;
}
inline bool Intersects(const Triangle& t, const Point& a, const Point& b)
{
  const Point d=Sub(b,a), e1=Sub(t[1],t[0]), e2=Sub(t[2],t[0]), h=Cross(d,e2);
  const float det=Dot(e1,h);
  if (std::abs(det)<1e-7f) return false;
  const Point s=Sub(a,t[0]); const float u=Dot(s,h)/det;
  if (u<0||u>1) return false;
  const Point q=Cross(s,e1); const float v=Dot(d,q)/det;
  if (v<0||u+v>1) return false;
  const float f=Dot(e2,q)/det;
  return f>.0001f&&f<1.0001f;
}
inline bool ClearStep(const std::vector<Triangle>& triangles, const Point& from,
                      const Point& to, float units)
{
  if (!Finite(from)||!Finite(to)||!std::isfinite(units)||units<=0) return false;
  // Check floor support under a small footprint, then sweep several torso
  // rays through known walls/objects. This bounded prototype is not a capsule
  // physics controller and deliberately limits exploration to nearby surfaces.
  for (const Point& offset:std::array<Point,5>{{{0,0,0},{.15f,0,0},{-.15f,0,0},{0,0,.15f},{0,0,-.15f}}}) {
    float ground=0;
    if (!Ground(triangles,to[0]+offset[0]*units,to[2]+offset[2]*units,
                to[1]-.20f*units,to[1]+.20f*units,ground)) return false;
    for (float h:{.35f,.9f,1.4f}) {
      Point a={from[0]+offset[0]*units,from[1]+h*units,from[2]+offset[2]*units};
      Point b={to[0]+offset[0]*units,to[1]+h*units,to[2]+offset[2]*units};
      for (const auto& triangle:triangles) if (Intersects(triangle,a,b)) return false;
    }
  }
  return true;
}
} }
#endif
