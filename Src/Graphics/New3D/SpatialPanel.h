#ifndef INCLUDED_SPATIAL_PANEL_H
#define INCLUDED_SPATIAL_PANEL_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include "GLSLShader.h"
#include "Mat4.h"

namespace New3D {

// A presentation-only texture plane. The content texture uses premultiplied
// RGBA; neither a native game camera nor emulated state is accessed here.
// Call Destroy() while the renderer's GL context is still current.
class SpatialPanel
{
public:
  // Everything Draw changes. A caller may supply an exact state proven by
  // its own preceding pass instead of synchronously querying the GL driver.
  // Generic/theater callers retain Capture(). Never use a guessed snapshot.
  struct State
  {
    GLint program = 0, vao = 0, viewport[4]{}, scissorBox[4]{};
    GLint activeTexture = GL_TEXTURE0, texture0 = 0;
    GLint srcRGB = GL_SRC_ALPHA, dstRGB = GL_ONE_MINUS_SRC_ALPHA;
    GLint srcAlpha = GL_SRC_ALPHA, dstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint eqRGB = GL_FUNC_ADD, eqAlpha = GL_FUNC_ADD;
    GLboolean colourMask[4]{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depth = GL_FALSE, cull = GL_FALSE, stencil = GL_FALSE;
    GLboolean blend = GL_FALSE, scissor = GL_FALSE;

    static State Capture()
    {
      State state;
      glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
      glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vao);
      glGetIntegerv(GL_VIEWPORT, state.viewport);
      glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox);
      glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
      glActiveTexture(GL_TEXTURE0);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture0);
      glActiveTexture(state.activeTexture);
      glGetIntegerv(GL_BLEND_SRC_RGB, &state.srcRGB);
      glGetIntegerv(GL_BLEND_DST_RGB, &state.dstRGB);
      glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.srcAlpha);
      glGetIntegerv(GL_BLEND_DST_ALPHA, &state.dstAlpha);
      glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.eqRGB);
      glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.eqAlpha);
      glGetBooleanv(GL_COLOR_WRITEMASK, state.colourMask);
      state.depth = glIsEnabled(GL_DEPTH_TEST);
      state.cull = glIsEnabled(GL_CULL_FACE);
      state.stencil = glIsEnabled(GL_STENCIL_TEST);
      state.blend = glIsEnabled(GL_BLEND);
      state.scissor = glIsEnabled(GL_SCISSOR_TEST);
      return state;
    }

    void Restore() const
    {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture0);
      glActiveTexture(activeTexture);
      glBindVertexArray(vao);
      glUseProgram(program);
      glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
      glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
      glColorMask(colourMask[0], colourMask[1], colourMask[2], colourMask[3]);
      glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
      glBlendEquationSeparate(eqRGB, eqAlpha);
      if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
      if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
      if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
      if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
      if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    }

    bool Equals(const State& other) const
    {
      return program == other.program && vao == other.vao &&
        std::equal(viewport, viewport+4, other.viewport) &&
        std::equal(scissorBox, scissorBox+4, other.scissorBox) &&
        std::equal(colourMask, colourMask+4, other.colourMask) &&
        activeTexture == other.activeTexture && texture0 == other.texture0 &&
        srcRGB == other.srcRGB && dstRGB == other.dstRGB &&
        srcAlpha == other.srcAlpha && dstAlpha == other.dstAlpha &&
        eqRGB == other.eqRGB && eqAlpha == other.eqAlpha &&
        depth == other.depth && cull == other.cull && stencil == other.stencil &&
        blend == other.blend && scissor == other.scissor;
    }
  };

  // Explicitly opt-in, off-headset diagnostic. No clocks, allocation, queries,
  // waits or output are added to ordinary rendering when disabled. These are
  // host call durations, never GPU completion or physical headset timings.
  void EnableCpuTiming(bool enabled)
  {
    if (enabled == m_cpuTimingEnabled) return;
    m_cpuTimingEnabled = enabled;
    m_cpuSamples.clear();
    m_cpuDropped = 0;
    if (enabled) m_cpuSamples.reserve(CpuSampleLimit);
  }

  // Called once at diagnostic shutdown by the renderer owner. The test's
  // existing output folder must already exist; never invent another folder.
  bool WriteCpuTimings(const std::string& prefix) const
  {
    if (!m_cpuTimingEnabled || prefix.empty()) return true;
    std::ofstream output(prefix + ".spatial-panel.json", std::ios::trunc);
    if (!output) return false;
    output << std::setprecision(17)
      << "{\"schema\":1,\"scope\":\"Opt-in CPU wall time of SpatialPanel Draw stages; no GPU queries/waits, no physical headset performance claim. Input validation excluded.\","
      << "\"limit\":" << CpuSampleLimit << ",\"dropped\":" << m_cpuDropped
      << ",\"samples\":[";
    for (size_t i = 0; i < m_cpuSamples.size(); ++i) {
      const auto& sample = m_cpuSamples[i];
      output << (i ? "," : "") << "{\"index\":" << i
        << ",\"first_use\":" << (sample.firstUse ? "true" : "false")
        << ",\"known_state\":" << (sample.knownState ? "true" : "false")
        << ",\"success\":" << (sample.success ? "true" : "false")
        << ",\"ready_ms\":" << sample.readyMs
        << ",\"capture_ms\":" << sample.captureMs
        << ",\"setup_draw_ms\":" << sample.setupDrawMs
        << ",\"restore_ms\":" << sample.restoreMs
        << ",\"total_ms\":" << sample.totalMs << "}";
    }
    output << "]}\n";
    return output.good();
  }

  void Destroy()
  {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
    m_shader.UnloadShaders();
    m_failed = false;
  }

  // view contains eye translation followed by the inverse head pose, in the
  // same units as distance. The panel faces the viewer at Z=-distance. Its
  // bounds and UV crop stay fixed while the supplied view changes, so turning
  // away reveals only the caller's background rather than another game view.
  // This method never changes framebuffer bindings or framebuffer contents
  // outside the supplied viewport. Ordinary homogeneous clipping handles a
  // plane crossing the near plane or moving completely behind the viewer.
  bool Draw(GLuint texture, int x, int y, int width, int height,
            const Mat4& view, const Mat4& projection,
            float halfWidth, float halfHeight, float distance, float centerY,
            float u0, float v0, float u1, float v1, float backdrop = 0.0f,
            const State* knownState = nullptr, GLuint readOnlyDepth = 0)
  {
    if (!texture || width <= 0 || height <= 0 ||
        !std::isfinite(halfWidth) || halfWidth <= 0.0f ||
        !std::isfinite(halfHeight) || halfHeight <= 0.0f ||
        !std::isfinite(distance) || distance <= 0.0f ||
        !std::isfinite(centerY) || !std::isfinite(backdrop) ||
        !std::isfinite(u0) || !std::isfinite(v0) ||
        !std::isfinite(u1) || !std::isfinite(v1) ||
        u0 < 0.0f || v0 < 0.0f || u1 > 1.0f || v1 > 1.0f ||
        u0 >= u1 || v0 >= v1) return false;
    for (int i = 0; i < 16; ++i)
      if (!std::isfinite(view[i]) || !std::isfinite(projection[i])) return false;
    const bool timing = m_cpuTimingEnabled;
    CpuSample sample;
    CpuClock::time_point start{}, ready{}, capture{}, drawn{};
    if (timing) { start = CpuClock::now(); sample.firstUse = !m_vao; sample.knownState = knownState != nullptr; }
    if (!EnsureReady()) {
      if (timing) {
        sample.readyMs = sample.totalMs = CpuElapsed(start, CpuClock::now());
        RecordCpuSample(sample);
      }
      return false;
    }
    if (timing) { ready = CpuClock::now(); sample.readyMs = CpuElapsed(start, ready); }

    const State state = knownState ? *knownState : State::Capture();
    GLint previousDepthFunc = GL_LESS;
    GLboolean previousDepthMask = GL_TRUE;
    if (readOnlyDepth) {
      // The caller owns a color-only destination and a same-sized opaque
      // scene depth snapshot. Borrow that renderbuffer only for this draw:
      // no copy, readback, new depth storage or world redraw is required.
      GLint target = 0, attachment = GL_NONE;
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
      if (!target) return false;
      glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attachment);
      if (attachment != GL_NONE) return false; // Never replace caller storage.
      glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
      glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
      glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, readOnlyDepth);
      if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        return false;
      }
    }
    if (timing) { capture = CpuClock::now(); sample.captureMs = CpuElapsed(ready, capture); }

    glViewport(x, y, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
    if (readOnlyDepth) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_GEQUAL); // Same infinite reverse-Z projection as the eye.
      glDepthMask(GL_FALSE);
    } else glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    m_shader.EnableShader();
    glUniformMatrix4fv(m_shader.uniformLoc[0], 1, GL_FALSE, view.currentMatrix);
    glUniformMatrix4fv(m_shader.uniformLoc[1], 1, GL_FALSE, projection.currentMatrix);
    glUniform4f(m_shader.uniformLoc[2], halfWidth, halfHeight, distance, centerY);
    glUniform4f(m_shader.uniformLoc[3], u0, v0, u1, v1);
    glUniform1i(m_shader.uniformLoc[4], 0);
    glUniform1f(m_shader.uniformLoc[5], std::clamp(backdrop, 0.0f, 1.0f));
    glUniform1f(m_shader.uniformLoc[6], readOnlyDepth ? projection[14] : 0.0f);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (timing) { drawn = CpuClock::now(); sample.setupDrawMs = CpuElapsed(capture, drawn); }

    if (readOnlyDepth) {
      glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
      glDepthFunc(previousDepthFunc);
      glDepthMask(previousDepthMask);
    }
    state.Restore();
    if (timing) {
      const auto end = CpuClock::now();
      sample.restoreMs = CpuElapsed(drawn, end);
      sample.totalMs = CpuElapsed(start, end);
      sample.success = true;
      RecordCpuSample(sample);
    }
    return true;
  }

private:
  using CpuClock = std::chrono::steady_clock;
  struct CpuSample {
    double readyMs = 0, captureMs = 0, setupDrawMs = 0, restoreMs = 0, totalMs = 0;
    bool firstUse = false, knownState = false, success = false;
  };
  static constexpr size_t CpuSampleLimit = 4096;
  static double CpuElapsed(CpuClock::time_point before, CpuClock::time_point after)
  {
    return std::chrono::duration<double, std::milli>(after - before).count();
  }
  void RecordCpuSample(const CpuSample& sample)
  {
    if (m_cpuSamples.size() < CpuSampleLimit) m_cpuSamples.push_back(sample);
    else ++m_cpuDropped;
  }

  bool EnsureReady()
  {
    if (m_failed) return false;
    if (m_vao) return true;
    const char* vertex = R"glsl(#version 410 core
      uniform mat4 panelView;
      uniform mat4 panelProjection;
      uniform vec4 panelGeometry;
      uniform vec4 panelUV;
      out vec2 textureUV;
      void main() {
        const vec2 corners[6] = vec2[6](
          vec2(-1,-1), vec2(1,-1), vec2(-1,1),
          vec2(-1,1), vec2(1,-1), vec2(1,1));
        vec2 p = corners[gl_VertexID];
        textureUV = mix(panelUV.xy, panelUV.zw, p * 0.5 + 0.5);
        vec4 position = vec4(p.x * panelGeometry.x,
          p.y * panelGeometry.y + panelGeometry.w, -panelGeometry.z, 1.0);
        gl_Position = panelProjection * panelView * position;
      })glsl";
    const char* fragment = R"glsl(#version 410 core
      uniform sampler2D panelTexture;
      uniform float panelBackdrop;
      uniform float panelReverseNear;
      in vec2 textureUV;
      out vec4 colour;
      void main() {
        vec4 source = texture(panelTexture, textureUV);
        float behind = panelBackdrop * (1.0 - source.a);
        colour = vec4(source.rgb + vec3(0.035, 0.040, 0.050) * behind,
          source.a + behind);
        // Real3D writes near/w explicitly, not OpenGL's default .5+.5*z/w.
        // Match that exact convention only when testing borrowed world depth.
        gl_FragDepth = panelReverseNear > 0.0 ? panelReverseNear * gl_FragCoord.w : gl_FragCoord.z;
      })glsl";
    // GLSLShader logs errors but its return value does not indicate link
    // success. Every uniform is used, so locations validate the linked program.
    m_shader.LoadShaders(vertex, fragment);
    const char* names[] = { "panelView", "panelProjection", "panelGeometry",
                            "panelUV", "panelTexture", "panelBackdrop", "panelReverseNear" };
    for (int i = 0; i < 7; ++i) {
      m_shader.uniformLoc[i] = m_shader.GetUniformLocation(names[i]);
      if (m_shader.uniformLoc[i] < 0) {
        m_shader.UnloadShaders();
        m_failed = true;
        return false;
      }
    }
    glGenVertexArrays(1, &m_vao);
    if (!m_vao) {
      m_shader.UnloadShaders();
      m_failed = true;
      return false;
    }
    return true;
  }

  GLSLShader m_shader;
  GLuint m_vao = 0;
  bool m_failed = false;
  bool m_cpuTimingEnabled = false;
  size_t m_cpuDropped = 0;
  std::vector<CpuSample> m_cpuSamples;
};

} // namespace New3D

#endif
