#ifndef INCLUDED_HEADSET_WORKER_TEST_H
#define INCLUDED_HEADSET_WORKER_TEST_H

// Bounded, off-headset integration diagnostic for the real Model3 worker wait.
// The fake runtime owns real GL eye targets; only scheduling is simulated.
#include "HeadsetRenderTest.h"
#include "HeadsetPresentationPump.h"
#include "HeadsetWorkerTemporal.h"
#include "Model3/IEmulator.h"
#include <chrono>
#include <iomanip>

class HeadsetWorkerTest
{
  struct Snapshot {
    bool valid = false;
    IRender3D::PresentationState state{};
    std::array<float, 4> los{};
  };
  static Snapshot ReadSnapshot(IRender3D* renderer)
  {
    Snapshot value;
    if (renderer) {
      value.valid = renderer->GetPresentationState(value.state);
      for (int i = 0; i < 4; ++i) value.los[i] = renderer->GetLosValue(i);
    }
    return value;
  }
  static bool SameSnapshot(const Snapshot& a, const Snapshot& b)
  {
    return a.valid == b.valid && a.los == b.los &&
      (!a.valid || HeadsetRenderTestDetail::SameState(a.state, b.state));
  }
  class TestSession {
  public:
    struct StereoSample {
      int64_t beginNs = 0, endNs = 0, predictedNs = 0;
      uint64_t nativeCompleted = 0, sceneFrame = 0, sceneEpoch = 0;
      double drawCallMs = 0;
      HeadsetWorkerTemporal::Pose pose;
      bool drawn = false, snapshotUnchanged = false, stereoGeometryValid = false;
    };
    bool Initialize()
    {
      GLint draw, read, drawBuffer, readBuffer;
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
      glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
      glGetIntegerv(GL_READ_BUFFER, &readBuffer);
      while (glGetError() != GL_NO_ERROR) {}
      for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &m_targets[i].texture);
        glBindTexture(GL_TEXTURE_2D, m_targets[i].texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &m_targets[i].framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_targets[i].framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_targets[i].texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) m_failed = true;
        m_eyes[i].framebuffer = m_targets[i].framebuffer;
        m_eyes[i].width = Width; m_eyes[i].height = Height;
        m_eyes[i].fov[0] = m_eyes[i].fov[2] = -0.85f;
        m_eyes[i].fov[1] = m_eyes[i].fov[3] = 0.85f;
      }
      m_failed = m_failed || glGetError() != GL_NO_ERROR;
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw); glDrawBuffer(drawBuffer);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, read); glReadBuffer(readBuffer);
      m_origin = Clock::now();
      m_samples.reserve(HeadsetWorkerTemporal::MaxStereoSamples);
      return !m_failed;
    }
    bool TryBegin(OpenXRSession::Frame& frame)
    {
      frame = {};
      if (m_failed) return false;
      const int64_t nowNs = NowNs();
      if (!m_readiness.TryBegin(nowNs, m_predictedNs)) return true;
      // Synthetic fixed readiness grid. Skip overdue slots without catch-up
      // draws. This is not xrWaitFrame, compositor pacing or hardware timing.
      frame.begun = frame.render = frame.focused = frame.tracked = true;
      frame.period = HeadsetWorkerTemporal::SyntheticPeriodNs;
      frame.predictedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(m_origin.time_since_epoch()).count() + m_predictedNs;
      m_beginNs = nowNs;
      m_begun = true;
      return true;
    }
    bool End(IRender3D* renderer, bool, int, int)
    {
      if (!m_begun) return !m_failed;
      m_begun = false;
      if (!renderer) return !m_failed;
      const Snapshot before = ReadSnapshot(renderer);
      if (!before.valid) { ++m_emptyFrames; return true; }
      // A continuous six-axis trajectory indexed by the synthetic predicted
      // time. Missed presentations cannot introduce an artificial yaw reset.
      StereoSample sample;
      sample.beginNs = m_beginNs; sample.predictedNs = m_predictedNs;
      sample.nativeCompleted = m_nativeCompleted;
      sample.sceneFrame = before.state.sceneFrame; sample.sceneEpoch = before.state.sceneEpoch;
      sample.pose = HeadsetWorkerTemporal::PoseAt(double(m_predictedNs) / 1e9);
      const auto& p = sample.pose.values;
      HeadsetRenderTestDetail::Pose(m_eyes, float(p[0]), float(p[1]), float(p[2]), float(p[3]), float(p[4]), float(p[5]), 0.064f);
      sample.stereoGeometryValid = ValidStereoGeometry(sample.pose);
      if (!sample.stereoGeometryValid) { ++m_geometryFailures; m_failed = true; }
      const int64_t drawBeginNs = NowNs();
      const bool drawn = renderer->RenderHeadsetEyes(m_eyes, 16);
      glFlush();
      sample.drawCallMs = double(NowNs() - drawBeginNs) / 1e6;
      m_failed = m_failed || !drawn || glGetError() != GL_NO_ERROR;
      sample.snapshotUnchanged = SameSnapshot(before, ReadSnapshot(renderer));
      if (!sample.snapshotUnchanged) {
        ++m_mutations;
        m_failed = true;
      }
      if (drawn) ++m_presents;
      sample.drawn = drawn; sample.endNs = NowNs();
      if (!HeadsetWorkerTemporal::AppendBounded(m_samples, sample, HeadsetWorkerTemporal::MaxStereoSamples)) {
        ++m_droppedSamples; m_failed = true;
      }
      return !m_failed;
    }
    bool Prefetch() { return !m_failed; }
    bool Focused() const { return true; }
    bool Tracked() const { return true; }
    bool WantsExit() const { return false; }
    bool Failed() const { return m_failed; }
    uint64_t Presents() const { return m_presents; }
    uint64_t EmptyFrames() const { return m_emptyFrames; }
    uint64_t Mutations() const { return m_mutations; }
    uint64_t GeometryFailures() const { return m_geometryFailures; }
    uint64_t DroppedSamples() const { return m_droppedSamples; }
    uint64_t SkippedSyntheticSlots() const { return m_readiness.skippedSlots; }
    const std::vector<StereoSample>& Samples() const { return m_samples; }
    void SetNativeCompleted(uint64_t count) { m_nativeCompleted = count; }
    int64_t NowNs() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_origin).count(); }
    const OpenXRSession::Controls& GetControls() const { return m_controls; }
    void Fail() { m_failed = true; }
    void Close()
    {
      for (auto& target : m_targets) {
        if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
        if (target.texture) glDeleteTextures(1, &target.texture);
        target = {};
      }
    }
    static constexpr int Width = 2026, Height = 2066;
  private:
    bool ValidStereoGeometry(const HeadsetWorkerTemporal::Pose& pose) const
    {
      double separationSquared = 0;
      for (int axis = 0; axis < 3; ++axis) {
        const double left = m_eyes[0].pose.position[axis], right = m_eyes[1].pose.position[axis];
        if (!std::isfinite(left) || !std::isfinite(right) || std::abs((left + right)*.5 - pose.values[axis+3]) > 1e-6) return false;
        separationSquared += (left-right)*(left-right);
      }
      if (std::abs(std::sqrt(separationSquared) - .064) > 1e-6) return false;
      for (const auto& eye : m_eyes) {
        double norm = 0;
        for (int axis = 0; axis < 4; ++axis) {
          const double value = eye.pose.orientation[axis];
          if (!std::isfinite(value) || value != m_eyes[0].pose.orientation[axis]) return false;
          norm += value * value;
        }
        if (std::abs(norm - 1) > 1e-5) return false;
      }
      return true;
    }
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_origin{};
    HeadsetWorkerTemporal::Readiness m_readiness;
    std::vector<StereoSample> m_samples;
    int64_t m_beginNs = 0, m_predictedNs = 0;
    HeadsetRenderTestDetail::Target m_targets[2];
    IRender3D::PresentationEye m_eyes[2];
    OpenXRSession::Controls m_controls{};
    uint64_t m_presents = 0, m_mutations = 0, m_emptyFrames = 0;
    uint64_t m_nativeCompleted = 0, m_geometryFailures = 0, m_droppedSamples = 0;
    bool m_begun = false, m_failed = false;
  };
public:
  HeadsetWorkerTest() : m_pump(m_session, m_paused) {}
  bool Initialize(IRender3D* renderer, bool enabled)
  {
    m_renderer = renderer;
    m_enabled = enabled;
    m_pump.SetRenderer(renderer);
    m_frames.reserve(HeadsetWorkerTemporal::MaxNativeSamples);
    m_nativeStamps.reserve(HeadsetWorkerTemporal::MaxNativeSamples);
    return renderer && m_session.Initialize();
  }
  void Install(IEmulator* emulator)
  {
    if (emulator) emulator->SetFrameWaitCallback(m_enabled ? &WorkerWait : nullptr, m_enabled ? this : nullptr);
  }
  void Detach(IEmulator* emulator)
  {
    if (emulator) emulator->SetFrameWaitCallback(nullptr, nullptr);
  }
  // Call once after each completed native RunFrame, outside its worker window.
  void ObserveNativeFrame()
  {
    const int64_t nowNs = m_session.NowNs();
    if (!HeadsetWorkerTemporal::AppendBounded(m_frames, ReadSnapshot(m_renderer), HeadsetWorkerTemporal::MaxNativeSamples) ||
        !HeadsetWorkerTemporal::AppendBounded(m_nativeStamps, nowNs, HeadsetWorkerTemporal::MaxNativeSamples)) {
      ++m_droppedNativeSamples; m_session.Fail();
    }
    m_session.SetNativeCompleted(m_frames.size());
  }
  bool Failed() const { return m_pump.Failed(); }
  void Close() { m_session.Close(); }
  bool Finish(const std::string& prefix, const std::function<bool(const std::string&)>& saveState)
  {
    const auto parent = std::filesystem::path(prefix).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    const bool saved = saveState(prefix + ".final.st0");
    const bool counts = m_enabled ? (m_callbacks > 0 && m_session.Presents() > 0 && m_pump.WorkerPresents() == m_session.Presents()) :
      (m_callbacks == 0 && m_session.Presents() == 0);
    std::vector<int64_t> stereoBegins, stereoEnds;
    std::vector<double> drawCosts;
    uint64_t sceneChanges = 0, poseChanges = 0;
    bool ordered = m_nativeStamps.size() == m_frames.size();
    const auto& samples = m_session.Samples();
    for (size_t i = 0; i < samples.size(); ++i) {
      const auto& sample = samples[i];
      stereoBegins.push_back(sample.beginNs); stereoEnds.push_back(sample.endNs); drawCosts.push_back(sample.drawCallMs);
      ordered = ordered && sample.beginNs >= 0 && sample.endNs >= sample.beginNs && sample.predictedNs > sample.beginNs &&
        sample.nativeCompleted <= m_frames.size();
      if (i) {
        const auto& previous = samples[i-1];
        ordered = ordered && sample.beginNs >= previous.endNs && sample.predictedNs > previous.predictedNs &&
          sample.nativeCompleted >= previous.nativeCompleted;
        sceneChanges += sample.sceneFrame != previous.sceneFrame || sample.sceneEpoch != previous.sceneEpoch;
        poseChanges += sample.pose.values != previous.pose.values;
      }
    }
    const auto nativeIntervals = HeadsetWorkerTemporal::IntervalsMs(m_nativeStamps);
    const auto stereoBeginIntervals = HeadsetWorkerTemporal::IntervalsMs(stereoBegins);
    const auto stereoEndIntervals = HeadsetWorkerTemporal::IntervalsMs(stereoEnds);
    const auto drawDistribution = HeadsetWorkerTemporal::Summarize(drawCosts);
    const bool timingValid = ordered && !nativeIntervals.invalid && !stereoBeginIntervals.invalid && !stereoEndIntervals.invalid && !drawDistribution.invalid;
    const bool passed = saved && !m_frames.empty() && !Failed() && !m_session.Mutations() && counts && timingValid &&
      !m_droppedNativeSamples && !m_session.DroppedSamples() && !m_session.GeometryFailures();
    std::ofstream report(prefix + ".json", std::ios::trunc);
    if (!report) return false;
    report << std::setprecision(17) << "{\n  \"passed\":" << (passed ? "true" : "false")
      << ",\n  \"diagnostic\":\"real_model3_worker_wait_render\",\n"
      << "  \"headset_runtime_test\":false, \"callback_enabled\":" << (m_enabled ? "true" : "false")
      << ",\n  \"width\":" << TestSession::Width << ", \"height\":" << TestSession::Height
      << ", \"target_hz\":120, \"callback_count\":" << m_callbacks << ",\n"
      << "  \"stereo_presents\":" << m_session.Presents() << ", \"worker_presents\":" << m_pump.WorkerPresents()
      << ", \"empty_frames\":" << m_session.EmptyFrames() << ", \"snapshot_mutations\":" << m_session.Mutations() << ",\n"
      << "  \"final_state_saved\":" << (saved ? "true" : "false") << ", \"native_frames\":" << m_frames.size() << ",\n"
      << "  \"frames\":[\n";
    for (size_t i = 0; i < m_frames.size(); ++i) {
      const auto& value = m_frames[i];
      const auto& state = value.state;
      report << (i ? ",\n" : "") << "    {\"valid\":" << (value.valid ? "true" : "false")
        << ",\"scene_frame\":" << state.sceneFrame << ",\"scene_epoch\":" << state.sceneEpoch
        << ",\"vertical_offset\":" << state.verticalOffset << ",\"vignette\":" << state.vignetteStrength
        << ",\"menu_vignette\":" << state.menuVignetteStrength << ",\"menu_ticks\":" << state.menuComfortTicks
        << ",\"los\":[" << value.los[0] << "," << value.los[1] << "," << value.los[2] << "," << value.los[3] << "]}";
    }
    report << "\n  ],\n  \"temporal\":{\n"
      << "    \"version\":1,\"hardware_fps_verified\":false,\"gpu_completion_measured\":false,\"production_admission_budget_enabled\":false,\n"
      << "    \"scope\":\"Unthrottled real native worker and real GL stereo draws; synthetic fixed-grid readiness and continuous six-axis poses. Times are owner-thread observations, not scanout, GPU completion or OpenXR runtime measurements. No outside-worker presentations or production admission budget.\",\n"
      << "    \"pose_clock\":\"synthetic predicted time relative to diagnostic initialization, seconds\",\n"
      << "    \"time_unit\":\"nanoseconds since diagnostic initialization on steady_clock\",\n"
      << "    \"quantile_method\":\"nearest rank\",\"timing_records_valid\":" << (timingValid ? "true" : "false")
      << ",\"native_sample_limit\":" << HeadsetWorkerTemporal::MaxNativeSamples << ",\"stereo_sample_limit\":" << HeadsetWorkerTemporal::MaxStereoSamples
      << ",\"dropped_native_samples\":" << m_droppedNativeSamples << ",\"dropped_stereo_samples\":" << m_session.DroppedSamples() << ",\n"
      << "    \"native_reference_hz\":" << HeadsetWorkerTemporal::NativeHz << ",\"emulated_native_seconds\":" << double(m_frames.size()) / HeadsetWorkerTemporal::NativeHz
      << ",\"native_observation_rate_hz\":" << HeadsetWorkerTemporal::RateHz(m_nativeStamps)
      << ",\"stereo_draw_return_rate_hz\":" << HeadsetWorkerTemporal::RateHz(stereoEnds) << ",\n"
      << "    \"synthetic_readiness_slots_skipped\":" << m_session.SkippedSyntheticSlots()
      << ",\"stereo_scene_changes\":" << sceneChanges << ",\"stereo_pose_changes\":" << poseChanges
      << ",\"stereo_geometry_failures\":" << m_session.GeometryFailures() << ",\n";
    const auto writeDistribution = [&](const char* name, const HeadsetWorkerTemporal::Distribution& value) {
      report << "    \"" << name << "\":{\"count\":" << value.count << ",\"invalid\":" << value.invalid
        << ",\"min\":" << value.minimum << ",\"mean\":" << value.mean << ",\"p50\":" << value.p50
        << ",\"p95\":" << value.p95 << ",\"p99\":" << value.p99 << ",\"max\":" << value.maximum << "},\n";
    };
    writeDistribution("native_observation_intervals_ms", nativeIntervals);
    writeDistribution("stereo_begin_intervals_ms", stereoBeginIntervals);
    writeDistribution("stereo_return_intervals_ms", stereoEndIntervals);
    writeDistribution("gl_stereo_call_and_flush_ms", drawDistribution);
    report << "    \"native_samples\":[\n";
    for (size_t i = 0; i < m_frames.size(); ++i)
      report << (i ? ",\n" : "") << "      {\"completed\":" << i+1 << ",\"observed_ns\":" << m_nativeStamps[i]
        << ",\"valid\":" << (m_frames[i].valid ? "true" : "false") << ",\"scene_frame\":" << m_frames[i].state.sceneFrame
        << ",\"scene_epoch\":" << m_frames[i].state.sceneEpoch << "}";
    report << "\n    ],\n    \"stereo_samples\":[\n";
    for (size_t i = 0; i < samples.size(); ++i) {
      const auto& sample = samples[i];
      report << (i ? ",\n" : "") << "      {\"begin_ns\":" << sample.beginNs << ",\"return_ns\":" << sample.endNs
        << ",\"predicted_ns\":" << sample.predictedNs << ",\"native_completed\":" << sample.nativeCompleted
        << ",\"scene_frame\":" << sample.sceneFrame << ",\"scene_epoch\":" << sample.sceneEpoch
        << ",\"gl_call_ms\":" << sample.drawCallMs << ",\"drawn\":" << (sample.drawn ? "true" : "false")
        << ",\"snapshot_unchanged\":" << (sample.snapshotUnchanged ? "true" : "false")
        << ",\"stereo_geometry_valid\":" << (sample.stereoGeometryValid ? "true" : "false") << ",\"pose_deg_metres\":[";
      for (size_t axis = 0; axis < sample.pose.values.size(); ++axis) report << (axis ? "," : "") << sample.pose.values[axis];
      report << "]}";
    }
    report << "\n    ]\n  }\n}\n";
    return passed && report.good();
  }
private:
  static bool WorkerWait(void* context) noexcept
  {
    auto& self = *static_cast<HeadsetWorkerTest*>(context);
    ++self.m_callbacks;
    try { return HeadsetPresentationPump<TestSession>::WorkerWait(&self.m_pump); }
    catch (...) { self.m_session.Fail(); return false; }
  }
  TestSession m_session;
  bool m_paused = false, m_enabled = false;
  HeadsetPresentationPump<TestSession> m_pump;
  IRender3D* m_renderer = nullptr;
  uint64_t m_callbacks = 0;
  uint64_t m_droppedNativeSamples = 0;
  std::vector<Snapshot> m_frames;
  std::vector<int64_t> m_nativeStamps;
};

#endif
