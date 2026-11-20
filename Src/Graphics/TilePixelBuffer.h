#ifndef INCLUDED_TILE_PIXEL_BUFFER_H
#define INCLUDED_TILE_PIXEL_BUFFER_H

#include <GL/glew.h>
#include <cstdint>
#include <cstring>
#include "TileUploadTracker.h"

// Optional GL-owner upload staging. Every changed plane receives a fresh
// orphaned store containing this same native snapshot, including source stride.
// Ordinary synchronized mapping is deliberate: an allocation failure may leave
// an old store, but mapping never overwrites GPU-owned bytes unsafely.
class TilePixelBuffer
{
public:
  static constexpr GLsizeiptr Bytes = GLsizeiptr(TileUploadTracker::Stride) *
    TileUploadTracker::Height * sizeof(uint32_t);

  TilePixelBuffer() = default;
  TilePixelBuffer(const TilePixelBuffer&) = delete;
  TilePixelBuffer& operator=(const TilePixelBuffer&) = delete;
  ~TilePixelBuffer() { glDeleteBuffers(2, m_buffers); }

  bool Initialize() {
    glGenBuffers(2, m_buffers);
    m_ready = m_buffers[0] != 0 && m_buffers[1] != 0;
    return m_ready;
  }
  bool Ready() const { return m_ready; }
  GLuint Buffer(unsigned layer) const { return m_buffers[layer]; }

  // Leaves this layer's unpack buffer bound, including on a mapping failure.
  // Caller must bind zero for client-pointer fallback and restore the renderer's
  // required zero unpack binding after all transfers. True guarantees the
  // current bytes were copied and unmapped; no previous snapshot is published.
  bool Stage(unsigned layer, const uint32_t* source) {
    if (!m_ready || layer >= 2 || !source) return false;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_buffers[layer]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, Bytes, nullptr, GL_STREAM_DRAW);
    void* destination = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, Bytes, GL_MAP_WRITE_BIT);
    if (!destination) return false;
    std::memcpy(destination, source, size_t(Bytes));
    return glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) == GL_TRUE;
  }

private:
  GLuint m_buffers[2] = {};
  bool m_ready = false;
};

#endif
