#ifndef SUPERMODEL_OPENXR_INPUT_H
#define SUPERMODEL_OPENXR_INPUT_H

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>
#include <memory>

// All calls use the session thread. Initialize before xrBeginSession; Shutdown
// before destroying the session. Buttons are held states; callers own edges.
class OpenXRInput
{
public:
  struct State
  {
    float steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    bool select = false;
    bool coin = false;
    bool view = false;
    bool music = false;
    bool pause = false;
    bool recenter = false;
    bool exitCombo = false;
    bool gearShiftUp = false;
    bool gearShiftDown = false;
    bool active = false;
    bool leftActive = false;
    bool rightActive = false;
    // Optional body mode only. Axes are finite/clamped raw stick values;
    // buttons are held states, with edges owned by the mode/renderer caller.
    float moveX = 0.0f, moveY = 0.0f, turnX = 0.0f;
    bool handsToggle = false, exploreToggle = false;
    bool grabLeft = false, grabRight = false;
  };
  struct HandPose
  {
    float orientation[4] = {0, 0, 0, 1};
    float position[3] = {};
    bool valid = false;
  };

  OpenXRInput();
  ~OpenXRInput();
  OpenXRInput(const OpenXRInput&) = delete;
  OpenXRInput& operator=(const OpenXRInput&) = delete;
  bool Initialize(XrInstance instance, XrSession session, PFN_xrGetInstanceProcAddr getProc, bool bodyInteraction = false);
  bool Sync(bool focused);
  // Raw grip poses in base-space metres at time. Always clears both outputs.
  // Disabled/unfocused/inactive input succeeds with invalid neutral poses.
  // Invalid arguments, wrong owner thread or API failures return false with
  // both invalid; callers may fall back to native arms without ending play.
  bool LocateHands(XrSpace base, XrTime time, HandPose (&out)[2]);
  const State& GetState() const { return m_state; }
  // Hand 0 is left, 1 is right. Pulses are bounded to one second and require focus.
  bool Pulse(unsigned hand, float amplitude, float seconds, float frequency = 0.0f);
  void Stop();
  void Shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  State m_state;
};

#endif
