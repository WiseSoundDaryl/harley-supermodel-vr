#ifndef INCLUDED_IRENDER3D_H
#define INCLUDED_IRENDER3D_H

#include <cstdint>
#include "Types.h"
#include "HarleyPresentation.h"

class CRender2D;

/*
 * IRender3D:
 *
 * Interface (abstract base class) for Real3D rendering engine.
 */
class IRender3D
{
public:
  // Head-to-base pose relative to the current game/rider camera. Coordinates
  // are right handed (+X right, +Y up, -Z forward), in game units. Orientation
  // is a near-unit quaternion in x,y,z,w order; callers convert metres before
  // submitting tracked positions. Presentation never advances the game clock.
  struct PresentationPose
  {
    float orientation[4] = { 0, 0, 0, 1 };
    float position[3] = { 0, 0, 0 };
  };
  struct PresentationState
  {
    uint64_t sceneFrame = 0, sceneEpoch = 0;
    double verticalOffset = 0, vignetteStrength = 0;
    double menuVignetteStrength = 0;
    uint64_t menuComfortTicks = 0;
  };
  // Optional runtime hidden-triangle mesh, in eye view space at z=-1.
  // Owner-thread pointers are valid only for this RenderHeadsetEyes call.
  // Generation is nonzero and never reused within the process. The renderer
  // caches its own GPU resources, never these pointers. Empty/invalid data
  // must leave the complete visible image rendered; no guessed mask is used.
  static constexpr uint32_t MaxVisibilityVertices = 4096;
  static constexpr uint32_t MaxVisibilityIndices = 12288;
  struct PresentationVisibilityVertex { float x = 0, y = 0; };
  struct PresentationVisibilityMask
  {
    const PresentationVisibilityVertex* vertices = nullptr;
    const uint32_t* indices = nullptr;
    uint32_t vertexCount = 0, indexCount = 0;
    uint64_t generation = 0;
  };
  // Runtime-supplied eye, independent of a particular headset API. Unlike
  // PresentationPose above, position is in METRES relative to the recentered
  // seated origin and already includes the runtime's eye/IPD offset. FOV angles
  // are signed radians in left, right, down, up order. The caller owns the
  // complete single-sample OpenGL framebuffer and its color attachment 0.
  struct PresentationEye
  {
    PresentationPose pose;
    float fov[4] = { -0.785398163f, 0.785398163f, -0.785398163f, 0.785398163f };
    uint32_t framebuffer = 0;
    int x = 0, y = 0, width = 0, height = 0;
    // Optional creation-time contract for direct final composition. True
    // certifies that color attachment 0 is a single-sample GL_RGBA8 or
    // GL_SRGB8_ALPHA8 image with exactly these full attachment dimensions.
    // Other formats could change the existing RGBA8 quantization. Defaults
    // preserve intermediate composition/copy for callers without this proof.
    int targetWidth = 0, targetHeight = 0;
    bool directColorTarget = false;
    // Optional opaque runtime display timestamp in nanoseconds. Both eyes must
    // carry the same positive value. Presentation-only scene interpolation
    // uses consecutive differences, never compares this epoch with CPU clocks.
    // Zero preserves exact native scene rendering for callers without timing.
    int64_t displayTimeNs = 0;
    PresentationVisibilityMask visibilityMask;
  };
  virtual void RenderFrame(void) = 0;
  // Non-owning GL-thread presentation dependency. Tile textures are uploaded
  // once by the native frame; independent eye redraws only sample them.
  virtual void Attach2DRenderer(CRender2D* renderer) {}
  virtual void BeginFrame(void) = 0;
  virtual void EndFrame(void) = 0;
  // Presentation clock for the immutable scene being drawn. Repeated draws of
  // a paused snapshot reuse its ID. Epoch changes invalidate temporal history.
  virtual void SetSceneClock(uint64_t frame, uint64_t epoch) {}
  using GamePresentationState = HarleyPresentation::State;
  // Published with the GPU snapshot. The renderer must retain this value for
  // independent presentation redraws, never sample live emulated main RAM.
  virtual void SetGamePresentationState(const GamePresentationState& state) {}
  // Sampled on the GL owner thread before the next native emulation frame.
  // Steering uses the game's 0..255 range (128 center); no state advances here.
  virtual void SubmitMenuInput(int steering, bool start) {}
  // Set an absolute head pose for the stereo draw in subsequent native frames.
  // GL owner thread only. This does not draw, invalidate a prepared scene, or
  // advance the game. An invalid pose leaves the previous pose unchanged.
  virtual bool SetPresentationPose(const PresentationPose& pose) { return false; }
  // Call only on the GL owner thread, between complete native frame renders.
  // A successful call redraws prepared 3D geometry/textures with a new head
  // pose, without RAM traversal, texture uploads, LOS updates or comfort ticks.
  // False means no current reprojectable scene; it leaves the framebuffer alone.
  virtual bool RenderPresentation(const PresentationPose& pose) { return false; }
  // Draw both eyes from one immutable prepared game snapshot using each eye's
  // exact pose and asymmetric lens. No game/LOS/comfort clock advances. The
  // default 16 game units/metre is provisional physical calibration: 64mm IPD
  // becomes 1.024 game units. False means no usable pair; submit neither eye.
  virtual bool RenderHeadsetEyes(const PresentationEye (&eyes)[2], float gameUnitsPerMeter = 16.0f) { return false; }
  // Headset scheduling owns final presentation. Native game frames retain
  // their authoritative center/LOS pass but omit redundant desktop eye draws.
  virtual void SetHeadsetPresentation(bool enabled) {}
  virtual bool GetPresentationState(PresentationState& state) const { return false; }
  // Reset/load/renderer reconfiguration must invalidate before the next redraw.
  virtual void InvalidatePresentation() {}
  virtual void UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height) = 0;
  virtual void AttachMemory(const uint32_t *cullingRAMLoPtr, const uint32_t *cullingRAMHiPtr, const uint32_t *polyRAMPtr, const uint32_t *vromPtr, const uint16_t *textureRAMPtr) = 0;
  virtual void SetStepping(int stepping) = 0;
  virtual Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget) = 0;
  virtual void SetSunClamp(bool enable) = 0;
  virtual void SetBlockCulling(bool enable) = 0;
  virtual float GetLosValue(int layer) = 0;

  virtual ~IRender3D()
  {
  }

  // Read only the immutable rendered snapshot. Append the interface so existing
  // virtual slots used by the preserved optimized emulator objects stay stable.
  struct HapticState
  {
    uint64_t sceneFrame = 0, sceneEpoch = 0;
    bool driving = false, anchorValid = false;
    double body[3]{};
    HarleyPresentation::MotionState motion;
  };
  virtual bool GetHapticState(HapticState& state) const { return false; }

  // Optional presentation-only body interaction. Pose units are metres in the
  // same recentered base as PresentationEye. No game RAM or native input writes.
  struct TrackedHands {
    PresentationPose pose[2];
    bool valid[2] = { false, false };
    int64_t displayTimeNs = 0;
  };
  struct BodyStatus {
    bool eligible = false, armsReady = false, exploring = false;
    uint64_t sceneFrame = 0, sceneEpoch = 0;
    float positionMeters[3] = {}, yawDegrees = 0;
    unsigned trackedArmParts = 0;
    float wristPositionMeters[2][3] = {};
    float wristErrorMeters[2] = {};
    bool wristClamped[2] = {false,false};
    bool handLatched[2] = {false,false}, handNearGrip[2] = {false,false};
    float gripPositionMeters[2][3] = {};
    float walkingVignette = 0;
  };
  virtual bool GetBodyStatus(BodyStatus& state) const { return false; }
  virtual bool SetBodyMode(bool arms, bool exploring, float unitsPerMeter) { return false; }
  virtual bool MoveBody(float rightMeters, float forwardMeters, float yawDegrees) { return false; }
  virtual void SetTrackedHands(const TrackedHands& hands) {}
  virtual void SetBodyBlackout(bool enabled) {}
  virtual void SetBodyVignette(float strength) {}
  // Held side-grip buttons, independent of throttle triggers. active=false
  // clears latches and requires a new released edge after focus/tracking loss.
  virtual void SetBodyGripButtons(bool left, bool right, bool active = true) {}
  // GL owner thread only. Non-owning, premultiplied RGBA 4:3 GL_TEXTURE_2D;
  // v=0 is the bottom row. Hidden/zero texture performs no drawing. A newly
  // visible panel anchors at the next valid headset pair; content revisions
  // never move that anchor. The caller keeps the texture alive while visible.
  // Append new virtuals here to retain existing optimized-object slot order.
  virtual void SetUserMenuPanel(unsigned texture, bool visible, uint64_t revision) {}
  // Owner thread, before native preparation. A renderer may permit one immutable
  // redraw only when delaying its pending hardware-feedback publication cannot
  // change values visible to the emulation worker. Unknown renderers decline.
  virtual bool CanDeferNativePreparation() { return false; }
};

#endif  // INCLUDED_IRENDER3D_H
