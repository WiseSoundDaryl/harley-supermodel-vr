#ifndef INCLUDED_OWNER_RENDER_TRACE_H
#define INCLUDED_OWNER_RENDER_TRACE_H

#include "LiveFrameTrace.h"

// Additional owner-only CPU wall spans, explicitly enabled separately from
// ordinary live captures. Set SMVR_LIVE_TRACE_RENDER_STAGES=1 before launch;
// the request is sampled once, at the first captured owner call. Disabled
// tracing performs only the existing atomic/TLS gate, with no clock query.
// This adds no GPU queries, barriers, waits, allocations or disk writes.
namespace LiveFrameTrace {
inline bool RenderStagesEnabled()
{
  if (!OwnerEnabled()) return false;
  static const bool requested = [] {
    const char* value = std::getenv("SMVR_LIVE_TRACE_RENDER_STAGES");
    return value && value[0] == '1' && value[1] == '\0';
  }();
  return requested;
}

// Each record uses the native video callback's ownerFrame, not the renderer's
// scene clock (the snapshot may lag). All records use metadata.owner_thread_id.
// RAII emits on exit, including early returns; nested spans must be sorted by
// QPC rather than file order and must not be added to their enclosing spans.
// New3D spans: a=scene frame, b=scene epoch, c/d=stage-specific details.
// Texture queue: a=scene frame,b=epoch,c=upload calls,d=sum(width*height).
// Tile upload: a=planes actually uploaded,b=submitted RGBA bytes,
// c=changed visible rows,d=glTexSubImage2D calls (zero for unchanged planes).
class OwnerRenderScope
{
public:
  explicit OwnerRenderScope(const char* stage, int64_t sceneFrame = 0, int64_t sceneEpoch = 0)
    : m_stage(stage)
  {
    if (!RenderStagesEnabled()) return;
    m_begin = RawNow();
    m_frame = Get().ownerFrame;
    a = sceneFrame; b = sceneEpoch;
  }
  ~OwnerRenderScope() { End(); }
  OwnerRenderScope(const OwnerRenderScope&) = delete;
  OwnerRenderScope& operator=(const OwnerRenderScope&) = delete;
  bool Active() const { return m_begin != 0; }
  void End()
  {
    if (!m_begin) return;
    Event(m_stage, m_begin, RawNow(), m_frame, a, b, c, d);
    m_begin = 0;
  }
  int64_t a = 0, b = 0, c = 0, d = 0;
private:
  const char* m_stage;
  uint64_t m_begin = 0, m_frame = 0;
};
}
#endif
