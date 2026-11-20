#ifndef INCLUDED_HEADSET_RENDER_TEST_H
#define INCLUDED_HEADSET_RENDER_TEST_H

// Explicit, bounded developer diagnostic. Uses the production headset draw
// entry point with ordinary GL targets, so no HMD or runtime is required.
#include <GL/glew.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "Graphics/IRender3D.h"

namespace HeadsetRenderTestDetail {
constexpr int Size = 1024;
using Image = std::vector<unsigned char>;
struct Target { GLuint texture = 0, framebuffer = 0; };
struct Check { std::string name; bool passed; };
inline std::string Quote(const std::string& text)
{
  std::string result = "\"";
  for (char c : text) {
    if (c == '\\' || c == '"') result += '\\';
    if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else result += c;
  }
  return result + '"';
}
inline std::array<float, 4> Multiply(const std::array<float, 4>& a, const std::array<float, 4>& b)
{
  return {a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
          a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
          a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3],
          a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2]};
}
inline void Pose(IRender3D::PresentationEye (&eyes)[2], float yaw, float pitch, float roll,
                 float x, float y, float z, float ipd)
{
  constexpr float HalfRadians = 0.00872664625997f;
  yaw *= HalfRadians; pitch *= HalfRadians; roll *= HalfRadians;
  auto q = Multiply(Multiply({0, std::sin(yaw), 0, std::cos(yaw)},
                             {std::sin(pitch), 0, 0, std::cos(pitch)}),
                             {0, 0, std::sin(roll), std::cos(roll)});
  const float right[3] = {1 - 2*(q[1]*q[1] + q[2]*q[2]),
                         2*(q[0]*q[1] + q[2]*q[3]), 2*(q[0]*q[2] - q[1]*q[3])};
  for (int eye = 0; eye < 2; ++eye) {
    std::copy(q.begin(), q.end(), eyes[eye].pose.orientation);
    const float side = (eye ? 0.5f : -0.5f) * ipd;
    eyes[eye].pose.position[0] = x + right[0]*side;
    eyes[eye].pose.position[1] = y + right[1]*side;
    eyes[eye].pose.position[2] = z + right[2]*side;
  }
}
inline Image Read(const Target& target, int size = Size)
{
  Image image(size*size*4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
  return image;
}
inline bool SaveBMP(const std::string& file, const Image& input, int size = Size)
{
  Image flipped(input.size());
  for (int y = 0; y < size; ++y)
    std::copy_n(input.data() + (size-1-y)*size*4, size*4, flipped.data() + y*size*4);
  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), size, size, 32, size*4, SDL_PIXELFORMAT_RGBA32);
  if (!surface) return false;
  const bool ok = SDL_SaveBMP(surface, file.c_str()) == 0;
  SDL_FreeSurface(surface);
  return ok;
}
inline size_t Difference(const Image& a, const Image& b)
{
  size_t result = 0;
  for (size_t i = 0; i < a.size(); i += 4)
    result += a[i] != b[i] || a[i+1] != b[i+1] || a[i+2] != b[i+2];
  return result;
}
inline bool Black(const Image& image)
{
  for (size_t i = 0; i < image.size(); i += 4)
    if (image[i] || image[i+1] || image[i+2]) return false;
  return true;
}
inline bool Opaque(const Image& image)
{
  for (size_t i = 3; i < image.size(); i += 4) if (image[i] != 255) return false;
  return true;
}
inline std::vector<char> Bytes(const std::string& file)
{
  std::ifstream stream(file, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
}
inline bool SameState(const IRender3D::PresentationState& a, const IRender3D::PresentationState& b)
{
  return a.sceneFrame == b.sceneFrame && a.sceneEpoch == b.sceneEpoch &&
    a.verticalOffset == b.verticalOffset && a.vignetteStrength == b.vignetteStrength &&
    a.menuVignetteStrength == b.menuVignetteStrength && a.menuComfortTicks == b.menuComfortTicks;
}
}

#include "HeadsetVisibilityMaskTest.h"

// Frozen-scene throughput diagnostic. Timer results and pixel readbacks are
// collected outside the measured submission loop. This is intentionally not a
// headset/compositor benchmark and never advances emulation or camera comfort.
inline bool RunHeadsetPerformanceDiagnostic(IRender3D* renderer, const std::string& prefix, 
  const std::function<bool(const std::string&)>& saveState, bool expectTheater)
{
  using namespace HeadsetRenderTestDetail;
  constexpr unsigned WarmupPairs = 48, TimedPairs = 240;
  struct Sample { double cpu = 0, gpu = 0; };
  struct Case {
    const char* name;
    int width, height;
    double scale;
    double wall = 0;
    std::vector<Sample> samples;
  };
  std::array<Case, 3> cases = {{
    {"observed_075", 3039, 3099, 0.75, 0, {}},
    {"reduced_050", 2026, 2066, 0.50, 0, {}},
    {"observed_075_repeat", 3039, 3099, 0.75, 0, {}}
  }};
  std::vector<Check> checks;
  std::string failure, gpuName, glVersion;
  Target targets[2];
  std::array<GLuint, TimedPairs> queries{};
  GLint savedDraw, savedRead, savedDrawBuffer, savedReadBuffer, savedViewport[4], savedPack;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedDraw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &savedRead);
  glGetIntegerv(GL_DRAW_BUFFER, &savedDrawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &savedReadBuffer);
  glGetIntegerv(GL_VIEWPORT, savedViewport);
  glGetIntegerv(GL_PACK_ALIGNMENT, &savedPack);
  IRender3D::PresentationState before{}, after{};
  auto require = [&](bool condition, const std::string& name) {
    checks.push_back({name, condition});
    if (!condition) throw std::runtime_error(name);
  };
  auto destroyTargets = [&]() {
    for (auto& target : targets) {
      if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
      if (target.texture) glDeleteTextures(1, &target.texture);
      target = {};
    }
  };
  try {
    require(renderer && !prefix.empty(), "renderer_and_output_available");
    const auto parent = std::filesystem::path(prefix).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    while (glGetError() != GL_NO_ERROR) {}
    const auto* gpuText = glGetString(GL_RENDERER);
    const auto* versionText = glGetString(GL_VERSION);
    gpuName = gpuText ? reinterpret_cast<const char*>(gpuText) : "unknown";
    glVersion = versionText ? reinterpret_cast<const char*>(versionText) : "unknown";
    require(renderer->GetPresentationState(before), "prepared_snapshot_available");
    const float losBefore[4] = {renderer->GetLosValue(0), renderer->GetLosValue(1), renderer->GetLosValue(2), renderer->GetLosValue(3)};
    require(saveState(prefix + ".before.st0"), "save_before");
    const auto beforeBytes = Bytes(prefix + ".before.st0");
    require(!beforeBytes.empty(), "saved_state_not_empty");
    GLint queryBits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &queryBits);
    require(queryBits >= 32 && glGetError() == GL_NO_ERROR, "gpu_elapsed_queries_supported");
    glGenQueries(TimedPairs, queries.data());
    require(glGetError() == GL_NO_ERROR, "gpu_elapsed_queries_allocated");
    const double millisecondsPerTick = 1000.0 / SDL_GetPerformanceFrequency();
    for (auto& item : cases) {
      destroyTargets();
      IRender3D::PresentationEye eyes[2];
      for (int eye = 0; eye < 2; ++eye) {
        glGenTextures(1, &targets[eye].texture);
        glBindTexture(GL_TEXTURE_2D, targets[eye].texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, item.width, item.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &targets[eye].framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, targets[eye].framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[eye].texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          std::string(item.name) + "_eye_" + std::to_string(eye) + "_target_complete");
        eyes[eye].framebuffer = targets[eye].framebuffer;
        eyes[eye].width = item.width; eyes[eye].height = item.height;
        eyes[eye].targetWidth = item.width; eyes[eye].targetHeight = item.height;
        eyes[eye].directColorTarget = true;
        // Fixed diagnostic lens, identical across sizes. Dimensions match the
        // actual PSVR2 request; these angles are not a hardware lens capture.
        eyes[eye].fov[0] = -0.85f; eyes[eye].fov[1] = 0.85f;
        eyes[eye].fov[2] = -0.85f; eyes[eye].fov[3] = 0.85f;
      }
      Pose(eyes, 0, 0, 0, 0, 0, 0, 0.064f);
      bool drawsSucceeded = true;
      for (unsigned n = 0; n < WarmupPairs; ++n)
        drawsSucceeded = renderer->RenderHeadsetEyes(eyes, 16) && drawsSucceeded;
      glFinish();
      require(drawsSucceeded && glGetError() == GL_NO_ERROR, std::string(item.name) + "_warmup_succeeded");
      // Validate that warmup actually produced visible, opaque pixels. No
      // images are saved and all readbacks precede the measured batch.
      for (int eye = 0; eye < 2; ++eye) {
        Image pixels(size_t(item.width) * item.height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, targets[eye].framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, item.width, item.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        require(glGetError() == GL_NO_ERROR && !Black(pixels) && Opaque(pixels),
          std::string(item.name) + "_eye_" + std::to_string(eye) + "_visible_and_opaque");
      }
      item.samples.resize(TimedPairs);
      glFinish();
      const auto batchStart = SDL_GetPerformanceCounter();
      for (unsigned n = 0; n < TimedPairs; ++n) {
        const auto cpuStart = SDL_GetPerformanceCounter();
        glBeginQuery(GL_TIME_ELAPSED, queries[n]);
        const bool drawn = renderer->RenderHeadsetEyes(eyes, 16);
        glEndQuery(GL_TIME_ELAPSED);
        glFlush();
        item.samples[n].cpu = (SDL_GetPerformanceCounter() - cpuStart) * millisecondsPerTick;
        drawsSucceeded = drawn && drawsSucceeded;
      }
      // Drain once, then retrieve completed query results. Never wait for an
      // individual query, read pixels, or advance simulation in the loop above.
      glFinish();
      item.wall = (SDL_GetPerformanceCounter() - batchStart) * millisecondsPerTick;
      for (unsigned n = 0; n < TimedPairs; ++n) {
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(queries[n], GL_QUERY_RESULT, &elapsed);
        item.samples[n].gpu = double(elapsed) / 1000000.0;
      }
      require(drawsSucceeded && glGetError() == GL_NO_ERROR, std::string(item.name) + "_measured_draws_succeeded");
      require(std::all_of(item.samples.begin(), item.samples.end(), [](const Sample& sample) { return sample.gpu > 0; }),
        std::string(item.name) + "_gpu_timings_nonzero");
      require(renderer->GetPresentationState(after) && SameState(before, after),
        std::string(item.name) + "_snapshot_and_comfort_unchanged");
      for (int i = 0; i < 4; ++i)
        require(renderer->GetLosValue(i) == losBefore[i], std::string(item.name) + "_native_los_" + std::to_string(i) + "_unchanged");
    }
    require(saveState(prefix + ".after.st0"), "save_after");
    require(beforeBytes == Bytes(prefix + ".after.st0"), "emulated_save_bytes_unchanged");
    require(renderer->GetPresentationState(after) && SameState(before, after), "final_snapshot_and_comfort_unchanged");
  } catch (const std::exception& error) {
    failure = error.what();
  }
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, savedDraw);
  glDrawBuffer(savedDrawBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, savedRead);
  glReadBuffer(savedReadBuffer);
  glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
  glPixelStorei(GL_PACK_ALIGNMENT, savedPack);
  destroyTargets();
  glDeleteQueries(TimedPairs, queries.data());
  std::ofstream report(prefix + ".json", std::ios::trunc);
  if (!report) return false;
  auto statistics = [&report](std::vector<double> values) {
    if (values.empty()) { report << "null"; return; }
    std::sort(values.begin(), values.end());
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    report << "{\"mean\":" << mean << ",\"p50\":" << values[values.size()/2]
      << ",\"p95\":" << values[std::min(values.size()-1, size_t(std::ceil(values.size()*0.95)-1))]
      << ",\"max\":" << values.back() << "}";
  };
  report << "{\n  \"passed\": " << (failure.empty() ? "true" : "false")
    << ",\n  \"diagnostic\": \"frozen_headset_render_performance\",\n"
    << "  \"headset_or_compositor_test\": false, \"lens\": \"fixed_symmetric_0.85_radian_test_lens\",\n"
    << "  \"gpu\": " << Quote(gpuName) << ", \"gl_version\": " << Quote(glVersion) << ",\n"
    << "  \"expect_theater\": " << (expectTheater ? "true" : "false") << ", \"units_per_metre\":16,\n"
    << "  \"scene_frame\":" << before.sceneFrame << ", \"scene_epoch\":" << before.sceneEpoch << ",\n"
    << "  \"warmup_pairs\":" << WarmupPairs << ", \"timed_pairs\":" << TimedPairs << ",\n"
    << "  \"timing_notes\":\"CPU includes draw submission, timer commands and glFlush; wall includes final GPU drain. Query retrieval, pixel reads, allocation and warmup are outside the measured batch. No emulator or compositor work is included.\",\n"
    << "  \"failure\":" << Quote(failure) << ",\n  \"cases\":[\n";
  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& item = cases[i];
    std::vector<double> cpu, gpu;
    for (const auto& sample : item.samples) { cpu.push_back(sample.cpu); gpu.push_back(sample.gpu); }
    report << (i ? ",\n" : "") << "    {\"name\":" << Quote(item.name) << ",\"width\":" << item.width
      << ",\"height\":" << item.height << ",\"runtime_dimension_scale\":" << item.scale
      << ",\"stereo_pixels\":" << 2ull*item.width*item.height << ",\"batch_wall_ms\":" << item.wall
      << ",\"wall_ms_per_pair\":" << (item.samples.empty() ? 0 : item.wall/item.samples.size()) << ",\"cpu_ms\":";
    statistics(cpu); report << ",\"gpu_ms\":"; statistics(gpu);
    report << ",\"samples\":[";
    for (size_t n = 0; n < item.samples.size(); ++n)
      report << (n ? "," : "") << "[" << item.samples[n].cpu << "," << item.samples[n].gpu << "]";
    report << "]}";
  }
  report << "\n  ],\n  \"checks\":[\n";
  for (size_t i = 0; i < checks.size(); ++i)
    report << (i ? ",\n" : "") << "    {\"name\":" << Quote(checks[i].name)
      << ",\"passed\":" << (checks[i].passed ? "true" : "false") << "}";
  report << "\n  ]\n}\n";
  return failure.empty() && report.good();
}

inline bool RunHeadsetRenderDiagnostic(IRender3D* renderer, const std::string& prefix,
  const std::function<bool(const std::string&)>& saveState, bool expectTheater, bool performanceOnly = false, bool compact = false, bool visibility = false)
{
  if (performanceOnly) return RunHeadsetPerformanceDiagnostic(renderer, prefix, saveState, expectTheater);
  using namespace HeadsetRenderTestDetail;
  // Optional smaller diagnostic artifacts only; normal rendering and the
  // original 1024-eye regression remain unchanged. All poses still run.
  const int Size = compact ? 512 : HeadsetRenderTestDetail::Size;
  if (!renderer || prefix.empty()) return false;
  std::vector<Check> checks;
  std::string failure;
  Target targets[2];
  GLint savedDraw, savedRead, savedDrawBuffer, savedReadBuffer, savedViewport[4], savedPack;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedDraw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &savedRead);
  glGetIntegerv(GL_DRAW_BUFFER, &savedDrawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &savedReadBuffer);
  glGetIntegerv(GL_VIEWPORT, savedViewport);
  glGetIntegerv(GL_PACK_ALIGNMENT, &savedPack);
  IRender3D::PresentationState before{}, after{};
  size_t stereoDifferences = 0, yawDifferences = 0, leanDifferences = 0, rollDifferences = 0;
  auto require = [&](bool condition, const std::string& name) {
    checks.push_back({name, condition});
    if (!condition) throw std::runtime_error(name);
  };
  try {
    const auto parent = std::filesystem::path(prefix).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    // Discard earlier native-frame GL errors; every operation below is checked.
    while (glGetError() != GL_NO_ERROR) {}
    require(renderer->GetPresentationState(before), "prepared_snapshot_available");
    const float losBefore[4] = {renderer->GetLosValue(0), renderer->GetLosValue(1), renderer->GetLosValue(2), renderer->GetLosValue(3)};
    require(saveState(prefix + ".before.st0"), "save_before");
    const auto beforeBytes = Bytes(prefix + ".before.st0");
    require(!beforeBytes.empty(), "saved_state_not_empty");
    IRender3D::PresentationEye eyes[2];
    for (int eye = 0; eye < 2; ++eye) {
      glGenTextures(1, &targets[eye].texture);
      glBindTexture(GL_TEXTURE_2D, targets[eye].texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glGenFramebuffers(1, &targets[eye].framebuffer);
      glBindFramebuffer(GL_FRAMEBUFFER, targets[eye].framebuffer);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[eye].texture, 0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "eye_" + std::to_string(eye) + "_target_complete");
      eyes[eye].framebuffer = targets[eye].framebuffer;
      eyes[eye].width = eyes[eye].height = Size;
      eyes[eye].targetWidth = eyes[eye].targetHeight = Size;
      eyes[eye].directColorTarget = true;
      eyes[eye].fov[0] = -0.85f; eyes[eye].fov[1] = 0.85f;
      eyes[eye].fov[2] = -0.85f; eyes[eye].fov[3] = 0.85f;
    }
    auto draw = [&](const std::string& name, bool capture = true) {
      require(renderer->RenderHeadsetEyes(eyes, 16), name + "_draw_succeeds");
      require(glGetError() == GL_NO_ERROR, name + "_no_gl_errors");
      std::array<Image, 2> result = {Read(targets[0], Size), Read(targets[1], Size)};
      require(glGetError() == GL_NO_ERROR, name + "_read_succeeds");
      // Compare the same frozen scene through both product paths, including
      // all alpha bytes, without retaining another image pair. Captured poses
      // cover theater/immersive scenes, HUD and the active comfort mask; the
      // repeated frozen-motion loop keeps its original bounded workload.
      if (capture) {
        eyes[0].directColorTarget = eyes[1].directColorTarget = false;
        const bool fallback = renderer->RenderHeadsetEyes(eyes, 16);
        eyes[0].directColorTarget = eyes[1].directColorTarget = true;
        require(fallback && glGetError() == GL_NO_ERROR, name + "_copy_fallback_succeeds");
        for (int eye = 0; eye < 2; ++eye)
          require(Read(targets[eye], Size) == result[eye], name + "_direct_and_copy_exact_rgba_eye_" + std::to_string(eye));
        require(glGetError() == GL_NO_ERROR, name + "_copy_comparison_read_succeeds");
      }
      for (int eye = 0; eye < 2; ++eye) {
        require(Opaque(result[eye]), name + "_opaque_eye_" + std::to_string(eye));
        if (capture) require(SaveBMP(prefix + "." + name + (eye ? ".right.bmp" : ".left.bmp"), result[eye], Size), name + "_capture_eye_" + std::to_string(eye));
      }
      require(renderer->GetPresentationState(after) && SameState(before, after), name + "_snapshot_and_comfort_unchanged");
      return result;
    };
    Pose(eyes, 0, 0, 0, 0, 0, 0, 0);
    auto zeroIPD = draw("zero_ipd");
    require(zeroIPD[0] == zeroIPD[1], "identical_eye_poses_and_lenses_produce_identical_pixels");
    require(!Black(zeroIPD[0]), "forward_view_contains_visible_content");

    Pose(eyes, 0, 0, 0, 0, 0, 0, 0.064f);
    auto stereo = draw("stereo");
    stereoDifferences = Difference(stereo[0], stereo[1]);
    require(stereoDifferences > 64, "runtime_ipd_produces_stereo_parallax");
    // Certified attachment extents do not grant permission to clear outside
    // a smaller eye rectangle. Exercise actual product fallback selection,
    // including asymmetric eye sizes, without generating more capture files.
    const IRender3D::PresentationEye fullEyes[2] = {eyes[0], eyes[1]};
    auto rectangleFallback = [&](const std::string& name, int x, int y, bool asymmetricSizes) {
      for (int eye = 0; eye < 2; ++eye) {
        eyes[eye] = fullEyes[eye];
        eyes[eye].x = x; eyes[eye].y = y;
        eyes[eye].width = Size - 22; eyes[eye].height = Size - 26;
      }
      if (asymmetricSizes) eyes[0] = fullEyes[0];
      auto fill = [&]() {
        const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(17.0f/255, 43.0f/255, 113.0f/255, 71.0f/255);
        for (const auto& target : targets) {
          glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
          glDrawBuffer(GL_COLOR_ATTACHMENT0); glClear(GL_COLOR_BUFFER_BIT);
        }
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
      };
      fill();
      require(renderer->RenderHeadsetEyes(eyes, 16), name + "_certified_target_draw_succeeds");
      const std::array<Image,2> certified = {Read(targets[0], Size), Read(targets[1], Size)};
      fill();
      eyes[0].directColorTarget = eyes[1].directColorTarget = false;
      require(renderer->RenderHeadsetEyes(eyes, 16), name + "_forced_copy_draw_succeeds");
      for (int eye = 0; eye < 2; ++eye) {
        require(certified[eye] == Read(targets[eye], Size), name + "_matches_forced_copy_eye_" + std::to_string(eye));
        bool outsideUnchanged = true, insideOpaque = true;
        for (int py = 0; py < Size; ++py) for (int px = 0; px < Size; ++px) {
          const size_t k = size_t(py*Size + px)*4;
          const auto& image = certified[eye];
          const auto& rect = eyes[eye];
          if (px >= rect.x && px < rect.x+rect.width && py >= rect.y && py < rect.y+rect.height)
            insideOpaque = insideOpaque && image[k+3] == 255;
          else outsideUnchanged = outsideUnchanged && image[k] == 17 && image[k+1] == 43 && image[k+2] == 113 && image[k+3] == 71;
        }
        require(outsideUnchanged && insideOpaque, name + "_preserves_outside_rgba_and_opaque_inside_eye_" + std::to_string(eye));
      }
      require(glGetError() == GL_NO_ERROR, name + "_no_gl_errors");
      require(renderer->GetPresentationState(after) && SameState(before, after), name + "_snapshot_unchanged");
    };
    rectangleFallback("offset_eye_rectangle", 11, 13, false);
    rectangleFallback("oversized_attachment", 0, 0, false);
    rectangleFallback("asymmetric_eye_sizes", 0, 0, true);
    eyes[0] = fullEyes[0]; eyes[1] = fullEyes[1];
    const auto restoredTargets = draw("restored_after_rectangle_fallback", false);
    require(restoredTargets[0] == stereo[0] && restoredTargets[1] == stereo[1],
      "rectangle_fallback_restores_exact_full_eye_pixels");
    Pose(eyes, 24, -12, 0, 0, 0, 0, 0.064f);
    auto yaw = draw("yaw_pitch");
    yawDifferences = Difference(yaw[0], stereo[0]);
    require(yawDifferences > 64, "head_rotation_changes_eye_view");
    Pose(eyes, 0, 0, 18, 0, 0, 0, 0.064f);
    auto roll = draw("roll");
    rollDifferences = Difference(roll[0], stereo[0]);
    require(rollDifferences > 64, "head_roll_changes_eye_view");
    Pose(eyes, 0, 0, 0, 0.15f, 0.08f, -0.1f, 0.064f);
    auto lean = draw("lean");
    leanDifferences = Difference(lean[0], stereo[0]);
    require(leanDifferences > 64, "metre_translation_changes_eye_view");

    Pose(eyes, 180, 0, 0, 0, 0, 0, 0.064f);
    auto behind = draw("behind");
    if (expectTheater) require(Black(behind[0]) && Black(behind[1]), "theater_looking_behind_is_exact_black");
    Pose(eyes, 0, 0, 0, 0, 0, 0, 0.064f);
    auto returned = draw("returned");
    require(returned[0] == stereo[0] && returned[1] == stereo[1], "return_to_identical_pose_restores_exact_pixels");

    // A prolonged native-worker pause must not age or mutate the prepared
    // scene. Exercise all six head axes for at least 250 ms without advancing
    // emulation. This is a terminal correctness test, never a timing sample.
    const auto frozenStart = SDL_GetPerformanceCounter();
    for (unsigned n = 0; n < 32; ++n) {
      const float t = float(n + 1) / 32.0f;
      Pose(eyes, 30 * std::sin(t * 4), 12 * std::sin(t * 5), 8 * std::sin(t * 6),
        .12f * std::sin(t * 7), .06f * std::sin(t * 8), .08f * std::sin(t * 9), .064f);
      draw("frozen_pose_" + std::to_string(n), false);
      SDL_Delay(8);
    }
    require(double(SDL_GetPerformanceCounter() - frozenStart) / SDL_GetPerformanceFrequency() >= .250,
      "frozen_scene_test_spans_at_least_250ms");
    Pose(eyes, 0, 0, 0, 0, 0, 0, .064f);
    const auto frozenReturned = draw("frozen_returned", false);
    require(frozenReturned[0] == stereo[0] && frozenReturned[1] == stereo[1],
      "prolonged_frozen_scene_restores_exact_stereo_pixels");

    // Changed runtime FOV must change actual rasterization, even with equal
    // target sizes. The pure geometry test verifies precise ray boundaries.
    eyes[0].fov[0] = -0.95f; eyes[0].fov[1] = 0.70f;
    eyes[1].fov[0] = -0.70f; eyes[1].fov[1] = 0.95f;
    auto asymmetric = draw("asymmetric");
    require(Difference(asymmetric[0], stereo[0]) > 64 && Difference(asymmetric[1], stereo[1]) > 64,
      "asymmetric_runtime_lenses_change_both_eyes");
    // A malformed SECOND eye must reject the pair before touching the first.
    const auto validSecond = eyes[1];
    for (int invalid = 0; invalid < 4; ++invalid) {
      eyes[1] = validSecond;
      if (invalid == 0) eyes[1].fov[0] = std::numeric_limits<float>::quiet_NaN();
      if (invalid == 1) eyes[1].pose.orientation[3] = 0;
      if (invalid == 2) eyes[1].width = 0;
      if (invalid == 3) eyes[1].framebuffer = 0;
      require(!renderer->RenderHeadsetEyes(eyes, 16), "invalid_second_eye_" + std::to_string(invalid) + "_rejected");
      require(Read(targets[0], Size) == asymmetric[0] && Read(targets[1], Size) == asymmetric[1],
        "invalid_second_eye_" + std::to_string(invalid) + "_leaves_both_images_unchanged");
      require(glGetError() == GL_NO_ERROR, "invalid_second_eye_" + std::to_string(invalid) + "_no_gl_errors");
    }
    eyes[1] = validSecond;
    // SyncSnapshots can publish the NEXT native frame's culling flag while
    // presentation still owns the last prepared scene. Neither value may
    // reject or alter either eye of that immutable snapshot.
    // This terminal diagnostic deliberately mutates renderer-only state; its
    // caller must exit after completion instead of advancing native emulation.
    for (bool pendingBlock : {true, false}) {
      renderer->SetBlockCulling(pendingBlock);
      const std::string name = pendingBlock ? "pending_block_culling" : "pending_unblock_culling";
      const auto pending = draw(name, false);
      require(pending[0] == asymmetric[0] && pending[1] == asymmetric[1],
        name + "_preserves_exact_pixels_in_both_eyes");
    }
    if (visibility)
      HeadsetVisibilityMaskTest::Run(renderer, eyes, targets, Size, expectTheater, asymmetric, require);
    for (int i = 0; i < 4; ++i)
      require(renderer->GetLosValue(i) == losBefore[i], "native_los_" + std::to_string(i) + "_unchanged");
    require(saveState(prefix + ".after.st0"), "save_after");
    require(beforeBytes == Bytes(prefix + ".after.st0"), "emulated_save_bytes_unchanged");
    require(renderer->GetPresentationState(after) && SameState(before, after), "final_snapshot_and_comfort_unchanged");
    // Once the next native clock is actually installed, old geometry must no
    // longer be presentable. This guards against fixing the pending-flag gap
    // by accidentally weakening snapshot/resource invalidation generally.
    renderer->SetSceneClock(before.sceneFrame + 1, before.sceneEpoch);
    require(!renderer->GetPresentationState(after), "next_native_clock_invalidates_prepared_snapshot");
    require(!renderer->RenderHeadsetEyes(eyes, 16), "next_native_clock_rejects_old_eye_pair");
    require(Read(targets[0], Size) == asymmetric[0] && Read(targets[1], Size) == asymmetric[1],
      "rejected_old_snapshot_leaves_both_images_unchanged");
    require(glGetError() == GL_NO_ERROR, "next_native_clock_rejection_no_gl_errors");
  } catch (const std::exception& error) {
    failure = error.what();
  }
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, savedDraw);
  glDrawBuffer(savedDrawBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, savedRead);
  glReadBuffer(savedReadBuffer);
  glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
  glPixelStorei(GL_PACK_ALIGNMENT, savedPack);
  for (auto& target : targets) {
    if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
    if (target.texture) glDeleteTextures(1, &target.texture);
  }
  std::ofstream report(prefix + ".json", std::ios::trunc);
  if (!report) return false;
  report << "{\n  \"passed\": " << (failure.empty() ? "true" : "false")
    << ",\n  \"expect_theater\": " << (expectTheater ? "true" : "false")
    << ",\n  \"visibility_diagnostic_requested\": " << (visibility ? "true" : "false")
    << ",\n  \"width\": " << Size << ", \"height\": " << Size << ", \"units_per_metre\": 16,\n"
    << "  \"scene_frame\": " << before.sceneFrame << ", \"scene_epoch\": " << before.sceneEpoch
    << ",\n  \"different_pixels\": {\"stereo\": " << stereoDifferences << ", \"yaw\": " << yawDifferences
    << ", \"roll\": " << rollDifferences << ", \"lean\": " << leanDifferences << "},\n"
    << "  \"failure\": " << Quote(failure) << ",\n  \"checks\": [\n";
  for (size_t i = 0; i < checks.size(); ++i)
    report << (i ? ",\n" : "") << "    {\"name\": " << Quote(checks[i].name)
      << ", \"passed\": " << (checks[i].passed ? "true" : "false") << "}";
  report << "\n  ]\n}\n";
  return failure.empty() && report.good();
}

#endif
