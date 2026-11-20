#ifndef INCLUDED_HEADSET_VISIBILITY_MASK_TEST_H
#define INCLUDED_HEADSET_VISIBILITY_MASK_TEST_H

// Included after HeadsetRenderTestDetail. This terminal developer diagnostic
// uses the already prepared ROM scene and production RenderHeadsetEyes entry.
// No emulator clock advances, runtime calls, output images, or GL cache access.
namespace HeadsetVisibilityMaskTest {
using namespace HeadsetRenderTestDetail;
using Eye = IRender3D::PresentationEye;
using Vertex = IRender3D::PresentationVisibilityVertex;
using Mask = IRender3D::PresentationVisibilityMask;
struct Mesh {
  std::array<Vertex, 4> vertices{};
  std::array<uint32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
  uint64_t generation = 0;
  Mask Describe() const {
    Mask mask;
    mask.vertices = vertices.data(); mask.indices = indices.data();
    mask.vertexCount = uint32_t(vertices.size()); mask.indexCount = uint32_t(indices.size());
    mask.generation = generation; return mask;
  }
};
inline Mesh Rectangle(const Eye& eye, float left, float right, uint64_t generation)
{
  Mesh mesh; mesh.generation = generation;
  const float l = std::tan(eye.fov[0]), r = std::tan(eye.fov[1]);
  const float b = std::tan(eye.fov[2]), t = std::tan(eye.fov[3]);
  const float x[] = {left, right, right, left}, y[] = {-1.2f, -1.2f, 1.2f, 1.2f};
  for (unsigned i = 0; i < 4; ++i)
    mesh.vertices[i] = {l + (x[i] + 1) * .5f * (r - l), b + (y[i] + 1) * .5f * (t - b)};
  return mesh;
}
inline bool VisibleEqual(const Image& before, const Image& after, const Eye& eye,
                         const Mesh& mesh, int size)
{
  // Independent view-space -> viewport mapping. Exempt only the actual hidden
  // rectangle plus two pixels for raster edge rounding; all four RGBA channels
  // outside it, including pixels outside a cropped eye, must remain exact.
  const double l = std::tan(eye.fov[0]), r = std::tan(eye.fov[1]);
  const double b = std::tan(eye.fov[2]), t = std::tan(eye.fov[3]);
  double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
  for (const auto& vertex : mesh.vertices) {
    const double x = eye.x + (double(vertex.x) - l) / (r - l) * eye.width;
    const double y = eye.y + (double(vertex.y) - b) / (t - b) * eye.height;
    minX = std::min(minX, x); maxX = std::max(maxX, x);
    minY = std::min(minY, y); maxY = std::max(maxY, y);
  }
  if (before.size() != after.size() || before.size() != size_t(size) * size * 4) return false;
  size_t compared = 0;
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const bool inEye = x >= eye.x && x < eye.x + eye.width && y >= eye.y && y < eye.y + eye.height;
    if (inEye && x + .5 >= minX - 2 && x + .5 <= maxX + 2 &&
        y + .5 >= minY - 2 && y + .5 <= maxY + 2) continue;
    const size_t p = (size_t(y) * size + x) * 4;
    for (unsigned channel = 0; channel < 4; ++channel)
      if (before[p + channel] != after[p + channel]) return false;
    ++compared;
  }
  return compared >= size_t(size) * size / 2;
}
struct Sentinel {
  GLint active = 0, texture = 0, sampler = 0;
  GLuint testTexture = 0, testSampler = 0;
  Sentinel() {
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture); glGetIntegeri_v(GL_SAMPLER_BINDING, 2, &sampler);
    glGenTextures(1, &testTexture); glBindTexture(GL_TEXTURE_2D, testTexture);
    const unsigned char pixel[] = {53, 97, 173, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenSamplers(1, &testSampler);
    glSamplerParameteri(testSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(testSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(testSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(testSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindSampler(2, testSampler); glActiveTexture(active);
  }
  void Bind() const {
    GLint current; glGetIntegerv(GL_ACTIVE_TEXTURE, &current); glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, testTexture); glBindSampler(2, testSampler); glActiveTexture(current);
  }
  bool Intact() const {
    GLint current, actualTexture, actualSampler;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &current); glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &actualTexture); glGetIntegeri_v(GL_SAMPLER_BINDING, 2, &actualSampler);
    glActiveTexture(current);
    return actualTexture == GLint(testTexture) && actualSampler == GLint(testSampler);
  }
  ~Sentinel() {
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    glBindSampler(2, GLuint(sampler)); glActiveTexture(active);
    if (testSampler) glDeleteSamplers(1, &testSampler);
    if (testTexture) glDeleteTextures(1, &testTexture);
  }
};
template<class Require>
inline void Run(IRender3D* renderer, Eye (&eyes)[2], const Target (&targets)[2], int size,
                bool theater, const std::array<Image, 2>& expected, Require require)
{
  struct RestoreEyes {
    Eye (&eyes)[2]; const Eye saved[2];
    explicit RestoreEyes(Eye (&value)[2]) : eyes(value), saved{value[0], value[1]} {}
    ~RestoreEyes() { eyes[0] = saved[0]; eyes[1] = saved[1]; }
  } restore(eyes);
  Sentinel sentinel;
  require(sentinel.testTexture && sentinel.testSampler && glGetError() == GL_NO_ERROR,
          "visibility_diagnostic_unit2_sentinel_created");
  // Synthetic generations live only in this no-runtime terminal diagnostic.
  uint64_t generation = 0x7000000000000000ull;
  unsigned draws = 0;
  auto clearMasks = [&]() { for (auto& eye : eyes) eye.visibilityMask = {}; };
  auto fill = [&]() {
    const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB), scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(17.f / 255, 43.f / 255, 113.f / 255, 71.f / 255);
    for (const auto& target : targets) {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
      glDrawBuffer(GL_COLOR_ATTACHMENT0); glClear(GL_COLOR_BUFFER_BIT);
    }
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST); if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
  };
  auto draw = [&](const std::string& name, bool validMask) {
    fill(); sentinel.Bind();
    require(renderer->RenderHeadsetEyes(eyes, 16), name + "_production_draw"); ++draws;
    if (validMask) require(sentinel.Intact(), name + "_unit2_texture_and_sampler_restored");
    require(glGetError() == GL_NO_ERROR, name + "_no_gl_error");
    std::array<Image, 2> image{{Read(targets[0], size), Read(targets[1], size)}};
    require(glGetError() == GL_NO_ERROR, name + "_read_rgba"); return image;
  };
  auto exact = [&](const std::array<Image, 2>& a, const std::array<Image, 2>& b, const std::string& name) {
    require(a[0] == b[0] && a[1] == b[1], name + "_both_eyes_exact_rgba");
  };
  for (unsigned mapping = 0; mapping < 4; ++mapping) {
    const std::string name = std::string("visibility_") +
      (mapping == 0 ? "direct" : mapping == 1 ? "copy" : mapping == 2 ? "cropped" : "asymmetric_size");
    for (unsigned eye = 0; eye < 2; ++eye) {
      eyes[eye] = restore.saved[eye]; eyes[eye].visibilityMask = {};
      if (mapping == 1) eyes[eye].directColorTarget = false;
      if (mapping == 2 || (mapping == 3 && eye == 1)) {
        eyes[eye].x = mapping == 2 ? 11 : 0; eyes[eye].y = mapping == 2 ? 13 : 0;
        eyes[eye].width = size - 22; eyes[eye].height = size - 26;
      }
    }
    const auto baseline = draw(name + "_baseline", false);
    if (mapping == 0) exact(baseline, expected, "visibility_initial_ordinary_baseline");
    std::array<Mesh, 2> edge;
    for (unsigned eye = 0; eye < 2; ++eye) {
      edge[eye] = Rectangle(eyes[eye], -1.2f, -.5f, ++generation);
      eyes[eye].visibilityMask = edge[eye].Describe();
    }
    const auto masked = draw(name + "_edge25", true);
    for (unsigned eye = 0; eye < 2; ++eye)
      require(VisibleEqual(baseline[eye], masked[eye], eyes[eye], edge[eye], size),
              name + "_visible_rgba_exact_two_pixel_edge_allowance_eye_" + std::to_string(eye));
    if (theater) exact(masked, baseline, name + "_theater_edge_ignored");
    if (mapping == 0) {
      exact(draw(name + "_cache_repeat", true), masked, name + "_cache_repeat");
      for (unsigned eye = 0; eye < 2; ++eye) {
        edge[eye].generation = ++generation; eyes[eye].visibilityMask = edge[eye].Describe();
      }
      exact(draw(name + "_generation_only", true), masked, name + "_generation_only");
      for (unsigned eye = 0; eye < 2; ++eye) {
        edge[eye] = Rectangle(eyes[eye], .5f, 1.2f, ++generation);
        eyes[eye].visibilityMask = edge[eye].Describe();
      }
      const auto changed = draw(name + "_changed_mesh_and_generation", true);
      for (unsigned eye = 0; eye < 2; ++eye)
        require(VisibleEqual(baseline[eye], changed[eye], eyes[eye], edge[eye], size),
                name + "_changed_mesh_visible_exact_eye_" + std::to_string(eye));
      // Same stable view-space geometry/generation, changed lens. Compare the
      // cache-invalidation result with an independently forced new generation.
      for (auto& eye : eyes) { eye.fov[0] -= .11f; eye.fov[1] += .06f; }
      clearMasks(); const auto lensBaseline = draw(name + "_changed_fov_baseline", false);
      for (unsigned eye = 0; eye < 2; ++eye) eyes[eye].visibilityMask = edge[eye].Describe();
      const auto lensCached = draw(name + "_changed_fov_same_generation", true);
      for (unsigned eye = 0; eye < 2; ++eye) {
        require(VisibleEqual(lensBaseline[eye], lensCached[eye], eyes[eye], edge[eye], size),
                name + "_changed_fov_visible_exact_eye_" + std::to_string(eye));
        edge[eye].generation = ++generation; eyes[eye].visibilityMask = edge[eye].Describe();
      }
      exact(draw(name + "_changed_fov_forced_generation", true), lensCached, name + "_fov_cache_matches_rebuild");
      for (unsigned eye = 0; eye < 2; ++eye) eyes[eye] = restore.saved[eye];
    }
    std::array<Mesh, 2> full;
    for (unsigned eye = 0; eye < 2; ++eye) {
      full[eye] = Rectangle(eyes[eye], -1.2f, 1.2f, ++generation);
      eyes[eye].visibilityMask = full[eye].Describe();
    }
    const auto hidden = draw(name + "_full_hidden", true);
    if (theater) exact(hidden, baseline, name + "_theater_full_mask_ignored");
    else require(Difference(baseline[0], hidden[0]) >= 64 && Difference(baseline[1], hidden[1]) >= 64,
                 name + "_full_mask_changes_at_least64_world_pixels_each_eye");
    clearMasks(); exact(draw(name + "_cleared", false), baseline, name + "_clear_restores_full_image");
    if (mapping == 0) {
      for (unsigned invalid = 0; invalid < 12; ++invalid) {
        std::array<Mesh, 2> bad = full;
        for (unsigned eye = 0; eye < 2; ++eye) {
          bad[eye].generation = ++generation;
          if (invalid == 9) bad[eye].indices[0] = 4;
          if (invalid == 10) bad[eye].vertices[0].x = std::numeric_limits<float>::quiet_NaN();
          eyes[eye].visibilityMask = bad[eye].Describe();
          auto& m = eyes[eye].visibilityMask;
          if (invalid == 0) m = {};
          if (invalid == 1) m.vertices = nullptr;
          if (invalid == 2) m.indices = nullptr;
          if (invalid == 3) m.vertexCount = 0;
          if (invalid == 4) m.indexCount = 0;
          if (invalid == 5) m.vertexCount = IRender3D::MaxVisibilityVertices + 1;
          if (invalid == 6) m.indexCount = IRender3D::MaxVisibilityIndices + 3;
          if (invalid == 7) m.vertexCount = 2;
          if (invalid == 8) m.indexCount = 4;
          if (invalid == 11) m.generation = 0;
        }
        const char* labels[] = {"empty", "null_vertices", "null_indices", "zero_vertices", "zero_indices",
          "vertex_cap", "index_cap", "short_vertices", "nontriangular_count", "bad_index", "nonfinite", "generation_zero"};
        exact(draw(name + "_invalid_" + labels[invalid], false), baseline,
              name + "_invalid_" + labels[invalid] + "_full_render_fallback");
      }
      clearMasks();
    }
  }
  for (unsigned eye = 0; eye < 2; ++eye) eyes[eye] = restore.saved[eye];
  clearMasks(); exact(draw("visibility_final_ordinary_restore", false), expected,
                      "visibility_final_asymmetric_images_preserved");
  require(draws == 35, "visibility_diagnostic_completed35_bounded_production_pairs");
}
}
#endif
