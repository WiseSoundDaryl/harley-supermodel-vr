#include "OpenXRInput.h"
#include "OSD/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

struct OpenXRInput::Impl
{
  XrInstance instance = XR_NULL_HANDLE;
  XrSession session = XR_NULL_HANDLE;
  XrActionSet actionSet = XR_NULL_HANDLE;
  XrPath hands[2] = {XR_NULL_PATH, XR_NULL_PATH};
  XrPath profiles[2] = {XR_NULL_PATH, XR_NULL_PATH};
  bool profileKnown[2] = {false, false};
  bool hapticPlaying[2] = {false, false};
  XrResult hapticError[2] = {XR_SUCCESS, XR_SUCCESS};
  bool hapticConfirmed[2] = {false, false};
  bool attached = false;
  bool focused = false;
  bool bodyInteraction = false;
  bool menuChordArmed = false;
  bool menuChordHeld = false;
  bool menuChordConsumesRecenter = false;
  std::thread::id bodyOwner;
  XrSpace handSpaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  enum class FailureStage { None, Sync, Query };
  FailureStage failureStage = FailureStage::None;
  XrResult lastSyncError = XR_SUCCESS;
  enum Action { Steering, Throttle, Brake, Select, Coin, View, Music, Pause,
                Recenter, LeftGrip, RightGrip, Gear, Haptic, GripPose, Explore, MenuAlternate, Count };
  XrAction actions[Count] = {};

#define XR_INPUT_FUNCTIONS(X) \
  X(xrStringToPath) X(xrPathToString) X(xrCreateActionSet) X(xrDestroyActionSet) \
  X(xrCreateAction) X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) \
  X(xrSyncActions) X(xrGetActionStateFloat) X(xrGetActionStateBoolean) \
  X(xrGetActionStateVector2f) X(xrGetCurrentInteractionProfile) \
  X(xrApplyHapticFeedback) X(xrStopHapticFeedback)
#define XR_DECLARE_INPUT(name) PFN_##name name = nullptr;
  XR_INPUT_FUNCTIONS(XR_DECLARE_INPUT)
#define XR_BODY_FUNCTIONS(X) \
  X(xrCreateActionSpace) X(xrDestroySpace) X(xrGetActionStatePose) X(xrLocateSpace)
  XR_BODY_FUNCTIONS(XR_DECLARE_INPUT)
#undef XR_DECLARE_INPUT

  bool Load(PFN_xrGetInstanceProcAddr getProc)
  {
#define XR_LOAD_INPUT(name) \
    if (XR_FAILED(getProc(instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&name))) || !name) \
    { ErrorLog("OpenXR input: missing %s", #name); return false; }
    XR_INPUT_FUNCTIONS(XR_LOAD_INPUT)
    if (bodyInteraction) {
      XR_BODY_FUNCTIONS(XR_LOAD_INPUT)
    }
#undef XR_LOAD_INPUT
    return true;
  }

  bool Check(XrResult result, const char* operation)
  {
    if (result == XR_SUCCESS) return true;
    ErrorLog("OpenXR input: %s returned %d", operation, static_cast<int>(result));
    return false;
  }

  void CancelMenuChord()
  {
    menuChordArmed = menuChordHeld = false;
    // Keep consuming a captured R3 until a valid right-hand query sees it
    // released. Losing focus or the left grip must never become a recenter.
  }

  XrPath Path(const char* value)
  {
    XrPath result = XR_NULL_PATH;
    if (!Check(xrStringToPath(instance, value, &result), value)) return XR_NULL_PATH;
    return result;
  }

  bool Create(Action index, const char* name, const char* label, XrActionType type, int hand)
  {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
    std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", label);
    info.actionType = type;
    info.countSubactionPaths = hand < 0 ? 2 : 1;
    info.subactionPaths = hand < 0 ? hands : &hands[hand];
    return Check(xrCreateAction(actionSet, &info, &actions[index]), name);
  }

  bool SuggestBindings(bool includePause)
  {
    struct Binding { Action action; const char* path; };
    // Sony's official SteamVR driver maps its Sense controls through this
    // profile, including Touch left menu -> Sense right Options single press.
    const Binding entries[] = {
      {Steering, "/user/hand/left/input/thumbstick"},
      {Throttle, "/user/hand/right/input/trigger/value"},
      {Brake, "/user/hand/left/input/trigger/value"},
      {Select, "/user/hand/right/input/a/click"},
      {Coin, "/user/hand/left/input/x/click"},
      {View, "/user/hand/left/input/y/click"},
      {Music, "/user/hand/right/input/b/click"},
      {Pause, "/user/hand/left/input/menu/click"},
      {MenuAlternate, "/user/hand/left/input/thumbstick/click"},
      {Recenter, "/user/hand/right/input/thumbstick/click"},
      {LeftGrip, "/user/hand/left/input/squeeze/value"},
      {RightGrip, "/user/hand/right/input/squeeze/value"},
      {Gear, "/user/hand/right/input/thumbstick"},
      {Haptic, "/user/hand/left/output/haptic"},
      {Haptic, "/user/hand/right/output/haptic"}
    };
    std::vector<XrActionSuggestedBinding> bindings;
    for (const auto& entry : entries)
    {
      if (entry.action == Pause && !includePause) continue;
      if (bodyInteraction && entry.action == MenuAlternate) continue;
      if (bodyInteraction && entry.action == Recenter) continue;
      XrPath path = Path(entry.path);
      if (path == XR_NULL_PATH) return false;
      bindings.push_back({actions[entry.action], path});
    }
    if (bodyInteraction) {
      for (const auto& entry : {Binding{Recenter, "/user/hand/left/input/thumbstick/click"},
                                Binding{Explore, "/user/hand/right/input/thumbstick/click"},
                                Binding{GripPose, "/user/hand/left/input/grip/pose"},
                                Binding{GripPose, "/user/hand/right/input/grip/pose"}}) {
        const XrPath path = Path(entry.path);
        if (path == XR_NULL_PATH) return false;
        bindings.push_back({actions[entry.action], path});
      }
    }
    XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    info.interactionProfile = Path("/interaction_profiles/oculus/touch_controller");
    if (info.interactionProfile == XR_NULL_PATH) return false;
    info.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    info.suggestedBindings = bindings.data();
    XrResult result = xrSuggestInteractionProfileBindings(instance, &info);
    if (includePause && result == XR_ERROR_PATH_UNSUPPORTED)
    {
      InfoLog("OpenXR input: retrying controller bindings without optional Pause/menu binding");
      return SuggestBindings(false);
    }
    return Check(result, "xrSuggestInteractionProfileBindings (Oculus Touch compatibility)");
  }

  void LogProfiles()
  {
    for (unsigned hand = 0; hand < 2; ++hand)
    {
      XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
      if (xrGetCurrentInteractionProfile(session, hands[hand], &profile) != XR_SUCCESS) continue;
      if (profileKnown[hand] && profiles[hand] == profile.interactionProfile) continue;
      profiles[hand] = profile.interactionProfile;
      profileKnown[hand] = true;
      char name[XR_MAX_PATH_LENGTH] = "unbound";
      uint32_t length = 0;
      if (profile.interactionProfile != XR_NULL_PATH &&
          xrPathToString(instance, profile.interactionProfile, sizeof(name), &length, name) != XR_SUCCESS)
        std::snprintf(name, sizeof(name), "%s", "unknown");
      InfoLog("OpenXR input: %s controller interaction profile: %s", hand == 0 ? "left" : "right", name);
    }
  }

  XrActionStateGetInfo GetInfo(Action action, unsigned hand) const
  {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = actions[action];
    info.subactionPath = hands[hand];
    return info;
  }

  bool ReadFloat(Action action, unsigned hand, float& value, bool& active)
  {
    auto info = GetInfo(action, hand);
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    if (xrGetActionStateFloat(session, &info, &state) != XR_SUCCESS) return false;
    if (state.isActive)
    {
      active = true;
      value = std::isfinite(state.currentState) ? std::max(0.0f, std::min(1.0f, state.currentState)) : 0.0f;
    }
    return true;
  }

  bool ReadBool(Action action, unsigned hand, bool& value, bool& active)
  {
    auto info = GetInfo(action, hand);
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (xrGetActionStateBoolean(session, &info, &state) != XR_SUCCESS) return false;
    if (state.isActive) { active = true; value = state.currentState == XR_TRUE; }
    return true;
  }

  bool ReadVector(Action action, unsigned hand, XrVector2f& value, bool& active)
  {
    auto info = GetInfo(action, hand);
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (xrGetActionStateVector2f(session, &info, &state) != XR_SUCCESS) return false;
    if (state.isActive)
    {
      active = true;
      value.x = std::isfinite(state.currentState.x) ? std::max(-1.0f, std::min(1.0f, state.currentState.x)) : 0.0f;
      value.y = std::isfinite(state.currentState.y) ? std::max(-1.0f, std::min(1.0f, state.currentState.y)) : 0.0f;
    }
    return true;
  }

  void StopHand(unsigned hand)
  {
    if (!hapticPlaying[hand] || !session || !actions[Haptic] || !xrStopHapticFeedback) return;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = actions[Haptic];
    info.subactionPath = hands[hand];
    xrStopHapticFeedback(session, &info);
    hapticPlaying[hand] = false;
  }
};

OpenXRInput::OpenXRInput() : m_impl(new Impl) {}
OpenXRInput::~OpenXRInput() { Shutdown(); }

bool OpenXRInput::Initialize(XrInstance instance, XrSession session, PFN_xrGetInstanceProcAddr getProc, bool bodyInteraction)
{
  Shutdown();
  if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE || !getProc) return false;
  auto& p = *m_impl;
  p.instance = instance;
  p.session = session;
  p.bodyInteraction = bodyInteraction;
  if (bodyInteraction) p.bodyOwner = std::this_thread::get_id();
  if (!p.Load(getProc)) { Shutdown(); return false; }
  p.hands[0] = p.Path("/user/hand/left");
  p.hands[1] = p.Path("/user/hand/right");
  if (!p.hands[0] || !p.hands[1]) { Shutdown(); return false; }
  XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::snprintf(setInfo.actionSetName, sizeof(setInfo.actionSetName), "%s", "harley_controls");
  std::snprintf(setInfo.localizedActionSetName, sizeof(setInfo.localizedActionSetName), "%s", "Harley-Davidson Riding Controls");
  setInfo.priority = 0;
  if (!p.Check(p.xrCreateActionSet(instance, &setInfo, &p.actionSet), "xrCreateActionSet"))
  { Shutdown(); return false; }

  if (!p.Create(Impl::Steering, "steering", "Steer / choose rider", XR_ACTION_TYPE_VECTOR2F_INPUT, 0) ||
      !p.Create(Impl::Throttle, "throttle", "Accelerate", XR_ACTION_TYPE_FLOAT_INPUT, 1) ||
      !p.Create(Impl::Brake, "brake", "Brake", XR_ACTION_TYPE_FLOAT_INPUT, 0) ||
      !p.Create(Impl::Select, "select", "Start / confirm selection", XR_ACTION_TYPE_BOOLEAN_INPUT, 1) ||
      !p.Create(Impl::Coin, "coin", "Insert coin", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
      !p.Create(Impl::View, "view", "Change camera view", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
      !p.Create(Impl::Music, "music", "Special / change music", XR_ACTION_TYPE_BOOLEAN_INPUT, 1) ||
      !p.Create(Impl::Pause, "pause", "Open HarleyVR menu", XR_ACTION_TYPE_BOOLEAN_INPUT, 0) ||
      (!p.bodyInteraction && !p.Create(Impl::MenuAlternate, "menu_alternate", "Open HarleyVR menu (left stick click)", XR_ACTION_TYPE_BOOLEAN_INPUT, 0)) ||
      !p.Create(Impl::Recenter, "recenter", "Recenter seated view", XR_ACTION_TYPE_BOOLEAN_INPUT, p.bodyInteraction ? 0 : 1) ||
      !p.Create(Impl::LeftGrip, "exit_left_grip", p.bodyInteraction ? "Toggle left handlebar grab" : "Hold both grips to exit (left)", XR_ACTION_TYPE_FLOAT_INPUT, 0) ||
      !p.Create(Impl::RightGrip, "exit_right_grip", p.bodyInteraction ? "Toggle right handlebar grab" : "Hold both grips to exit (right)", XR_ACTION_TYPE_FLOAT_INPUT, 1) ||
      !p.Create(Impl::Gear, "gear_shift", "Manual gear up / down", XR_ACTION_TYPE_VECTOR2F_INPUT, 1) ||
      !p.Create(Impl::Haptic, "controller_vibration", "Controller vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, -1) ||
      (p.bodyInteraction && (!p.Create(Impl::GripPose, "body_grip_pose", "Optional body hand poses", XR_ACTION_TYPE_POSE_INPUT, -1) ||
                            !p.Create(Impl::Explore, "body_explore", "Dismount / return to bike", XR_ACTION_TYPE_BOOLEAN_INPUT, 1))))
  { Shutdown(); return false; }
  if (p.bodyInteraction) for (unsigned hand = 0; hand < 2; ++hand) {
    XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    spaceInfo.action = p.actions[Impl::GripPose];
    spaceInfo.subactionPath = p.hands[hand];
    spaceInfo.poseInActionSpace.orientation.w = 1.0f;
    if (!p.Check(p.xrCreateActionSpace(session, &spaceInfo, &p.handSpaces[hand]), "xrCreateActionSpace (body hand)") ||
        p.handSpaces[hand] == XR_NULL_HANDLE) { Shutdown(); return false; }
  }
  if (!p.SuggestBindings(true)) { Shutdown(); return false; }
  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach.countActionSets = 1;
  attach.actionSets = &p.actionSet;
  if (!p.Check(p.xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets"))
  { Shutdown(); return false; }
  p.attached = true;
  InfoLog("OpenXR input: riding actions attached; Sense uses SteamVR Oculus Touch compatibility bindings");
  return true;
}

bool OpenXRInput::Sync(bool focused)
{
  m_state = {};
  auto& p = *m_impl;
  if (!p.attached || !focused)
  {
    p.CancelMenuChord();
    Stop();
    p.focused = false;
    return p.attached;
  }
  XrActiveActionSet activeSet{p.actionSet, XR_NULL_PATH};
  XrActionsSyncInfo info{XR_TYPE_ACTIONS_SYNC_INFO};
  info.countActiveActionSets = 1;
  info.activeActionSets = &activeSet;
  XrResult result = p.xrSyncActions(p.session, &info);
  // XR_SESSION_NOT_FOCUSED is a positive result, but must never reuse inputs.
  if (result != XR_SUCCESS)
  {
    p.CancelMenuChord();
    Stop();
    p.focused = false;
    if (result != XR_SESSION_NOT_FOCUSED) {
      if (p.failureStage != Impl::FailureStage::Sync || result != p.lastSyncError)
        ErrorLog("OpenXR input: xrSyncActions returned %d; controls neutralized", static_cast<int>(result));
      p.failureStage = Impl::FailureStage::Sync;
      p.lastSyncError = result;
    }
    return false;
  }
  p.focused = true;
  p.LogProfiles();
  State next;
  XrVector2f steering{}, gear{};
  float leftGrip = 0.0f, rightGrip = 0.0f;
  bool recenterActive = false;
  bool ok = p.ReadVector(Impl::Steering, 0, steering, next.leftActive) &&
    p.ReadVector(Impl::Gear, 1, gear, next.rightActive) &&
    p.ReadFloat(Impl::Throttle, 1, next.throttle, next.rightActive) &&
    p.ReadFloat(Impl::Brake, 0, next.brake, next.leftActive) &&
    p.ReadFloat(Impl::LeftGrip, 0, leftGrip, next.leftActive) &&
    p.ReadFloat(Impl::RightGrip, 1, rightGrip, next.rightActive) &&
    p.ReadBool(Impl::Select, 1, next.select, next.rightActive) &&
    p.ReadBool(Impl::Coin, 0, next.coin, next.leftActive) &&
    p.ReadBool(Impl::View, 0, next.view, next.leftActive) &&
    p.ReadBool(Impl::Music, 1, next.music, next.rightActive) &&
    p.ReadBool(Impl::Pause, 0, next.pause, next.leftActive) &&
    (p.bodyInteraction ? p.ReadBool(Impl::Recenter, 0, next.recenter, recenterActive) :
                         p.ReadBool(Impl::Recenter, 1, next.recenter, recenterActive));
  if (p.bodyInteraction) next.leftActive = next.leftActive || recenterActive;
  else next.rightActive = next.rightActive || recenterActive;
  if (ok && p.bodyInteraction) ok = p.ReadBool(Impl::Explore, 1, next.exploreToggle, next.rightActive);
  if (ok && !p.bodyInteraction) {
    // The runtime may reserve its Menu button. L3 provides the same menu action
    // without changing R3 recenter or the optional body mode's stick controls.
    bool alternate = false;
    ok = p.ReadBool(Impl::MenuAlternate, 0, alternate, next.leftActive);
    next.pause = next.pause || alternate;
  }
  if (!ok)
  {
    p.CancelMenuChord();
    Stop();
    p.focused = false;
    if (p.failureStage != Impl::FailureStage::Query)
      ErrorLog("OpenXR input: action query failed; controls neutralized");
    p.failureStage = Impl::FailureStage::Query;
    return false;
  }
  if (!p.bodyInteraction) {
    // The system/Home buttons may belong to SteamVR. Keep L3 as the direct
    // shortcut and offer left grip + a fresh R3 click using existing bindings.
    // Callers own the pause edge; hold this action until R3 is released so a
    // long press opens the menu once. Press grip before R3, not vice versa.
    if (!next.leftActive || !recenterActive) p.CancelMenuChord();
    if (recenterActive && !next.recenter) {
      p.menuChordArmed = next.leftActive;
      p.menuChordHeld = p.menuChordConsumesRecenter = false;
    } else if (recenterActive && next.recenter) {
      if (p.menuChordArmed && next.leftActive && leftGrip > 0.75f)
        p.menuChordHeld = p.menuChordConsumesRecenter = true;
      p.menuChordArmed = false;
    }
    next.pause = next.pause || p.menuChordHeld;
    if (p.menuChordConsumesRecenter) next.recenter = false;
  }
  constexpr float deadzone = 0.12f;
  if (std::fabs(steering.x) > deadzone)
    next.steering = std::copysign((std::fabs(steering.x) - deadzone) / (1.0f - deadzone), steering.x);
  next.gearShiftUp = gear.y > 0.6f;
  next.gearShiftDown = gear.y < -0.6f;
  next.exitCombo = leftGrip > 0.75f && rightGrip > 0.75f && !p.menuChordConsumesRecenter;
  if (p.bodyInteraction) {
    next.moveX = steering.x; next.moveY = steering.y; next.turnX = gear.x;
    next.grabLeft = leftGrip > 0.75f; next.grabRight = rightGrip > 0.75f;
    // Grips now toggle individual hands. Preserve the ordinary game's exit
    // chord outside this opt-in; body play uses deliberate Square+Circle hold.
    next.exitCombo = next.coin && next.music;
    if (next.exitCombo) next.coin = next.music = false;
  }
  next.active = next.leftActive || next.rightActive;
  if (!next.leftActive) p.StopHand(0);
  if (!next.rightActive) p.StopHand(1);
  m_state = next;
  // Retry and neutralization behavior is unchanged. Log only transitions:
  // repeated runtime errors must not cause synchronous file I/O every frame.
  // Focus loss is not recovery; every query must succeed before clearing it.
  if (p.failureStage != Impl::FailureStage::None)
    InfoLog("OpenXR input: controller input recovered");
  p.failureStage = Impl::FailureStage::None;
  p.lastSyncError = XR_SUCCESS;
  return true;
}

bool OpenXRInput::LocateHands(XrSpace base, XrTime time, HandPose (&out)[2])
{
  out[0] = {}; out[1] = {};
  auto& p = *m_impl;
  if (!p.bodyInteraction) return true;
  if (std::this_thread::get_id() != p.bodyOwner) return false;
  if (!p.attached || !p.focused || !m_state.active) return true;
  if (base == XR_NULL_HANDLE || time <= 0) return false;
  HandPose next[2];
  for (unsigned hand = 0; hand < 2; ++hand) {
    if (!(hand == 0 ? m_state.leftActive : m_state.rightActive)) continue;
    auto info = p.GetInfo(Impl::GripPose, hand);
    XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
    if (p.xrGetActionStatePose(p.session, &info, &state) != XR_SUCCESS) return false;
    if (!state.isActive) continue;
    if (p.handSpaces[hand] == XR_NULL_HANDLE) return false;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (p.xrLocateSpace(p.handSpaces[hand], base, time, &location) != XR_SUCCESS) return false;
    constexpr XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT |
      XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
      XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if ((location.locationFlags & required) != required) continue;
    const auto& q = location.pose.orientation;
    const auto& v = location.pose.position;
    const float orientation[] = {q.x, q.y, q.z, q.w};
    const float position[] = {v.x, v.y, v.z};
    double norm = 0.0;
    bool finite = true;
    for (float value : orientation) { finite &= std::isfinite(value); norm += double(value)*value; }
    for (float value : position) finite &= std::isfinite(value);
    if (!finite || std::abs(norm - 1.0) > 0.01) continue;
    std::copy(orientation, orientation + 4, next[hand].orientation);
    std::copy(position, position + 3, next[hand].position);
    next[hand].valid = true;
  }
  out[0] = next[0]; out[1] = next[1];
  return true;
}

bool OpenXRInput::Pulse(unsigned hand, float amplitude, float seconds, float frequency)
{
  auto& p = *m_impl;
  if (hand >= 2 || !std::isfinite(amplitude) || !std::isfinite(seconds) ||
      !std::isfinite(frequency) || frequency < 0.0f) return false;
  if (amplitude <= 0.0f || seconds <= 0.0f) { p.StopHand(hand); return true; }
  if (!p.attached || !p.focused || !(hand == 0 ? m_state.leftActive : m_state.rightActive)) return false;
  XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
  info.action = p.actions[Impl::Haptic];
  info.subactionPath = p.hands[hand];
  XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
  vibration.amplitude = std::min(1.0f, amplitude);
  vibration.duration = static_cast<XrDuration>(std::min(1.0f, seconds) * 1000000000.0);
  vibration.frequency = frequency > 0.0f ? std::min(320.0f, frequency) : XR_FREQUENCY_UNSPECIFIED;
  XrResult result = p.xrApplyHapticFeedback(p.session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
  if (result != XR_SUCCESS) {
    // Rejected output must be visible in a capture without logging every pulse.
    if (result != p.hapticError[hand])
      ErrorLog("OpenXR haptics: %s controller output returned %d", hand == 0 ? "left" : "right", static_cast<int>(result));
    p.hapticError[hand] = result;
    return false;
  }
  if (!p.hapticConfirmed[hand] || p.hapticError[hand] != XR_SUCCESS)
    InfoLog("OpenXR haptics: %s controller output accepted by runtime (physical vibration requires user verification)", hand == 0 ? "left" : "right");
  p.hapticConfirmed[hand] = true;
  p.hapticError[hand] = XR_SUCCESS;
  p.hapticPlaying[hand] = true;
  return true;
}

void OpenXRInput::Stop()
{
  m_impl->StopHand(0);
  m_impl->StopHand(1);
}

void OpenXRInput::Shutdown()
{
  Stop();
  if (m_impl->xrDestroySpace) for (auto& space : m_impl->handSpaces) {
    if (space != XR_NULL_HANDLE) m_impl->xrDestroySpace(space);
    space = XR_NULL_HANDLE;
  }
  if (m_impl->actionSet && m_impl->xrDestroyActionSet)
    m_impl->xrDestroyActionSet(m_impl->actionSet);
  *m_impl = Impl{};
  m_state = {};
}
