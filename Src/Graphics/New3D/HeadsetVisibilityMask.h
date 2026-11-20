#ifndef INCLUDED_HEADSET_VISIBILITY_MASK_H
#define INCLUDED_HEADSET_VISIBILITY_MASK_H

#include <GL/glew.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "Graphics/IRender3D.h"
#include "Mat4.h"

namespace New3D {

// GL-owner-only cache. No game state, stencil/depth writes, CPU pixel image,
// headset approximation or unfinished GPU query is used. Destroy explicitly
// while the renderer's context is still current.
class HeadsetVisibilityMask
{
public:
  struct Counters {
    uint64_t builds = 0, cacheHits = 0, empty = 0, invalid = 0, failures = 0;
  } counters;

  // Geometry and final layer composition both use texture unit 2. The world
  // shader rebinds the mask before each geometry pass; this scope restores the
  // caller's unit after the entire world/composite sequence, before HUD work.
  class WorldBinding {
    bool enabled;
    GLint active = 0, texture = 0, sampler = 0;
  public:
    explicit WorldBinding(bool use) : enabled(use) {
      if (!enabled) return;
      glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
      glActiveTexture(GL_TEXTURE2);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
      glGetIntegeri_v(GL_SAMPLER_BINDING, 2, &sampler);
      glActiveTexture(active);
    }
    ~WorldBinding() {
      if (!enabled) return;
      // Preserve the ordinary post-world active unit (0), rather than replacing
      // the renderer's documented contract with the entry active unit.
      GLint current; glGetIntegerv(GL_ACTIVE_TEXTURE, &current);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, GLuint(texture));
      glBindSampler(2, GLuint(sampler));
      glActiveTexture(current);
    }
    GLuint Sampler() const { return GLuint(sampler); }
    WorldBinding(const WorldBinding&) = delete;
    WorldBinding& operator=(const WorldBinding&) = delete;
  };

  GLuint Get(unsigned index, const IRender3D::PresentationEye& eye,
             const Mat4& projection, int targetWidth, int targetHeight)
  {
    if (index >= eyes.size()) return 0;
    const auto& mesh = eye.visibilityMask;
    if (!mesh.generation || !mesh.vertexCount || !mesh.indexCount ||
        !mesh.vertices || !mesh.indices) { ++counters.empty; return 0; }
    Key key;
    key.generation = mesh.generation;
    key.width = targetWidth; key.height = targetHeight;
    key.eyeWidth = eye.width; key.eyeHeight = eye.height;
    key.vertices = mesh.vertexCount; key.indices = mesh.indexCount;
    std::copy(eye.fov, eye.fov + 4, key.fov.begin());
    std::copy(projection.currentMatrix, projection.currentMatrix + 16, key.projection.begin());
    auto& cached = eyes[index];
    // Generation guarantees mesh stability. Failed keys are also remembered:
    // allocation/validation failure cannot become a per-frame retry loop.
    if (cached.attempted && cached.key == key) {
      if (cached.valid) ++counters.cacheHits;
      return cached.valid ? cached.texture : 0;
    }
    cached.key = key; cached.attempted = true; cached.valid = false;
    if (!Valid(eye, projection, targetWidth, targetHeight)) {
      ++counters.invalid; return 0;
    }
    RasterState state;
    // This temporary FBO is not managed by R3DFrameBuffers. Both bindings are
    // restored before its cached layer state can be used again.
    if (!EnsureProgram() || !Raster(cached, eye, projection)) {
      if (cached.framebuffer) glDeleteFramebuffers(1, &cached.framebuffer);
      if (cached.texture) glDeleteTextures(1, &cached.texture);
      cached.framebuffer = cached.texture = 0;
      ++counters.failures; return 0;
    }
    cached.valid = true; ++counters.builds;
    return cached.texture;
  }

  void DestroyTargets() {
    for (auto& eye : eyes) {
      if (eye.framebuffer) glDeleteFramebuffers(1, &eye.framebuffer);
      if (eye.texture) glDeleteTextures(1, &eye.texture);
      eye = Eye{};
    }
  }
  void Destroy() {
    DestroyTargets();
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    program = vao = vbo = ebo = 0;
    programAttempted = false;
  }

  static bool Valid(const IRender3D::PresentationEye& eye, const Mat4& projection,
                    int width, int height) {
    const auto& mesh = eye.visibilityMask;
    if (!mesh.generation || !mesh.vertices || !mesh.indices ||
        mesh.vertexCount < 3 || mesh.vertexCount > IRender3D::MaxVisibilityVertices ||
        mesh.indexCount < 3 || mesh.indexCount > IRender3D::MaxVisibilityIndices || mesh.indexCount % 3 ||
        width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        eye.width <= 0 || eye.height <= 0 || eye.width > width || eye.height > height) return false;
    for (float angle : eye.fov)
      if (!std::isfinite(angle) || std::abs(angle) >= 1.57069632679f) return false;
    if (!(eye.fov[0] < eye.fov[1] && eye.fov[2] < eye.fov[3])) return false;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(projection.currentMatrix[i])) return false;
    for (uint32_t i = 0; i < mesh.vertexCount; ++i) {
      const auto& vertex = mesh.vertices[i];
      if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) return false;
      // Finite source coordinates can still overflow the shader's float clip
      // transform. Reject, never clamp or reinterpret, those runtime meshes.
      for (unsigned row = 0; row < 4; ++row) {
        const double terms[] = { double(projection.currentMatrix[row]) * vertex.x,
          double(projection.currentMatrix[4 + row]) * vertex.y,
          -double(projection.currentMatrix[8 + row]), double(projection.currentMatrix[12 + row]) };
        double clip = 0;
        for (double term : terms) {
          clip += term;
          if (!std::isfinite(term) || std::abs(term) > std::numeric_limits<float>::max() ||
              !std::isfinite(clip) || std::abs(clip) > std::numeric_limits<float>::max()) return false;
        }
        if (!std::isfinite(clip) || std::abs(clip) > std::numeric_limits<float>::max() ||
            (row == 3 && !(clip > 0))) return false;
      }
    }
    for (uint32_t i = 0; i < mesh.indexCount; ++i) if (mesh.indices[i] >= mesh.vertexCount) return false;
    return true;
  }

private:
  struct Key {
    uint64_t generation = 0;
    int width = 0, height = 0, eyeWidth = 0, eyeHeight = 0;
    uint32_t vertices = 0, indices = 0;
    std::array<float, 4> fov{};
    std::array<float, 16> projection{};
    bool operator==(const Key& b) const {
      return generation == b.generation && width == b.width && height == b.height &&
        eyeWidth == b.eyeWidth && eyeHeight == b.eyeHeight && vertices == b.vertices && indices == b.indices &&
        std::memcmp(fov.data(), b.fov.data(), sizeof(float) * fov.size()) == 0 &&
        std::memcmp(projection.data(), b.projection.data(), sizeof(float) * projection.size()) == 0;
    }
  };
  struct Eye { Key key; GLuint texture = 0, framebuffer = 0; bool attempted = false, valid = false; };
  std::array<Eye, 2> eyes;
  GLuint program = 0, vao = 0, vbo = 0, ebo = 0;
  GLint projectionLocation = -1;
  bool programAttempted = false;

  struct RasterState {
    GLint program, vao, array, unpack, active, texture, sampler, read, draw;
    GLint viewport[4], polygon[2];
    GLboolean colour[4], blend;
    static constexpr GLenum caps[] = {GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE,
      GL_SCISSOR_TEST, GL_FRAMEBUFFER_SRGB, GL_DITHER, GL_RASTERIZER_DISCARD,
      GL_COLOR_LOGIC_OP, GL_POLYGON_OFFSET_FILL, GL_SAMPLE_ALPHA_TO_COVERAGE,
      GL_SAMPLE_COVERAGE, GL_SAMPLE_MASK};
    GLboolean enabled[sizeof(caps) / sizeof(caps[0])];
    GLint clipCount = 0;
    std::array<GLboolean, 32> clips{};
    RasterState() {
      glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
      glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array); glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
      glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE2);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture); glGetIntegeri_v(GL_SAMPLER_BINDING, 2, &sampler);
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
      glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_POLYGON_MODE, polygon);
      glGetBooleani_v(GL_COLOR_WRITEMASK, 0, colour); blend = glIsEnabledi(GL_BLEND, 0);
      for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i) enabled[i] = glIsEnabled(caps[i]);
      glGetIntegerv(GL_MAX_CLIP_DISTANCES, &clipCount);
      for (GLint i = 0; i < std::min(clipCount, GLint(clips.size())); ++i) clips[i] = glIsEnabled(GL_CLIP_DISTANCE0 + i);
    }
    ~RasterState() {
      glUseProgram(GLuint(program)); glBindVertexArray(GLuint(vao)); glBindBuffer(GL_ARRAY_BUFFER, GLuint(array));
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(unpack));
      glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read)); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw));
      glViewport(viewport[0], viewport[1], viewport[2], viewport[3]); glPolygonMode(GL_FRONT_AND_BACK, GLenum(polygon[0]));
      glColorMaski(0, colour[0], colour[1], colour[2], colour[3]);
      if (blend) glEnablei(GL_BLEND, 0); else glDisablei(GL_BLEND, 0);
      for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i)
        if (enabled[i]) glEnable(caps[i]); else glDisable(caps[i]);
      for (GLint i = 0; i < std::min(clipCount, GLint(clips.size())); ++i)
        if (clips[i]) glEnable(GL_CLIP_DISTANCE0 + i); else glDisable(GL_CLIP_DISTANCE0 + i);
      glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, GLuint(texture)); glBindSampler(2, GLuint(sampler));
      glActiveTexture(active);
    }
  };

  static GLuint Compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr); glCompileShader(shader);
    GLint okay = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) { glDeleteShader(shader); return 0; }
    return shader;
  }
  bool EnsureProgram() {
    if (programAttempted) return program && vao && vbo && ebo;
    programAttempted = true;
    const char* vertex = R"glsl(#version 410 core
layout(location=0) in vec2 position;
uniform mat4 projection;
void main(){ gl_Position=projection*vec4(position,-1.0,1.0); }
)glsl";
    const char* fragment = R"glsl(#version 410 core
layout(location=0) out float hidden;
void main(){ hidden=1.0; }
)glsl";
    GLuint vs = Compile(GL_VERTEX_SHADER, vertex), fs = Compile(GL_FRAGMENT_SHADER, fragment);
    if (vs && fs) {
      program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
      GLint okay = 0; glGetProgramiv(program, GL_LINK_STATUS, &okay);
      if (!okay) { glDeleteProgram(program); program = 0; }
    }
    if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs);
    if (!program) return false;
    projectionLocation = glGetUniformLocation(program, "projection");
    if (projectionLocation < 0) { glDeleteProgram(program); program = 0; return false; }
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    if (vao && vbo && ebo) return true;
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    program = vao = vbo = ebo = 0;
    return false;
  }
  bool Raster(Eye& cached, const IRender3D::PresentationEye& eye, const Mat4& projection) {
    GLint maximum = 0, clipCount = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum); glGetIntegerv(GL_MAX_CLIP_DISTANCES, &clipCount);
    if (cached.key.width > maximum || cached.key.height > maximum || clipCount < 0 || clipCount > 32) return false;
    if (!cached.texture) glGenTextures(1, &cached.texture);
    if (!cached.framebuffer) glGenFramebuffers(1, &cached.framebuffer);
    if (!cached.texture || !cached.framebuffer) return false;
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, cached.texture); glBindSampler(2, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cached.key.width, cached.key.height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, cached.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cached.texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    for (GLenum cap : RasterState::caps) glDisable(cap);
    glDisablei(GL_BLEND, 0); glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    for (GLint i = 0; i < clipCount; ++i) glDisable(GL_CLIP_DISTANCE0 + i);
    const GLfloat zero[4] = {}; glClearBufferfv(GL_COLOR, 0, zero);
    // Geometry always starts at (0,0) inside the larger shared target; final
    // eye.x/y offsets occur later during output blit and do not belong here.
    glViewport(0, 0, eye.width, eye.height);
    glUseProgram(program); glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, projection.currentMatrix);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(IRender3D::PresentationVisibilityVertex) * eye.visibilityMask.vertexCount,
      eye.visibilityMask.vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(IRender3D::PresentationVisibilityVertex), nullptr);
    glEnableVertexAttribArray(0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * eye.visibilityMask.indexCount,
      eye.visibilityMask.indices, GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, GLsizei(eye.visibilityMask.indexCount), GL_UNSIGNED_INT, nullptr);
    return glGetError() == GL_NO_ERROR;
  }
};

} // namespace New3D
#endif
