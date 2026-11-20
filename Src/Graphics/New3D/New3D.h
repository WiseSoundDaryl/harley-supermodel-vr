/**
** Supermodel
** A Sega Model 3 Arcade Emulator.
** Copyright 2011 Bart Trzynadlowski, Nik Henson
**
** This file is part of Supermodel.
**
** Supermodel is free software: you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free
** Software Foundation, either version 3 of the License, or (at your option)
** any later version.
**
** Supermodel is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
** more details.
**
** You should have received a copy of the GNU General Public License along
** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
**/

/*
* New3D.h
*
* Header file defining the CNew3D class: OpenGL Real3D graphics engine.
*/

#ifndef INCLUDED_NEW3D_H
#define INCLUDED_NEW3D_H

#include <unordered_map>
#include <atomic>
#include <memory>
#include <GL/glew.h>
#include "Types.h"
#include "Graphics/IRender3D.h"
#include "Model.h"
#include "SceneModelBuffers.h"
#include "SceneInterpolation.h"
#include "InspectionGeometry.h"
#include "TrackedArms.h"
#include "HandlebarGrip.h"
#include "Mat4.h"
#include "RiderComfort.h"
#include "MenuComfort.h"
#include "FlatIntro.h"
#include "SpatialPanel.h"
#include "DesktopEyeLayout.h"
#include "StableChase.h"
#include "RenderProfiler.h"
#include "R3DShader.h"
#include "HeadsetVisibilityMask.h"
#include "VBO.h"
#include "R3DData.h"
#include "Plane.h"
#include "Vec.h"
#include "R3DScrollFog.h"
#include "PolyHeader.h"
#include "R3DFrameBuffers.h"
#include <mutex>
#include <cstdio>
#include "TextureBank.h"

namespace New3D {

class CNew3D final : public IRender3D
{
	friend struct SceneInterpolationTestAccess;
public:
	/*
	* RenderFrame(void):
	*
	* Renders the complete scene database. Must be called between BeginFrame() and
	* EndFrame(). This function traverses the scene database and builds up display
	* lists.
	*/
	void RenderFrame(void);
	void Attach2DRenderer(CRender2D* renderer) override;
	void SetSceneClock(uint64_t frame, uint64_t epoch) override;
	void SetGamePresentationState(const GamePresentationState& state) override;
	void SubmitMenuInput(int steering, bool start) override;
	bool SetPresentationPose(const PresentationPose& pose) override;
	bool RenderPresentation(const PresentationPose& pose) override;
	bool RenderHeadsetEyes(const PresentationEye (&eyes)[2], float gameUnitsPerMeter = 16.0f) override;
	void SetHeadsetPresentation(bool enabled) override;
	bool CanDeferNativePreparation() override;
	// Owner thread, outside a presentation: allocate fixed eye resources without
	// consuming a scene or changing caller-owned OpenGL bindings.
	bool PrepareHeadsetTargets(int width, int height);
	bool GetPresentationState(PresentationState& state) const override;
	bool GetHapticState(HapticState& state) const override;
	bool GetBodyStatus(BodyStatus& state) const override;
	bool SetBodyMode(bool arms, bool exploring, float unitsPerMeter) override;
	bool MoveBody(float rightMeters, float forwardMeters, float yawDegrees) override;
	void SetTrackedHands(const TrackedHands& hands) override { m_bodyHands = hands; }
	void SetBodyBlackout(bool enabled) override { m_bodyBlackout = m_bodyOptIn && enabled; }
	void SetBodyVignette(float strength) override;
	void SetBodyGripButtons(bool left, bool right, bool active = true) override;
	void SetUserMenuPanel(unsigned texture, bool visible, uint64_t revision) override;
	void InvalidatePresentation() override;

	/*
	* BeginFrame(void):
	*
	* Prepare to render a new frame. Must be called once per frame prior to
	* drawing anything.
	*/
	void BeginFrame(void);

	/*
	* EndFrame(void):
	*
	* Signals the end of rendering for this frame. Must be called last during
	* the frame.
	*/
	void EndFrame(void);

	/*
	* UploadTextures(x, y, width, height):
	*
	* Signals that a portion of texture RAM has been updated.
	*
	* Parameters:
	*		x		X position within texture RAM.
	*		y		Y position within texture RAM.
	*		width	Width of texture data in texels.
	*		height	Height.
	*/
	void UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height);

	/*
	* AttachMemory(cullingRAMLoPtr, cullingRAMHiPtr, polyRAMPtr, vromPtr,
	* 				textureRAMPtr):
	*
	* Attaches RAM and ROM areas. This must be done prior to any rendering
	* otherwise the program may crash with an access violation.
	*
	* Parameters:
	*		cullingRAMLoPtr		Pointer to low culling RAM (4 MB).
	*		cullingRAMHiPtr		Pointer to high culling RAM (1 MB).
	*		polyRAMPtr			Pointer to polygon RAM (4 MB).
	*		vromPtr				Pointer to video ROM (64 MB).
	*		textureRAMPtr		Pointer to texture RAM (8 MB).
	*/
	void AttachMemory(const UINT32 *cullingRAMLoPtr,
		const UINT32 *cullingRAMHiPtr, const UINT32 *polyRAMPtr,
		const UINT32 *vromPtr, const UINT16 *textureRAMPtr);

	/*
	* SetStepping(stepping):
	*
	* Sets the Model 3 hardware stepping, which also determines the Real3D
	* functionality. The default is Step 1.0. This should be called prior to
	* any other emulation functions and after Init().
	*
	* Parameters:
	*   stepping  0x10 for Step 1.0, 0x15 for Step 1.5, 0x20 for Step 2.0, or
	*             0x21 for Step 2.1. Anything else defaults to 1.0.
	*/
	void SetStepping(int stepping);

	/*
	* Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes):
	*
	* One-time initialization of the context. Must be called before any other
	* members (meaning it should be called even before being attached to any
	* other objects that want to use it).
	*
	* External shader files are loaded according to configuration settings.
	*
	* Parameters:
	*		xOffset		X offset of the viewable area within OpenGL display
	*                  surface, in pixels.
	*		yOffset		Y offset.
	*		xRes		Horizontal resolution of the viewable area.
	*		yRes		Vertical resolution.
	*		totalXRes	Horizontal resolution of the complete display area.
	*		totalYRes	Vertical resolution.
	*
	* Returns:
	*		OKAY if successful, otherwise FAILED if a non-recoverable error
	*		occurred. Any allocated memory will not be freed until the
	*		destructor is called. Prints own error messages.
	*/
	Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget);

	/*
	* SetSunClamp(bool enable);
	*
	* Sets or unsets the clamped light model
	*
	* Parameters:
	*		enable	Set clamp mode
	*/
	void SetSunClamp(bool enable);

	/*
	* SetBlockCulling(bool enable);
	*
	* When enabled, clears screen to white and disables 3D rendering
	*
	* Parameters:
	*		enable	Set block culling
	*/
	void SetBlockCulling(bool enable);

	/*
	* GetLosValue(int layer);
	*
	* Gets the line of sight value for the priority layer
	*
	* Parameters:
	*		layer	Priority layer to read from
	*/
	float GetLosValue(int layer);

	/*
	* CRender3D(config):
	* ~CRender3D(void):
	*
	* Constructor and destructor.
	*
	* Parameters:
	*   config  Run-time configuration.
	*/
	CNew3D(const Util::Config::Node &config, const std::string& gameName);
	~CNew3D(void);

private:
	/*
	* Private Members
	*/

	// Real3D address translation
	const UINT32 *TranslateCullingAddress(UINT32 addr);
	const UINT32 *TranslateModelAddress(UINT32 addr);

	// Matrix stack
	void MultMatrix(UINT32 matrixOffset, Mat4& mat);
	void InitMatrixStack(UINT32 matrixBaseAddr, Mat4& mat);
	void ResetMatrix(Mat4& mat) const;

	// Scene database traversal
	bool DrawModel(UINT32 modelAddr);
	void DescendCullingNode(UINT32 addr);
	void DescendPointerList(UINT32 addr);
	void DescendNodePtr(UINT32 nodeAddr);
	void RenderViewport(UINT32 addr);

	// building the scene
	int	GetTexFormat(int originalFormat, bool contour) const;
	void SetMeshValues(SortingMesh *currentMesh, PolyHeader &ph);
	void CacheModel(Model *m, const UINT32 *data);
	void CopyVertexData(const R3DPoly& r3dPoly, std::vector<FVertex>& vertexArray);
	void GetCoordinates(int width, int height, UINT16 uIn, UINT16 vIn, float uvScale, float& uOut, float& vOut) const;

	bool RenderScene(int priority, bool renderOverlay, Layer layer);		// returns if has overlay plane
	bool IsDynamicModel(UINT32 *data) const;				// check if the model has a colour palette
	bool IsVROMModel(UINT32 modelAddr) const;
	void DrawScrollFog();
	void DrawAmbientFog();
	bool SkipLayer(int layer);
	void SetRenderStates();
	void DisableRenderStates();
	void TranslateLosPosition(int inX, int inY, int& outX, int& outY) const;
	bool ProcessLos(int priority);
	void CalcViewport(Viewport* vp);

	// Desktop stereo diagnostic. A draw pair always shares one decoded scene.
	void BuildScene();
	struct RaceLayout { bool valid = false, settled = false; };
	RaceLayout InspectRaceLayout() const;
	struct MenuLayout {
		MenuComfort::Phase phase = MenuComfort::Phase::Other;
		unsigned worldViewports = 0, clockViewports = 0, nativeUI = 0;
		float clockHeight = -1.0f;
		bool raceLayout = false, raceSettled = false;
	};
	MenuLayout InspectMenuLayout() const;
	void SelectPresentationWorld();
	void UpdateMenuComfort();
	void ResetMenuComfort();
	bool FlatIntroActive() const;
	bool TheaterIntroActive() const;
	bool GeneralTheaterEnabled() const;
	bool ImmersiveOverlayPanel() const;
	float TheaterScreenAngle() const;
	float TheaterHalfWidth() const;
	bool StereoTheaterActive() const;
	void ClearPresentationTarget();
	bool DrivingHUDScene() const;
	bool EnsureHUDTarget();
	void PrepareHUDPanel();
	void DrawHUDPanel(GLuint target, const Mat4& headView);
	bool CertifyHUDDepth() const;
	bool DrawBikeHUD(int x, int y, int width, int height, const Mat4& view, const Mat4& projection,
		float unitsPerMeter, GLuint depth, const SpatialPanel::State* knownState = nullptr);
	Mat4 PanelProjection(int width, int height) const;
	float CinematicVignetteStrength() const;
	float UserComfortStrength() const;
	bool UploadScene();
	void StressROMCache(bool bypass);
	void DrawPreparedScene(bool processLos, GLuint outputTarget, bool preserveAlpha = false);
	void DrawPreparedSceneOnce(bool processLos, GLuint outputTarget, bool preserveAlpha);
	bool OpenDuplicateAudit();
	void PrepareDuplicateAuditMask();
	void CaptureDuplicateAuditStage(unsigned stage, bool finalColor = false);
	void RecordDuplicateAuditMesh(const Model& model, const Mesh& mesh, int priority, bool overlay, Layer layer);
	void UpdateDiagnosticSettings();
	Viewport GetRenderViewport(const Viewport& viewport, bool interpolate = true);
	void UpdateEyeLayout();
	bool HasWideEyeViewport(const Viewport& viewport) const;
	void GetEyeSlopes(const Viewport& viewport, float (&slopes)[4]) const;
	Mat4 GetEyeTransform(const Viewport& viewport, bool interpolate = true) const;
	void ResetSceneInterpolation();
	void PrepareSceneInterpolation(int64_t nowNs);
	void SampleSceneInterpolation(int64_t nowNs, int64_t displayTimeNs, bool matchingEyes = true);
	void SampleSceneInterpolationForEyes(int64_t nowNs, const PresentationEye (&eyes)[2]);
	const float* InterpolatedModel(const Node& node, const Model& model, bool& anchored) const;
	bool ShouldDrawViewport(const Viewport& viewport) const;
	void CaptureRootCamera(UINT32 addr, UINT32 nodeFlags, UINT32 matrixIndex, UINT32 sibling);
	void PrepareRiderAnchor();
	void ApplyRiderComfort();
	void DrawComfortVignette(int x, int y, int width, int height, bool cinematic = false, float amount = 1.0f, const float* headsetAperture = nullptr, const SpatialPanel::State* knownState = nullptr);
	void TraceCameraScene();
	void TraceCameraLandmarks(FILE* file);
	void TraceDrawStateDuplicates(bool bypass);
	bool OpenCameraTrace();
	bool EnsureEyeTargets();
	void DestroyEyeTargets();
	void DrawStereoDiagnostic(const Mat4* headView = nullptr, bool composite = true);
	void PrepareHeadsetScene();
	bool EnsureHeadsetTargets(int width, int height);
	void DestroyHeadsetTargets();
	bool DrawHeadsetPanel(bool theater, int width, int height, const Mat4& view, const Mat4& projection, float unitsPerMeter, const SpatialPanel::State* knownState = nullptr, int theaterEye = 0);
	void AnchorUserMenu(const PresentationEye (&eyes)[2], const Mat4 (&views)[2]);
	bool DrawUserMenu(int width, int height, const Mat4& view, const Mat4& projection, float unitsPerMeter);
	void CompositeStereoEyes();
	bool CanReuseStereoEyes() const;
	bool CanPresentPreparedScene() const;
	bool PresentationSettingsMatch() const;
	void LogDiagnosticScene(size_t centerModels);
	void TranslateTexture(unsigned& x, unsigned& y, int width, int height, int& page) const;

	/*
	* Data
	*/

	// Misc
	std::string m_gameName;
	const Util::Config::Node& m_config;
	bool m_stereoDiagnostic = false;
	bool m_stereoUI = false;
	bool m_stereoMonoPreview = false;
	bool m_stereoCinematicVignette = false;
	bool m_stereoMenuComfort = false;
	bool m_stereoFlatIntro = false;
	bool m_stereoTheaterIntro = false;
	int m_stereoTheaterDepth = 0;
	float m_cinemaUnits = 16.0f, m_cinemaAngle = 70.0f;
	bool m_cinemaCaptureStereo = false, m_eyeImagesStereoTheater = false;
	int m_cinemaCaptureEye = 0;
	GamePresentationState m_gamePresentationState;
	bool m_stereoVRHUD = false, m_stereoHideHUD = false, m_stereoBikeHUD = false;
	SpatialPanel m_spatialPanel;
	Mat4 m_panelHeadView;
	bool m_hudPanelValid = false;
	bool m_hudDepthCertified = false;
	FlatIntro m_flatIntro;
	MenuComfort m_menuComfort;
	MenuComfort::Phase m_menuPhase = MenuComfort::Phase::Other;
	bool m_menuSelectMarkerA = false, m_menuSelectMarkerB = false;
	bool m_menuIntroMarkerA = false, m_menuIntroMarkerB = false;
	bool m_menuLayoutMatch = false;
	bool m_menuRaceLayout = false, m_menuRaceSettled = false;
	unsigned m_menuClockViewports = 0, m_menuWorldViewports = 0, m_menuNativeUI = 0;
	float m_menuClockHeight = -1.0f;
	bool m_menuTicked = false;
	uint64_t m_menuFrame = 0, m_menuEpoch = 0, m_menuComfortTicks = 0;
	uint32_t m_menuCameraViewport = 0;
	int m_menuSteering = 128, m_menuNavigation = 0;
	bool m_menuStart = false;
	CRender2D* m_tileRenderer = nullptr; // non-owning, same GL owner/lifetime as renderer
	bool m_uiOnlyScene = false;
	bool m_hasImmersiveWorld = false;
	enum class ViewportPass { All, World, Overlay };
	ViewportPass m_viewportPass = ViewportPass::All;
	bool m_stereoCullBypass = false;
	float m_stereoLodDistance = 1.0f;
	float m_stereoEyeSeparation = 1.0f;
	float m_stereoYaw = 0.0f;
	float m_stereoPitch = 0.0f;
	bool m_stereoHorizonLock = false;
	bool m_stereoRiderAnchor = false;
	bool m_stereoFullBike = false;
	bool m_stereoStableChase = false;
	float m_stereoChaseHeight = 20.0f;
	float m_stereoChaseBack = 40.0f;
	float m_stereoRiderUp = 0.0f;
	float m_stereoRiderBack = 0.0f;
	bool m_stereoRiderComfort = false;
	float m_stereoVignetteScale = 1.0f;
	uint64_t m_sceneFrame = 0, m_sceneEpoch = 0;
	RiderComfort m_riderComfort;
	RiderComfort::Result m_comfortResult;
	GLSLShader m_comfortShader;
	GLuint m_comfortVao = 0;
	bool m_comfortShaderFailed = false;
	GLSLShader m_cinematicShader;
	GLuint m_cinematicVao = 0;
	bool m_cinematicShaderFailed = false;
	float m_stereoHorizonFov = 90.0f;
	std::vector<uint32_t> m_diagnosticNodePath;
	std::vector<Mat4> m_riderMatrixPath;
	std::vector<bool> m_riderMatrixNodePath;
	struct RiderCandidate {
		uint32_t viewport = 0, objectNode = 0, sourceModel = 0;
		bool topologyValid = false;
		Mat4 bodyView, modelView;
	};
	std::vector<RiderCandidate> m_riderCandidates;
	std::string m_riderLastStatus;
	bool m_recordModelPaths = false;
	struct DuplicateAuditSnapshot { unsigned stage = 0; std::vector<uint8_t> bytes; };
	std::string m_duplicateAuditPath;
	FILE* m_duplicateAuditFile = nullptr;
	bool m_duplicateAuditActive = false, m_duplicateAuditCandidate = false, m_duplicateAuditFailed = false;
	bool m_duplicateAuditMaskComplete = true;
	bool m_duplicateAuditMeshDirty = true, m_duplicateAuditLastLayered = false, m_duplicateAuditLastNoLos = false;
	uint32_t m_duplicateAuditLastSource = 0;
	unsigned m_duplicateAuditDraws = 0, m_duplicateAuditStageCursor = 0, m_duplicateAuditTransitionsLogged = 0;
	size_t m_duplicateAuditBytes = 0, m_duplicateAuditCandidates = 0, m_duplicateAuditSelected = 0;
	size_t m_duplicateAuditSkipped = 0, m_duplicateAuditTransitions = 0;
	std::vector<DuplicateAuditSnapshot> m_duplicateAuditBaseline;
	std::unordered_map<const Model*, std::vector<bool>> m_duplicateAuditMask;
	std::string m_cameraTracePath;
	FILE* m_cameraTraceFile = nullptr;
	bool m_cameraTraceFailed = false, m_cameraTraceModelsWritten = false;
	uint64_t m_cameraTraceFrame = 0, m_cameraTraceLastFrame = 0;
	uint64_t m_cameraTraceStart = 1;
	uint64_t m_drawStateTraceLastFrame[2] = { 0, 0 };
	unsigned m_cameraTraceLimit = 120, m_cameraTraceSamples = 0;
	bool m_diagnosticEyeActive = false;
	bool m_bypassFrustum = false;
	bool m_diagnosticLogPending = true;
	bool m_eyeTargetsFailed = false;
	bool m_eyeImagesValid = false;
	bool m_eyeImagesTheater = false;
	bool m_preparedSceneValid = false;
	uint64_t m_preparedSceneFrame = 0, m_preparedSceneEpoch = 0;
	bool m_diagnosticCenterFallback = false, m_diagnosticCachedPair = false;
	uint64_t m_eyeImageFrame = 0, m_eyeImageEpoch = 0;
	uint64_t m_diagnosticRenderSerial = 0;
	RenderProfiler m_renderProfiler;
	Mat4 m_eyeView;
	bool m_bodyOptIn = false, m_bodyArms = false, m_bodyExploring = false, m_bodyBlackout = false;
	float m_bodyUnits = 16, m_bodyYaw = 0, m_bodyGround = 0;
	float m_bodyVignette = 0;
	HandlebarGrip::Controller m_bodyGrip;
	std::array<TrackedArms::Vec3,2> m_bodyGripPositions{};
	bool m_bodyNearGrip[2] = {false,false};
	uint32_t m_bodyGripRider = 0, m_bodyGripCharacter = 0;
	std::array<float,3> m_bodyPosition{};
	uint64_t m_bodyFrame = 0, m_bodyEpoch = 0;
	TrackedHands m_bodyHands;
	std::vector<InspectionGeometry::Triangle> m_bodyFloor;
	struct BodyArmModel { const Model* source = nullptr; TrackedArms::Matrix matrix{}; };
	std::array<BodyArmModel,6> m_bodyArmModels{};
	std::array<TrackedArms::Vec3,2> m_bodyWrists{};
	float m_bodyWristError[2] = {};
	bool m_bodyWristClamped[2] = {false,false};
	std::array<TrackedArms::Quaternion,2> m_bodyHandAlignment{};
	bool m_bodyHandAligned[2] = {false,false};
	const Viewport* BodyAnchor() const;
	bool BuildInspectionFloor();
	void PrepareTrackedArms(int64_t displayTimeNs);
	const float* BodyArmMatrix(const Model& model) const;
	bool IsBodyArm(const Viewport& viewport, const Model& model) const;
	Mat4 BodyNavigationView() const;
	Mat4 m_presentationHeadView;
	bool m_presentationPoseSet = false, m_presentationPoseIdentity = true;
	struct EyeTarget { GLuint framebuffer = 0; GLuint texture = 0; };
	bool m_headsetPresentation = false, m_headsetEyeActive = false;
	Mat4 m_headsetProjection;
	int m_headsetEyeWidth = 0, m_headsetEyeHeight = 0;
	int m_headsetTargetWidth = 0, m_headsetTargetHeight = 0;
	std::unique_ptr<R3DFrameBuffers> m_headsetFrameBuffers;
	HeadsetVisibilityMask m_visibilityMasks;
	EyeTarget m_headsetTarget;
	EyeTarget m_hudTarget;
	EyeTarget m_eyeTargets[2];
	int m_eyeSourceX = 0, m_eyeSourceY = 0;
	int m_eyeSourceWidth = 0, m_eyeSourceHeight = 0;
	DesktopEyeLayout m_eyeLayout;
	int m_numPolyVerts;
	GLenum m_primType;

	// GPU configuration
	std::atomic_bool m_sunClamp;
	std::atomic_bool m_blockCulling;
	bool m_noWhiteFlash;

	// Stepping
	int		m_step;
	int		m_offset;			// offset to subtract for words 3 and higher of culling nodes
	float	m_vertexFactor;		// fixed-point conversion factor for vertices
	float	m_textureNPFactor;	// fixed-point conversion factor for texture NP values

	// Memory (passed from outside)
	const UINT32	*m_cullingRAMLo;	// 4 MB
	const UINT32	*m_cullingRAMHi;	// 1 MB
	const UINT32	*m_polyRAM;			// 4 MB
	const UINT32	*m_vrom;			// 64 MB
	const UINT16	*m_textureRAM;		// 8 MB

	// Resolution and scaling factors (to support resolutions higher than 496x384) and offsets
	float		m_xRatio, m_yRatio;
	unsigned	m_xOffs, m_yOffs;
	unsigned	m_xRes, m_yRes;           // resolution of Model 3's 496x384 display area within the window
	unsigned 	m_totalXRes, m_totalYRes; // total OpenGL window resolution
	bool		m_wideScreen;

	// Real3D Base Matrix Pointer
	const float	*m_matrixBasePtr;
	UINT32 m_colorTableAddr = 0x400;		// address of color table in polygon RAM
	LODBlendTable* m_LODBlendTable;

	NodeAttributes	m_nodeAttribs;
	Mat4			m_modelMat;				// current modelview matrix

	struct LOS
	{
		float value[4] = { 0,0,0,0 };		// line of sight value for each priority layer
	} m_los[2];

	LOS* m_losFront = &m_los[0];			// we need to double buffer this because 3d works in separate thread
	LOS* m_losBack = &m_los[1];
	std::mutex m_losMutex;

	Vertex			m_prev[4];				// these are class variables because sega bass fishing starts meshes with shared vertices from the previous one
	UINT16			m_prevTexCoords[4][2];	// basically relying on undefined behavour

	std::vector<Node>	 m_nodes;				// this represents the entire render frame
	SceneModelBuffers m_sceneModelBuffers;	// bounded empty storage; no models outlive their scene
	// Bounded transforms only. Model/node pointers refer exclusively to the
	// current prepared scene; previous records are matched by unique ROM asset
	// and viewport signature, never traversal addresses or vector positions.
	struct InterpolationModel {
		uint32_t source = 0;
		uint64_t signature = 0;
		float scale = 1; // constant positive uniform model scale; cameras remain rigid
		SceneInterpolation::Pose pose, previous;
		SceneInterpolation::Matrix sampled{};
		bool rigid = false, matched = false, anchored = false;
	};
	struct InterpolationViewport {
		std::array<uint32_t, 5> key{};
		const Node* node = nullptr;
		size_t offset = 0, count = 0;
		SceneInterpolation::Pose camera, previous;
		SceneInterpolation::Matrix game{}, inverseGame{}, correction{};
		bool matched = false;
	};
	std::vector<InterpolationModel> m_interpolationModels, m_previousInterpolationModels;
	std::vector<InterpolationViewport> m_interpolationViewports, m_previousInterpolationViewports;
	std::vector<size_t> m_interpolationOrder, m_previousInterpolationOrder;
	SceneInterpolation::Clock m_scenePlayback;
	SceneInterpolation::Sample m_interpolationSample;
	bool m_sceneInterpolationEnabled = false, m_sceneInterpolationActive = false;
	uint64_t m_interpolationFrame = 0, m_interpolationEpoch = 0, m_interpolationPair = 0;
	uint32_t m_interpolationMatched = 0, m_interpolationFallback = 0, m_interpolationAnchored = 0;
	std::vector<FVertex> m_polyBufferRam;		// dynamic polys
	std::vector<FVertex> m_polyBufferRom;		// rom polys
	std::unordered_map<UINT32, std::shared_ptr<std::vector<Mesh>>> m_romMap;	// a hash table for all the ROM models. The meshes don't have model matrices or tex offsets yet
	bool m_romCacheStressApplied = false;
	bool m_romCacheResetPending = false;
	bool m_geometryUploadFailed = false;
	TextureBank			m_textureBank[2];

	GLuint m_vao;
	VBO m_vbo;								// large VBO to hold our poly data, start of VBO is ROM data, ram polys follow
	R3DShader m_r3dShader;
	R3DScrollFog m_r3dScrollFog;
	R3DFrameBuffers m_r3dFrameBuffers;
	GLuint m_aaTarget;						// optional, maybe zero

	struct
	{
		float bnlu;
		float bnlv;
		float bntu;
		float bntw;
		float bnru;
		float bnrv;
		float bnbu;
		float bnbw;
		float correction;
	} m_planes;	

	// Presentation-only UI. Append storage so existing renderer fields retain
	// their offsets; the owner of this texture is the application menu.
	GLuint m_userMenuTexture = 0;
	uint64_t m_userMenuRevision = 0;
	bool m_userMenuVisible = false, m_userMenuAnchorValid = false;
	float m_userMenuPositionMeters[3] = {};
	float m_userMenuYawDegrees = 0;
};

} // New3D

#endif  // INCLUDED_NEW3D_H
