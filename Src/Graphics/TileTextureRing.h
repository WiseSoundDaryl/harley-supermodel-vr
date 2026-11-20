#ifndef INCLUDED_TILE_TEXTURE_RING_H
#define INCLUDED_TILE_TEXTURE_RING_H

#include <GL/glew.h>
#include "TileUploadTracker.h"

// GL-owner-only optional storage for headset tile layers. Slot zero borrows
// the renderer's original texture/tracker; only the other two slots are owned.
// Each physical texture has its own exact CPU shadow. Rotating a dirty-row plan
// computed against another texture would leave stale pixels behind.
class TileTextureRing
{
public:
  struct Prepared {
    TileUploadTracker::Plan plan;
    GLuint texture = 0;
    unsigned slot = 0;
  };

  TileTextureRing() = default;
  TileTextureRing(const TileTextureRing&) = delete;
  TileTextureRing& operator=(const TileTextureRing&) = delete;
  ~TileTextureRing() {
    for (unsigned layer = 0; layer < 2; ++layer)
      glDeleteTextures(2, &m_textures[layer][1]);
  }

  // Called once on a newly created helper, after original storage and shadows
  // have been initialized. A failure leaves ownership of originals untouched;
  // destroying this helper retires any partially allocated extra slots.
  bool Initialize(const GLuint original[2], TileUploadTracker baseTrackers[2], GLint filter) {
    for (unsigned layer = 0; layer < 2; ++layer) {
      m_textures[layer][0] = original[layer];
      m_base[layer] = &baseTrackers[layer];
      for (unsigned extra = 0; extra < 2; ++extra)
        if (!m_extra[layer][extra].Reset()) return false;
    }
    bool complete = true;
    for (unsigned layer = 0; layer < 2 && complete; ++layer) {
      glGenTextures(2, &m_textures[layer][1]);
      for (unsigned slot = 1; slot < 3; ++slot) {
        if (!m_textures[layer][slot]) { complete = false; break; }
        glBindTexture(GL_TEXTURE_2D, m_textures[layer][slot]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TileUploadTracker::Width,
          TileUploadTracker::Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        GLint width = 0, height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        if (width != GLint(TileUploadTracker::Width) || height != GLint(TileUploadTracker::Height)) {
          complete = false; break;
        }
      }
    }
    glBindTexture(GL_TEXTURE_2D, original[1]); // Original Init binding contract.
    return complete;
  }

  Prepared Prepare(unsigned layer, const uint32_t* source) const {
    Prepared result;
    result.slot = m_active[layer];
    result.plan = Tracker(layer, result.slot).PlanUpload(source);
    // No new data means no rotation or write to a texture the GPU may be
    // sampling. The first upload also uses the original unused slot directly.
    if (result.plan.rangeCount && m_haveContent[layer]) {
      result.slot = (result.slot + 1) % 3;
      result.plan = Tracker(layer, result.slot).PlanUpload(source);
    }
    result.texture = m_textures[layer][result.slot];
    return result;
  }

  // Publish only after all matching GL subimage calls. A destination that
  // already contains these pixels needs no upload but still becomes active.
  void Commit(unsigned layer, const uint32_t* source, const Prepared& prepared) {
    Tracker(layer, prepared.slot).Commit(source, prepared.plan);
    m_active[layer] = prepared.slot;
    if (source) m_haveContent[layer] = true;
  }
  GLuint OriginalTexture(unsigned layer) const { return m_textures[layer][0]; }

private:
  TileUploadTracker& Tracker(unsigned layer, unsigned slot) {
    return slot ? m_extra[layer][slot - 1] : *m_base[layer];
  }
  const TileUploadTracker& Tracker(unsigned layer, unsigned slot) const {
    return slot ? m_extra[layer][slot - 1] : *m_base[layer];
  }
  GLuint m_textures[2][3] = {};
  TileUploadTracker* m_base[2] = {};
  TileUploadTracker m_extra[2][2];
  unsigned m_active[2] = {};
  bool m_haveContent[2] = {};
};

#endif
