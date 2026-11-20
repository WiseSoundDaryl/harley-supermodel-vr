#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <windows.h>
#include <unknwn.h>
#include <GL/glew.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "OpenXRSession.h"
#include "OpenXRInput.h"
#include "OpenXRTimingReadySignal.h"
#include "Graphics/IRender3D.h"
#include "OSD/Logger.h"
#include "OSD/SDL/LiveFrameTrace.h"
#include "OSD/SDL/HarleyHaptics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <limits>
#include <new>

namespace {
using XRClock = std::chrono::steady_clock;
double ElapsedMs(XRClock::time_point start)
{ return std::chrono::duration<double, std::milli>(XRClock::now() - start).count(); }
int64_t TraceFloatPair(float low, float high)
{
  static_assert(sizeof(float) == sizeof(uint32_t), "Trace uses float32 bit pairs");
  uint32_t lo = 0, hi = 0;
  std::memcpy(&lo, &low, sizeof(lo)); std::memcpy(&hi, &high, sizeof(hi));
  const uint64_t bits = uint64_t(lo) | (uint64_t(hi) << 32);
  int64_t result = 0; std::memcpy(&result, &bits, sizeof(result));
  return result;
}

// BEGIN bounded optional visibility policy (also compiled by the mock fixture).
enum class VisibilityResult { Ready, Empty, Unavailable, Invalid, Exhausted };
bool VisibilityExtensionAvailable(bool requested, const std::vector<XrExtensionProperties>& extensions)
{
  if (!requested) return false;
  for (const auto& extension : extensions)
    if (std::strcmp(extension.extensionName, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME) == 0) return true;
  return false;
}
template<class Create>
XrResult CreateOptionalVisibilityInstance(XrInstanceCreateInfo& create, bool& enabled,
  XrInstance& instance, bool& retried, Create call)
{
  retried = false; create.enabledExtensionCount = enabled ? 2 : 1;
  XrResult result = call(&create, &instance);
  if (XR_FAILED(result) && enabled) {
    // A failed call does not return a valid instance to destroy.
    enabled = false; instance = XR_NULL_HANDLE; retried = true; create.enabledExtensionCount = 1;
    result = call(&create, &instance);
  }
  return result;
}
template<class GetProc>
bool ResolveVisibilityFunction(bool enabled, PFN_xrGetVisibilityMaskKHR& function, GetProc call)
{
  function = nullptr;
  if (!enabled) return false;
  if (call("xrGetVisibilityMaskKHR", reinterpret_cast<PFN_xrVoidFunction*>(&function)) != XR_SUCCESS || !function) {
    function = nullptr; return false;
  }
  return true;
}
struct VisibilityData
{
  std::vector<IRender3D::PresentationVisibilityVertex> vertices;
  std::vector<uint32_t> indices;
  uint64_t generation = 0;
  void Clear() { generation = 0; vertices.clear(); indices.clear(); }
  IRender3D::PresentationVisibilityMask Describe() const
  {
    IRender3D::PresentationVisibilityMask result;
    if (generation && !vertices.empty() && !indices.empty()) {
      result.vertices = vertices.data(); result.indices = indices.data();
      result.vertexCount = uint32_t(vertices.size()); result.indexCount = uint32_t(indices.size());
      result.generation = generation;
    }
    return result;
  }
};
uint64_t NextVisibilityGeneration(std::atomic<uint64_t>& serial)
{
  // Saturate instead of wrapping: a cache identity is never reused, even after
  // Close constructs a new Impl or another session is opened in this process.
  uint64_t previous = serial.load(std::memory_order_relaxed);
  while (previous != std::numeric_limits<uint64_t>::max()) {
    if (serial.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) return previous + 1;
  }
  return 0;
}
bool VisibilityCountsValid(uint32_t vertices, uint32_t indices)
{
  return vertices >= 3 && vertices <= IRender3D::MaxVisibilityVertices &&
    indices >= 3 && indices <= IRender3D::MaxVisibilityIndices && indices % 3 == 0;
}
template<class Query, class Generation>
VisibilityResult RefreshVisibility(VisibilityData& destination, Query query, Generation nextGeneration)
{
  // Never keep an old occlusion mask after an event reports it changed.
  destination.Clear();
  try {
    // One fresh two-call enumeration, with at most one restart if the runtime's
    // required capacity changes. There is no per-frame retry or allocation.
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      XrVisibilityMaskKHR counts{XR_TYPE_VISIBILITY_MASK_KHR};
      if (query(&counts) != XR_SUCCESS) return VisibilityResult::Unavailable;
      if (!counts.vertexCountOutput && !counts.indexCountOutput) return VisibilityResult::Empty;
      if (!VisibilityCountsValid(counts.vertexCountOutput, counts.indexCountOutput)) return VisibilityResult::Invalid;
      std::vector<XrVector2f> runtimeVertices(counts.vertexCountOutput);
      std::vector<uint32_t> indices(counts.indexCountOutput);
      XrVisibilityMaskKHR data{XR_TYPE_VISIBILITY_MASK_KHR};
      data.vertexCapacityInput = uint32_t(runtimeVertices.size()); data.vertices = runtimeVertices.data();
      data.indexCapacityInput = uint32_t(indices.size()); data.indices = indices.data();
      const XrResult fetched = query(&data);
      if (fetched == XR_ERROR_SIZE_INSUFFICIENT) continue;
      if (fetched != XR_SUCCESS) return VisibilityResult::Unavailable;
      if (!data.vertexCountOutput && !data.indexCountOutput) return VisibilityResult::Empty;
      if (!VisibilityCountsValid(data.vertexCountOutput, data.indexCountOutput) ||
          data.vertexCountOutput > runtimeVertices.size() || data.indexCountOutput > indices.size())
        return VisibilityResult::Invalid;
      runtimeVertices.resize(data.vertexCountOutput); indices.resize(data.indexCountOutput);
      std::vector<IRender3D::PresentationVisibilityVertex> vertices;
      vertices.reserve(runtimeVertices.size());
      for (const auto& vertex : runtimeVertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) return VisibilityResult::Invalid;
        // Copy field by field; XrVector2f is not aliased as an unrelated type.
        vertices.push_back({vertex.x, vertex.y});
      }
      for (const auto index : indices) if (index >= vertices.size()) return VisibilityResult::Invalid;
      for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& a = vertices[indices[i]]; const auto& b = vertices[indices[i + 1]];
        const auto& c = vertices[indices[i + 2]];
        const double area = (double(b.x) - a.x) * (double(c.y) - a.y) -
          (double(b.y) - a.y) * (double(c.x) - a.x);
        // The extension defines counter-clockwise triangle winding. An invalid
        // or degenerate mesh falls back to rendering the complete eye.
        if (!std::isfinite(area) || area <= 0) return VisibilityResult::Invalid;
      }
      const uint64_t generation = nextGeneration();
      if (!generation) return VisibilityResult::Exhausted;
      destination.vertices.swap(vertices); destination.indices.swap(indices);
      destination.generation = generation;
      return VisibilityResult::Ready;
    }
  }
  catch (const std::bad_alloc&) { return VisibilityResult::Unavailable; }
  return VisibilityResult::Unavailable;
}
int VisibilityChangedEye(const XrEventDataBuffer& event, XrSession session)
{
  if (!session || event.type != XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR) return -1;
  const auto& change = reinterpret_cast<const XrEventDataVisibilityMaskChangedKHR&>(event);
  if (change.session != session || change.viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ||
      change.viewIndex >= 2) return -1;
  return int(change.viewIndex);
}
// END bounded optional visibility policy.
std::atomic<uint64_t> visibilityGeneration{0};
}

#define XR_SESSION_FUNCTIONS(X) \
  X(xrDestroyInstance) X(xrGetInstanceProperties) X(xrGetSystem) X(xrGetSystemProperties) \
  X(xrGetOpenGLGraphicsRequirementsKHR) X(xrEnumerateViewConfigurationViews) \
  X(xrEnumerateEnvironmentBlendModes) X(xrCreateSession) X(xrDestroySession) \
  X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrEnumerateSwapchainFormats) \
  X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) \
  X(xrPollEvent) X(xrBeginSession) X(xrEndSession) X(xrWaitFrame) X(xrBeginFrame) \
  X(xrEndFrame) X(xrLocateViews) X(xrLocateSpace) X(xrAcquireSwapchainImage) \
  X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) X(xrResultToString)

struct OpenXRSession::Impl
{
  HMODULE loader = nullptr;
  PFN_xrGetInstanceProcAddr getProc = nullptr;
  PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions = nullptr;
  PFN_xrCreateInstance createInstance = nullptr;
  PFN_xrGetVisibilityMaskKHR getVisibilityMask = nullptr;
  bool visibilityExtension = false;
  std::array<VisibilityData, 2> visibility;
  unsigned visibilityLogged = 0;
#define XR_DECLARE(name) PFN_##name name = nullptr;
  XR_SESSION_FUNCTIONS(XR_DECLARE)
#undef XR_DECLARE
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace space = XR_NULL_HANDLE, headSpace = XR_NULL_HANDLE;
  XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
  XrFrameState timing{XR_TYPE_FRAME_STATE};
  std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
  // Swapchain acquisition belongs to the resource, not an eye. A shared array
  // has one image index and one acquire/wait/release for both eye layers.
  struct Chain {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t index = 0, arraySize = 1;
    bool acquired = false, waited = false;
    std::vector<XrSwapchainImageOpenGLKHR> images;
  };
  std::array<Chain, 2> chains;
  struct Eye {
    int width = 0, height = 0;
    uint32_t owner = 0, layer = 0;
    std::vector<GLuint> framebuffers;
  };
  std::array<Eye, 2> eyes;
  OpenXRInput input;
  Controls controls;
  HarleyHaptics haptics;
  HapticStats hapticStats;
  HarleyHaptics::Output pendingHaptic;
  double pendingHapticAt = 0;
  uint64_t hapticSceneFrame = 0, hapticSceneEpoch = 0;
  bool hapticSceneSeen = false, hapticStereoReady = false;
  bool hapticsPaused = false;
  bool bodyInteraction = false;
  float units = 16, originYaw = 0;
  XrVector3f originPosition{};
  bool recenterPending = true, running = false, begun = false;
  bool render = false, tracked = false, failed = false, exit = false;
  bool inputAvailable = false;
  uint64_t presents = 0;
  std::vector<XrTime> referenceChanges;
  TimingStats stats;
  // All mutable session/render state stays owner-thread-only. The worker
  // exchanges only one timing result under this mutex; dispatch/session stay
  // alive and unchanged until StopWaitWorker has joined it.
  std::thread waitWorker;
  std::mutex waitMutex;
  std::condition_variable waitCondition;
  bool waitStop = false, waitRequested = false, waitPending = false, waitReady = false;
  OpenXRTimingReadySignal timingReadySignal;
  XrFrameState waitedTiming{XR_TYPE_FRAME_STATE};
  XrResult waitResult = XR_SUCCESS;
  double waitedMs = 0;
  XRClock::time_point waitedReturnedAt{};
  // Diagnostic IDs and timestamps only: no frame admission or timing decisions
  // depend on these fields. Pending request data is protected by waitMutex;
  // traceFrame is read/written only by the SDL/OpenGL owner.
  uint64_t traceNextFrame = 0, traceRequestedFrame = 0, traceFrame = 0;
  uint64_t traceQueuedAt = 0, tracePublishedAt = 0;

  void RefreshVisibilityEye(unsigned eye)
  {
    if (eye >= visibility.size()) return;
    visibility[eye].Clear();
    if (!visibilityExtension || !getVisibilityMask || !session) return;
    const auto result = RefreshVisibility(visibility[eye], [this, eye](XrVisibilityMaskKHR* mask) {
      return getVisibilityMask(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
        XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, mask);
    }, [] { return NextVisibilityGeneration(visibilityGeneration); });
    // At most one line for each eye/outcome in a session, including runtimes
    // that repeatedly return an invalid or changing mesh.
    const unsigned bit = 1u << (eye * 5 + unsigned(result));
    if (!(visibilityLogged & bit)) {
      visibilityLogged |= bit;
      if (result == VisibilityResult::Ready)
        InfoLog("OpenXR visibility mask eye %u: %u vertices, %u indices; optional world-only optimization.", eye,
          unsigned(visibility[eye].vertices.size()), unsigned(visibility[eye].indices.size()));
      else InfoLog("OpenXR visibility mask eye %u unavailable or invalid (%u); rendering the full eye.", eye, unsigned(result));
    }
  }
  void ClearVisibility()
  { for (auto& mask : visibility) mask.Clear(); }

  bool HapticOutputAllowed() const
  {
    return hapticStereoReady && running && sessionState == XR_SESSION_STATE_FOCUSED &&
      !exit && !failed && tracked && !recenterPending && !hapticsPaused &&
      controls.active && !controls.pause && !controls.exitCombo &&
      std::isfinite(controls.throttle) && std::isfinite(controls.brake);
  }
  void ApplyHaptic(const HarleyHaptics::Output& motor)
  {
    if (motor.kind == HarleyHaptics::Kind::None) return;
    const auto at = LiveFrameTrace::Now();
    unsigned accepted = 0;
    if (motor.kind == HarleyHaptics::Kind::Stop) {
      ++hapticStats.stopEvents;
      input.Stop();
    }
    else {
      const bool active[2] = {controls.leftActive, controls.rightActive};
      for (unsigned hand = 0; hand < 2; ++hand) if (active[hand]) {
        ++hapticStats.handAttempts[hand];
        if (input.Pulse(hand, motor.amplitude, motor.seconds, motor.frequency)) {
          ++hapticStats.handAccepted[hand]; accepted |= 1u << hand;
        }
      }
      if (accepted) ++hapticStats.deliveredPulses;
    }
    LiveFrameTrace::Event("xr.haptics", at, LiveFrameTrace::Now(), traceFrame,
      int(motor.kind), accepted, int64_t(motor.amplitude * 1000), int64_t(motor.seconds * 1000000));
  }
  void QueueHaptic(const HarleyHaptics::Output& motor, double nowMs)
  {
    if (motor.kind == HarleyHaptics::Kind::None) return;
    if (motor.kind == HarleyHaptics::Kind::Stop) {
      pendingHaptic = {};
      ApplyHaptic(motor);
    }
    else {
      ++hapticStats.detectedPulses;
      pendingHaptic = motor; pendingHapticAt = nowMs;
    }
  }
  void StopHaptics()
  {
    ApplyHaptic(haptics.Suspend());
    pendingHaptic = {};
    hapticStereoReady = hapticSceneSeen = false;
    // Input owns the actual runtime motor state; stopping it is idempotent.
    input.Stop();
  }
  void PollHaptics(bool allowed, double nowMs)
  {
    QueueHaptic(haptics.Poll(allowed, nowMs), nowMs);
    if (pendingHaptic.kind == HarleyHaptics::Kind::None) return;
    // Never replay an impact after a long presentation stall. This queue has
    // one fixed-size slot and lasts no longer than the original 160 ms pulse.
    if (!std::isfinite(nowMs) || nowMs < pendingHapticAt || nowMs - pendingHapticAt > 160.0) {
      ++hapticStats.expiredPulses;
      pendingHaptic = {};
    }
    else if (allowed) {
      ApplyHaptic(pendingHaptic);
      pendingHaptic = {};
    }
  }

  void StopWaitWorker()
  {
    {
      std::lock_guard<std::mutex> lock(waitMutex);
      waitStop = true;
    }
    waitCondition.notify_all();
    if (waitWorker.joinable()) waitWorker.join();
    waitStop = waitRequested = waitPending = waitReady = false;
    timingReadySignal.Reset(); // No publisher remains after the join.
    waitedReturnedAt = {};
  }
  bool QueueWait()
  {
    const auto traceQueueStart = LiveFrameTrace::Now();
    std::lock_guard<std::mutex> lock(waitMutex);
    if (waitPending) return true;
    if (!waitWorker.joinable()) {
      try {
        waitWorker = std::thread([this] {
          std::unique_lock<std::mutex> lock(waitMutex);
          for (;;) {
            waitCondition.wait(lock, [this] { return waitStop || waitRequested; });
            if (waitStop) return;
            waitRequested = false;
            const auto traceRequest = traceRequestedFrame, traceQueued = traceQueuedAt;
            lock.unlock();
            XrFrameState resultTiming{XR_TYPE_FRAME_STATE};
            XrFrameWaitInfo info{XR_TYPE_FRAME_WAIT_INFO};
            const auto start = XRClock::now();
            const auto traceWaitStart = LiveFrameTrace::Now();
            const XrResult result = xrWaitFrame(session, &info, &resultTiming);
            const auto traceWaitEnd = LiveFrameTrace::Now();
            const auto returnedAt = XRClock::now();
            const double elapsed = std::chrono::duration<double, std::milli>(returnedAt - start).count();
            LiveFrameTrace::Event("xr.wait_dispatch", traceQueued, traceWaitStart, traceRequest);
            LiveFrameTrace::Event("xr.wait_frame", traceWaitStart, traceWaitEnd, traceRequest,
              result, resultTiming.predictedDisplayTime, resultTiming.predictedDisplayPeriod, resultTiming.shouldRender);
            lock.lock();
            waitedTiming = resultTiming; waitResult = result; waitedMs = elapsed; waitedReturnedAt = returnedAt;
            waitReady = true;
            tracePublishedAt = LiveFrameTrace::Now();
            LiveFrameTrace::Event("xr.wait_publish", traceWaitEnd, tracePublishedAt, traceRequest);
            timingReadySignal.Publish(); // Mirror readiness under waitMutex.
            waitCondition.notify_all();
          }
        });
      } catch (const std::system_error& error) {
        ErrorLog("OpenXR frame timing worker could not start: %s.", error.what());
        failed = true; return false;
      }
    }
    waitPending = waitRequested = true; waitReady = false;
    timingReadySignal.Reset(); // A queued request has no result to consume yet.
    traceRequestedFrame = ++traceNextFrame;
    traceQueuedAt = LiveFrameTrace::Now();
    LiveFrameTrace::Event("xr.queue_wait", traceQueueStart, traceQueuedAt, traceRequestedFrame);
    waitCondition.notify_one();
    return true;
  }
  bool TakeWait(bool blocking, bool& ready)
  {
    const auto traceTakeStart = LiveFrameTrace::Now();
    std::unique_lock<std::mutex> lock(waitMutex);
    ready = false;
    if (!waitPending) return true;
    if (blocking) waitCondition.wait(lock, [this] { return waitReady; });
    if (!waitReady) return true;
    const auto traceTakenAt = LiveFrameTrace::Now();
    traceFrame = traceRequestedFrame;
    LiveFrameTrace::Event("xr.wait_ready_residence", tracePublishedAt, traceTakenAt, traceFrame);
    LiveFrameTrace::Event("xr.take_wait", traceTakeStart, traceTakenAt, traceFrame, blocking);
    timing = waitedTiming;
    const XrResult result = waitResult;
    ++stats.waits; stats.waitMs += waitedMs; stats.maxWaitMs = std::max(stats.maxWaitMs, waitedMs);
    stats.RecordReadyWaitAge(ElapsedMs(waitedReturnedAt), timing.predictedDisplayPeriod);
    waitPending = waitReady = false;
    timingReadySignal.Reset(); // Cannot race the next publication under this lock.
    waitedReturnedAt = {};
    ready = true;
    lock.unlock();
    return Check(result, "xrWaitFrame");
  }

  bool Check(XrResult result, const char* operation)
  {
    if (result == XR_SESSION_LOSS_PENDING) {
      exit = true;
      controls = {};
      StopHaptics();
    }
    if (XR_SUCCEEDED(result)) return true;
    char description[XR_MAX_RESULT_STRING_SIZE]{};
    if (!instance || !xrResultToString || XR_FAILED(xrResultToString(instance, result, description)))
      std::snprintf(description, sizeof(description), "result %d", int(result));
    ErrorLog("OpenXR %s failed: %s (%d).", operation, description, int(result));
    failed = true;
    return false;
  }
  template<class T> bool Load(T& function, const char* name, XrInstance from)
  {
    const XrResult result = getProc(from, name, reinterpret_cast<PFN_xrVoidFunction*>(&function));
    if (!Check(result, name)) return false;
    if (function) return true;
    ErrorLog("OpenXR runtime returned a null function for %s.", name);
    failed = true;
    return false;
  }
  bool Poll()
  {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    for (;;) {
      const XrResult result = xrPollEvent(instance, &event);
      if (result == XR_EVENT_UNAVAILABLE) break;
      if (!Check(result, "xrPollEvent")) return false;
      if (visibilityExtension) {
        const int eye = VisibilityChangedEye(event, session);
        if (eye >= 0) RefreshVisibilityEye(unsigned(eye));
      }
      if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { exit = true; StopHaptics(); }
      if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
        const auto& change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(event);
        if (change.session == session && change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
          referenceChanges.push_back(change.changeTime);
          InfoLog("OpenXR local reference-space change scheduled; seated origin will follow its display time.");
        }
      }
      if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto& change = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
        if (change.session == session) {
          sessionState = change.state;
          const auto traceStateAt = LiveFrameTrace::Now();
          LiveFrameTrace::Event("xr.session_state", traceStateAt, traceStateAt, traceFrame, sessionState, change.time);
          InfoLog("OpenXR session state %d.", int(sessionState));
          if (sessionState == XR_SESSION_STATE_READY && !running) {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (!Check(xrBeginSession(session, &begin), "xrBeginSession")) return false;
            running = true;
          }
          if (sessionState == XR_SESSION_STATE_STOPPING && running) {
            StopHaptics();
            StopWaitWorker();
            if (!Check(xrEndSession(session), "xrEndSession")) return false;
            running = false;
          }
          if (sessionState == XR_SESSION_STATE_EXITING || sessionState == XR_SESSION_STATE_LOSS_PENDING)
            exit = true;
          if (sessionState != XR_SESSION_STATE_FOCUSED) { controls = {}; StopHaptics(); }
        }
      }
      event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    if (exit) StopWaitWorker();
    return true;
  }
  bool Locate(const char* traceStage)
  {
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = timing.predictedDisplayTime; locate.space = space;
    XrViewState state{XR_TYPE_VIEW_STATE}; uint32_t count = 0;
    const auto traceLocateStart = LiveFrameTrace::Now();
    const auto locateResult = xrLocateViews(session, &locate, &state, 2, &count, views.data());
    LiveFrameTrace::Event(traceStage, traceLocateStart, LiveFrameTrace::Now(), traceFrame,
      locateResult, state.viewStateFlags, count, timing.predictedDisplayTime);
    if (!Check(locateResult, "xrLocateViews")) return false;
    const XrViewStateFlags required = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
      XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    tracked = count == 2 && (state.viewStateFlags & required) == required;
    if (tracked && recenterPending) {
      XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
      const auto traceOriginStart = LiveFrameTrace::Now();
      const auto originResult = xrLocateSpace(headSpace, space, timing.predictedDisplayTime, &head);
      LiveFrameTrace::Event("xr.locate_origin", traceOriginStart, LiveFrameTrace::Now(), traceFrame,
        originResult, head.locationFlags, timing.predictedDisplayTime);
      if (!Check(originResult, "locate seated origin")) return false;
      const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
      if ((head.locationFlags & valid) == valid) {
        const auto& q = head.pose.orientation;
        originYaw = std::atan2(2*(q.x*q.z + q.w*q.y), 1-2*(q.x*q.x + q.y*q.y));
        originPosition = head.pose.position; recenterPending = false;
        InfoLog("OpenXR seated origin centered; headset pitch and roll remain independent.");
      } else tracked = false;
    }
    return true;
  }
  IRender3D::PresentationPose RelativePose(const XrPosef& pose) const
  {
    // Rotate by inverse seated yaw, preserving pitch/roll and physical IPD.
    IRender3D::PresentationPose out;
    const float c = std::cos(originYaw), s = std::sin(originYaw);
    const float x = pose.position.x - originPosition.x;
    const float z = pose.position.z - originPosition.z;
    out.position[0] = c * x - s * z;
    out.position[1] = pose.position.y - originPosition.y;
    out.position[2] = s * x + c * z;
    const float qy = -std::sin(originYaw * .5f), qw = std::cos(originYaw * .5f);
    const auto& q = pose.orientation;
    out.orientation[0] = qw*q.x + qy*q.z;
    out.orientation[1] = qw*q.y + qy*q.w;
    out.orientation[2] = qw*q.z - qy*q.x;
    out.orientation[3] = qw*q.w - qy*q.y;
    return out;
  }
  void ReleaseImages()
  {
    for (auto& chain : chains) {
      if (chain.swapchain && chain.acquired && chain.waited) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const auto traceReleaseStart = LiveFrameTrace::Now();
        const XrResult result = xrReleaseSwapchainImage(chain.swapchain, &release);
        LiveFrameTrace::Event("xr.release_image", traceReleaseStart, LiveFrameTrace::Now(), traceFrame,
          &chain - chains.data(), result, chain.index);
        if (Check(result, "xrReleaseSwapchainImage"))
          chain.acquired = chain.waited = false;
      }
      // Never release an image whose wait did not succeed. A fatal wait or
      // release error leaves ownership recorded until xrDestroySwapchain.
    }
  }
};

OpenXRSession::OpenXRSession() : m(new Impl) {}
OpenXRSession::~OpenXRSession() { Close(); }

bool OpenXRSession::Initialize(float resolutionScale, float unitsPerMeter, bool preferArray, bool bodyInteraction, bool readyWake, bool visibilityMask)
{
  if (m->loader || m->instance || m->session) return false;
  m->failed = m->exit = false;
  m->presents = 0;
  m->stats = {};
  if (!std::isfinite(resolutionScale) || resolutionScale < .5f || resolutionScale > 1.5f ||
      !std::isfinite(unitsPerMeter) || unitsPerMeter < 1 || unitsPerMeter > 100) {
    ErrorLog("OpenXR resolution scale or world scale is invalid.");
    m->failed = true; return false;
  }
  m->units = unitsPerMeter;
  wchar_t executable[32768]{};
  const DWORD length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
  if (!length || length >= std::size(executable)) { m->failed = true; return false; }
  std::wstring path(executable, length);
  path.resize(path.find_last_of(L"\\/") + 1);
  path += L"libopenxr_loader.dll";
  m->loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!m->loader) {
    ErrorLog("OpenXR loader is missing or cannot load (Windows error %lu). Use the dedicated VR installation.", GetLastError());
    m->failed = true; return false;
  }
  const FARPROC getProcAddress = GetProcAddress(m->loader, "xrGetInstanceProcAddr");
  static_assert(sizeof(getProcAddress) == sizeof(m->getProc), "Windows function pointer sizes must match");
  std::memcpy(&m->getProc, &getProcAddress, sizeof(m->getProc));
  if (!m->getProc) { ErrorLog("OpenXR loader has no xrGetInstanceProcAddr entry point."); m->failed = true; return false; }
  if (!m->Load(m->enumerateExtensions, "xrEnumerateInstanceExtensionProperties", XR_NULL_HANDLE) ||
      !m->Load(m->createInstance, "xrCreateInstance", XR_NULL_HANDLE)) return false;
  uint32_t count = 0;
  if (!m->Check(m->enumerateExtensions(nullptr, 0, &count, nullptr), "enumerate extensions")) return false;
  std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
  if (!m->Check(m->enumerateExtensions(nullptr, count, &count, extensions.data()), "enumerate extensions")) return false;
  bool openGL = false;
  for (const auto& extension : extensions) openGL |= std::strcmp(extension.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0;
  m->visibilityExtension = VisibilityExtensionAvailable(visibilityMask, extensions);
  if (!openGL) { ErrorLog("Selected OpenXR runtime does not support OpenGL."); m->failed = true; return false; }
  const char* enabled[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME};
  XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strcpy(create.applicationInfo.applicationName, "SupermodelVR");
  std::strcpy(create.applicationInfo.engineName, "Supermodel New3D");
  create.applicationInfo.applicationVersion = create.applicationInfo.engineVersion = 1;
  create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
  create.enabledExtensionNames = enabled;
  bool retriedWithoutVisibility = false;
  const XrResult created = CreateOptionalVisibilityInstance(create, m->visibilityExtension, m->instance,
    retriedWithoutVisibility, [this](const XrInstanceCreateInfo* info, XrInstance* instance) {
      return m->createInstance(info, instance);
    });
  if (retriedWithoutVisibility) InfoLog("OpenXR optional visibility extension could not be enabled; using full-eye startup fallback.");
  if (!m->Check(created, "xrCreateInstance")) return false;
#define XR_LOAD(name) if (!m->Load(m->name, #name, m->instance)) return false;
  XR_SESSION_FUNCTIONS(XR_LOAD)
#undef XR_LOAD
  if (m->visibilityExtension) {
    if (!ResolveVisibilityFunction(true, m->getVisibilityMask, [this](const char* name, PFN_xrVoidFunction* function) {
        return m->getProc(m->instance, name, function);
      })) {
      m->visibilityExtension = false; m->getVisibilityMask = nullptr;
      InfoLog("OpenXR optional visibility function unavailable; rendering full eyes.");
    }
  }
  else if (visibilityMask) InfoLog("OpenXR visibility masks unavailable; rendering full eyes.");
  XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
  if (!m->Check(m->xrGetInstanceProperties(m->instance, &runtime), "runtime properties")) return false;
  InfoLog("OpenXR runtime: %s version %u.%u.%u.", runtime.runtimeName, unsigned(XR_VERSION_MAJOR(runtime.runtimeVersion)),
    unsigned(XR_VERSION_MINOR(runtime.runtimeVersion)), unsigned(XR_VERSION_PATCH(runtime.runtimeVersion)));
  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO}; systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  if (!m->Check(m->xrGetSystem(m->instance, &systemInfo, &m->system), "xrGetSystem: open your headset PC software and confirm the headset is connected and ready")) return false;
  XrSystemProperties system{XR_TYPE_SYSTEM_PROPERTIES};
  if (!m->Check(m->xrGetSystemProperties(m->instance, m->system, &system), "system properties")) return false;
  InfoLog("OpenXR headset: %s; orientation=%d position=%d.", system.systemName,
    int(system.trackingProperties.orientationTracking), int(system.trackingProperties.positionTracking));
  XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  if (!m->Check(m->xrGetOpenGLGraphicsRequirementsKHR(m->instance, m->system, &requirements), "OpenGL graphics requirements")) return false;
  InfoLog("OpenXR requires OpenGL %u.%u.%u through %u.%u.%u.",
    unsigned(XR_VERSION_MAJOR(requirements.minApiVersionSupported)), unsigned(XR_VERSION_MINOR(requirements.minApiVersionSupported)),
    unsigned(XR_VERSION_PATCH(requirements.minApiVersionSupported)), unsigned(XR_VERSION_MAJOR(requirements.maxApiVersionSupported)),
    unsigned(XR_VERSION_MINOR(requirements.maxApiVersionSupported)), unsigned(XR_VERSION_PATCH(requirements.maxApiVersionSupported)));
  if (!wglGetCurrentDC() || !wglGetCurrentContext()) {
    ErrorLog("OpenXR cannot create a session because no OpenGL rendering context and device context are current on this thread.");
    m->failed = true; return false;
  }
  GLint major = 0, minor = 0; glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
  InfoLog("OpenXR current OpenGL context: %d.%d.", major, minor);
  const XrVersion version = XR_MAKE_VERSION(major, minor, 0);
  if (version < requirements.minApiVersionSupported || version > requirements.maxApiVersionSupported) {
    ErrorLog("The current OpenGL %d.%d context is outside the OpenXR runtime's supported range logged above.", major, minor);
    m->failed = true; return false;
  }
  uint32_t blendCount = 0;
  if (!m->Check(m->xrEnumerateEnvironmentBlendModes(m->instance, m->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendCount, nullptr), "blend modes")) return false;
  std::vector<XrEnvironmentBlendMode> blends(blendCount);
  if (!m->Check(m->xrEnumerateEnvironmentBlendModes(m->instance, m->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendCount, &blendCount, blends.data()), "blend modes")) return false;
  if (std::find(blends.begin(), blends.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == blends.end()) {
    ErrorLog("This headset build requires an opaque VR display."); m->failed = true; return false;
  }
  XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
  binding.hDC = wglGetCurrentDC(); binding.hGLRC = wglGetCurrentContext();
  XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO}; sessionInfo.next = &binding; sessionInfo.systemId = m->system;
  if (!m->Check(m->xrCreateSession(m->instance, &sessionInfo, &m->session), "xrCreateSession")) return false;
  if (m->visibilityExtension) for (unsigned eye = 0; eye < 2; ++eye) m->RefreshVisibilityEye(eye);
  XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; spaceInfo.poseInReferenceSpace.orientation.w = 1;
  spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  if (!m->Check(m->xrCreateReferenceSpace(m->session, &spaceInfo, &m->space), "local space")) return false;
  spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  if (!m->Check(m->xrCreateReferenceSpace(m->session, &spaceInfo, &m->headSpace), "head space")) return false;
  m->bodyInteraction=bodyInteraction;
  m->inputAvailable = m->input.Initialize(m->instance, m->session, m->getProc, bodyInteraction);
  if (!m->inputAvailable) { ErrorLog("OpenXR controller initialization failed."); m->failed = true; return false; }
  uint32_t viewCount = 0;
  if (!m->Check(m->xrEnumerateViewConfigurationViews(m->instance, m->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr), "view count")) return false;
  if (viewCount != 2) { ErrorLog("OpenXR stereo configuration must have exactly two views."); m->failed = true; return false; }
  std::array<XrViewConfigurationView,2> configs{{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
  if (!m->Check(m->xrEnumerateViewConfigurationViews(m->instance, m->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, configs.data()), "views")) return false;
  if (viewCount != 2) { ErrorLog("OpenXR stereo view count changed during enumeration."); m->failed = true; return false; }
  uint32_t formatCount = 0;
  if (!m->Check(m->xrEnumerateSwapchainFormats(m->session, 0, &formatCount, nullptr), "swapchain formats")) return false;
  std::vector<int64_t> formats(formatCount);
  if (!m->Check(m->xrEnumerateSwapchainFormats(m->session, formatCount, &formatCount, formats.data()), "swapchain formats")) return false;
  // The game outputs display-encoded colors; the headset renderer writes those
  // bytes without a second gamma conversion. A linear RGBA8 fallback would
  // therefore be wrong unless that renderer adds explicit linearization.
  const int64_t format = GL_SRGB8_ALPHA8;
  if (std::find(formats.begin(), formats.end(), format) == formats.end()) {
    ErrorLog("OpenXR runtime has no SRGB8_ALPHA8 swapchain format required by this build."); m->failed = true; return false;
  }
  struct FramebufferBindings {
    GLint read = 0, draw = 0;
    FramebufferBindings() { glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw); }
    ~FramebufferBindings() { glBindFramebuffer(GL_READ_FRAMEBUFFER, read); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw); }
  } previousBindings;
  for (size_t i = 0; i < m->eyes.size(); ++i) {
    auto& eye = m->eyes[i]; const auto& config = configs[i];
    const auto maxWidth = std::min(config.maxImageRectWidth, system.graphicsProperties.maxSwapchainImageWidth);
    const auto maxHeight = std::min(config.maxImageRectHeight, system.graphicsProperties.maxSwapchainImageHeight);
    if (maxWidth < 64 || maxHeight < 64 || !config.recommendedImageRectWidth || !config.recommendedImageRectHeight) {
      ErrorLog("OpenXR runtime reported invalid swapchain dimensions."); m->failed = true; return false;
    }
    eye.width = int(std::clamp(std::lround(config.recommendedImageRectWidth * resolutionScale), 64l, long(maxWidth)));
    eye.height = int(std::clamp(std::lround(config.recommendedImageRectHeight * resolutionScale), 64l, long(maxHeight)));
    eye.owner = uint32_t(i);
  }
  auto createChain = [&](size_t owner, uint32_t layers) {
    auto& chain = m->chains[owner];
    const auto& eye = m->eyes[owner];
    chain.arraySize = layers;
    XrSwapchainCreateInfo swap{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swap.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swap.format = format; swap.sampleCount = 1; swap.width = eye.width; swap.height = eye.height;
    swap.faceCount = swap.mipCount = 1; swap.arraySize = layers;
    return m->xrCreateSwapchain(m->session, &swap, &chain.swapchain);
  };
  bool shared = preferArray && m->eyes[0].width == m->eyes[1].width &&
    m->eyes[0].height == m->eyes[1].height && glFramebufferTextureLayer;
  if (shared) {
    const XrResult result = createChain(0, 2);
    // The optional probe must not poison Check's fatal state before a known
    // unsupported configuration can fall back. Never hide loss, validation,
    // memory, limit or generic runtime failures behind a second allocation.
    if ((result == XR_ERROR_FEATURE_UNSUPPORTED || result == XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED) &&
        !m->chains[0].swapchain) {
      InfoLog("OpenXR shared array swapchain unsupported (%d); using separate eye swapchains.", int(result));
      m->chains[0] = {};
      shared = false;
    } else {
      if (!m->Check(result, "xrCreateSwapchain (stereo array)") || m->exit) return false;
      if (!m->chains[0].swapchain) { ErrorLog("OpenXR returned a null stereo array swapchain."); m->failed = true; return false; }
      m->eyes[1].owner = 0; m->eyes[1].layer = 1;
    }
  } else if (preferArray) {
    InfoLog("OpenXR shared array unavailable for these eye dimensions or OpenGL entry points; using separate eye swapchains.");
  }
  if (!shared) {
    for (size_t i = 0; i < m->chains.size(); ++i) {
      if (!m->Check(createChain(i, 1), "xrCreateSwapchain") || m->exit) return false;
      if (!m->chains[i].swapchain) { ErrorLog("OpenXR returned a null eye swapchain."); m->failed = true; return false; }
    }
  }
  for (auto& chain : m->chains) {
    if (!chain.swapchain) continue;
    uint32_t images = 0;
    if (!m->Check(m->xrEnumerateSwapchainImages(chain.swapchain, 0, &images, nullptr), "swapchain image count") || m->exit) return false;
    chain.images.resize(images, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    if (!images) { ErrorLog("OpenXR swapchain contains no images."); m->failed = true; return false; }
    if (!m->Check(m->xrEnumerateSwapchainImages(chain.swapchain, images, &images, reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data())), "swapchain images") || m->exit) return false;
    if (!images || images > chain.images.size()) { ErrorLog("OpenXR swapchain image count changed unexpectedly."); m->failed = true; return false; }
    chain.images.resize(images);
  }
  for (size_t i = 0; i < m->eyes.size(); ++i) {
    auto& eye = m->eyes[i]; const auto& chain = m->chains[eye.owner];
    const auto images = uint32_t(chain.images.size());
    eye.framebuffers.resize(images); glGenFramebuffers(GLsizei(images), eye.framebuffers.data());
    for (size_t image = 0; image < images; ++image) {
      if (!eye.framebuffers[image]) { ErrorLog("Could not allocate OpenXR eye framebuffer."); m->failed = true; return false; }
      glBindFramebuffer(GL_FRAMEBUFFER, eye.framebuffers[image]);
      if (chain.arraySize > 1)
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, chain.images[image].image, 0, GLint(eye.layer));
      else
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, chain.images[image].image, 0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ErrorLog("OpenXR eye framebuffer is incomplete."); m->failed = true;
        return false;
      }
    }
    InfoLog("OpenXR eye %u: %dx%d at scale %.3f, %u images, format 0x%llx.", unsigned(i), eye.width, eye.height, resolutionScale, images, static_cast<unsigned long long>(format));
  }
  // No timing worker can start before initialization returns. Event creation
  // failure only disables this optional wake hint; ordinary polling still works.
  if (readyWake && !m->timingReadySignal.Initialize())
    InfoLog("OpenXR timing-ready wake unavailable; retaining timer polling.");
  InfoLog("OpenXR timing-ready wake: %s.", m->timingReadySignal.Get() ? "enabled" : "disabled");
  InfoLog("OpenXR swapchain layout: %s.", shared ? "shared two-layer array (sequential eye rendering)" : "separate eye images");
  InfoLog("OpenXR initialized. Game clock remains 57.524160Hz; headset timing follows the runtime. World scale %.3f units/metre.", m->units);
  return true;
}

bool OpenXRSession::GetRenderTargetSize(int& width, int& height) const
{
  width = height = 0;
  if (!m->session || m->failed || m->exit ||
      m->eyes[0].owner >= m->chains.size() || m->eyes[1].owner >= m->chains.size() ||
      !m->chains[m->eyes[0].owner].swapchain || !m->chains[m->eyes[1].owner].swapchain ||
      m->eyes[0].width <= 0 || m->eyes[1].width <= 0 ||
      m->eyes[0].height <= 0 || m->eyes[1].height <= 0) return false;
  width = std::max(m->eyes[0].width, m->eyes[1].width);
  height = std::max(m->eyes[0].height, m->eyes[1].height);
  return true;
}

bool OpenXRSession::Begin(Frame& frame)
{ return BeginInternal(frame, true); }

bool OpenXRSession::Prefetch()
{
  if (!m->session || m->failed || m->exit) return false;
  if (!m->running || m->begun) return true;
  return m->QueueWait();
}

bool OpenXRSession::TryBegin(Frame& frame)
{ return BeginInternal(frame, false); }

void* OpenXRSession::TimingReadySignal() const
{ return m->timingReadySignal.Get(); }

bool OpenXRSession::BeginInternal(Frame& frame, bool blocking)
{
  const auto traceBeginStart = LiveFrameTrace::Now();
  uint64_t tracePollStart = 0, tracePollEnd = 0;
  // Pending nonblocking probes are covered by the outer presentation pump.
  // Only actual frame starts/errors get this full scope, avoiding a poll-rate
  // stream that could exhaust the bounded recorder during runtime standby.
  const auto finish = [&](bool result) {
    if (frame.begun || m->failed) {
      LiveFrameTrace::Event("xr.poll", tracePollStart, tracePollEnd, m->traceFrame, m->sessionState);
      LiveFrameTrace::Event("xr.begin_scope", traceBeginStart, LiveFrameTrace::Now(), m->traceFrame,
        result, (frame.begun ? 1 : 0) | (frame.render ? 2 : 0) | (frame.focused ? 4 : 0) | (frame.tracked ? 8 : 0),
        m->timing.predictedDisplayTime, m->timing.predictedDisplayPeriod);
    }
    return result;
  };
  frame = {};
  if (!m->session || m->failed || m->begun) return finish(false);
  tracePollStart = LiveFrameTrace::Now();
  const bool polled = m->Poll();
  tracePollEnd = LiveFrameTrace::Now();
  if (!polled) return finish(false);
  if (m->exit || !m->running) { m->controls = {}; return true; }
  frame.focused = m->sessionState == XR_SESSION_STATE_FOCUSED;
  if (!blocking && !m->QueueWait()) return finish(false);
  bool ready = false;
  if (!m->TakeWait(blocking, ready)) return finish(false);
  if (!ready) {
    if (!blocking) return true;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    m->timing = {XR_TYPE_FRAME_STATE};
    const auto start = XRClock::now();
    m->traceFrame = ++m->traceNextFrame;
    const auto traceWaitStart = LiveFrameTrace::Now();
    const XrResult result = m->xrWaitFrame(m->session, &wait, &m->timing);
    LiveFrameTrace::Event("xr.wait_frame_blocking", traceWaitStart, LiveFrameTrace::Now(), m->traceFrame,
      result, m->timing.predictedDisplayTime, m->timing.predictedDisplayPeriod, m->timing.shouldRender);
    const double elapsed = ElapsedMs(start);
    ++m->stats.waits; m->stats.waitMs += elapsed; m->stats.maxWaitMs = std::max(m->stats.maxWaitMs, elapsed);
    if (!m->Check(result, "xrWaitFrame")) return finish(false);
  }
  XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
  const auto beginStart = XRClock::now();
  const auto traceBeginCallStart = LiveFrameTrace::Now();
  const XrResult beginResult = m->xrBeginFrame(m->session, &begin);
  LiveFrameTrace::Event("xr.begin_frame", traceBeginCallStart, LiveFrameTrace::Now(), m->traceFrame, beginResult);
  m->stats.beginMs += ElapsedMs(beginStart);
  if (!m->Check(beginResult, "xrBeginFrame")) return finish(false);
  ++m->stats.frames;
  m->begun = frame.begun = true;
  frame.traceId = m->traceFrame;
  frame.predictedTime = m->timing.predictedDisplayTime; frame.period = m->timing.predictedDisplayPeriod;
  m->referenceChanges.erase(std::remove_if(m->referenceChanges.begin(), m->referenceChanges.end(), [&](XrTime time) {
    if (time > m->timing.predictedDisplayTime) return false;
    m->recenterPending = true;
    return true;
  }), m->referenceChanges.end());
  frame.focused = m->sessionState == XR_SESSION_STATE_FOCUSED;
  m->render = frame.render = m->timing.shouldRender == XR_TRUE;
  m->tracked = false;
  if (m->inputAvailable) {
    // Input owns failure/recovery diagnostics and neutralizes on every error.
    // A per-frame wrapper log would bypass its repeated-error suppression.
    const auto traceInputStart = LiveFrameTrace::Now();
    m->input.Sync(frame.focused);
    const auto traceInputEnd = LiveFrameTrace::Now();
    const auto& s = m->input.GetState();
    m->controls = {s.steering,s.throttle,s.brake,s.select,s.coin,s.view,s.music,s.pause,s.recenter,s.exitCombo,s.gearShiftUp,s.gearShiftDown,s.active,s.leftActive,s.rightActive};
    m->controls.moveX=s.moveX; m->controls.moveY=s.moveY; m->controls.turnX=s.turnX;
    m->controls.handsToggle=s.handsToggle; m->controls.exploreToggle=s.exploreToggle;
    m->controls.grabLeft=s.grabLeft; m->controls.grabRight=s.grabRight;
    LiveFrameTrace::Event("xr.input_sync", traceInputStart, traceInputEnd, m->traceFrame,
      frame.focused, s.active, s.leftActive, s.rightActive);
  }
  if (!m->render) return finish(true);
  if (!m->Locate("xr.locate_initial")) return finish(false);
  frame.tracked = m->tracked;
  return finish(true);
}

bool OpenXRSession::End(IRender3D* renderer, bool mirror, int mirrorWidth, int mirrorHeight)
{
  const auto traceEndScopeStart = LiveFrameTrace::Now();
  if (!m->begun) return !m->failed;
  bool rendered = false;
  IRender3D::PresentationEye targets[2];
  if (m->bodyInteraction && renderer) renderer->SetTrackedHands({});
  std::array<XrCompositionLayerProjectionView,2> projected{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
  if (!m->failed && !m->exit && m->render && m->tracked && !m->recenterPending && renderer) {
    bool acquired = true;
    for (size_t i = 0; i < m->chains.size(); ++i) {
      auto& chain = m->chains[i];
      if (!chain.swapchain) continue;
      XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      const auto traceAcquireStart = LiveFrameTrace::Now();
      const auto acquireResult = m->xrAcquireSwapchainImage(chain.swapchain, &acquire, &chain.index);
      LiveFrameTrace::Event("xr.acquire_image", traceAcquireStart, LiveFrameTrace::Now(), m->traceFrame,
        i, acquireResult, chain.index);
      if (!m->Check(acquireResult, "xrAcquireSwapchainImage")) { acquired = false; break; }
      chain.acquired = true;
      XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout = XR_INFINITE_DURATION;
      const auto waitStart = XRClock::now();
      const auto traceImageWaitStart = LiveFrameTrace::Now();
      const XrResult waited = m->xrWaitSwapchainImage(chain.swapchain, &wait);
      LiveFrameTrace::Event("xr.wait_image", traceImageWaitStart, LiveFrameTrace::Now(), m->traceFrame,
        i, waited, chain.index);
      m->stats.swapchainWaitMs += ElapsedMs(waitStart);
      if (waited != XR_SUCCESS && waited != XR_SESSION_LOSS_PENDING) {
        m->Check(waited, "xrWaitSwapchainImage");
        if (XR_SUCCEEDED(waited)) ErrorLog("OpenXR swapchain wait did not complete (%d); image will not be rendered or released.", int(waited));
        m->failed = true; acquired = false; break;
      }
      m->Check(waited, "xrWaitSwapchainImage");
      chain.waited = true;
      if (m->exit) { acquired = false; break; }
      if (chain.index >= chain.images.size()) {
        ErrorLog("OpenXR acquired image index %u exceeds the swapchain image count.", chain.index);
        m->failed = true; acquired = false; break;
      }
    }
    if (acquired) {
      for (const auto& eye : m->eyes) {
        if (eye.owner >= m->chains.size() || !m->chains[eye.owner].swapchain ||
            !m->chains[eye.owner].waited || eye.layer >= m->chains[eye.owner].arraySize ||
            m->chains[eye.owner].index >= eye.framebuffers.size()) {
          ErrorLog("OpenXR eye has no valid acquired swapchain layer/framebuffer.");
          m->failed = true; acquired = false; break;
        }
      }
    }
    if (acquired) {
      // Refresh after emulator work and image waits. Rendering and compositor
      // metadata use this one pose pair at the original predicted display time.
      const auto locateStart = XRClock::now();
      acquired = m->Locate("xr.locate_late") && m->tracked && !m->exit;
      m->stats.lateLocateMs += ElapsedMs(locateStart);
    }
    if (acquired) {
      for (size_t i = 0; i < m->eyes.size(); ++i) {
        const auto& eye = m->eyes[i]; const auto& chain = m->chains[eye.owner];
        targets[i].framebuffer = eye.framebuffers[chain.index]; targets[i].width = eye.width; targets[i].height = eye.height;
        // These owned FBOs attach the exact-sized SRGB8_ALPHA8, single-sample
        // swapchain images created by Initialize; no per-frame driver query.
        targets[i].targetWidth = eye.width; targets[i].targetHeight = eye.height;
        targets[i].directColorTarget = true;
        // Owner-thread cache, refreshed only on creation/change events. It
        // remains alive throughout this RenderHeadsetEyes/End call.
        targets[i].visibilityMask = m->visibility[i].Describe();
        // Opaque runtime nanoseconds: the renderer may use consecutive deltas
        // for scene playback, never compare this epoch with its local clock.
        // Both eyes and compositor metadata retain this exact prediction.
        targets[i].displayTimeNs = m->timing.predictedDisplayTime;
        targets[i].pose = m->RelativePose(m->views[i].pose);
        const auto& fov = m->views[i].fov;
        targets[i].fov[0] = fov.angleLeft; targets[i].fov[1] = fov.angleRight; targets[i].fov[2] = fov.angleDown; targets[i].fov[3] = fov.angleUp;
        projected[i].pose = m->views[i].pose; projected[i].fov = fov;
        projected[i].subImage.swapchain = chain.swapchain;
        projected[i].subImage.imageRect = {{0,0},{eye.width,eye.height}};
        projected[i].subImage.imageArrayIndex = eye.layer;
        if (LiveFrameTrace::Enabled()) {
          // Exact compositor pose and renderer-relative pose from this same
          // late locate. Each signed payload is two raw float32 values, low
          // half first; these are bit containers, not numeric fixed point.
          const auto at = LiveFrameTrace::Now();
          const auto& pose = projected[i].pose;
          LiveFrameTrace::Event(i ? "xr.pose_right" : "xr.pose_left", at, at, m->traceFrame,
            TraceFloatPair(pose.position.x, pose.position.y), TraceFloatPair(pose.position.z, pose.orientation.x),
            TraceFloatPair(pose.orientation.y, pose.orientation.z), TraceFloatPair(pose.orientation.w, 0));
          const auto& relative = targets[i].pose;
          LiveFrameTrace::Event(i ? "xr.render_pose_right" : "xr.render_pose_left", at, at, m->traceFrame,
            TraceFloatPair(relative.position[0], relative.position[1]), TraceFloatPair(relative.position[2], relative.orientation[0]),
            TraceFloatPair(relative.orientation[1], relative.orientation[2]), TraceFloatPair(relative.orientation[3], m->units));
          LiveFrameTrace::Event(i ? "xr.fov_right" : "xr.fov_left", at, at, m->traceFrame,
            TraceFloatPair(fov.angleLeft, fov.angleRight), TraceFloatPair(fov.angleDown, fov.angleUp), eye.width, eye.height);
        }
      }
      const auto renderStart = XRClock::now();
      if (m->bodyInteraction) {
        IRender3D::TrackedHands hands;
        hands.displayTimeNs=m->timing.predictedDisplayTime;
        OpenXRInput::HandPose raw[2];
        if (m->inputAvailable && m->input.LocateHands(m->space,m->timing.predictedDisplayTime,raw)) {
          for (unsigned h=0;h<2;++h) if (raw[h].valid) {
            XrPosef pose{{raw[h].orientation[0],raw[h].orientation[1],raw[h].orientation[2],raw[h].orientation[3]},
                        {raw[h].position[0],raw[h].position[1],raw[h].position[2]}};
            hands.pose[h]=m->RelativePose(pose); hands.valid[h]=true;
          }
        }
        renderer->SetTrackedHands(hands);
      }
      const auto traceRenderStart = LiveFrameTrace::Now();
      rendered = renderer->RenderHeadsetEyes(targets, m->units);
      LiveFrameTrace::Event("xr.render_eyes", traceRenderStart, LiveFrameTrace::Now(), m->traceFrame, rendered);
      const double renderMs = ElapsedMs(renderStart);
      ++m->stats.eyeDraws; m->stats.eyeRenderMs += renderMs; m->stats.maxEyeRenderMs = std::max(m->stats.maxEyeRenderMs, renderMs);
      if (rendered && mirror && mirrorWidth > 0 && mirrorHeight > 0) {
        const auto traceMirrorStart = LiveFrameTrace::Now();
        GLint previousRead, previousDraw, readBuffer, drawBuffer, box[4]; GLboolean mask[4]; GLfloat clear[4];
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);
        glGetIntegerv(GL_READ_BUFFER,&readBuffer); glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer); glGetIntegerv(GL_SCISSOR_BOX,box);
        glGetBooleanv(GL_COLOR_WRITEMASK,mask); glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST); glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0); glDrawBuffer(GL_BACK); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        const auto& eye = m->eyes[0]; const float fit = std::min(float(mirrorWidth)/eye.width,float(mirrorHeight)/eye.height);
        const int w = int(eye.width*fit), h = int(eye.height*fit), x = (mirrorWidth-w)/2, y = (mirrorHeight-h)/2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER,targets[0].framebuffer); glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(0,0,eye.width,eye.height,x,y,x+w,y+h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead); glReadBuffer(readBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previousDraw); glDrawBuffer(drawBuffer);
        glColorMask(mask[0],mask[1],mask[2],mask[3]); glClearColor(clear[0],clear[1],clear[2],clear[3]);
        glScissor(box[0],box[1],box[2],box[3]); if (scissor) glEnable(GL_SCISSOR_TEST);
        LiveFrameTrace::Event("xr.mirror", traceMirrorStart, LiveFrameTrace::Now(), m->traceFrame);
      }
      const auto traceFlushStart = LiveFrameTrace::Now();
      glFlush(); // complete submission before returning runtime-owned images
      LiveFrameTrace::Event("xr.gl_flush", traceFlushStart, LiveFrameTrace::Now(), m->traceFrame);
    }
    m->ReleaseImages();
  }
  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space = m->space;
  layer.viewCount = 2; layer.views = projected.data();
  const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO}; end.displayTime = m->timing.predictedDisplayTime;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = rendered && !m->failed && !m->exit ? 1 : 0; end.layers = end.layerCount ? layers : nullptr;
  const auto endStart = XRClock::now();
  const auto traceEndStart = LiveFrameTrace::Now();
  const auto endResult = m->xrEndFrame(m->session, &end);
  LiveFrameTrace::Event("xr.end_frame", traceEndStart, LiveFrameTrace::Now(), m->traceFrame,
    endResult, end.layerCount, end.displayTime, m->timing.predictedDisplayPeriod);
  const bool result = m->Check(endResult, "xrEndFrame");
  const double endMs = ElapsedMs(endStart);
  m->stats.endMs += endMs; m->stats.maxEndMs = std::max(m->stats.maxEndMs, endMs);
  m->begun = false;
  if (result && end.layerCount) ++m->presents;
  // Detection consumes EVERY completed native scene via ObserveNativeHaptics,
  // not the subset reaching End when the runtime throttles below native rate.
  // Inspect this presentation only for safety; never advance detection history.
  IRender3D::HapticState hapticState;
  const bool safeScene = renderer && renderer->GetHapticState(hapticState) &&
    hapticState.driving && hapticState.anchorValid &&
    std::isfinite(hapticState.body[0]) && std::isfinite(hapticState.body[1]) && std::isfinite(hapticState.body[2]) &&
    (!m->hapticSceneSeen || (hapticState.sceneEpoch == m->hapticSceneEpoch &&
      hapticState.sceneFrame >= m->hapticSceneFrame));
  m->hapticStereoReady = result && end.layerCount && safeScene;
  const double hapticNow = std::chrono::duration<double, std::milli>(XRClock::now().time_since_epoch()).count();
  m->PollHaptics(m->HapticOutputAllowed(), hapticNow);
  LiveFrameTrace::Event("xr.end_scope", traceEndScopeStart, LiveFrameTrace::Now(), m->traceFrame,
    result && !m->failed, end.layerCount,
    (m->render ? 1 : 0) | (m->tracked ? 2 : 0) | (m->sessionState == XR_SESSION_STATE_FOCUSED ? 4 : 0) |
      (m->recenterPending ? 8 : 0) | (m->failed ? 16 : 0) | (m->exit ? 32 : 0), m->presents);
  return result && !m->failed;
}

void OpenXRSession::Recenter() { m->recenterPending = true; }
void OpenXRSession::SetHapticsPaused(bool paused)
{
  m->hapticsPaused = paused;
  if (paused) m->StopHaptics();
}
void OpenXRSession::ObserveNativeHaptics(IRender3D* renderer)
{
  // Called after RunFrame returns on the same owner that runs End. The native
  // worker and the timing worker never read these anchors or call motor APIs.
  ++m->hapticStats.nativeSamples;
  IRender3D::HapticState state;
  HarleyHaptics::Sample sample;
  if (renderer && renderer->GetHapticState(state)) {
    sample.frame = state.sceneFrame; sample.epoch = state.sceneEpoch;
    sample.driving = state.driving; sample.anchorValid = state.anchorValid;
    std::copy(state.body, state.body + 3, sample.body.begin());
    sample.motion = state.motion;
  }
  const bool valid = sample.driving && sample.anchorValid &&
    std::all_of(sample.body.begin(), sample.body.end(), [](double value) { return std::isfinite(value); });
  if (valid) ++m->hapticStats.validSamples;
  const bool allowed = m->HapticOutputAllowed();
  if (allowed) ++m->hapticStats.allowedSamples;
  if (m->hapticSceneSeen && (sample.epoch != m->hapticSceneEpoch ||
      sample.frame <= m->hapticSceneFrame || sample.frame - m->hapticSceneFrame != 1))
    ++m->hapticStats.discontinuities;
  m->hapticSceneFrame = sample.frame; m->hapticSceneEpoch = sample.epoch;
  m->hapticSceneSeen = valid;
  const double now = std::chrono::duration<double, std::milli>(XRClock::now().time_since_epoch()).count();
  m->QueueHaptic(m->haptics.Update(sample, m->controls.throttle, m->controls.brake, allowed, now), now);
}
const OpenXRSession::Controls& OpenXRSession::GetControls() const { return m->controls; }
bool OpenXRSession::WantsExit() const { return m->exit; }
bool OpenXRSession::Failed() const { return m->failed; }
bool OpenXRSession::Focused() const { return m->running && m->sessionState == XR_SESSION_STATE_FOCUSED && !m->exit && !m->failed; }
bool OpenXRSession::Tracked() const { return m->tracked && !m->recenterPending && m->running && !m->failed && !m->exit; }
uint64_t OpenXRSession::Presents() const { return m->presents; }
OpenXRSession::TimingStats OpenXRSession::Stats() const { return m->stats; }
OpenXRSession::HapticStats OpenXRSession::HapticsStats() const { return m->hapticStats; }
void OpenXRSession::Close()
{
  if (!m) return;
  m->StopWaitWorker();
  m->timingReadySignal.CloseAfterWorkerJoin();
  m->StopHaptics();
  if (m->begun && m->xrEndFrame) End(nullptr, false, 0, 0);
  m->ClearVisibility();
  m->ReleaseImages();
  m->input.Shutdown(); m->inputAvailable = false;
  // xrDestroySwapchain requires submitted graphics commands to have finished.
  // Close is called on the owning GL thread before its context is destroyed.
  if (wglGetCurrentContext() && (m->chains[0].swapchain || m->chains[1].swapchain)) glFinish();
  for (auto& eye : m->eyes) {
    if (!eye.framebuffers.empty()) glDeleteFramebuffers(GLsizei(eye.framebuffers.size()), eye.framebuffers.data());
    eye.framebuffers.clear();
  }
  for (auto& chain : m->chains) {
    chain.images.clear();
    if (chain.swapchain && m->xrDestroySwapchain) m->xrDestroySwapchain(chain.swapchain);
    chain.swapchain = XR_NULL_HANDLE;
  }
  if (m->headSpace && m->xrDestroySpace) m->xrDestroySpace(m->headSpace);
  if (m->space && m->xrDestroySpace) m->xrDestroySpace(m->space);
  m->headSpace = m->space = XR_NULL_HANDLE;
  if (m->session && m->xrDestroySession) m->xrDestroySession(m->session);
  m->session = XR_NULL_HANDLE; m->running = false;
  if (m->instance && m->xrDestroyInstance) m->xrDestroyInstance(m->instance);
  m->instance = XR_NULL_HANDLE;
  if (m->loader) FreeLibrary(m->loader);
  // Discard all dispatch pointers with the unloaded loader. Keep only
  // public outcome counters/status available to the caller after cleanup.
  const bool failed = m->failed, exit = m->exit;
  const uint64_t presents = m->presents;
  const TimingStats stats = m->stats;
  const HapticStats hapticStats = m->hapticStats;
  m.reset(new Impl);
  m->failed = failed; m->exit = exit; m->presents = presents;
  m->stats = stats;
  m->hapticStats = hapticStats;
}
