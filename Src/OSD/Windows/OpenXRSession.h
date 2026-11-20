#ifndef INCLUDED_OPENXR_SESSION_H
#define INCLUDED_OPENXR_SESSION_H
#include <cstdint>
#include <cmath>
#include <memory>

class IRender3D;

// Optional OpenXR runtime, entirely separate from native game state. All calls
// except xrWaitFrame are made on the SDL/OpenGL owner thread. The optional
// timing worker never touches GL, game inputs, or emulator state.
class OpenXRSession
{
public:
  struct Controls {
    float steering = 0, throttle = 0, brake = 0;
    bool select = false, coin = false, view = false, music = false;
    bool pause = false, recenter = false, exitCombo = false;
    bool gearShiftUp = false, gearShiftDown = false;
    bool active = false, leftActive = false, rightActive = false;
    float moveX = 0, moveY = 0, turnX = 0;
    bool handsToggle = false, exploreToggle = false;
    bool grabLeft = false, grabRight = false;
  };
  struct Frame {
    bool begun = false, render = false, focused = false, tracked = false;
    int64_t predictedTime = 0, period = 0;
    // Diagnostic request ID; never used for timing or admission decisions.
    uint64_t traceId = 0;
  };
  struct TimingStats {
    // waitMs measures runtime throttling, not CPU compute. eyeRenderMs is CPU
    // submission time, not completed GPU execution time.
    uint64_t frames = 0, waits = 0, eyeDraws = 0;
    double waitMs = 0, beginMs = 0, swapchainWaitMs = 0;
    double lateLocateMs = 0, eyeRenderMs = 0, endMs = 0;
    double maxWaitMs = 0, maxEyeRenderMs = 0, maxEndMs = 0;
    // Residence after the timing worker's xrWaitFrame returned, before its
    // result is consumed on the owner. Includes all focus states; no claim
    // that a prediction was late for physical display without clock mapping.
    uint64_t readyWaits = 0, readyWaitsOverPeriod = 0, invalidReadyWaitAges = 0;
    double readyWaitResidenceMs = 0, maxReadyWaitResidenceMs = 0;
    void RecordReadyWaitAge(double ageMs, int64_t periodNs)
    {
      if (!std::isfinite(ageMs) || ageMs < 0) { ++invalidReadyWaitAges; return; }
      ++readyWaits; readyWaitResidenceMs += ageMs;
      if (ageMs > maxReadyWaitResidenceMs) maxReadyWaitResidenceMs = ageMs;
      if (periodNs > 0 && ageMs > double(periodNs) / 1e6) ++readyWaitsOverPeriod;
    }
  };
  struct HapticStats {
    // Fixed-size diagnostics, not a per-frame trace. API acceptance does not
    // prove a controller physically vibrated.
    uint64_t nativeSamples = 0, validSamples = 0, allowedSamples = 0;
    uint64_t discontinuities = 0, detectedPulses = 0, deliveredPulses = 0;
    uint64_t expiredPulses = 0, stopEvents = 0;
    uint64_t handAttempts[2]{}, handAccepted[2]{};
  };
#ifdef SUPERMODEL_WIN32
  OpenXRSession();
  ~OpenXRSession();
  // Experimental shared two-layer swapchain; separate chains remain default.
  bool Initialize(float resolutionScale, float unitsPerMeter, bool preferArray = false, bool bodyInteraction = false, bool readyWake = false, bool visibilityMask = false);
  // Actual allocated eye dimensions, for owner-thread renderer allocation
  // before the first native update or headset presentation.
  bool GetRenderTargetSize(int& width, int& height) const;
  bool Begin(Frame& frame);
  // Queue at most one wait after End. TryBegin never waits for frame timing;
  // true with frame.begun=false means the timing worker is not ready yet.
  bool Prefetch();
  bool TryBegin(Frame& frame);
  // Owner-thread borrowed wake hint; valid until Close, never close/reset it
  // here. A signal is not admission: recheck the native deadline and full cost.
  // Null means disabled/unavailable; callers retain their ordinary timer wait.
  void* TimingReadySignal() const;
  bool End(IRender3D* renderer, bool mirror, int mirrorWidth, int mirrorHeight);
  void Recenter();
  // Owner-thread gameplay gate; pause must stop controller motors immediately.
  void SetHapticsPaused(bool paused);
  // Exactly once per completed native RunFrame, on the owner thread. Detection
  // must not depend on the (potentially lower) headset presentation rate.
  void ObserveNativeHaptics(IRender3D* renderer);
  const Controls& GetControls() const;
  bool WantsExit() const;
  bool Failed() const;
  bool Focused() const;
  bool Tracked() const;
  uint64_t Presents() const;
  TimingStats Stats() const;
  HapticStats HapticsStats() const;
  void Close();
private:
  struct Impl;
  std::unique_ptr<Impl> m;
  bool BeginInternal(Frame& frame, bool blocking);
#else
  bool Initialize(float, float, bool = false, bool = false, bool = false, bool = false) { return false; }
  bool GetRenderTargetSize(int& width, int& height) const { width = height = 0; return false; }
  bool Begin(Frame&) { return false; }
  bool Prefetch() { return false; }
  bool TryBegin(Frame&) { return false; }
  void* TimingReadySignal() const { return nullptr; }
  bool End(IRender3D*, bool, int, int) { return false; }
  void Recenter() {}
  void SetHapticsPaused(bool) {}
  void ObserveNativeHaptics(IRender3D*) {}
  const Controls& GetControls() const { static Controls value; return value; }
  bool WantsExit() const { return true; }
  bool Failed() const { return true; }
  bool Focused() const { return false; }
  bool Tracked() const { return false; }
  uint64_t Presents() const { return 0; }
  TimingStats Stats() const { return {}; }
  HapticStats HapticsStats() const { return {}; }
  void Close() {}
#endif
};
#endif
