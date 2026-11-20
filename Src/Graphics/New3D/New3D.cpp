/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
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

#include "New3D.h"
#include "HarleyRiderProfiles.h"
#include "RomGeometryCache.h"
#include "HeadsetGeometry.h"
#include "StereoCinema.h"
#include "HeadsetVignette.h"
#include "HeadsetGpuProfiler.h"
#include <cstdlib>
#include "BikeHUD.h"
#include "Graphics/Render2D.h"
#include "Vec.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>
#include <unordered_map>
#include <map>
#include <cerrno>
#include <filesystem>
#include <chrono>
#include <optional>
#include <fcntl.h>
#ifdef SUPERMODEL_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "R3DFloat.h"
#include "Util/BitCast.h"
#include "OSD/Logger.h"
#include "OSD/SDL/OwnerRenderTrace.h"

#define MAX_RAM_VERTS 300000
#define MAX_ROM_VERTS 1500000

#define BYTE_TO_FLOAT(B)	((2.0f * (B) + 1.0f) * (float)(1.0/255.0))

#define NEAR_PLANE 1e-3f

namespace New3D {

namespace {

// Optional probe owns no renderer members, so the production class layout and
// all unrelated linked objects stay unchanged. GL resources close explicitly
// from the renderer destructor, never from a process-global static destructor.
struct HeadsetGpuBackend {
    static bool TraceEnabled() { return LiveFrameTrace::OwnerEnabled(); }
    static uint64_t Now() { return LiveFrameTrace::RawNow(); }
    static uintptr_t Context() {
#ifdef SUPERMODEL_WIN32
        return reinterpret_cast<uintptr_t>(wglGetCurrentContext());
#else
        return 0; // This private diagnostic is for the Windows OpenXR path.
#endif
    }
    static uint64_t Thread() {
#ifdef SUPERMODEL_WIN32
        return GetCurrentThreadId();
#else
        return 0;
#endif
    }
    static bool Supported(unsigned& bits) {
        bits=0;
        if(!(GLEW_VERSION_3_3 || GLEW_ARB_timer_query) || !glQueryCounter ||
            !glGetQueryObjectui64v || !glGetQueryObjectiv || !glGenQueries || !glDeleteQueries)
            return false;
        GLint result=0;glGetQueryiv(GL_TIMESTAMP,GL_QUERY_COUNTER_BITS,&result);
        if(result>0)bits=unsigned(result);
        return bits>0;
    }
    static void Generate(unsigned count,unsigned* queries) { glGenQueries(GLsizei(count),queries); }
    static void Delete(unsigned count,const unsigned* queries) {
        if(std::any_of(queries,queries+count,[](unsigned value){return value!=0;}))
            glDeleteQueries(GLsizei(count),queries);
    }
    static void Stamp(unsigned query) { glQueryCounter(query,GL_TIMESTAMP); }
    static bool Available(unsigned query) {
        GLint ready=GL_FALSE;glGetQueryObjectiv(query,GL_QUERY_RESULT_AVAILABLE,&ready);
        return ready==GL_TRUE;
    }
    static uint64_t Read(unsigned query) {
        GLuint64 value=0;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&value);return value;
    }
    static void Emit(const char* stage,uint64_t begin,uint64_t end,uint64_t frame,
                     int64_t a,int64_t b,int64_t c,int64_t d) {
        LiveFrameTrace::Event(stage,begin,end,frame,a,b,c,d);
    }
    static void Summary(unsigned bits,const HeadsetGpuTiming::Counters& c) {
        InfoLog("Headset GPU timing: schema=1 bits=%u ring=16 markers=18 pairs=%llu sampled=%llu ready=%llu partial=%llu invalid=%llu dropped=%llu unavailable=%llu unresolved=%llu context_lost=%llu rejected=%llu capacity_skipped=%llu markers_read=%llu first_serial=%llu last_serial=%llu.",
            bits,(unsigned long long)c.pairs,(unsigned long long)c.sampled,
            (unsigned long long)c.ready,(unsigned long long)c.partial,(unsigned long long)c.invalid,
            (unsigned long long)c.dropped,(unsigned long long)c.unavailable,(unsigned long long)c.unresolved,
            (unsigned long long)c.contextLost,(unsigned long long)c.rejected,
            (unsigned long long)c.capacitySkipped,(unsigned long long)c.markersRead,
            (unsigned long long)c.firstSerial,(unsigned long long)c.lastSerial);
    }
};
using HeadsetGpuProbeType=HeadsetGpuTiming::Probe<HeadsetGpuBackend>;
HeadsetGpuProbeType& HeadsetGpuProbe() { static HeadsetGpuProbeType probe;return probe; }
bool HeadsetGpuRequested() {
    static const bool enabled=[] {
        const char* value=std::getenv("SMVR_HEADSET_GPU_TIMING");
        return value && std::strcmp(value,"1")==0;
    }();
    return enabled;
}

int64_t SceneInterpolationNow()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t InterpolationSignature(const Model& model)
{
	// A signature is a continuity guard, not an instance identity. Repeated
	// sources additionally require mutually unambiguous spatial pose cohorts.
	uint64_t hash = 14695981039346656037ull;
	auto add = [&](uint32_t word) { hash ^= word; hash *= 1099511628211ull; };
	auto number = [&](float f) { uint32_t bits; memcpy(&bits, &f, 4); add(bits); };
	add(model.textureOffsetX); add(model.textureOffsetY); add(model.page);
	number(model.scale); number(model.alpha); add(model.hideInRiderEyes);
	if (!model.meshes) return 0;
	add(uint32_t(model.meshes->size()));
	for (const auto& mesh : *model.meshes) {
		add(mesh.format); add(mesh.x); add(mesh.y); add(mesh.width); add(mesh.height); add(mesh.page); add(mesh.inverted);
		add(mesh.wrapModeU); add(mesh.wrapModeV); add(mesh.microTexture); add(mesh.microTextureID); number(mesh.microTextureMinLOD);
		add(mesh.textured); add(mesh.polyAlpha); add(mesh.textureAlpha); add(mesh.alphaTest); add(mesh.layered); add(mesh.highPriority);
		add(mesh.transLSelect); add(mesh.translatorMap); add(mesh.noLosReturn); add(mesh.fixedShading); add(mesh.smoothShading);
		add(mesh.lighting); add(mesh.specular); number(mesh.shininess); number(mesh.specularValue); number(mesh.fogIntensity); add(mesh.vertexCount);
	}
	return hash;
}

// Reject scale, shear, reflection and invalid values instead of treating an
// arbitrary model transform as a camera. Real3D transforms use column-major GL.
bool InvertRigidCamera(const Mat4& view, Mat4& inverse)
{
	const float* m = view;
	for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
	if (std::abs(m[3]) > 1e-5f || std::abs(m[7]) > 1e-5f ||
		std::abs(m[11]) > 1e-5f || std::abs(m[15] - 1.0f) > 1e-5f) return false;
	for (int a = 0; a < 3; ++a) {
		for (int b = 0; b < 3; ++b) {
			float dot = 0.0f;
			for (int row = 0; row < 3; ++row) dot += m[a * 4 + row] * m[b * 4 + row];
			if (std::abs(dot - (a == b ? 1.0f : 0.0f)) > 0.001f) return false;
		}
	}
	const float determinant = m[0] * (m[5] * m[10] - m[9] * m[6]) -
		m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
	if (std::abs(determinant - 1.0f) > 0.001f) return false;
	inverse.LoadIdentity();
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) inverse[col * 4 + row] = m[row * 4 + col];
		inverse[12 + row] = -(m[row * 4] * m[12] + m[row * 4 + 1] * m[13] + m[row * 4 + 2] * m[14]);
	}
	return true;
}

bool MakeLevelCamera(const Mat4& game, Mat4& level, Mat4& correction)
{
	Mat4 inverse;
	if (!InvertRigidCamera(game, inverse)) return false;
	// World Y is the measured up axis in the Harley traces. Keep horizontal
	// heading and the same position; no rider offset or translation filtering.
	const float headingLength = std::hypot(game[2], game[10]);
	if (headingLength < 1e-4f) return false;
	const float forwardX = -game[2] / headingLength;
	const float forwardZ = -game[10] / headingLength;
	Mat4 levelRotation;
	levelRotation[0] = -forwardZ;
	levelRotation[8] = forwardX;
	levelRotation[2] = -forwardX;
	levelRotation[10] = -forwardZ;
	inverse[12] = inverse[13] = inverse[14] = 0.0f;
	correction = levelRotation;
	correction.MultMatrix(inverse);
	// The same-position correction is pure rotation; avoid subtracting large
	// world translations and introducing a floating-point position offset.
	level = correction;
	level.MultMatrix(game);
	return true;
}

bool MakePresentationView(const IRender3D::PresentationPose& pose, Mat4& inverse)
{
	double normSquared = 0.0;
	for (float component : pose.orientation) {
		if (!std::isfinite(component)) return false;
		normSquared += double(component) * component;
	}
	// Tracking quaternions can accumulate rounding error, but an arbitrary
	// non-unit/zero quaternion is not a valid head pose. Reject instead of hiding
	// a coordinate conversion error. Positions are local offsets, not world GPS.
	if (std::abs(normSquared - 1.0) > 0.01) return false;
	for (float component : pose.position)
		if (!std::isfinite(component) || std::abs(component) > 1000.0f) return false;
	const float scale = float(1.0 / std::sqrt(normSquared));
	const float x = pose.orientation[0] * scale, y = pose.orientation[1] * scale;
	const float z = pose.orientation[2] * scale, w = pose.orientation[3] * scale;
	Mat4 head;
	head[0] = 1.0f - 2.0f * (y*y + z*z);
	head[1] = 2.0f * (x*y + z*w);
	head[2] = 2.0f * (x*z - y*w);
	head[4] = 2.0f * (x*y - z*w);
	head[5] = 1.0f - 2.0f * (x*x + z*z);
	head[6] = 2.0f * (y*z + x*w);
	head[8] = 2.0f * (x*z + y*w);
	head[9] = 2.0f * (y*z - x*w);
	head[10] = 1.0f - 2.0f * (x*x + y*y);
	std::copy(pose.position, pose.position + 3, head.currentMatrix + 12);
	return InvertRigidCamera(head, inverse);
}

void TraceFloats(FILE* file, const float* values, size_t count)
{
	fputc('[', file);
	for (size_t i = 0; i < count; ++i) {
		if (i) fputc(',', file);
		if (std::isfinite(values[i])) fprintf(file, "%.9g", double(values[i]));
		else fputs("null", file);
	}
	fputc(']', file);
}

// Diagnostic words preserve exact float bits and omit C++ padding/pointers.
// They are never used to suppress or change a draw.
struct DrawTraceWords {
	std::vector<uint32_t> values;
	void Add(uint32_t value) { values.push_back(value); }
	void Float(float value) { uint32_t bits; memcpy(&bits, &value, sizeof(bits)); Add(bits); }
	void Floats(const float* values, size_t count) { for (size_t i = 0; i < count; ++i) Float(values[i]); }
	uint64_t Hash() const {
		uint64_t hash = 14695981039346656037ull;
		for (uint32_t word : values) for (unsigned shift = 0; shift < 32; shift += 8) {
			hash ^= (word >> shift) & 255u; hash *= 1099511628211ull;
		}
		return hash;
	}
};

DrawTraceWords DrawTraceModel(const Model& model)
{
	DrawTraceWords words;
	words.Add(model.sourceAddress); words.Add(model.dynamic);
	words.Add(model.textureOffsetX); words.Add(model.textureOffsetY); words.Add(model.page);
	words.Float(model.scale); words.Float(model.alpha); words.Add(model.hideInRiderEyes);
	words.Floats(model.modelMat, 16);
	return words;
}

DrawTraceWords DrawTraceMesh(const Mesh& mesh)
{
	DrawTraceWords words;
	words.Add(mesh.format); words.Add(mesh.x); words.Add(mesh.y); words.Add(mesh.width);
	words.Add(mesh.height); words.Add(mesh.page); words.Add(mesh.inverted);
	words.Add(mesh.wrapModeU); words.Add(mesh.wrapModeV); words.Add(mesh.microTexture);
	words.Add(mesh.microTextureID); words.Float(mesh.microTextureMinLOD);
	words.Add(mesh.textured); words.Add(mesh.polyAlpha); words.Add(mesh.textureAlpha);
	words.Add(mesh.alphaTest); words.Add(mesh.layered); words.Add(mesh.highPriority);
	words.Add(mesh.transLSelect); words.Add(mesh.translatorMap); words.Add(mesh.noLosReturn);
	words.Add(mesh.fixedShading); words.Add(mesh.smoothShading); words.Add(mesh.lighting);
	words.Add(mesh.specular); words.Float(mesh.shininess); words.Float(mesh.specularValue);
	words.Float(mesh.fogIntensity); words.Add(mesh.vertexCount);
	return words;
}

DrawTraceWords DrawTraceViewport(const Viewport& vp)
{
	DrawTraceWords words;
	words.Add(vp.sourceAddress); words.Add(vp.priority); words.Add(vp.select); words.Add(vp.number);
	words.Add(vp.x); words.Add(vp.y); words.Add(vp.width); words.Add(vp.height);
	words.Float(vp.vpX); words.Float(vp.vpY); words.Float(vp.vpWidth); words.Float(vp.vpHeight);
	words.Float(vp.angle_left); words.Float(vp.angle_right); words.Float(vp.angle_bottom); words.Float(vp.angle_top);
	words.Float(vp.cota); words.Floats(vp.projectionMatrix, 16);
	words.Floats(vp.lightingParams, 6); words.Add(vp.sunClamp); words.Add(vp.intensityClamp);
	words.Floats(vp.spotEllipse, 4); words.Floats(vp.spotRange, 2); words.Floats(vp.spotColor, 3);
	words.Floats(vp.fogColour, 3); words.Float(vp.fogStart); words.Float(vp.fogDensity);
	words.Float(vp.fogAttenuation); words.Float(vp.fogAmbient); words.Float(vp.scrollFog);
	words.Add(vp.losPosX); words.Add(vp.losPosY); words.Floats(vp.spotFogColor, 3);
	words.Float(vp.scrollAtt); words.Add(vp.hardwareStep);
	words.Add(vp.worldCandidate); words.Add(vp.presentationWorld); words.Add(vp.cameraValid);
	return words;
}

void TraceWords(FILE* file, const std::vector<uint32_t>& words)
{
	fputc('[', file);
	for (size_t i = 0; i < words.size(); ++i) {
		if (i) fputc(',', file);
		fprintf(file, "%u", words[i]);
	}
	fputc(']', file);
}

} // anonymous namespace

CNew3D::CNew3D(const Util::Config::Node &config, const std::string& gameName) : 
	m_gameName(gameName),
	m_config(config),
	m_blockCulling(false),
	m_cullingRAMLo(nullptr),
	m_cullingRAMHi(nullptr),
	m_polyRAM(nullptr),
	m_vrom(nullptr),
	m_textureRAM(nullptr),
	m_xRatio(0),
	m_yRatio(0),
	m_xOffs(0),
	m_yOffs(0),
	m_xRes(0),
	m_yRes(0),
	m_totalXRes(0),
	m_totalYRes(0),
	m_wideScreen(false),
	m_matrixBasePtr(nullptr),
	m_LODBlendTable(nullptr),
	m_prev{{}},
	m_prevTexCoords{{}},
	m_vao(0),
	m_r3dShader(config),
	m_r3dScrollFog(),
	m_aaTarget(0)
{
	m_sunClamp		= true;
	m_bodyOptIn = config["BodyInteraction"].ValueAsDefault<bool>(false);
	m_bodyArms = m_bodyOptIn;
	m_numPolyVerts	= 3;
	m_primType		= GL_TRIANGLES;

	if (config["QuadRendering"].ValueAs<bool>()) {
		m_numPolyVerts	= 4;
		m_primType		= GL_LINES_ADJACENCY;
	}

	m_wideScreen = config["WideScreen"].ValueAs<bool>();
	m_noWhiteFlash = config["NoWhiteFlash"].ValueAs<bool>();

	m_r3dShader.LoadShader();
	glUseProgram(0);

	// setup up our vertex buffer memory

	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);
	m_vbo.Create(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, sizeof(FVertex) * (MAX_RAM_VERTS + MAX_ROM_VERTS));
	m_vbo.Bind(true);

	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inVertex"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inNormal"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inTexCoord"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inColour"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inFaceNormal"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inFixedShade"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inTextureNP"));

	// before draw, specify vertex and index arrays with their offsets, offsetof is maybe evil ..
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inVertex"), 4, GL_FLOAT, GL_FALSE, sizeof(FVertex), 0);
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inNormal"), 3, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, base.normal));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inTexCoord"), 2, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, base.texcoords));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inColour"), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FVertex), (void*)offsetof(FVertex, faceColour));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inFaceNormal"), 3, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, faceNormal));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inFixedShade"), 1, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, base.fixedShade));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inTextureNP"), 1, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, textureNP));

	glBindVertexArray(0);
	m_vbo.Bind(false);
	m_spatialPanel.EnableCpuTiming(!m_config["HeadsetWorkerTest"].ValueAsDefault<std::string>("").empty());
	m_duplicateAuditPath = m_config["StereoDuplicateAudit"].ValueAsDefault<std::string>("");
}

CNew3D::~CNew3D()
{
	HeadsetGpuProbe().Close(this);
	if (m_config["OpenXRVisibilityMask"].ValueAsDefault<bool>(false)) {
		const auto& v = m_visibilityMasks.counters;
		InfoLog("Headset visibility mask cache: %llu builds, %llu hits, %llu empty, %llu invalid, %llu failures; renderer shader %s.",
			(unsigned long long)v.builds, (unsigned long long)v.cacheHits, (unsigned long long)v.empty,
			(unsigned long long)v.invalid, (unsigned long long)v.failures,
			m_r3dShader.VisibilityMaskSupported() ? "available" : "original fallback");
	}
	m_visibilityMasks.Destroy();

	DestroyHeadsetTargets();
	m_renderProfiler.Close();
	m_spatialPanel.WriteCpuTimings(m_config["HeadsetWorkerTest"].ValueAsDefault<std::string>(""));
	m_spatialPanel.Destroy();
	if (m_comfortVao) glDeleteVertexArrays(1, &m_comfortVao);
	if (m_cinematicVao) glDeleteVertexArrays(1, &m_cinematicVao);
	if (m_cameraTraceFile) fclose(m_cameraTraceFile);
	if (m_duplicateAuditFile) {
		fprintf(m_duplicateAuditFile, "{\"type\":\"duplicate_audit_end\",\"draws\":%u,\"failed\":%s}\n",
			m_duplicateAuditDraws, m_duplicateAuditFailed ? "true" : "false");
		fclose(m_duplicateAuditFile);
	}
	DestroyEyeTargets();
	m_vbo.Destroy();
	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
		m_vao = 0;
	}

	m_r3dShader.UnloadShader();
}

void CNew3D::AttachMemory(const UINT32 *cullingRAMLoPtr, const UINT32 *cullingRAMHiPtr, const UINT32 *polyRAMPtr, const UINT32 *vromPtr, const UINT16 *textureRAMPtr)
{
	InvalidatePresentation();
	m_cullingRAMLo	= cullingRAMLoPtr;
	m_cullingRAMHi	= cullingRAMHiPtr;
	m_polyRAM		= polyRAMPtr;
	m_vrom			= vromPtr;
	m_textureRAM	= textureRAMPtr;

	m_textureBank[0].AttachMemory(textureRAMPtr);
	m_textureBank[1].AttachMemory(textureRAMPtr + (2048*1024));
}

void CNew3D::SetStepping(int stepping)
{
	InvalidatePresentation();
	m_step = stepping;

	if ((m_step != 0x10) && (m_step != 0x15) && (m_step != 0x20) && (m_step != 0x21)) {
		m_step = 0x10;
	}

	if (m_step > 0x10) {
		m_offset = 0;							// culling nodes are 10 words
		m_vertexFactor = (1.0f / 2048.0f);		// vertices are in 13.11 format
		m_textureNPFactor = (1.0f / 16384.0f);	// texture NP values are in 10.14 format
	}
	else {
		m_offset = 2;							// 8 words
		m_vertexFactor = (1.0f / 128.0f);		// 17.7
		m_textureNPFactor = (1.0f / 4096.0f);	// 12.12
	}
}

Result CNew3D::Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXResParam, unsigned totalYResParam, unsigned aaTarget)
{
	ResetMenuComfort();
	m_presentationHeadView.LoadIdentity();
	m_presentationPoseSet = false;
	m_presentationPoseIdentity = true;
	// Resolution and offset within physical display area
	m_xRatio	= xRes * (float)(1.0 / 496.0);
	m_yRatio	= yRes * (float)(1.0 / 384.0);
	m_xOffs		= xOffset;
	m_yOffs		= yOffset;
	m_xRes		= xRes;
	m_yRes		= yRes;
	m_totalXRes	= totalXResParam;
	m_totalYRes = totalYResParam;
	m_aaTarget	= aaTarget;

	DestroyEyeTargets();
	m_eyeTargetsFailed = false;
	DestroyHeadsetTargets();
	m_diagnosticLogPending = true;
	m_r3dFrameBuffers.DestroyFBO();		// remove any old ones if created
	if (!m_renderProfiler.Configure(m_config["FrameProfile"].ValueAsDefault<std::string>(""),
		m_config["FrameProfileStart"].ValueAsDefault<unsigned>(121),
		m_config["FrameProfileFrames"].ValueAsDefault<unsigned>(600),
		m_config["MultiThreaded"].ValueAsDefault<bool>(true),
		m_config["GPUMultiThreaded"].ValueAsDefault<bool>(true),
		{ xOffset, yOffset, xRes, yRes, totalXResParam, totalYResParam },
		m_config["Supersampling"].ValueAsDefault<unsigned>(1))) return Result::FAIL;

	return m_r3dFrameBuffers.CreateFBO(totalXResParam, totalYResParam);
}

void CNew3D::UploadTextures(unsigned level, unsigned x, unsigned y, unsigned width, unsigned height)
{
	// Prepared geometry and these GPU textures must describe the same snapshot.
	m_preparedSceneValid = false;
	// handle case of entire sheet invalidation
	if (width == 2048 && height == 2048) {

		height = 1024;

		static constexpr int mipXBase[] = { 0, 1024, 1536, 1792, 1920, 1984, 2016, 2032, 2040, 2044, 2046, 2047 };
		static constexpr int mipYBase[] = { 0, 512, 768, 896, 960, 992, 1008, 1016, 1020, 1022, 1023 };

		for (int i = 0; i < m_textureBank[0].GetNumberOfLevels(); i++) {
			m_textureBank[0].UploadTextures(i, mipXBase[i], mipYBase[i], width, height);
			m_textureBank[1].UploadTextures(i, mipXBase[i], mipYBase[i], width, height);
			width = (width > 1) ? width / 2 : 1;
			height = (height > 1) ? height / 2 : 1;
		}

		return;
	}

	int page;
	TranslateTexture(x, y, width, height, page);

	m_textureBank[page].UploadTextures(level, x, y, width, height);
}

void CNew3D::DrawScrollFog()
{
	// this is my best guess at the logic based upon what games are doing
	//
	// ocean hunter		- every viewport has scroll fog values set. Must start with lowest priority layers as the higher ones sometimes are garbage
	// scud race		- first viewports in priority layer missing scroll values. The latter ones all contain valid scroll values.
	// daytona			- doesn't seem to use scroll fog at all. Will set scroll values for the first viewports, the end ones contain no scroll values. End credits have scroll fog, but constrained to the viewport
	// vf3				- first viewport only has it set. But set with highest select value ?? Rest of the viewports in priority layer contain a lower select value
	// sega bassfishing	- first viewport in priority 1 sets scroll value. The rest all contain the wrong value + a higher select value ..
	// spikeout final	- 2nd viewport in the priority layer has scroll values set, none of the others do. It also uses the highest select value

	// I think the basic logic is this: the real3d picks the highest scroll fog value, starting from the lowest priority layer. 
	// If it finds a value for priority layer 0 for example, it then bails out looking for any more.
	// Fogging seems to be constrained to whatever the viewport is that is set.
	// Scroll fog needs a density or start value to work, but these can come from another viewport if the fog colour is the same

	// For fullscreen viewports I am fairly certain the fogging aspect is drawn at the mixer level, not directly to the frame buffer, otherwise alpha can't work correctly.
	// For spotlight on fog, I think it must be drawn with the 3d h/w. Think only ECA uses this, maybe ocean hunter

	Node* nodePtr = nullptr;

	for (int i = 0; i < 4 && !nodePtr; i++) {
		for (auto &n : m_nodes) {
			if (!ShouldDrawViewport(n.viewport)) continue;
			if (n.viewport.priority == i) {
				if (n.viewport.scrollFog > 0.f) {

					// check to see if we have a higher scroll fog value
					if (nodePtr) {
						if (nodePtr->viewport.scrollFog < n.viewport.scrollFog) {
							nodePtr = &n;
						}

						continue;
					}

					nodePtr = &n;
				}
			}
		}
	}

	if (nodePtr) {

		// iterate nodes to see if any viewports with that fog colour actually set a fog density, start or spotlight on fog
		// if all of these are zero fogging is effectively disabled

		for (auto& n : m_nodes) {
			if (!ShouldDrawViewport(n.viewport)) continue;

			if (nodePtr->viewport.fogColour[0] == n.viewport.fogColour[0] &&
				nodePtr->viewport.fogColour[1] == n.viewport.fogColour[1] &&
				nodePtr->viewport.fogColour[2] == n.viewport.fogColour[2])
			{

				// spotlight on fog is disabled if colour = black
				bool spotOnFogEnabled = false;
				for (auto c : n.viewport.spotFogColor) {
					if (c > 0.0f) {
						spotOnFogEnabled = true;
						break;
					}
				}

				bool fog			= n.viewport.fogDensity > 0.0f || n.viewport.fogStart > 0.0f || n.viewport.scrollAtt > 0.0f;
				bool spotlightFog	= n.viewport.fogAttenuation > 0.0f && spotOnFogEnabled;

				if (fog || spotlightFog) {

					auto vp = GetRenderViewport(nodePtr->viewport);
					float rgba[4];
					
					rgba[0] = vp.fogColour[0];
					rgba[1] = vp.fogColour[1];
					rgba[2] = vp.fogColour[2];
					rgba[3] = vp.scrollFog;

					float fogAttentuation = (fog && spotlightFog) ? 0.0f : n.viewport.fogAttenuation;	// if regular fog is enabled h/w seems to only apply this and skip the spotlight on fog (ECA)

					glViewport(vp.x, vp.y, vp.width, vp.height);

					Viewport fogViewport = GetRenderViewport(n.viewport);
					m_r3dScrollFog.DrawScrollFog(rgba, fogAttentuation, fogViewport.fogAmbient, fogViewport.spotFogColor, fogViewport.spotEllipse);
					break;
				}
			}
		}
	}
}

void CNew3D::DrawAmbientFog()
{
	// logic here is still not totally understood
	// some games are setting fog ambient which seems to darken the 2d background layer too when scroll fogging is not set
	// The logic is something like tileGenColour * fogAmbient
	// If fogAmbient = 1.0 it's a no-op. Lower values darken the image
	// Does this work with scroll fog? Well technically scroll fog already takes into account the fog ambient as it darkens the fog colour
	// Like scroll fog I am sure this works at the mixer level, rather than drawing into the frame buffer.

	// lemans24 every viewport will set ambient fog, scroll attentuation is sometimes set (for every viewport) for explosion effects from car exhaust. So has no effect on ambient fog
	// otherwise we'll make the ambient fog flash 
	// sega rally will set ambient fog to zero for every viewport in priority layers 1-3 with a fog density set. Disabled viewports with priority zero have ambient fog disabled (1.0). Don't think srally uses ambient fog
	// vf3 almost all viewports in all priority layers have ambient fog set (<1.0)
	// lost world is setting an ambient fog value every every viewport but has no density or fog start value set. Don't think lost world is using ambient fog

	// Let's pick the lowest fog ambient value from only the first priority layer
	// Check for fog density or a fog start value, otherwise the effect seems to be disabled (lost world)

	float fogAmbient = 1.0f;
	Node* nodePtr = nullptr;

	for (int i = 0; i < 4; i++) {

		bool hasPriority = false;

		for (auto& n : m_nodes) {

			auto& vp = n.viewport;
			if (!ShouldDrawViewport(vp)) continue;

			if (vp.priority == i) {

				hasPriority = true;
				
				if (vp.scrollFog > 0.0f) {
					continue;
				}

				// if we don't have a fog density or fog start skip this viewport
				if (vp.fogDensity <= 0.0f && vp.fogStart <= 0.0f) {
					continue;
				}

				if (vp.fogAmbient < fogAmbient) {
					nodePtr = &n;
					fogAmbient = vp.fogAmbient;
				}
			}
		}

		if (nodePtr || hasPriority) {
			break;
		}

	}

	if (nodePtr) {
		auto vp = GetRenderViewport(nodePtr->viewport);
		float rgba[] = { 0.0f, 0.0f, 0.0f, 1.0f - fogAmbient };
		glViewport(vp.x, vp.y, vp.width, vp.height);
		m_r3dScrollFog.DrawScrollFog(rgba, 0.0f, 1.0f, vp.spotFogColor, vp.spotEllipse); // we assume spot light is not used
	}
}

bool CNew3D::RenderScene(int priority, bool renderOverlay, Layer layer)
{
	glActiveTexture(GL_TEXTURE0);
	m_textureBank[0].Bind();
	glActiveTexture(GL_TEXTURE1);
	m_textureBank[1].Bind();
	glActiveTexture(GL_TEXTURE0);

	bool hasOverlay = false;		// (high priority polys)
	// Presentation settings and the classified scene remain fixed throughout
	// this draw. Avoid reclassifying them for every submitted model.
	const bool hideDrivingHUD = m_diagnosticEyeActive && (m_stereoHideHUD || m_bodyExploring) && DrivingHUDScene();
	const bool hideRiderModels = m_diagnosticEyeActive && !FlatIntroActive();

	for (auto &n : m_nodes) {

		if (n.viewport.priority != priority || n.models.empty() || !ShouldDrawViewport(n.viewport)) {
			continue;
		}

		const Viewport viewport = GetRenderViewport(n.viewport);
		glViewport(viewport.x, viewport.y, viewport.width, viewport.height);

		m_r3dShader.SetViewportUniforms(&viewport);
		const Mat4 viewportEyeTransform = m_diagnosticEyeActive ? GetEyeTransform(n.viewport) : Mat4();
		std::optional<Viewport> anchoredViewport;
		if (m_sceneInterpolationActive && m_headsetEyeActive) anchoredViewport = GetRenderViewport(n.viewport, false);
		bool anchoredLighting = false;
		const bool hideRouteArrows = hideDrivingHUD && n.viewport.presentationWorld;

		for (auto &m : n.models) {
			// These two verified Harley ROM meshes are the floating yellow/green
			// route arrows. Suppress only their presentation in the no-HUD ride;
			// native decoding, LOS, city geometry and traffic remain untouched.
			if (hideRouteArrows &&
				(m.sourceAddress == 0x81AAA9 || m.sourceAddress == 0x81AB55)) continue;
			if (hideRiderModels && m.hideInRiderEyes) continue;
			if (hideRiderModels && m_bodyExploring && IsBodyArm(n.viewport,m) && !BodyArmMatrix(m)) continue;

			bool matrixLoaded = false;

			if (m.meshes->empty()) {
				continue;
			}

			const std::vector<bool>* omissionMask = nullptr;
			if (m_duplicateAuditCandidate && m_headsetEyeActive && layer == Layer::colour && !renderOverlay) {
				const auto found = m_duplicateAuditMask.find(&m);
				if (found != m_duplicateAuditMask.end()) omissionMask = &found->second;
			}
			size_t meshIndex = 0;
			for (auto &mesh : *m.meshes) {
				const size_t index = meshIndex++;

				if (mesh.highPriority) {
					hasOverlay = true;
				}

				if (!mesh.Render(layer, m.alpha)) continue;
				if (mesh.highPriority != renderOverlay) continue;

				if (!matrixLoaded) {
					if (m_diagnosticEyeActive) {
						// The submitted scene and game matrices stay unchanged for both eyes.
						bool anchored = false;
						const float* model = InterpolatedModel(n, m, anchored);
						if (m_sceneInterpolationActive && m_headsetEyeActive && anchored != anchoredLighting) {
							m_r3dShader.SetViewportUniforms(anchored ? &*anchoredViewport : &viewport);
							anchoredLighting = anchored;
						}
						Mat4 matrix = anchored ? GetEyeTransform(n.viewport, false) : viewportEyeTransform;
						if (const float* arm = BodyArmMatrix(m)) {
							matrix = m_eyeView; matrix.MultMatrix(arm);
						} else matrix.MultMatrix(model);
						m_r3dShader.SetModelStates(&m, matrix.currentMatrix);
					}
					else {
						m_r3dShader.SetModelStates(&m);
					}
					matrixLoaded = true;		// do this here to stop loading matrices we don't need. Ie when rendering non transparent etc
				}
				
				if (m_duplicateAuditActive) RecordDuplicateAuditMesh(m, mesh, priority, renderOverlay, layer);
				// Keep all shader/state setters even for an omitted draw. Their
				// cached layered/noLos transitions can affect later geometry.
				m_r3dShader.SetMeshUniforms(&mesh);
				if (omissionMask && index < omissionMask->size() && (*omissionMask)[index]) ++m_duplicateAuditSkipped;
				else glDrawArrays(m_primType, mesh.vboOffset, mesh.vertexCount);
			}
		}
	}

	return hasOverlay;
}

bool CNew3D::SkipLayer(int layer)
{
	for (const auto &n : m_nodes) {
		if (!ShouldDrawViewport(n.viewport)) continue;
		if (n.viewport.priority == layer) {
			if (!n.models.empty()) {
				return false;
			}
		}
	}

	return true;
}

void CNew3D::SetRenderStates()
{
	m_vbo.Bind(true);
	glBindVertexArray(m_vao);

	m_r3dShader.SetShader(true);
	if (m_duplicateAuditActive) m_duplicateAuditMeshDirty = true;

	glDepthFunc		(GL_GEQUAL);
	glEnable		(GL_DEPTH_TEST);
	glDepthMask		(GL_TRUE);
	glActiveTexture	(GL_TEXTURE0);
	glDisable		(GL_CULL_FACE);					// we'll emulate this in the shader		

	glEnable		(GL_STENCIL_TEST);
	glStencilMask	(0xFF);

	glBlendFunc		(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable		(GL_BLEND);
}

void CNew3D::DisableRenderStates()
{
	m_vbo.Bind(false);
	glBindVertexArray(0);

	m_r3dShader.SetShader(false);

	glDisable(GL_STENCIL_TEST);
}

void CNew3D::BuildScene()
{
	m_bodyArmModels = {};
	m_polyBufferRam.clear();
	m_sceneModelBuffers.ClearScene(m_nodes);
	m_modelMat.Release();
	m_nodeAttribs.Reset();
	m_diagnosticNodePath.clear();
	m_riderMatrixPath.clear();
	m_riderMatrixNodePath.clear();
	m_riderCandidates.clear();
	RenderViewport(0x800000);
	SelectPresentationWorld();
	// An unrecognized camera still has valid native 3D content. Put that
	// content on the screen; only the hardware-blank path is truly tile-only.
	m_hasImmersiveWorld = false;
	for (const auto& node : m_nodes)
		if (node.viewport.cameraValid && node.viewport.presentationWorld && !node.models.empty())
			m_hasImmersiveWorld = true;
	UpdateMenuComfort();
	PrepareRiderAnchor();
}

void CNew3D::SubmitMenuInput(int steering, bool start)
{
	m_menuSteering = std::clamp(steering, 0, 255);
	m_menuStart = start;
}

void CNew3D::ResetMenuComfort()
{
	m_menuComfort.Reset();
	m_flatIntro.Reset();
	m_menuTicked = false;
	m_menuPhase = MenuComfort::Phase::Other;
	m_menuSelectMarkerA = m_menuSelectMarkerB = false;
	m_menuIntroMarkerA = m_menuIntroMarkerB = false;
	m_menuLayoutMatch = false;
	m_menuRaceLayout = m_menuRaceSettled = false;
	m_menuClockViewports = m_menuWorldViewports = m_menuNativeUI = 0;
	m_menuClockHeight = -1.0f;
	m_menuCameraViewport = 0;
	m_menuNavigation = 0;
	m_menuComfortTicks = 0;
	// Preserve the newest submitted input. Epoch/config reset can happen after
	// Main samples a button but before that native frame reaches the renderer.
}

CNew3D::RaceLayout CNew3D::InspectRaceLayout() const
{
	RaceLayout layout;
	if ((m_gameName != "harley" && m_gameName != "harleyb") || m_nodes.size() != 7) return layout;
	unsigned worldViews = 0, speedHUD = 0, mapBorder = 0, mapContent = 0;
	bool borderSettled = false, contentSettled = false;
	auto fixedCamera = [](const Viewport& vp, float z, bool map) {
		if (vp.cameraRootBranches != 1) return false;
		for (int i = 0; i < 16; ++i) {
			const float expected = map && i == 6 ? 1.0f : map && i == 9 ? -1.0f :
				map && (i == 5 || i == 10) ? 0.0f : i == 14 ? z : i % 5 == 0 ? 1.0f : 0.0f;
			if (!std::isfinite(vp.gameCamera[i]) || std::abs(vp.gameCamera[i] - expected) > 0.0001f) return false;
		}
		return true;
	};
	for (const auto& node : m_nodes) {
		const auto& vp = node.viewport;
		if (vp.cameraValid && vp.worldCandidate && vp.priority == 1 &&
			vp.vpX == 0.0f && vp.vpY == 0.0f && vp.vpWidth == 495.75f && vp.vpHeight == 384.0f) ++worldViews;
		if (vp.priority == 3 && vp.vpX == 357.0f && vp.vpY == 238.0f && vp.vpWidth == 129.75f &&
			vp.vpHeight == 160.0f && fixedCamera(vp, -24.0f, false)) ++speedHUD;
		if (vp.priority != 2 || vp.cameraRootBranches != 1) continue;
		if (vp.vpX == 14.0f && vp.vpY >= 110.0f && vp.vpY <= 190.0f &&
			vp.vpWidth >= 123.75f && vp.vpWidth <= 227.75f && vp.vpHeight >= 148.0f && vp.vpHeight <= 252.0f &&
			fixedCamera(vp, -5.0f, true)) {
			++mapBorder;
			borderSettled = vp.vpY == 190.0f && vp.vpWidth == 123.75f && vp.vpHeight == 148.0f;
		}
		Mat4 inverse;
		// Map content has a moving translation and a planar road bearing. Its
		// separate rectangle updates one frame before the border. Both must be
		// settled before exposing the first-person handoff; checking only the
		// border misses the very first far-away race camera frame.
		if (vp.vpX == 16.0f && vp.vpY >= 112.0f && vp.vpY <= 192.0f &&
			vp.vpWidth >= 119.75f && vp.vpWidth <= 223.75f && vp.vpHeight >= 144.0f && vp.vpHeight <= 248.0f &&
			InvertRigidCamera(vp.gameCamera, inverse) && std::abs(vp.gameCamera[2]) < 0.0001f &&
			std::abs(vp.gameCamera[6] - 1.0f) < 0.0001f && std::abs(vp.gameCamera[10]) < 0.0001f) {
			++mapContent;
			contentSettled = vp.vpY == 192.0f && vp.vpWidth == 119.75f && vp.vpHeight == 144.0f;
		}
	}
	layout.valid = worldViews == 4 && speedHUD == 1 && mapBorder == 1 && mapContent == 1;
	layout.settled = layout.valid && borderSettled && contentSettled;
	return layout;
}

CNew3D::MenuLayout CNew3D::InspectMenuLayout() const
{
	MenuLayout layout;
	if (m_gameName != "harley" && m_gameName != "harleyb") return layout;
	const RaceLayout race = InspectRaceLayout();
	layout.raceLayout = race.valid;
	layout.raceSettled = race.settled;
	bool matchingClockHeights = true, haveWorldModels = false;
	for (const auto& node : m_nodes) {
		const auto& vp = node.viewport;
		const bool fullScreen = vp.vpX == 0.0f && vp.vpY == 0.0f &&
			vp.vpWidth == 495.75f && vp.vpHeight == 384.0f;
		if (vp.cameraValid && vp.worldCandidate && fullScreen) {
			++layout.worldViewports;
			haveWorldModels |= !node.models.empty();
		}
		if (vp.priority != 3 || vp.cameraRootBranches != 1) continue;
		auto fixedCamera = [&vp](float z) {
			Mat4 reference;
			reference.Translate(0.0f, 0.0f, z);
			for (int i = 0; i < 16; ++i)
				if (!std::isfinite(vp.gameCamera[i]) || std::abs(vp.gameCamera[i] - reference[i]) > 0.0001f) return false;
			return true;
		};
		// These cameras and rectangles identify the measured layout. Model IDs
		// are changing countdown glyphs, and cannot identify menu phases.
		if (vp.vpX == 320.0f && vp.vpY == 80.0f && vp.vpWidth == 179.75f &&
			vp.vpHeight >= 0.0f && vp.vpHeight <= 100.0f && fixedCamera(-14.0f)) {
			++layout.clockViewports;
			if (layout.clockHeight < 0.0f) layout.clockHeight = vp.vpHeight;
			else if (layout.clockHeight != vp.vpHeight) matchingClockHeights = false;
		}
		if (fullScreen && fixedCamera(-10.0f)) ++layout.nativeUI;
	}
	if (!haveWorldModels || !matchingClockHeights) return layout;
	if (m_nodes.size() == 3 && layout.worldViewports == 1 && layout.clockViewports == 2)
		layout.phase = layout.clockHeight == 100.0f ? MenuComfort::Phase::Select : MenuComfort::Phase::Intro;
	// AT/MT grows from two to four world nodes while the selected rider leaves
	// the shop and its clock collapses. Keep that departure animation protected.
	else if (layout.worldViewports >= 2 && layout.worldViewports <= 4 &&
		layout.clockViewports == 1 && layout.clockHeight >= 0.0f && layout.clockHeight <= 100.0f && layout.nativeUI == 3 &&
		m_nodes.size() == layout.worldViewports + 4)
		layout.phase = MenuComfort::Phase::Transmission;
	else if (race.valid && !race.settled)
		layout.phase = MenuComfort::Phase::RaceIntro;
	return layout;
}

void CNew3D::UpdateMenuComfort()
{
	// Native center decode owns the sample. The optional wider eye decode and
	// every independent head redraw must not advance this presentation filter.
	if (m_bypassFrustum || (m_menuTicked && m_menuFrame == m_sceneFrame && m_menuEpoch == m_sceneEpoch)) return;
	if (m_menuTicked && (m_menuEpoch != m_sceneEpoch || m_sceneFrame != m_menuFrame + 1)) ResetMenuComfort();
	m_menuTicked = true;
	m_menuFrame = m_sceneFrame;
	m_menuEpoch = m_sceneEpoch;
	m_menuSelectMarkerA = m_menuSelectMarkerB = false;
	m_menuIntroMarkerA = m_menuIntroMarkerB = false;
	const bool supported = m_gameName == "harley" || m_gameName == "harleyb";
	const MenuLayout layout = InspectMenuLayout();
	m_menuWorldViewports = layout.worldViewports;
	m_menuClockViewports = layout.clockViewports;
	m_menuClockHeight = layout.clockHeight;
	m_menuNativeUI = layout.nativeUI;
	m_menuRaceLayout = layout.raceLayout;
	m_menuRaceSettled = layout.raceSettled;
	m_menuPhase = layout.phase;
	m_menuLayoutMatch = m_menuPhase != MenuComfort::Phase::Other;
	const Node* primary = nullptr;
	for (const auto& node : m_nodes) {
		if (node.viewport.cameraValid && !node.models.empty() &&
			(!m_stereoUI || node.viewport.presentationWorld) &&
			(!primary || node.models.size() > primary->models.size())) primary = &node;
		if (!supported) continue;
		for (const auto& model : node.models) {
			switch (model.sourceAddress) {
			case 0xD0B49A: m_menuSelectMarkerA = true; break;
			case 0xD0D26C: m_menuSelectMarkerB = true; break;
			case 0xD0B5E7: m_menuIntroMarkerA = true; break;
			case 0xD0B026: m_menuIntroMarkerB = true; break;
			}
		}
	}
	m_menuCameraViewport = primary ? primary->viewport.sourceAddress : 0;
	m_menuNavigation = m_menuSteering >= 136 ? 1 : m_menuSteering <= 120 ? -1 : 0;
	if (m_stereoMenuComfort && supported) {
		m_menuComfort.Tick(m_menuPhase, m_menuNavigation, m_menuStart, primary != nullptr,
			primary ? primary->viewport.gameCamera.currentMatrix : nullptr);
		++m_menuComfortTicks;
	}
	else m_menuComfort.Reset();
	if (m_stereoFlatIntro && supported) m_flatIntro.Tick(m_menuPhase, m_menuComfort.State());
	else m_flatIntro.Reset();
}

bool CNew3D::FlatIntroActive() const
{
	if (GeneralTheaterEnabled()) return TheaterIntroActive();
	return m_stereoDiagnostic && m_stereoUI && m_stereoFlatIntro && m_flatIntro.Active();
}

bool CNew3D::TheaterIntroActive() const
{
	// The game-state policy is deliberately independent of viewport geometry.
	// An unknown stage or a frame made entirely of flat assets stays on screen.
	return GeneralTheaterEnabled() && (m_uiOnlyScene || !m_hasImmersiveWorld ||
		HarleyPresentation::Classify(m_gamePresentationState) == HarleyPresentation::Mode::Theater);
}

bool CNew3D::GeneralTheaterEnabled() const
{
	return m_stereoDiagnostic && m_stereoUI && m_stereoTheaterIntro &&
		(m_gameName == "harley" || m_gameName == "harleyb");
}

bool CNew3D::ImmersiveOverlayPanel() const
{
	return GeneralTheaterEnabled() && !TheaterIntroActive() && !DrivingHUDScene();
}

float CNew3D::TheaterScreenAngle() const
{
	const float angle = m_config["StereoTheaterScreenAngle"].ValueAsDefault<float>(70.0f);
	return std::isfinite(angle) ? std::clamp(angle, 40.0f, 100.0f) : 70.0f;
}

float CNew3D::TheaterHalfWidth() const
{
	return 40.0f * std::tan(TheaterScreenAngle() * 0.00872664626f);
}

bool CNew3D::StereoTheaterActive() const
{
	// Cinema content is a flat image; the surrounding screen retains head tracking.
	return false;
}

void CNew3D::ClearPresentationTarget()
{
	const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_aaTarget);
	glDrawBuffer(m_aaTarget ? GL_COLOR_ATTACHMENT0 : GL_BACK);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (scissor) glEnable(GL_SCISSOR_TEST);
}

bool CNew3D::DrivingHUDScene() const
{
	if (GeneralTheaterEnabled())
		return !m_uiOnlyScene && HarleyPresentation::Classify(m_gamePresentationState) == HarleyPresentation::Mode::Driving;
	// Keep attract, all selection screens, and unrecognized layouts readable.
	return m_stereoUI && !FlatIntroActive() && m_menuPhase == MenuComfort::Phase::Other &&
		m_menuRaceLayout && m_menuRaceSettled && !m_uiOnlyScene;
}

float CNew3D::CinematicVignetteStrength() const
{
	// A frozen jump/camera effect must not persist while inspecting the city.
	// Walking has its own stick-driven shared aperture instead.
	if (m_bodyExploring) return 0.0f;
	const float forced = m_config["StereoCinematicVignette"].ValueAsDefault<bool>(false) ? 1.0f : 0.0f;
	return std::max(forced, m_stereoMenuComfort ? float(m_menuComfort.State().strength) : 0.0f);
}

float CNew3D::UserComfortStrength() const
{
	// Presentation-only preference: read during every mask draw so a paused
	// headset can change it without resetting contact detection or stabilization.
	const float strength = m_config["UserComfortStrength"].ValueAsDefault<float>(1.0f);
	return std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 1.0f;
}

void CNew3D::Attach2DRenderer(CRender2D* renderer)
{
	if (m_tileRenderer != renderer) InvalidatePresentation();
	m_tileRenderer = renderer;
	if (m_tileRenderer) m_tileRenderer->SetDeferredUploads(m_headsetPresentation);
}

void CNew3D::SelectPresentationWorld()
{
	if (!m_stereoUI) return;
	const Node* primary = nullptr;
	for (const auto& node : m_nodes) {
		if (node.viewport.cameraValid && !node.models.empty() &&
			(!primary || node.models.size() > primary->models.size())) primary = &node;
	}
	if (!primary) return;
	const bool transmission = InspectMenuLayout().phase == MenuComfort::Phase::Transmission;
	// Full-screen 3D lettering can satisfy the same root checks as the street.
	// Keep it in native screen space unless it shares the dominant scene camera.
	for (auto& node : m_nodes) {
		if (!node.viewport.cameraValid) continue;
		bool sameCamera = true;
		for (int i = 0; i < 16; ++i)
			if (std::abs(node.viewport.gameCamera[i] - primary->viewport.gameCamera[i]) > 0.0001f) sameCamera = false;
		// AT/MT supplies additional world cameras with small matrix differences.
		// Keep every validated world component in the masked stereo pass only
		// for that complete layout; fixed Z=-10 lettering remains native UI.
		node.viewport.presentationWorld = sameCamera || (transmission && node.viewport.worldCandidate);
	}
}

bool CNew3D::UploadScene()
{
	auto fail = [this](const char* reason) {
		// Reject an incomplete scene, but do not poison every future frame or
		// flood the console. Discard these cache entries at the next native frame.
		if (!m_geometryUploadFailed)
			ErrorLog("Geometry upload failed: %s (RAM=%llu ROM=%llu vertices).", reason,
				(unsigned long long)m_polyBufferRam.size(), (unsigned long long)m_polyBufferRom.size());
		m_geometryUploadFailed = true;
		m_romCacheResetPending = true;
		m_eyeImagesValid = false;
		return false;
	};
	// This vector is the cumulative static-model cache, not just this frame's
	// geometry. Evict old scenes while retaining every current mesh and its
	// exact vertices. Clearing the cache and drawing its old offsets is unsafe.
	if (m_polyBufferRom.size() > MAX_ROM_VERTS) {
		const auto compact = RomGeometryCache::Compact(m_polyBufferRom, m_romMap, m_nodes, MAX_ROM_VERTS);
		if (compact.status != RomGeometryCache::Status::Compacted)
			return fail(RomGeometryCache::StatusName(compact.status));
		m_vbo.Reset();
		InfoLog("ROM cache compacted: before=%llu after=%llu frame=%llu models_before=%llu models_after=%llu.",
			(unsigned long long)compact.oldVertices, (unsigned long long)compact.liveVertices,
			(unsigned long long)m_sceneFrame, (unsigned long long)compact.oldCachedModels,
			(unsigned long long)compact.retainedModels);
	}
	if (m_polyBufferRam.size() > MAX_RAM_VERTS || m_polyBufferRom.size() > MAX_ROM_VERTS)
		return fail("active scene exceeds fixed GPU partition");
	const int romBytes = int(m_polyBufferRom.size() * sizeof(FVertex));
	const int uploaded = m_vbo.GetSize();
	if (uploaded < 0 || uploaded > romBytes || uploaded % sizeof(FVertex))
		return fail("invalid ROM upload cursor");
	m_vbo.Bind(true);
	m_vbo.BufferSubData(MAX_ROM_VERTS * sizeof(FVertex), m_polyBufferRam.size() * sizeof(FVertex), m_polyBufferRam.data());
	if (romBytes > uploaded && !m_vbo.AppendData(romBytes - uploaded, m_polyBufferRom.data() + uploaded / sizeof(FVertex)))
		return fail("ROM upload rejected by GPU buffer bounds");
	return true;
}

void CNew3D::StressROMCache(bool bypass)
{
	// A bounded regression test seeds unreachable cache entries, reproducing a
	// long session's cache pressure without changing any game or ROM memory.
	const unsigned frame = m_config["StereoROMCacheStress"].ValueAsDefault<unsigned>(0);
	if (!frame || m_romCacheStressApplied || m_sceneFrame != frame ||
		bypass != m_config["StereoROMCacheStressBypass"].ValueAsDefault<bool>(false)) return;
	const size_t before = m_polyBufferRom.size();
	const R3DPoly unusedPoly{};
	const FVertex unused(unusedPoly, 0);
	m_polyBufferRom.resize(std::max(before, size_t(MAX_ROM_VERTS) + 1), unused);
	m_romCacheStressApplied = true;
	InfoLog("ROM cache stress injected: frame=%u pass=%s before=%llu after=%llu.", frame, bypass ? "bypass" : "center",
		(unsigned long long)before, (unsigned long long)m_polyBufferRom.size());
}

void CNew3D::DrawPreparedScene(bool processLos, GLuint outputTarget, bool preserveAlpha)
{
	const bool audit = !m_duplicateAuditPath.empty() && !m_duplicateAuditFailed && !processLos &&
		m_headsetEyeActive && m_diagnosticEyeActive && m_viewportPass == ViewportPass::World &&
		m_duplicateAuditDraws < 16;
	if (!audit) { DrawPreparedSceneOnce(processLos, outputTarget, preserveAlpha); return; }
	if (!OpenDuplicateAudit()) { DrawPreparedSceneOnce(processLos, outputTarget, preserveAlpha); return; }
	if (m_headsetEyeWidth <= 0 || m_headsetEyeHeight <= 0 || m_headsetEyeWidth > 1024 || m_headsetEyeHeight > 1024) {
		m_duplicateAuditFailed = true;
		ErrorLog("Duplicate audit exceeds the 1024x1024 eye bound.");
		DrawPreparedSceneOnce(processLos, outputTarget, preserveAlpha); return;
	}
	++m_duplicateAuditDraws;
	PrepareDuplicateAuditMask();
	// This off-headset oracle is entered just after RenderHeadsetEyes clears
	// the destination to opaque black. Replays must start from that same
	// destination, not blend again over the already-composited baseline.
	const SpatialPanel::State entryState = SpatialPanel::State::Capture();
	GLfloat entryClearColor[4];
	glGetFloatv(GL_COLOR_CLEAR_VALUE, entryClearColor);
	GLboolean entryDepthMask;
	GLint entryDepthFunc;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &entryDepthMask);
	glGetIntegerv(GL_DEPTH_FUNC, &entryDepthFunc);
	GLint stencilState[2][7];
	const GLenum stencilNames[2][7] = {
		{GL_STENCIL_FUNC, GL_STENCIL_REF, GL_STENCIL_VALUE_MASK, GL_STENCIL_WRITEMASK, GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL, GL_STENCIL_PASS_DEPTH_PASS},
		{GL_STENCIL_BACK_FUNC, GL_STENCIL_BACK_REF, GL_STENCIL_BACK_VALUE_MASK, GL_STENCIL_BACK_WRITEMASK, GL_STENCIL_BACK_FAIL, GL_STENCIL_BACK_PASS_DEPTH_FAIL, GL_STENCIL_BACK_PASS_DEPTH_PASS}
	};
	for (int face = 0; face < 2; ++face) for (int field = 0; field < 7; ++field)
		glGetIntegerv(stencilNames[face][field], &stencilState[face][field]);
	auto resetReplay = [&]() {
		// Once leaves the layered framebuffer cache at Layer::none. Bind only
		// the external destination here, preserving that cache contract.
		glBindFramebuffer(GL_FRAMEBUFFER, outputTarget);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		entryState.Restore();
		glClearColor(entryClearColor[0], entryClearColor[1], entryClearColor[2], entryClearColor[3]);
		glDepthMask(entryDepthMask);
		glDepthFunc(entryDepthFunc);
		for (int face = 0; face < 2; ++face) {
			const GLenum side = face ? GL_BACK : GL_FRONT;
			const auto& s = stencilState[face];
			glStencilFuncSeparate(side, s[0], s[1], s[2]);
			glStencilMaskSeparate(side, s[3]);
			glStencilOpSeparate(side, s[4], s[5], s[6]);
		}
	};
	m_duplicateAuditActive = true;
	m_duplicateAuditCandidate = false;
	m_duplicateAuditBytes = 0;
	m_duplicateAuditBaseline.clear();
	m_duplicateAuditStageCursor = 0;
	const size_t transitionsBefore = m_duplicateAuditTransitions;
	DrawPreparedSceneOnce(false, outputTarget, preserveAlpha);
	const size_t baselineTransitions = m_duplicateAuditTransitions - transitionsBefore;
	const size_t stages = m_duplicateAuditBaseline.size();
	m_duplicateAuditCandidate = !m_duplicateAuditFailed;
	m_duplicateAuditStageCursor = 0;
	m_duplicateAuditSkipped = 0;
	if (m_duplicateAuditCandidate) {
		resetReplay();
		DrawPreparedSceneOnce(false, outputTarget, preserveAlpha);
	}
	if (m_duplicateAuditStageCursor != stages) m_duplicateAuditFailed = true;
	m_duplicateAuditCandidate = m_duplicateAuditActive = false;
	fprintf(m_duplicateAuditFile, "{\"type\":\"duplicate_audit_draw\",\"draw\":%u,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"width\":%d,\"height\":%d,\"eligible_earlier_meshes\":%llu,\"selected_earlier_meshes\":%llu,\"omitted_draws\":%llu,\"stage_count\":%llu,\"baseline_bytes\":%llu,\"baseline_layered_noLos_transitions\":%llu,\"candidate_layered_noLos_transitions\":%llu,\"mask_complete\":%s,\"passed\":%s,\"eye_view\":",
		m_duplicateAuditDraws, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch,
		m_headsetEyeWidth, m_headsetEyeHeight, (unsigned long long)m_duplicateAuditCandidates,
		(unsigned long long)m_duplicateAuditSelected, (unsigned long long)m_duplicateAuditSkipped,
		(unsigned long long)stages, (unsigned long long)m_duplicateAuditBytes, (unsigned long long)baselineTransitions,
		(unsigned long long)(m_duplicateAuditTransitions - transitionsBefore - baselineTransitions),
		m_duplicateAuditMaskComplete ? "true" : "false", m_duplicateAuditFailed ? "false" : "true");
	TraceFloats(m_duplicateAuditFile, m_eyeView, 16);
	fputs(",\"projection\":", m_duplicateAuditFile);
	TraceFloats(m_duplicateAuditFile, m_headsetProjection, 16);
	fputs("}\n", m_duplicateAuditFile);
	if (fflush(m_duplicateAuditFile) != 0 || ferror(m_duplicateAuditFile)) m_duplicateAuditFailed = true;
	m_duplicateAuditBaseline.clear();
	m_duplicateAuditMask.clear();
	if (m_duplicateAuditFailed) {
		ErrorLog("Duplicate audit failed; restoring the unmodified draw output.");
		resetReplay();
		DrawPreparedSceneOnce(false, outputTarget, preserveAlpha);
	}
}

bool CNew3D::OpenDuplicateAudit()
{
	if (m_duplicateAuditFile) return true;
	const std::filesystem::path path(m_duplicateAuditPath);
#ifdef SUPERMODEL_WIN32
	HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle != INVALID_HANDLE_VALUE) {
		const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_WRONLY | _O_BINARY);
		if (fd >= 0) { m_duplicateAuditFile = _fdopen(fd, "wb"); if (!m_duplicateAuditFile) _close(fd); }
		else CloseHandle(handle);
	}
#else
	const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd >= 0) { m_duplicateAuditFile = fdopen(fd, "wb"); if (!m_duplicateAuditFile) close(fd); }
#endif
	if (!m_duplicateAuditFile) {
		m_duplicateAuditFailed = true;
		ErrorLog("Unable to exclusively create duplicate audit '%s'; existing files are preserved.", m_duplicateAuditPath.c_str());
		return false;
	}
	fputs("{\"type\":\"duplicate_audit_header\",\"schema\":1,\"eye_draw_limit\":16,\"eye_dimension_limit\":1024,\"stage_limit\":25,\"baseline_byte_limit\":469762048,\"selection\":\"one_exact_opaque_mesh_cohort_per_viewport; keep_last; layered_draw_barrier; preserve_all_state_setters\",\"scope\":\"off_headset_test_only; native_LOS_and_transparent_draws_unchanged; exact_color_depth_stencil_byte_comparison; no_performance_or_z_fighting_claim\"}\n", m_duplicateAuditFile);
	return true;
}

void CNew3D::PrepareDuplicateAuditMask()
{
	m_duplicateAuditMask.clear();
	m_duplicateAuditCandidates = m_duplicateAuditSelected = 0;
	m_duplicateAuditMaskComplete = true;
	size_t models = 0, meshes = 0, viewports = 0;
	auto abandon = [&]() {
		m_duplicateAuditMaskComplete = false; m_duplicateAuditMask.clear(); m_duplicateAuditSelected = 0;
	};
	const bool hideArrows = m_stereoHideHUD && DrivingHUDScene();
	for (const auto& node : m_nodes) {
		if (!ShouldDrawViewport(node.viewport)) continue;
		if (++viewports > 16) { abandon(); return; }
		std::map<std::vector<uint32_t>, const Model*> last;
		std::vector<uint32_t> selected;
		for (auto modelIt = node.models.rbegin(); modelIt != node.models.rend(); ++modelIt) {
			if (++models > 8192) { abandon(); return; }
			const Model& model = *modelIt;
			if (model.hideInRiderEyes || !model.meshes ||
				(hideArrows && node.viewport.presentationWorld && (model.sourceAddress == 0x81AAA9 || model.sourceAddress == 0x81AB55))) continue;
			for (size_t index = model.meshes->size(); index-- > 0;) {
				if (++meshes > 65536) { abandon(); return; }
				Mesh mesh = (*model.meshes)[index];
				if (mesh.highPriority || !mesh.Render(Layer::colour, model.alpha)) continue;
				// Any intervening actual layered draw is a hard barrier. Even
				// when low stencil increments match, upper LOS bits can differ.
				if (mesh.layered) { last.clear(); continue; }
				if (model.dynamic || model.alpha != 1.0f || mesh.polyAlpha || mesh.textureAlpha ||
					mesh.alphaTest || mesh.microTexture || mesh.vertexCount <= 0) continue;
				auto key = DrawTraceModel(model);
				const uint64_t geometry = uint64_t(reinterpret_cast<uintptr_t>(&(*model.meshes)[index]));
				key.Add(uint32_t(geometry)); key.Add(uint32_t(geometry >> 32));
				const auto inserted = last.emplace(key.values, &model);
				if (!inserted.second) {
					++m_duplicateAuditCandidates;
					if (selected.empty()) {
						selected = key.values;
						fprintf(m_duplicateAuditFile, "{\"type\":\"duplicate_audit_selection\",\"draw\":%u,\"viewport\":%u,\"source\":%u,\"mesh_index\":%llu,\"earlier_model_index\":%llu,\"later_model_index\":%llu,\"model_words\":",
							m_duplicateAuditDraws, node.viewport.sourceAddress, model.sourceAddress, (unsigned long long)index,
							(unsigned long long)(&model - node.models.data()), (unsigned long long)(inserted.first->second - node.models.data()));
						TraceWords(m_duplicateAuditFile, DrawTraceModel(model).values);
						fputs(",\"material_words\":", m_duplicateAuditFile);
						TraceWords(m_duplicateAuditFile, DrawTraceMesh(mesh).values);
						fputs("}\n", m_duplicateAuditFile);
					}
					if (key.values == selected) {
						auto& mask = m_duplicateAuditMask[&model];
						if (mask.empty()) mask.resize(model.meshes->size(), false);
						mask[index] = true; ++m_duplicateAuditSelected;
					}
				}
			}
		}
	}
}

void CNew3D::RecordDuplicateAuditMesh(const Model& model, const Mesh& mesh, int priority, bool overlay, Layer layer)
{
	if (!m_duplicateAuditMeshDirty && m_duplicateAuditLastLayered && mesh.layered && m_duplicateAuditLastNoLos != mesh.noLosReturn) {
		++m_duplicateAuditTransitions;
		if (m_duplicateAuditTransitionsLogged++ < 16)
			fprintf(m_duplicateAuditFile, "{\"type\":\"duplicate_audit_layered_transition\",\"draw\":%u,\"candidate\":%s,\"priority\":%d,\"overlay\":%s,\"layer\":%d,\"previous_source\":%u,\"source\":%u,\"previous_noLos\":%s,\"noLos\":%s}\n",
				m_duplicateAuditDraws, m_duplicateAuditCandidate ? "true" : "false", priority, overlay ? "true" : "false", int(layer),
				m_duplicateAuditLastSource, model.sourceAddress, m_duplicateAuditLastNoLos ? "true" : "false", mesh.noLosReturn ? "true" : "false");
	}
	m_duplicateAuditMeshDirty = false;
	m_duplicateAuditLastLayered = mesh.layered; m_duplicateAuditLastNoLos = mesh.noLosReturn;
	m_duplicateAuditLastSource = model.sourceAddress;
}

void CNew3D::CaptureDuplicateAuditStage(unsigned stage, bool finalColor)
{
	if (!m_duplicateAuditActive) return;
	const size_t pixels = size_t(m_headsetEyeWidth) * size_t(m_headsetEyeHeight);
	const size_t byteCount = pixels * (finalColor ? 4 : 17);
	if (m_duplicateAuditStageCursor >= 25 || (!m_duplicateAuditCandidate && m_duplicateAuditBytes + byteCount > 448ull * 1024 * 1024)) {
		m_duplicateAuditFailed = true; return;
	}
	GLint drawFbo, readFbo, readBuffer, drawReadBuffer, packBuffer, packAlignment, packRowLength, packSkipRows, packSkipPixels, packSwap;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
	glGetIntegerv(GL_READ_BUFFER, &readBuffer); glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
	glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment); glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
	glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
	glGetIntegerv(GL_PACK_SWAP_BYTES, &packSwap);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFbo);
	glGetIntegerv(GL_READ_BUFFER, &drawReadBuffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
	DuplicateAuditSnapshot snapshot;
	snapshot.stage = stage; snapshot.bytes.resize(byteCount);
	for (int color = 0; color < (finalColor ? 1 : 3); ++color) {
		glReadBuffer(GL_COLOR_ATTACHMENT0 + color);
		glReadPixels(0, 0, m_headsetEyeWidth, m_headsetEyeHeight, GL_RGBA, GL_UNSIGNED_BYTE, snapshot.bytes.data() + size_t(color) * pixels * 4);
	}
	if (!finalColor) {
		glReadPixels(0, 0, m_headsetEyeWidth, m_headsetEyeHeight, GL_DEPTH_COMPONENT, GL_FLOAT, snapshot.bytes.data() + pixels * 12);
		glReadPixels(0, 0, m_headsetEyeWidth, m_headsetEyeHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, snapshot.bytes.data() + pixels * 16);
	}
	const GLenum error = glGetError();
	glReadBuffer(drawReadBuffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo); glReadBuffer(readBuffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer); glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
	glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength); glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows);
	glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels); glPixelStorei(GL_PACK_SWAP_BYTES, packSwap);
	if (error != GL_NO_ERROR) { m_duplicateAuditFailed = true; ErrorLog("Duplicate audit GL readback failed: 0x%x.", unsigned(error)); }
	if (!m_duplicateAuditCandidate) {
		m_duplicateAuditBytes += byteCount;
		m_duplicateAuditBaseline.push_back(std::move(snapshot));
	} else {
		if (m_duplicateAuditStageCursor >= m_duplicateAuditBaseline.size() ||
			m_duplicateAuditBaseline[m_duplicateAuditStageCursor].stage != stage ||
			m_duplicateAuditBaseline[m_duplicateAuditStageCursor].bytes.size() != byteCount) { m_duplicateAuditFailed = true; return; }
		const auto& reference = m_duplicateAuditBaseline[m_duplicateAuditStageCursor].bytes;
		auto differences = [&](size_t begin, size_t end) {
			size_t count = 0; for (size_t i = begin; i < end; ++i) count += reference[i] != snapshot.bytes[i]; return count;
		};
		const size_t color = differences(0, pixels * (finalColor ? 4 : 12));
		const size_t depth = finalColor ? 0 : differences(pixels * 12, pixels * 16);
		const size_t stencil = finalColor ? 0 : differences(pixels * 16, pixels * 17);
		if (color || depth || stencil) m_duplicateAuditFailed = true;
		fprintf(m_duplicateAuditFile, "{\"type\":\"duplicate_audit_stage\",\"draw\":%u,\"stage\":%u,\"final_color\":%s,\"bytes\":%llu,\"color_different_bytes\":%llu,\"depth_different_bytes\":%llu,\"stencil_different_bytes\":%llu,\"gl_error\":%u}\n",
			m_duplicateAuditDraws, stage, finalColor ? "true" : "false", (unsigned long long)byteCount,
			(unsigned long long)color, (unsigned long long)depth, (unsigned long long)stencil, unsigned(error));
	}
	++m_duplicateAuditStageCursor;
}

void CNew3D::DrawPreparedSceneOnce(bool processLos, GLuint outputTarget, bool preserveAlpha)
{
	R3DFrameBuffers& frameBuffers = m_headsetEyeActive ? *m_headsetFrameBuffers : m_r3dFrameBuffers;
	if (m_stereoDiagnostic) glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	frameBuffers.SetFBO(Layer::colour);		// colour will draw to all 3 buffers. For regular opaque pixels the transparent layers will be essentially masked
	glClear(GL_COLOR_BUFFER_BIT);

	DrawAmbientFog();
	DrawScrollFog();								// fog layer if applicable must be drawn here

	for (int pri = 0; pri <= 3; pri++) {

		if (SkipLayer(pri)) continue;

		for (int i = 0; i < 2; i++) {

			bool renderOverlay = (i == 1);

			SetRenderStates();

			frameBuffers.SetFBO(Layer::colour);

			glClearDepth(0.0);
			glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			m_r3dShader.DiscardAlpha(true);
			m_r3dShader.SetLayer(Layer::colour);
			bool hasOverlay = RenderScene(pri, renderOverlay, Layer::colour);
			if (m_duplicateAuditActive) CaptureDuplicateAuditStage(unsigned(pri * 6 + i * 3));

			if (processLos && !renderOverlay) {
				ProcessLos(pri);
			}

			glDepthFunc(GL_GREATER);

			m_r3dShader.DiscardAlpha(false);

			frameBuffers.StoreDepth();
			m_r3dShader.SetLayer(Layer::trans1);
			frameBuffers.SetFBO(Layer::trans1);
			RenderScene(pri, renderOverlay, Layer::trans1);
			if (m_duplicateAuditActive) CaptureDuplicateAuditStage(unsigned(pri * 6 + i * 3 + 1));

			frameBuffers.RestoreDepth();
			m_r3dShader.SetLayer(Layer::trans2);
			frameBuffers.SetFBO(Layer::trans2);
			RenderScene(pri, renderOverlay, Layer::trans2);
			if (m_duplicateAuditActive) CaptureDuplicateAuditStage(unsigned(pri * 6 + i * 3 + 2));
						
			DisableRenderStates();

			if (!hasOverlay) break;								// no high priority polys						
		}
	}

	if (m_headsetEyeActive) HeadsetGpuProbe().GeometryEnd(this);
	frameBuffers.SetFBO(Layer::none);

	glBindFramebuffer(GL_FRAMEBUFFER, outputTarget);
	if (outputTarget) glDrawBuffer(GL_COLOR_ATTACHMENT0);
	else glDrawBuffer(GL_BACK);
	frameBuffers.Draw(preserveAlpha);
	if (m_duplicateAuditActive) CaptureDuplicateAuditStage(1000, true);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (m_headsetEyeActive) HeadsetGpuProbe().LayerEnd(this);
}

void CNew3D::UpdateDiagnosticSettings()
{
	const bool interpolate = m_config["HeadsetSceneInterpolation"].ValueAsDefault<bool>(false);
	if (interpolate != m_sceneInterpolationEnabled) ResetSceneInterpolation();
	m_sceneInterpolationEnabled = interpolate;
	const bool enabled = m_config["StereoDiagnostic"].ValueAsDefault<bool>(false);
	const bool ui = m_config["StereoUI"].ValueAsDefault<bool>(false);
	const bool monoPreview = m_config["StereoMonoPreview"].ValueAsDefault<bool>(false);
	const bool menuComfort = m_config["StereoMenuComfort"].ValueAsDefault<bool>(false);
	const bool flatIntro = m_config["StereoFlatIntro"].ValueAsDefault<bool>(false);
	const bool theaterIntro = m_config["StereoTheaterIntro"].ValueAsDefault<bool>(false) ||
		m_config["StereoTheater"].ValueAsDefault<bool>(false);
	const int theaterDepth = 0; // Legacy preferences cannot restore stereo cinema.
	const float cinemaUnits = StereoCinema::Units(m_config["OpenXRUnitsPerMeter"].ValueAsDefault<float>(16.0f));
	const float cinemaAngle = TheaterScreenAngle();
	const bool vrHUD = m_config["StereoVRHUD"].ValueAsDefault<bool>(false);
	const bool bikeHUD = m_config["StereoBikeHUD"].ValueAsDefault<bool>(false);
	const bool hideHUD = m_config["StereoHideHUD"].ValueAsDefault<bool>(false);
	if (menuComfort != m_stereoMenuComfort || flatIntro != m_stereoFlatIntro) ResetMenuComfort();
	const bool bypass = m_config["StereoCullBypass"].ValueAsDefault<bool>(false);
	const bool horizon = m_config["StereoHorizonLock"].ValueAsDefault<bool>(false);
	const bool riderAnchor = m_config["StereoRiderAnchor"].ValueAsDefault<bool>(false);
	const bool fullBike = m_config["StereoFullBike"].ValueAsDefault<bool>(false);
	const bool stableChase = m_config["StereoStableChase"].ValueAsDefault<bool>(false);
	const bool riderComfort = m_config["StereoRiderComfort"].ValueAsDefault<bool>(false);
	auto finiteValue = [this](const char* key, float fallback, float limit) {
		const float value = m_config[key].ValueAsDefault<float>(fallback);
		return std::isfinite(value) ? std::clamp(value, -limit, limit) : fallback;
	};
	const float separation = finiteValue("StereoEyeSeparation", 1.0f, 1000.0f);
	const float yaw = finiteValue("StereoYaw", 0.0f, 180.0f);
	const float pitch = finiteValue("StereoPitch", 0.0f, 89.0f);
	const float horizonFov = std::clamp(finiteValue("StereoHorizonFov", 90.0f, 140.0f), 30.0f, 140.0f);
	const float riderUp = std::clamp(finiteValue("StereoRiderUp", 0.0f, 30.0f), -10.0f, 30.0f);
	const float riderBack = std::clamp(finiteValue("StereoRiderBack", 0.0f, 30.0f), -20.0f, 30.0f);
	const float chaseHeight = std::clamp(finiteValue("StereoChaseHeight", 20.0f, 60.0f), 0.0f, 60.0f);
	const float chaseBack = std::clamp(finiteValue("StereoChaseBack", 40.0f, 100.0f), 10.0f, 100.0f);
	const float vignetteScale = std::clamp(finiteValue("StereoVignetteScale", 1.0f, 1.0f), 0.0f, 1.0f);
	const float lodDistance = std::clamp(finiteValue("StereoLodDistance", 1.0f, 4.0f), 1.0f, 4.0f);
	if (riderComfort != m_stereoRiderComfort || riderAnchor != m_stereoRiderAnchor || fullBike != m_stereoFullBike ||
		stableChase != m_stereoStableChase || chaseHeight != m_stereoChaseHeight || chaseBack != m_stereoChaseBack ||
		enabled != m_stereoDiagnostic || horizon != m_stereoHorizonLock ||
		riderUp != m_stereoRiderUp || riderBack != m_stereoRiderBack) {
		m_eyeImagesValid = false;
		m_riderComfort = RiderComfort();
		m_comfortResult = RiderComfort::Result();
	}
	if (enabled != m_stereoDiagnostic || ui != m_stereoUI || monoPreview != m_stereoMonoPreview ||
		menuComfort != m_stereoMenuComfort || flatIntro != m_stereoFlatIntro || bypass != m_stereoCullBypass ||
		theaterIntro != m_stereoTheaterIntro || theaterDepth != m_stereoTheaterDepth || cinemaUnits != m_cinemaUnits ||
		(theaterDepth > 0 && cinemaAngle != m_cinemaAngle) ||
		vrHUD != m_stereoVRHUD || hideHUD != m_stereoHideHUD || bikeHUD != m_stereoBikeHUD ||
		stableChase != m_stereoStableChase || chaseHeight != m_stereoChaseHeight || chaseBack != m_stereoChaseBack ||
		lodDistance != m_stereoLodDistance ||
		separation != m_stereoEyeSeparation || yaw != m_stereoYaw || pitch != m_stereoPitch ||
		horizon != m_stereoHorizonLock || horizonFov != m_stereoHorizonFov || riderAnchor != m_stereoRiderAnchor || fullBike != m_stereoFullBike ||
		riderUp != m_stereoRiderUp || riderBack != m_stereoRiderBack) {
		m_diagnosticLogPending = true;
		m_eyeImagesValid = false;
		ResetSceneInterpolation();
	}
	m_stereoDiagnostic = enabled;
	m_stereoUI = ui;
	m_stereoMonoPreview = monoPreview;
	m_stereoMenuComfort = menuComfort;
	m_stereoFlatIntro = flatIntro;
	m_stereoTheaterIntro = theaterIntro;
	m_stereoTheaterDepth = theaterDepth;
	m_cinemaUnits = cinemaUnits;
	m_cinemaAngle = cinemaAngle;
	m_stereoVRHUD = vrHUD;
	m_stereoBikeHUD = bikeHUD;
	m_stereoHideHUD = hideHUD;
	// This cosmetic choice can change between frozen redraws; it does not
	// invalidate the scene or participate in PresentationSettingsMatch().
	m_stereoCinematicVignette = m_config["StereoCinematicVignette"].ValueAsDefault<bool>(false);
	m_stereoCullBypass = bypass;
	m_stereoLodDistance = lodDistance;
	m_stereoEyeSeparation = separation;
	m_stereoYaw = yaw;
	m_stereoPitch = pitch;
	m_stereoHorizonLock = horizon;
	m_stereoRiderAnchor = riderAnchor;
	m_stereoFullBike = fullBike;
	m_stereoStableChase = stableChase;
	m_stereoChaseHeight = chaseHeight;
	m_stereoChaseBack = chaseBack;
	m_stereoRiderComfort = riderComfort;
	m_stereoVignetteScale = vignetteScale;
	m_stereoRiderUp = riderUp;
	m_stereoRiderBack = riderBack;
	m_stereoHorizonFov = horizonFov;
	if (m_cameraTracePath.empty()) m_cameraTracePath = m_config["StereoCameraTrace"].ValueAsDefault<std::string>("");
	m_cameraTraceFrame = m_config["StereoCameraTraceFrameId"].ValueAsDefault<uint64_t>(0);
	m_cameraTraceStart = std::max<uint64_t>(1, m_config["StereoCameraTraceStart"].ValueAsDefault<uint64_t>(1));
	m_cameraTraceLimit = std::clamp(m_config["StereoCameraTraceFrames"].ValueAsDefault<unsigned>(120), 1u, 600u);
	if (m_config["StereoCameraTraceAllModels"].ValueAsDefault<bool>(false)) m_cameraTraceLimit = std::min(m_cameraTraceLimit, 8u);
	// Landmark continuity needs exact instance provenance on successive native
	// frames. No paths are copied when this bounded diagnostic is disabled.
	m_recordModelPaths = !m_cameraTracePath.empty() && !m_cameraTraceFailed && m_cameraTraceSamples < m_cameraTraceLimit &&
		m_cameraTraceFrame >= m_cameraTraceStart && m_cameraTraceFrame - m_cameraTraceStart < m_cameraTraceLimit;
}

bool CNew3D::ShouldDrawViewport(const Viewport& viewport) const
{
	if (m_diagnosticEyeActive && m_stereoUI) {
		if (m_viewportPass == ViewportPass::World) return viewport.presentationWorld;
		if (m_viewportPass == ViewportPass::Overlay) return !viewport.presentationWorld;
		return true;
	}
	// Horizon mode is a road-only prototype. Unsupported/ambiguous roots and
	// map/instrument viewports are omitted; the authoritative center pass remains.
	return !m_diagnosticEyeActive || !m_stereoHorizonLock || viewport.cameraValid;
}

void CNew3D::ResetSceneInterpolation()
{
	m_sceneInterpolationActive = false;
	m_interpolationModels.clear(); m_previousInterpolationModels.clear();
	m_interpolationViewports.clear(); m_previousInterpolationViewports.clear();
	m_interpolationOrder.clear(); m_previousInterpolationOrder.clear();
	m_scenePlayback.Reset(); m_interpolationSample = {};
	m_interpolationMatched = m_interpolationFallback = m_interpolationAnchored = 0;
}

void CNew3D::PrepareSceneInterpolation(int64_t nowNs)
{
	LiveFrameTrace::OwnerRenderScope trace("prep.interpolation", m_sceneFrame, m_sceneEpoch);
	using namespace SceneInterpolation;
	if (!m_sceneInterpolationEnabled || !m_headsetPresentation || !m_preparedSceneValid ||
		!DrivingHUDScene() || TheaterIntroActive()) { ResetSceneInterpolation(); return; }
	// A paused native redraw can rebuild vectors under the same scene ID.
	// Rebind all current pointers and seed anew instead of retaining old ones.
	if (!m_interpolationViewports.empty() && m_interpolationFrame == m_sceneFrame && m_interpolationEpoch == m_sceneEpoch) ResetSceneInterpolation();
	const bool adjacent = !m_interpolationViewports.empty() && m_interpolationEpoch == m_sceneEpoch && m_interpolationFrame + 1 == m_sceneFrame;
	m_interpolationModels.swap(m_previousInterpolationModels); m_interpolationModels.clear();
	m_interpolationViewports.swap(m_previousInterpolationViewports); m_interpolationViewports.clear();
	m_interpolationOrder.swap(m_previousInterpolationOrder); m_interpolationOrder.clear();
	m_sceneInterpolationActive = false;
	m_interpolationFrame = m_sceneFrame; m_interpolationEpoch = m_sceneEpoch;
	m_interpolationMatched = m_interpolationFallback = m_interpolationAnchored = 0;
	// Reserve once, never retain meshes, textures, vertices or old scene nodes.
	m_interpolationModels.reserve(MaxModels); m_interpolationViewports.reserve(MaxViewports); m_interpolationOrder.reserve(MaxModels);
	std::array<uint32_t, 3> stationarySources{};
	size_t stationaryCount = 0;
	for (const auto& node : m_nodes) {
		const auto& vp = node.viewport;
		if (!vp.cameraValid || !vp.presentationWorld || node.models.empty()) continue;
		if (m_interpolationViewports.size() >= MaxViewports || node.models.size() > MaxModels - m_interpolationModels.size()) { ResetSceneInterpolation(); return; }
		InterpolationViewport view;
		view.node = &node; view.offset = m_interpolationModels.size(); view.count = node.models.size();
		view.key = {{vp.sourceAddress, uint32_t(vp.priority), uint32_t(vp.select), uint32_t(vp.number),
			uint32_t(m_stereoHorizonLock) | (uint32_t(vp.riderAnchorApplied) << 1) | (uint32_t(vp.stableChaseApplied) << 2)}};
		std::copy(vp.gameCamera.currentMatrix, vp.gameCamera.currentMatrix + 16, view.game.begin());
		Pose rigidGame;
		if (!ReadRigid(view.game.data(), rigidGame)) { ResetSceneInterpolation(); return; }
		view.inverseGame = Inverse(view.game.data());
		Mat4 camera = vp.gameCamera;
		if (m_stereoHorizonLock) {
			camera = vp.riderAnchorApplied ? vp.riderCorrection : vp.horizonCorrection;
			camera.MultMatrix(vp.gameCamera);
		}
		Pose rigidView;
		if (!ReadRigid(camera, rigidView)) { ResetSceneInterpolation(); return; }
		const auto cameraPose = Inverse(camera);
		if (!ReadRigid(cameraPose.data(), view.camera)) { ResetSceneInterpolation(); return; }
		// Current-scene association only. A bike spans many object children, so
		// excluding just the selected child would tear wheels/body during delay.
		uint32_t bikeRoot = 0; bool rootsValid = true;
		if (vp.riderAnchorApplied) {
			// The verified rider may be submitted in another priority viewport.
			// Find its root within this frame's shared native camera only.
			for (const auto& owner : m_nodes) {
				if (!owner.viewport.cameraValid || !owner.viewport.presentationWorld) continue;
				bool sameCamera = true;
				for (int i = 0; i < 16; ++i) if (std::abs(owner.viewport.gameCamera[i] - vp.gameCamera[i]) > .0001f) sameCamera = false;
				if (!sameCamera) continue;
				for (const auto& model : owner.models) {
				if (model.sourceAddress == vp.riderSourceModel && model.riderObjectNode == vp.riderObjectNode) {
					if (!model.presentationRoot || (bikeRoot && bikeRoot != model.presentationRoot)) rootsValid = false;
					bikeRoot = model.presentationRoot;
				}
				}
			}
			rootsValid = rootsValid && bikeRoot;
		}
		const InterpolationViewport* old = nullptr;
		if (adjacent) for (const auto& candidate : m_previousInterpolationViewports) {
			if (candidate.key != view.key) continue;
			if (old) { old = nullptr; break; } // Never guess between repeated viewports.
			old = &candidate;
		}
		for (const auto& model : node.models) {
			InterpolationModel item; item.source = model.sourceAddress;
			item.anchored = vp.riderAnchorApplied &&
				((model.presentationRoot && model.presentationRoot == bikeRoot) ||
				(vp.riderCharacterNode && model.riderObjectNode == vp.riderCharacterNode));
			if (!model.dynamic && model.meshes && !model.meshes->empty() && !item.anchored) {
				const auto world = Multiply(view.inverseGame.data(), model.modelMat);
				item.rigid = ReadModelRigid(world.data(), item.pose, item.scale);
				if (item.rigid) item.signature = InterpolationSignature(model);
			}
			m_interpolationOrder.push_back(m_interpolationModels.size());
			m_interpolationModels.push_back(item);
		}
		auto first = m_interpolationOrder.begin() + view.offset, last = first + view.count;
		std::sort(first, last, [&](size_t a, size_t b) { return m_interpolationModels[a].source < m_interpolationModels[b].source; });
		if (old && rootsValid && Nearby(old->camera, view.camera, 24.0, 30.0) &&
			std::abs(old->camera.position[1] - view.camera.position[1]) <= 12.0) {
			// Both source ranges are ordered. Never search the consumed prefix again.
			auto previousCursor = m_previousInterpolationOrder.begin() + old->offset;
			const auto previousLast = previousCursor + old->count;
			for (auto group = first; group != last;) {
				const uint32_t source = m_interpolationModels[*group].source;
				auto end = group + 1;
				while (end != last && m_interpolationModels[*end].source == source) ++end;
				// Nearby groups take a few forward steps; a large removed range
				// falls back to binary search without revisiting consumed groups.
				unsigned advanced = 0;
				while (previousCursor != previousLast && m_previousInterpolationModels[*previousCursor].source < source && advanced < 8) {
					++previousCursor; ++advanced;
				}
				if (previousCursor != previousLast && m_previousInterpolationModels[*previousCursor].source < source)
					previousCursor = std::lower_bound(previousCursor, previousLast, source,
						[&](size_t index, uint32_t value) { return m_previousInterpolationModels[index].source < value; });
				const auto found = previousCursor;
				auto oldEnd = found;
				while (oldEnd != previousLast && m_previousInterpolationModels[*oldEnd].source == source) ++oldEnd;
				previousCursor = oldEnd;
				const size_t count = size_t(end - group), oldCount = size_t(oldEnd - found);
				// The pairwise neighborhood check is explicitly capped per source;
				// crowds and pathological repeated scenery never cause unbounded work.
				if (count <= MaxSourceCopies && oldCount && oldCount <= MaxSourceCopies) {
					auto currentAt = [&](size_t i) -> const InterpolationModel& { return m_interpolationModels[*(group + i)]; };
					auto previousAt = [&](size_t i) -> const InterpolationModel& { return m_previousInterpolationModels[*(found + i)]; };
					for (size_t i = 0; i < count; ++i) {
						auto& item = m_interpolationModels[*(group + i)];
						const size_t match = FindMutualPoseMatch(i, count, oldCount, currentAt, previousAt);
						if (match == NoPoseMatch) continue;
						const auto& previous = previousAt(match);
						item.previous = previous.pose; item.matched = true;
						// Distinct unchanged assets confirm a common world basis, even
						// when the native game submits an identical draw more than once.
						if (Nearby(previous.pose, item.pose, .05, .25) && stationaryCount < stationarySources.size() &&
							std::find(stationarySources.begin(), stationarySources.begin() + stationaryCount, item.source) == stationarySources.begin() + stationaryCount)
							stationarySources[stationaryCount++] = item.source;
					}
				}
				group = end;
			}
			view.matched = true;
			view.previous = old->camera;
		}
		if (!view.matched) for (size_t i = view.offset; i < view.offset + view.count; ++i) m_interpolationModels[i].matched = false;
		m_interpolationViewports.push_back(view);
	}
	// Duplicate viewport identity in the current frame is also ambiguous.
	for (auto& a : m_interpolationViewports) for (const auto& b : m_interpolationViewports)
		if (&a != &b && a.key == b.key) a.matched = false;
	// Every world priority shares one camera timeline. A scenery-only viewport
	// can supply the stationary landmarks for a traffic-only viewport, but no
	// unmatched/different camera is allowed to move at a different scene time.
	bool compatible = !m_interpolationViewports.empty() && stationaryCount == stationarySources.size();
	for (const auto& view : m_interpolationViewports) {
		const auto& primary = m_interpolationViewports.front();
		compatible = compatible && view.matched && Nearby(view.camera, primary.camera, .05, .05) &&
			Nearby(view.previous, primary.previous, .05, .05);
	}
	if (!m_scenePlayback.Publish(m_sceneFrame, m_sceneEpoch, nowNs, compatible))
		for (auto& view : m_interpolationViewports) view.matched = false;
}

void CNew3D::SampleSceneInterpolationForEyes(int64_t nowNs, const PresentationEye (&eyes)[2])
{
	SampleSceneInterpolation(nowNs, eyes[0].displayTimeNs, eyes[0].displayTimeNs == eyes[1].displayTimeNs);
}

void CNew3D::SampleSceneInterpolation(int64_t nowNs, int64_t displayTimeNs, bool matchingEyes)
{
	using namespace SceneInterpolation;
	m_sceneInterpolationActive = false;
	m_interpolationMatched = m_interpolationFallback = m_interpolationAnchored = 0;
	m_interpolationSample = m_scenePlayback.AtDisplay(nowNs, displayTimeNs, matchingEyes);
	for (auto& view : m_interpolationViewports) {
		const bool active = view.matched && m_interpolationSample.continuous && m_interpolationSample.alpha < 1.0;
		if (active) view.correction = CameraCorrection(view.previous, view.camera, m_interpolationSample.alpha, view.inverseGame.data());
		m_sceneInterpolationActive |= active;
		for (size_t i = view.offset; i < view.offset + view.count; ++i) {
			auto& item = m_interpolationModels[i];
			if (item.anchored) ++m_interpolationAnchored;
			else if (active && item.matched) {
				item.sampled = ObjectModelView(item.previous, item.pose, m_interpolationSample.alpha, view.game.data(), item.scale);
				++m_interpolationMatched;
			} else ++m_interpolationFallback;
		}
	}
}

const float* CNew3D::InterpolatedModel(const Node& node, const Model& model, bool& anchored) const
{
	anchored = false;
	if (!m_sceneInterpolationActive || !m_headsetEyeActive) return model.modelMat;
	for (const auto& view : m_interpolationViewports) {
		if (view.node != &node || !view.matched) continue;
		const auto& item = m_interpolationModels[view.offset + size_t(&model - node.models.data())];
		anchored = item.anchored;
		return item.matched && !anchored ? item.sampled.data() : model.modelMat;
	}
	return model.modelMat;
}

Mat4 CNew3D::GetEyeTransform(const Viewport& viewport, bool interpolate) const
{
	// Cinema captures use the submitted camera, never head pose, horizon
	// correction or rider anchor. Optional stereo adds only parallel camera
	// spacing; physical head movement still affects the stationary screen alone.
	if (FlatIntroActive()) {
		Mat4 movie;
		if (m_cinemaCaptureStereo && viewport.cameraValid && viewport.presentationWorld) {
			const auto eye = StereoCinema::CaptureEye(m_stereoTheaterDepth, m_cinemaCaptureEye,
				m_cinemaUnits, m_cinemaAngle, viewport.angle_left, viewport.angle_right);
			if (eye.stereo) movie.Translate(eye.viewX, 0, 0);
		}
		return movie;
	}
	if (m_stereoUI && !viewport.presentationWorld) return Mat4();
	Mat4 view = m_eyeView;
	if (m_bodyExploring && viewport.presentationWorld) view.MultMatrix(BodyNavigationView());
	if (interpolate && m_headsetEyeActive && m_sceneInterpolationActive) {
		for (const auto& sample : m_interpolationViewports) if (sample.matched && &sample.node->viewport == &viewport) {
			view.MultMatrix(sample.correction.data());
			return view;
		}
	}
	if (m_stereoHorizonLock && viewport.cameraValid) {
		view.MultMatrix(viewport.riderAnchorApplied ? viewport.riderCorrection : viewport.horizonCorrection);
	}
	return view;
}

void CNew3D::CaptureRootCamera(UINT32 addr, UINT32 nodeFlags, UINT32 matrixIndex, UINT32 sibling)
{
	Viewport& vp = m_nodes.back().viewport;
	if (m_diagnosticNodePath.size() != 1) return;
	if (addr == vp.rootAddress) {
		vp.rootFlags = nodeFlags;
		vp.rootMatrixIndex = matrixIndex;
	}
	++vp.cameraRootBranches;
	if (vp.cameraRootBranches == 1) {
		vp.cameraSourceRoot = addr;
		vp.cameraSourceMatrix = matrixIndex;
		// Presentation needs the current transform, not the traversal stack.
		vp.gameCamera.LoadMatrix(m_modelMat.currentMatrix);
	}
	if (!vp.worldCandidate || vp.cameraRejected) return;
	auto reject = [&vp](const char* reason) {
		vp.cameraRejected = true;
		vp.cameraValid = false;
		vp.cameraStatus = reason;
	};
	if ((nodeFlags & 3) != 1) { reject("not_root_node"); return; }
	if (nodeFlags & 0x10) { reject("root_translation_mode"); return; }
	if (nodeFlags & 0x80) { reject("root_billboard_reset"); return; }
	const bool activeSibling = (nodeFlags & 7) != 6 && !(sibling & 0x1000000) && (sibling & 0xFFFFFF);
	if (activeSibling && !TranslateCullingAddress(sibling)) { reject("root_sibling_unavailable"); return; }
	if (!matrixIndex || !m_matrixBasePtr ||
		!TranslateCullingAddress(vp.matrixBaseAddress + matrixIndex * 12 + 11)) {
		reject("root_matrix_unavailable");
		return;
	}
	Mat4 inverse;
	if (!InvertRigidCamera(m_modelMat, inverse)) { reject("root_not_rigid"); return; }
	// Harley submits multiple sibling root branches for a world viewport. They
	// are safe only when every branch carries the same camera, never an object
	// transform or a second view. A rejection is sticky until the next scene.
	for (int i = 0; i < 16; ++i) {
		if (std::abs(vp.gameCamera[i] - m_modelMat[i]) > 0.0001f) {
			reject("root_sibling_camera_mismatch");
			return;
		}
	}
	if (!MakeLevelCamera(vp.gameCamera, vp.levelCamera, vp.horizonCorrection)) {
		reject("vertical_heading_ambiguous");
		return;
	}
	vp.cameraValid = true;
	vp.cameraStatus = "valid";
}

void CNew3D::PrepareRiderAnchor()
{
	if (!m_stereoRiderAnchor) return;
	if (m_stereoFullBike || m_stereoStableChase) {
		// A showroom may contain the same bike and rider. Require the measured
		// driving HUD/map layout as well as rejecting every recognized menu phase.
		// An attract driving replay can share this layout; it is not an input-
		// ownership detector. Ordinary cockpit-only behavior remains unchanged.
		const bool drivingLayout = InspectRaceLayout().settled;
		for (auto& node : m_nodes) node.viewport.riderDrivingLayout = drivingLayout;
		if (m_menuPhase != MenuComfort::Phase::Other || !drivingLayout) {
			for (auto& node : m_nodes) node.viewport.riderAnchorStatus =
				m_menuPhase != MenuComfort::Phase::Other ? "menu_scene" : "driving_layout_unavailable";
			return;
		}
	}
	// Each rider has independently measured bike and arm signatures.
	// The selected ROM model sits at this fixed offset below the first object
	// matrix after the camera root. Never identify an object by a RAM address or
	// a matrix-table slot: both are allocations that the game can change.
	const HarleyRiders::Profile* selectedProfile = nullptr;
	const char* status = "model_not_submitted";
	bool found = false, ambiguous = false;
	Mat4 selectedBody, selectedCamera, selectedBodyView;
	uint32_t selectedNode = 0, selectedSource = 0, characterNode = 0;
	bool selectedFullBike = false;
	const bool supportedGame = m_gameName == "harley" || m_gameName == "harleyb";
	if (!supportedGame) status = "unsupported_game";
	else for (const auto& candidate : m_riderCandidates) {
		const auto* profile = HarleyRiders::Find(candidate.sourceModel, m_stereoFullBike || m_stereoStableChase);
		if (!profile) continue;
		const Viewport* vp = nullptr;
		for (const auto& node : m_nodes) {
			if (node.viewport.sourceAddress == candidate.viewport) { vp = &node.viewport; break; }
		}
		if (!vp || !vp->cameraValid || (m_stereoUI && !vp->presentationWorld)) { status = "anchor_camera_invalid"; continue; }
		if (!candidate.topologyValid) { status = "anchor_object_topology"; continue; }
		Mat4 bodyInverse, gameInverse;
		if (!InvertRigidCamera(candidate.bodyView, bodyInverse) || !InvertRigidCamera(vp->gameCamera, gameInverse)) {
			status = "anchor_not_rigid"; continue;
		}
		Mat4 localModel = bodyInverse;
		localModel.MultMatrix(candidate.modelView);
		const bool chase = candidate.sourceModel == profile->chaseModel;
		if (m_stereoStableChase && !chase) { status = "stable_chase_requires_native_chase"; continue; }
		const float* offset = chase ? profile->chaseOffset : profile->cockpitOffset;
		bool signatureValid = true;
		for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row) {
			const float expected = col == 3 && row < 3 ? offset[row] : (col == row ? 1.0f : 0.0f);
			const float tolerance = col == 3 && row < 3 ? 0.01f : 0.001f;
			if (!std::isfinite(localModel[col * 4 + row]) || std::abs(localModel[col * 4 + row] - expected) > tolerance) signatureValid = false;
		}
		if (!signatureValid) { status = "anchor_model_offset_mismatch"; continue; }
		// In object coordinates the game camera is the inverse object-view
		// translation. Validate the measured native head offset before anchoring,
		// rejecting chase/cinematic views and other instances of the same model.
		if (!chase) {
			if (std::abs(bodyInverse[12] - profile->headOffset[0]) > 0.05f ||
				std::abs(bodyInverse[13] - profile->headOffset[1]) > 0.05f ||
				std::abs(bodyInverse[14] - profile->headOffset[2]) > 0.05f) {
				status = "native_cockpit_offset_mismatch"; continue;
			}
		}
		else {
			// Chase mode is opt-in. A candidate must sit in front of its native
			// camera, within the two observed chase distances. A nearby rider
			// group is also required below; matching a ROM model alone is unsafe.
			if ((!m_stereoFullBike && !m_stereoStableChase) || std::abs(bodyInverse[12]) > 40.0f ||
				bodyInverse[13] < 0.0f || bodyInverse[13] > 70.0f ||
				bodyInverse[14] < -130.0f || bodyInverse[14] > -8.0f) {
				status = "native_chase_offset_mismatch"; continue;
			}
		}
		Mat4 bodyWorld = gameInverse;
		bodyWorld.MultMatrix(candidate.bodyView);
		Mat4 bodyWorldInverse;
		if (!InvertRigidCamera(bodyWorld, bodyWorldInverse)) { status = "anchor_world_not_rigid"; continue; }
		// The native chase camera intentionally lags the bike's heading in a
		// turn. Heading agreement cannot identify the player; the measured body
		// signature, bounded camera offset and unique nearby arm group do so.
		if (found) {
			for (int i = 0; i < 16; ++i) {
				const float tolerance = i >= 12 && i <= 14 ? 0.02f : 0.001f;
				if (std::abs(bodyWorld[i] - selectedBody[i]) > tolerance) ambiguous = true;
			}
		}
		else {
			found = true;
			selectedBody = bodyWorld;
			selectedCamera = vp->gameCamera;
			selectedBodyView = candidate.bodyView;
			selectedNode = candidate.objectNode;
			selectedSource = candidate.sourceModel;
			selectedProfile = profile;
			selectedFullBike = chase;
		}
	}
	if (ambiguous) status = "multiple_distinct_anchors";
	else if (found) status = "valid";
	// The rider uses an identity object root and world-space animated bones.
	// Associate a whole group only when all six known arm parts are near this
	// validated bike. Never hide a ROM address globally: other riders can share it.
	auto armBit = [selectedProfile](uint32_t source) -> unsigned {
		return selectedProfile ? HarleyRiders::ArmBit(*selectedProfile, source) : 0u;
	};
	if (found && !ambiguous && selectedFullBike) {
		struct CharacterGroup { unsigned arms = 0; bool outsideBike = false; };
		std::unordered_map<uint32_t, CharacterGroup> groups;
		Mat4 bodyInverse;
		InvertRigidCamera(selectedBodyView, bodyInverse);
		for (const auto& node : m_nodes) {
			if (!node.viewport.cameraValid) continue;
			bool sameCamera = true;
			for (int i = 0; i < 16; ++i) if (std::abs(node.viewport.gameCamera[i] - selectedCamera[i]) > 0.001f) sameCamera = false;
			if (!sameCamera) continue;
			for (const auto& model : node.models) {
				if (!model.riderObjectNode || !armBit(model.sourceAddress)) continue;
				Mat4 local = bodyInverse;
				local.MultMatrix(model.modelMat);
				auto& group = groups[model.riderObjectNode];
				const float distance = std::hypot(local[12], local[13], local[14]);
				if (!std::isfinite(distance) || distance > 25.0f) group.outsideBike = true;
				group.arms |= armBit(model.sourceAddress);
			}
		}
		for (const auto& entry : groups) {
			if (entry.second.arms != 63u || entry.second.outsideBike) continue;
			if (characterNode && characterNode != entry.first) { ambiguous = true; break; }
			characterNode = entry.first;
		}
		if (ambiguous) status = "multiple_nearby_rider_groups";
		else if (!characterNode) { found = false; status = "full_bike_rider_group_unavailable"; }
	}
	for (auto& node : m_nodes) {
		auto& vp = node.viewport;
		vp.riderAnchorStatus = status;
		if (m_stereoUI && !vp.presentationWorld) { vp.riderAnchorStatus = "screen_overlay"; continue; }
		if (!vp.cameraValid) { vp.riderAnchorStatus = "camera_invalid"; continue; }
		if (!found || ambiguous) continue;
		bool sameCamera = true;
		for (int i = 0; i < 16; ++i) if (std::abs(vp.gameCamera[i] - selectedCamera[i]) > 0.001f) sameCamera = false;
		if (!sameCamera) { vp.riderAnchorStatus = "world_camera_mismatch"; continue; }
		Mat4 inverse;
		if (!InvertRigidCamera(vp.gameCamera, inverse)) continue;
		// Compute target-minus-game position using the small camera-relative body
		// translation, avoiding cancellation of large world coordinates. Target
		// is body origin plus measured height and presentation rise along WORLD up.
		// Pullback follows the upright camera's world-back axis (row 2), not bike
		// lean or simulated head rotation. Native cockpit validation above stays at
		// its independently measured height. Vehicle translation is deliberately unfiltered.
		if (m_stereoStableChase) {
			StableChase::Pose chase;
			if (!StableChase::Calculate(selectedBody.currentMatrix, selectedBodyView.currentMatrix,
				vp.gameCamera.currentMatrix, m_stereoChaseHeight, m_stereoChaseBack, chase)) {
				vp.riderAnchorStatus = "stable_chase_heading_ambiguous";
				continue;
			}
			std::copy(chase.correction.begin(), chase.correction.end(), vp.riderCorrection.currentMatrix);
			vp.stableChaseApplied = true;
		}
		else {
			float delta[3];
			for (int row = 0; row < 3; ++row) {
				delta[row] = inverse[row] * selectedBodyView[12] + inverse[4 + row] * selectedBodyView[13] +
					inverse[8 + row] * selectedBodyView[14] + (row == 1 ? selectedProfile->headOffset[1] + m_stereoRiderUp : 0.0f) +
					(m_stereoRiderBack - selectedProfile->headOffset[2]) * vp.levelCamera[row * 4 + 2];
			}
			vp.riderCorrection = vp.horizonCorrection;
			for (int row = 0; row < 3; ++row) {
				vp.riderCorrection[12 + row] = -(vp.levelCamera[row] * delta[0] + vp.levelCamera[4 + row] * delta[1] + vp.levelCamera[8 + row] * delta[2]);
			}
		}
		vp.riderCamera = vp.riderCorrection;
		vp.riderCamera.MultMatrix(vp.gameCamera);
		vp.riderBodyWorld = selectedBody;
		vp.riderObjectNode = selectedNode;
		vp.riderSourceModel = selectedSource;
		vp.riderAnchorApplied = true;
		vp.fullBikeApplied = selectedFullBike && !m_stereoStableChase;
		vp.riderCharacterNode = selectedFullBike ? characterNode : 0;
		if (selectedFullBike) for (auto& model : node.models) {
			if (model.riderObjectNode != characterNode) continue;
			model.hideInRiderEyes = !m_stereoStableChase && armBit(model.sourceAddress) == 0;
			if (model.hideInRiderEyes) ++vp.riderHiddenModels;
			if (armBit(model.sourceAddress)) ++vp.riderArmModels;
		}
	}
}

void CNew3D::ApplyRiderComfort()
{
	if (!m_stereoRiderComfort) return;
	const Viewport* anchor = nullptr;
	for (const auto& node : m_nodes) {
		if (node.viewport.riderAnchorApplied) { anchor = &node.viewport; break; }
	}
	std::array<double, 3> body{};
	std::array<double, 2> back{};
	if (anchor) {
		for (int i = 0; i < 3; ++i) body[i] = anchor->riderBodyWorld[12 + i];
		back = anchor->stableChaseApplied ? std::array<double, 2>{ anchor->riderCamera[2], anchor->riderCamera[10] } :
			std::array<double, 2>{ anchor->levelCamera[2], anchor->levelCamera[10] };
	}
	m_comfortResult = m_riderComfort.Update(m_sceneFrame, m_sceneEpoch, anchor != nullptr, body, back, m_gamePresentationState.motion);
	if (!anchor) return;
	// Apply one shared vertical displacement after the final scene decode.
	// The center/LOS image, body matrix, horizontal position, heading, and
	// subsequent head/eye transforms are not filtered.
	for (auto& node : m_nodes) {
		auto& vp = node.viewport;
		if (!vp.riderAnchorApplied) continue;
		for (int row = 0; row < 3; ++row)
			vp.riderCorrection[12 + row] -= vp.levelCamera[4 + row] * float(m_comfortResult.verticalOffset);
		vp.riderCamera = vp.riderCorrection;
		vp.riderCamera.MultMatrix(vp.gameCamera);
	}
}

void CNew3D::DrawComfortVignette(int x, int y, int width, int height, bool cinematic, float amount, const float* headsetAperture, const SpatialPanel::State* knownState)
{
	const float riding = m_bodyExploring ? 0.0f : float(m_comfortResult.vignetteStrength) * m_stereoVignetteScale;
	// Scale once at the common draw seam: riding, walking, cinematic and both
	// headset eyes obey the same preference. Identity retains the established
	// mask; zero exits before any GL work. Theater darkness and body blackout
	// are independent presentation boundaries, not comfort masks.
	const float strength = (cinematic ? std::clamp(amount, 0.0f, 1.0f) : std::max(riding, m_bodyVignette)) * UserComfortStrength();
	GLSLShader& shader = cinematic ? m_cinematicShader : m_comfortShader;
	GLuint& vaoObject = cinematic ? m_cinematicVao : m_comfortVao;
	bool& shaderFailed = cinematic ? m_cinematicShaderFailed : m_comfortShaderFailed;
	if ((!cinematic && !m_stereoRiderComfort && m_bodyVignette <= 0.0f) || strength <= 0.0f || shaderFailed) return;
	if (!vaoObject) {
		const char* vertex = R"glsl(#version 410 core
		out vec2 localPosition;
		void main() {
			const vec2 p[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
			localPosition = p[gl_VertexID];
			gl_Position = vec4(localPosition, 0, 1);
		})glsl";
		// LoadShaders does not bind a program, and logs failures but returns true.
		shader.LoadShaders(vertex, HeadsetVignette::FragmentShader(cinematic));
		shader.uniformLoc[0] = shader.GetUniformLocation("strength");
		shader.uniformLoc[1] = shader.GetUniformLocation("sharedAperture");
		shader.uniformLoc[2] = shader.GetUniformLocation("apertureFromNdc");
		shader.uniformLoc[3] = shader.GetUniformLocation("apertureProfile");
		if (shader.uniformLoc[0] < 0 || shader.uniformLoc[1] < 0 || shader.uniformLoc[2] < 0 || shader.uniformLoc[3] < 0) {
			shaderFailed = true;
			shader.UnloadShaders();
			ErrorLog("%s vignette shader failed; leaving the scene visible without the overlay.", cinematic ? "Cinematic" : "Rider");
			return;
		}
		glGenVertexArrays(1, &vaoObject);
	}
	GLint program, vao, viewport[4], scissorBox[4], srcRGB, dstRGB, srcAlpha, dstAlpha, eqRGB, eqAlpha;
	GLboolean depth, cull, blend, stencil, scissor;
	if (knownState) {
		// The immersive world pass establishes these exact values. Reuse them
		// without a synchronous query after submitting the expensive eye draw.
		program = knownState->program; vao = knownState->vao;
		std::copy(knownState->viewport, knownState->viewport + 4, viewport);
		std::copy(knownState->scissorBox, knownState->scissorBox + 4, scissorBox);
		srcRGB = knownState->srcRGB; dstRGB = knownState->dstRGB;
		srcAlpha = knownState->srcAlpha; dstAlpha = knownState->dstAlpha;
		eqRGB = knownState->eqRGB; eqAlpha = knownState->eqAlpha;
		depth = knownState->depth; cull = knownState->cull;
		blend = knownState->blend; stencil = knownState->stencil; scissor = knownState->scissor;
	} else {
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
		glGetIntegerv(GL_VIEWPORT, viewport);
		glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
		glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
		depth = glIsEnabled(GL_DEPTH_TEST); cull = glIsEnabled(GL_CULL_FACE);
		blend = glIsEnabled(GL_BLEND); stencil = glIsEnabled(GL_STENCIL_TEST);
		scissor = glIsEnabled(GL_SCISSOR_TEST);
	}
	glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
	glEnable(GL_SCISSOR_TEST); glScissor(x, y, width, height);
	glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	glViewport(x, y, width, height);
	shader.EnableShader();
	const auto profile = HeadsetVignette::ProfileFor(headsetAperture != nullptr, cinematic, strength,
		m_bodyExploring || m_bodyVignette > 0.0f ? 0.0f : m_riderComfort.CrashRelief());
	glUniform1f(shader.uniformLoc[0], strength);
	if (headsetAperture) glUniform3f(shader.uniformLoc[3], profile.clearRadius, profile.darkRadius, profile.opacity);
	glUniform1i(shader.uniformLoc[1], headsetAperture != nullptr);
	if (headsetAperture) glUniformMatrix3fv(shader.uniformLoc[2], 1, GL_FALSE, headsetAperture);
	glBindVertexArray(vaoObject);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(vao); glUseProgram(program);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
	glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
	glBlendEquationSeparate(eqRGB, eqAlpha);
	if (depth) glEnable(GL_DEPTH_TEST);
	if (cull) glEnable(GL_CULL_FACE);
	if (stencil) glEnable(GL_STENCIL_TEST);
	if (!blend) glDisable(GL_BLEND);
	if (!scissor) glDisable(GL_SCISSOR_TEST);
}

bool CNew3D::OpenCameraTrace()
{
	if (m_cameraTraceFile) return true;
	if (m_cameraTraceFailed || m_cameraTracePath.empty()) return false;
	// Main validates the requested destination. Exclusive creation also protects
	// a file that appears between validation and the first sampled frame.
#ifdef SUPERMODEL_WIN32
	const std::filesystem::path path(m_cameraTracePath);
	HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle != INVALID_HANDLE_VALUE) {
		const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_WRONLY | _O_BINARY);
		if (fd >= 0) {
			m_cameraTraceFile = _fdopen(fd, "wb");
			if (!m_cameraTraceFile) _close(fd);
		}
		else CloseHandle(handle);
	}
#else
	const int fd = open(m_cameraTracePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd >= 0) {
		m_cameraTraceFile = fdopen(fd, "wb");
		if (!m_cameraTraceFile) close(fd);
	}
#endif
	if (!m_cameraTraceFile) {
		ErrorLog("Unable to exclusively create stereo camera trace '%s'; existing files are preserved.", m_cameraTracePath.c_str());
		m_cameraTraceFailed = true;
		return false;
	}
	fprintf(m_cameraTraceFile,
		"{\"type\":\"header\",\"schema\":1,\"matrix_layout\":\"column_major\",\"world_up_assumption\":\"Y\",\"sample_start\":%llu,\"sample_limit\":%u,\"model_limit\":4096,\"models\":\"%s\",\"position_filter\":%s,\"comfort_model_version\":6,\"contact_source\":\"revision_b_wheel_support_and_impact_timers\",\"contact_attack_seconds\":0,\"contact_release_seconds\":0.12,\"vignette_step_game_units\":0.4,\"vignette_travel_game_units\":8.0,\"vignette_motion_frames\":6,\"vignette_settled_frames\":12,\"vignette_settled_step\":0.25,\"vignette_settled_velocity_span\":0.025,\"vignette_rough_frames\":60,\"vignette_rough_height_span\":3.0,\"vignette_rough_minimum_step\":0.025,\"vignette_rough_reversals\":2,\"vignette_attack_seconds\":0.12,\"vignette_release_seconds\":0.4,\"vignette_clear_radius\":0.58,\"vignette_edge_opacity\":0.9,\"vignette_scale\":%.9g,\"filter_tau_seconds\":0.18,\"filter_max_lag_game_units\":1.5,\"scene_hz\":57.524160,\"rider_offset\":%s,\"rider_profile_count\":5,\"legacy_fat_boy_height_game_units\":11.5,\"rider_up_game_units\":%.9g,\"rider_back_game_units\":%.9g,\"legacy_fat_boy_model_source\":9913245,\"effective_view_convention\":\"base_camera_before_head_and_eye\"}\n",
		(unsigned long long)m_cameraTraceStart, m_cameraTraceLimit, m_config["StereoCameraTraceAllModels"].ValueAsDefault<bool>(false) ? "every_bounded_native_sample" : "first_valid_sample_only", m_stereoRiderComfort ? "true" : "false", double(m_stereoVignetteScale), m_stereoRiderAnchor ? "true" : "false",
		double(m_stereoRiderUp), double(m_stereoRiderBack));
	InfoLog("Writing bounded camera trace to '%s'.", m_cameraTracePath.c_str());
	fputs("{\"type\":\"landmark_header\",\"schema\":2,\"sample_limit\":600,\"viewport_limit\":4,\"per_viewport_limit\":16,\"path_limit\":64,\"selection\":\"deep_paths_then_vertex_count\",\"identity\":\"viewport_source_full_path_scale_texture\",\"static_geometry_is_not_static_transform\":true}\n", m_cameraTraceFile);
	fputs("{\"type\":\"draw_state_header\",\"schema\":1,\"word_encoding\":\"uint32_exact_float_bits_no_padding\",\"hash\":\"fnv1a64_little_endian_words_fingerprint_not_equality_proof\",\"model_fields\":[\"source\",\"dynamic\",\"textureOffsetX\",\"textureOffsetY\",\"page\",\"scale_f32\",\"alpha_f32\",\"hidden\",\"model_matrix_f32x16\"],\"mesh_fields\":[\"format\",\"x\",\"y\",\"width\",\"height\",\"page\",\"inverted\",\"wrapU\",\"wrapV\",\"microTexture\",\"microID\",\"microMinLOD_f32\",\"textured\",\"polyAlpha\",\"textureAlpha\",\"alphaTest\",\"layered\",\"highPriority\",\"transLSelect\",\"translatorMap\",\"noLosReturn\",\"fixedShading\",\"smoothShading\",\"lighting\",\"specular\",\"shininess_f32\",\"specularValue_f32\",\"fogIntensity_f32\",\"vertexCount\"],\"viewport_fields\":[\"source\",\"priority\",\"select\",\"number\",\"rect_i32x4\",\"native_rect_f32x4\",\"slopes_f32x4\",\"cota_f32\",\"projection_f32x16\",\"lighting_f32x6\",\"sunClamp\",\"intensityClamp\",\"spotEllipse_f32x4\",\"spotRange_f32x2\",\"spotColor_f32x3\",\"fogColor_f32x3\",\"fogStart_f32\",\"fogDensity_f32\",\"fogAttenuation_f32\",\"fogAmbient_f32\",\"scrollFog_f32\",\"los_i32x2\",\"spotFogColor_f32x3\",\"scrollAtt_f32\",\"hardwareStep\",\"worldCandidate\",\"presentationWorld\",\"cameraValid\"],\"pipeline\":\"opaque_GEQUAL_trans_GREATER_reverseZ_depth32f_stencil8; clear_each_priority_and_overlay; layered_stencil_EQUAL0_INCR; nonlayered_LOS_upper_bit_REPLACE; color_blending_disabled_then_two_layer_composite; backface_culling_in_shader\",\"scope\":\"successful_prepared_decode_before_presentation_eye_transform; materials_texture_banks_frozen_within_decode; no_inter_decode_texture_content_equality_claim; excludes_culling_history_from_draw_equality_but_records_raw_path_nodes\"}\n", m_cameraTraceFile);
	return true;
}

void CNew3D::TraceDrawStateDuplicates(bool bypass)
{
	// Called once after each successful native/bypass upload, never per eye or
	// RenderScene transparency pass. Same-viewport cohorts cannot accidentally
	// count the legitimate second decode/eye as duplicated geometry.
	if (!m_recordModelPaths || m_cameraTraceFailed || m_cameraTraceSamples >= m_cameraTraceLimit ||
		m_cameraTraceFrame < m_cameraTraceStart || m_cameraTraceFrame - m_cameraTraceStart >= 4 ||
		m_drawStateTraceLastFrame[bypass ? 1 : 0] == m_cameraTraceFrame) return;
	if (!OpenCameraTrace()) return;
	m_drawStateTraceLastFrame[bypass ? 1 : 0] = m_cameraTraceFrame;
	FILE* file = m_cameraTraceFile;
	const char* pass = bypass ? "bypass_decode" : "native_decode";
	constexpr size_t viewportLimit = 16, modelLimit = 8192, sampleLimit = 8, pathLimit = 64;
	constexpr size_t meshDetailLimit = 16, meshVisitBudget = 65536, geometryVertexBudget = 250000;
	size_t scanned = 0, samples = 0, hashedVertices = 0, duplicateGroups = 0, extraModels = 0;
	size_t extraVertices = 0, invalidGeometry = 0, viewportIndex = 0, meshVisits = 0;
	size_t layerExtra[3] = {};
	bool truncated = false;
	struct Cohort { const Model* first = nullptr; const Model* second = nullptr; size_t count = 0, firstIndex = 0, secondIndex = 0; };
	for (const auto& node : m_nodes) {
		if (viewportIndex >= viewportLimit || scanned >= modelLimit) { truncated = true; break; }
		const auto& vp = node.viewport;
		const auto viewportWords = DrawTraceViewport(vp);
		fprintf(file, "{\"type\":\"draw_state_viewport\",\"schema\":1,\"frame\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"decode\":\"%s\",\"viewport_index\":%llu,\"viewport\":%u,\"priority\":%d,\"primitive\":%u,\"state_hash\":\"%016llx\",\"state_words\":",
			(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch,
			pass, (unsigned long long)viewportIndex, vp.sourceAddress, vp.priority, unsigned(m_primType),
			(unsigned long long)viewportWords.Hash());
		TraceWords(file, viewportWords.values);
		fputs("}\n", file);

		// Exact grouping uses shared prepared mesh identity plus every model
		// shader input bit, NOT a hash or rounded camera/world coordinates.
		// Distinct but byte-equal mesh allocations deliberately remain separate.
		std::map<std::vector<uint32_t>, Cohort> cohorts;
		std::unordered_map<const std::vector<Mesh>*, uint32_t> preparedGeometry;
		size_t modelIndex = 0;
		for (const auto& model : node.models) {
			if (scanned >= modelLimit) { truncated = true; break; }
			++scanned;
			if (!model.meshes || model.meshes->empty()) { ++modelIndex; continue; }
			auto geometry = preparedGeometry.emplace(model.meshes.get(), uint32_t(preparedGeometry.size()));
			auto key = DrawTraceModel(model);
			key.Add(geometry.first->second);
			auto& cohort = cohorts[key.values];
			if (!cohort.count) { cohort.first = &model; cohort.firstIndex = modelIndex; }
			else if (cohort.count == 1) { cohort.second = &model; cohort.secondIndex = modelIndex; }
			++cohort.count; ++modelIndex;
		}
		for (const auto& entry : cohorts) {
			const auto& cohort = entry.second;
			if (cohort.count < 2) continue;
			const Model& model = *cohort.first;
			if (model.meshes->size() > meshVisitBudget - meshVisits) { truncated = true; break; }
			meshVisits += model.meshes->size();
			++duplicateGroups; extraModels += cohort.count - 1;
			size_t vertices = 0, layered = 0, transparent = 0, highPriority = 0;
			DrawTraceWords materialWords;
			for (const auto& mesh : *model.meshes) {
				vertices += size_t(std::max(0, mesh.vertexCount));
				layered += mesh.layered; highPriority += mesh.highPriority;
				transparent += mesh.polyAlpha || mesh.textureAlpha || model.alpha < 1.0f;
				Mesh drawMesh = mesh;
				for (int layer = 0; layer < 3; ++layer)
					if (drawMesh.Render(static_cast<Layer>(layer), model.alpha)) layerExtra[layer] += cohort.count - 1;
				if (samples < sampleLimit) {
					const auto words = DrawTraceMesh(mesh);
					materialWords.values.insert(materialWords.values.end(), words.values.begin(), words.values.end());
				}
			}
			extraVertices += vertices * (cohort.count - 1);
			if (samples >= sampleLimit) continue;
			++samples;

			uint64_t geometryHash = 14695981039346656037ull;
			bool geometryComplete = vertices <= geometryVertexBudget - hashedVertices;
			if (geometryComplete) {
				const auto& buffer = model.dynamic ? m_polyBufferRam : m_polyBufferRom;
				DrawTraceWords vertexWords;
				vertexWords.values.reserve(18);
				for (const auto& mesh : *model.meshes) {
					const int begin = mesh.vboOffset - (model.dynamic ? MAX_ROM_VERTS : 0);
					if (begin < 0 || mesh.vertexCount < 0 || size_t(begin) > buffer.size() ||
						size_t(mesh.vertexCount) > buffer.size() - size_t(begin)) { geometryComplete = false; ++invalidGeometry; break; }
					for (int i = 0; i < mesh.vertexCount; ++i) {
						const auto& vertex = buffer[size_t(begin) + size_t(i)];
						vertexWords.values.clear();
						vertexWords.Floats(vertex.base.pos, 4); vertexWords.Floats(vertex.base.normal, 3);
						vertexWords.Floats(vertex.base.texcoords, 2); vertexWords.Float(vertex.base.fixedShade);
						vertexWords.Floats(vertex.faceNormal, 3);
						for (auto color : vertex.faceColour) vertexWords.Add(color);
						vertexWords.Float(vertex.textureNP);
						for (uint32_t word : vertexWords.values) for (unsigned shift = 0; shift < 32; shift += 8) {
							geometryHash ^= (word >> shift) & 255u; geometryHash *= 1099511628211ull;
						}
						++hashedVertices;
					}
				}
			}
			const auto modelWords = DrawTraceModel(model);
			fprintf(file, "{\"type\":\"draw_state_duplicate\",\"schema\":1,\"frame\":%llu,\"decode\":\"%s\",\"viewport_index\":%llu,\"viewport\":%u,\"source\":%u,\"count\":%llu,\"model_indices\":[%llu,%llu],\"exact_shared_prepared_geometry\":true,\"within_single_decode_viewport\":true,\"dedup_safe\":false,\"dynamic\":%s,\"vertices\":%llu,\"mesh_count\":%llu,\"layered_meshes\":%llu,\"transparent_meshes\":%llu,\"high_priority_meshes\":%llu,\"model_hash\":\"%016llx\",\"material_hash\":\"%016llx\",\"geometry_hash_complete\":%s,\"geometry_hash\":\"%016llx\",\"model_words\":",
				(unsigned long long)m_cameraTraceFrame, pass, (unsigned long long)viewportIndex, vp.sourceAddress, model.sourceAddress,
				(unsigned long long)cohort.count, (unsigned long long)cohort.firstIndex, (unsigned long long)cohort.secondIndex,
				model.dynamic ? "true" : "false", (unsigned long long)vertices, (unsigned long long)model.meshes->size(),
				(unsigned long long)layered, (unsigned long long)transparent, (unsigned long long)highPriority,
				(unsigned long long)modelWords.Hash(), (unsigned long long)materialWords.Hash(), geometryComplete ? "true" : "false", (unsigned long long)geometryHash);
			TraceWords(file, modelWords.values);
			fputs(",\"material_words\":", file);
			if (model.meshes->size() <= meshDetailLimit) TraceWords(file, materialWords.values); else fputs("null", file);
			fputs(",\"culling_paths\":[", file);
			for (int which = 0; which < 2; ++which) {
				if (which) fputc(',', file);
				const auto& path = (which ? cohort.second : cohort.first)->diagnosticNodePath;
				fprintf(file, "{\"truncated\":%s,\"nodes\":[", path.size() > pathLimit ? "true" : "false");
				for (size_t i = 0; i < std::min(path.size(), pathLimit); ++i) {
					if (i) fputc(',', file);
					fprintf(file, "{\"address\":%u,\"words\":[", path[i]);
					for (unsigned word = 0; word < unsigned(10 - m_offset); ++word) {
						if (word) fputc(',', file);
						const UINT32* value = TranslateCullingAddress(path[i] + word);
						if (value) fprintf(file, "%u", *value); else fputs("null", file);
					}
					fputs("]}", file);
				}
				fputs("]}", file);
			}
			fputs("]}\n", file);
		}
		++viewportIndex;
	}
	fprintf(file, "{\"type\":\"draw_state_summary\",\"schema\":1,\"frame\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"decode\":\"%s\",\"probe_frame_limit\":4,\"viewport_limit\":16,\"model_limit\":8192,\"sample_limit\":8,\"scanned_models\":%llu,\"duplicate_groups\":%llu,\"extra_model_instances\":%llu,\"extra_vertices\":%llu,\"potential_extra_mesh_draws_by_layer\":[%llu,%llu,%llu],\"samples\":%llu,\"truncated\":%s,\"geometry_vertices_hashed\":%llu,\"invalid_geometry_ranges\":%llu,\"interpretation\":\"prepared_model_cohorts_not_final_pixels_or_draw_calls; shared_mesh_identity_and_exact_model_bits; counts_never_combine_native_bypass_or_eyes; layered_stencil_and_intervening_coplanars_may_make_repetition_intentional\"}\n",
		(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch, pass,
		(unsigned long long)scanned, (unsigned long long)duplicateGroups, (unsigned long long)extraModels, (unsigned long long)extraVertices,
		(unsigned long long)layerExtra[0], (unsigned long long)layerExtra[1], (unsigned long long)layerExtra[2],
		(unsigned long long)samples, truncated ? "true" : "false", (unsigned long long)hashedVertices, (unsigned long long)invalidGeometry);
}

void CNew3D::TraceCameraLandmarks(FILE* file)
{
	// This is evidence collection, never a rendering/culling decision. Immutable
	// ROM geometry also describes moving cars and pedestrians; the offline
	// analyzer must establish temporal stability before calling anything static.
	struct Candidate { const Model* model; size_t vertices; };
	unsigned viewports = 0, records = 0;
	size_t eligible = 0, rejectedPaths = 0;
	for (const auto& node : m_nodes) {
		const auto& vp = node.viewport;
		if (!vp.cameraValid || !vp.presentationWorld) continue;
		Mat4 inverse;
		if (!InvertRigidCamera(vp.gameCamera, inverse)) continue;
		if (viewports++ >= 4) break;
		std::vector<Candidate> selected;
		selected.reserve(17);
		auto earlier = [](const Candidate& a, const Candidate& b) {
			// A largest-mesh-only pilot mostly selected cars and pedestrians.
			// Diversify toward nested scene geometry without claiming it is static;
			// that still requires the analyzer's independent temporal checks.
			const bool deepA = a.model->diagnosticNodePath.size() >= 5;
			const bool deepB = b.model->diagnosticNodePath.size() >= 5;
			if (deepA != deepB) return deepA;
			if (a.vertices != b.vertices) return a.vertices > b.vertices;
			if (a.model->sourceAddress != b.model->sourceAddress) return a.model->sourceAddress < b.model->sourceAddress;
			return a.model->diagnosticNodePath < b.model->diagnosticNodePath;
		};
		for (const auto& model : node.models) {
			if (model.dynamic || !model.meshes || model.hideInRiderEyes ||
				(vp.riderObjectNode && model.riderObjectNode == vp.riderObjectNode)) continue;
			// Do not truncate an instance path into a misleading shared identity.
			if (model.diagnosticNodePath.empty() || model.diagnosticNodePath.size() > 64) { ++rejectedPaths; continue; }
			size_t vertices = 0;
			for (const auto& mesh : *model.meshes) vertices += mesh.vertexCount;
			if (!vertices) continue;
			++eligible;
			const Candidate candidate{&model, vertices};
			const auto at = std::lower_bound(selected.begin(), selected.end(), candidate, earlier);
			if (at == selected.end() && selected.size() == 16) continue;
			selected.insert(at, candidate);
			if (selected.size() > 16) selected.pop_back();
		}
		for (const auto& candidate : selected) {
			const auto& model = *candidate.model;
			unsigned occurrences = 0;
			for (const auto& other : node.models) {
				if (other.sourceAddress == model.sourceAddress && other.diagnosticNodePath == model.diagnosticNodePath &&
					other.scale == model.scale && other.textureOffsetX == model.textureOffsetX &&
					other.textureOffsetY == model.textureOffsetY && other.page == model.page) ++occurrences;
			}
			Mat4 world = inverse;
			world.MultMatrix(model.modelMat);
			fprintf(file, "{\"type\":\"landmark\",\"frame\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"viewport\":%u,\"source\":%u,\"immutable_geometry\":true,\"identity_occurrences\":%u,\"mesh_count\":%llu,\"vertex_count\":%llu,\"scale\":%.9g,\"texture\":[%d,%d,%d],\"path\":[",
				(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch,
				vp.sourceAddress, model.sourceAddress, occurrences, (unsigned long long)model.meshes->size(),
				(unsigned long long)candidate.vertices, double(model.scale), model.textureOffsetX, model.textureOffsetY, model.page);
			for (size_t i = 0; i < model.diagnosticNodePath.size(); ++i) {
				if (i) fputc(',', file);
				fprintf(file, "%u", model.diagnosticNodePath[i]);
			}
			fputs("],\"root_relative\":", file);
			TraceFloats(file, world, 16);
			fputs(",\"model_view\":", file);
			TraceFloats(file, model.modelMat, 16);
			fputs("}\n", file);
			++records;
		}
	}
	fprintf(file, "{\"type\":\"landmarks_complete\",\"frame\":%llu,\"written\":%u,\"eligible_in_sampled_viewports\":%llu,\"rejected_paths\":%llu,\"truncated\":%s}\n",
		(unsigned long long)m_cameraTraceFrame, records, (unsigned long long)eligible, (unsigned long long)rejectedPaths,
		eligible > records || viewports > 4 ? "true" : "false");
	// The selected chassis is only one child of the bike's camera-root branch.
	// Record a small candidate-membership summary including mutable RAM meshes;
	// it is deliberately not promoted to a presentation exception by this code.
	unsigned assemblies = 0;
	for (const auto& node : m_nodes) {
		const auto& vp = node.viewport;
		if (!vp.cameraValid || !vp.riderAnchorApplied || !vp.presentationWorld) continue;
		uint32_t root = 0;
		unsigned anchors = 0;
		for (const auto& model : node.models) {
			if (model.sourceAddress != vp.riderSourceModel || model.riderObjectNode != vp.riderObjectNode ||
				model.diagnosticNodePath.empty()) continue;
			root = model.diagnosticNodePath.front();
			++anchors;
		}
		if (!anchors) continue;
		if (assemblies++ >= 4) break;
		Mat4 inverse;
		if (!InvertRigidCamera(vp.gameCamera, inverse)) continue;
		unsigned members = 0, mutableMembers = 0, arms = 0, nearOtherRoots = 0, invalid = 0;
		size_t vertices = 0;
		float maximumDistance = 0.0f;
		for (const auto& model : node.models) {
			const bool bike = !model.diagnosticNodePath.empty() && model.diagnosticNodePath.front() == root;
			const bool arm = vp.riderCharacterNode && model.riderObjectNode == vp.riderCharacterNode && !model.hideInRiderEyes;
			Mat4 world = inverse;
			world.MultMatrix(model.modelMat);
			const float distance = std::hypot(world[12] - vp.riderBodyWorld[12],
				world[13] - vp.riderBodyWorld[13], world[14] - vp.riderBodyWorld[14]);
			if (bike) {
				++members;
				mutableMembers += model.dynamic ? 1 : 0;
				if (!std::isfinite(distance)) ++invalid;
				else maximumDistance = std::max(maximumDistance, distance);
				if (model.meshes) for (const auto& mesh : *model.meshes) vertices += mesh.vertexCount;
			}
			else if (!arm && std::isfinite(distance) && distance < 30.0f) ++nearOtherRoots;
			if (arm) ++arms;
		}
		const bool bounded = anchors == 1 && members > 1 && members <= 64 && !invalid && maximumDistance <= 30.0f;
		fprintf(file, "{\"type\":\"bike_assembly\",\"frame\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"viewport\":%u,\"chassis_source\":%u,\"chassis_object_node\":%u,\"candidate_camera_root\":%u,\"matching_anchors\":%u,\"root_models\":%u,\"mutable_root_models\":%u,\"root_vertices\":%llu,\"visible_character_models\":%u,\"other_nearby_models_same_viewport\":%u,\"maximum_origin_distance\":%.9g,\"invalid_origins\":%u,\"candidate_bounds_pass\":%s,\"full_membership_proven\":false,\"status\":\"%s\"}\n",
			(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch,
			vp.sourceAddress, vp.riderSourceModel, vp.riderObjectNode, root, anchors, members, mutableMembers,
			(unsigned long long)vertices, arms, nearOtherRoots, double(maximumDistance), invalid,
			bounded ? "true" : "false", bounded ? "bounded_candidate_requires_membership_oracle" : "candidate_rejected");
	}
}

void CNew3D::TraceCameraScene()
{
	if (m_cameraTracePath.empty() || m_cameraTraceFailed || m_cameraTraceSamples >= m_cameraTraceLimit ||
		m_cameraTraceFrame < m_cameraTraceStart || m_cameraTraceFrame - m_cameraTraceStart >= m_cameraTraceLimit ||
		m_cameraTraceFrame == m_cameraTraceLastFrame) return;
	if (!OpenCameraTrace()) return;
	m_cameraTraceLastFrame = m_cameraTraceFrame;
	++m_cameraTraceSamples;
	const bool traceAllModels = m_config["StereoCameraTraceAllModels"].ValueAsDefault<bool>(false);
	// The frame guard above runs once per native scene, never once per eye.
	// This explicit diagnostic mode is capped at eight frames in both callers.
	if (traceAllModels) m_cameraTraceModelsWritten = false;
	FILE* file = m_cameraTraceFile;
	const bool eyeScene = m_stereoDiagnostic && m_eyeTargets[0].framebuffer && !m_diagnosticCenterFallback && !m_diagnosticCachedPair;
	fprintf(file,
		"{\"type\":\"frame\",\"frame\":%llu,\"render_serial\":%llu,\"sample\":%u,\"scene\":\"%s\",\"cull_bypass\":%s,\"world_lod_distance_multiplier\":%.9g,\"stereo_ui\":%s,\"cinematic_vignette\":%s,\"ui_only_scene\":%s,\"horizon_requested\":%s,\"horizon_fov_horizontal\":%.9g,\"viewport_count\":%llu}\n",
		(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_diagnosticRenderSerial,
		m_cameraTraceSamples, m_diagnosticCachedPair ? "cached_eyes" : eyeScene ? "eyes" : "center", (eyeScene && m_stereoCullBypass && !FlatIntroActive()) ? "true" : "false",
		double(eyeScene && m_stereoCullBypass && !FlatIntroActive() ? m_stereoLodDistance : 1.0f),
		m_stereoUI ? "true" : "false", m_config["StereoCinematicVignette"].ValueAsDefault<bool>(false) ? "true" : "false",
		m_uiOnlyScene ? "true" : "false",
		m_stereoHorizonLock ? "true" : "false", double(m_stereoHorizonFov), (unsigned long long)m_nodes.size());
	fprintf(file, "{\"type\":\"intro_presentation\",\"frame\":%llu,\"requested\":%s,\"flat\":%s}\n",
		(unsigned long long)m_cameraTraceFrame, m_stereoFlatIntro ? "true" : "false", FlatIntroActive() ? "true" : "false");
	fprintf(file, "{\"type\":\"spatial_ui\",\"frame\":%llu,\"theater\":%s,\"hud_scene\":%s,\"hud_panel\":%s,\"hud_hidden\":%s,\"panel_distance\":40,\"theater_half_width\":%.9g,\"screen_angle\":%.9g,\"hud_half_width\":16,\"hud_center_y\":-14,\"overlay_panel\":%s,\"presentation_mode\":\"%s\",\"phase_mode\":\"%s\",\"revision_b\":%s,\"main_phase\":%u,\"race_phase\":%u,\"attract_flag\":%u,\"intro_complete\":%u,\"rider_confirmed\":%u,\"selection_stage\":%u}\n",
		(unsigned long long)m_cameraTraceFrame, TheaterIntroActive() ? "true" : "false", DrivingHUDScene() ? "true" : "false",
		DrivingHUDScene() && (m_stereoVRHUD || GeneralTheaterEnabled()) && !m_stereoHideHUD ? "true" : "false",
		DrivingHUDScene() && m_stereoHideHUD ? "true" : "false", double(TheaterHalfWidth()), double(TheaterScreenAngle()),
		ImmersiveOverlayPanel() ? "true" : "false", TheaterIntroActive() ? "theater" : "immersive",
		HarleyPresentation::Name(HarleyPresentation::Classify(m_gamePresentationState)),
		m_gamePresentationState.revisionB ? "true" : "false", m_gamePresentationState.mainPhase,
		m_gamePresentationState.racePhase, m_gamePresentationState.attractFlag, m_gamePresentationState.introComplete,
		m_gamePresentationState.riderConfirmed, m_gamePresentationState.selectionStage);
	fprintf(file,
		"{\"type\":\"eye_layout\",\"frame\":%llu,\"wide_requested\":%s,\"wide_applied\":%s,\"mono_preview\":%s,\"rendered_eyes\":%d,\"target_width\":%u,\"target_height\":%u,\"world_rect\":[%d,%d,%d,%d],\"native_ui_rect\":[%d,%d,%d,%d],\"horizontal_expansion\":%.9g}\n",
		(unsigned long long)m_cameraTraceFrame, m_wideScreen ? "true" : "false",
		eyeScene && m_wideScreen && m_eyeLayout.HorizontalExpansion() > 1.0f ? "true" : "false",
		m_stereoMonoPreview ? "true" : "false", eyeScene ? (StereoTheaterActive() ? 2 : (m_stereoMonoPreview || TheaterIntroActive() ? 1 : 2)) : 0, m_totalXRes, m_totalYRes,
		m_eyeLayout.world.x, m_eyeLayout.world.y, m_eyeLayout.world.width, m_eyeLayout.world.height,
		m_eyeLayout.nativeUI.x, m_eyeLayout.nativeUI.y, m_eyeLayout.nativeUI.width, m_eyeLayout.nativeUI.height,
		double(m_eyeLayout.HorizontalExpansion()));
	fprintf(file,
		"{\"type\":\"comfort\",\"frame\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"enabled\":%s,\"vertical_offset\":%.12g,\"vignette_strength\":%.12g,\"large_motion\":%s,\"vertical_travel\":%.12g,\"fast_frames\":%u,\"motion_qualified\":%s,\"settled\":%s,\"height_span\":%.12g,\"velocity_span\":%.12g,\"rough_reversals\":%u,\"status\":\"%s\"}\n",
		(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch,
		m_stereoRiderComfort ? "true" : "false", m_comfortResult.verticalOffset, m_comfortResult.vignetteStrength, m_comfortResult.largeMotion ? "true" : "false",
		m_comfortResult.verticalTravel, m_comfortResult.fastFrames,
		m_comfortResult.motionQualified ? "true" : "false", m_comfortResult.settled ? "true" : "false",
		m_comfortResult.heightSpan, m_comfortResult.velocitySpan, m_comfortResult.roughReversals, m_comfortResult.status);
	const auto& contact = m_gamePresentationState.motion;
	fprintf(file,
		"{\"type\":\"comfort_contact\",\"frame\":%llu,\"scene_frame\":%llu,\"valid\":%s,\"front_supported\":%s,\"rear_supported\":%s,\"front_impact_frames\":%u,\"rear_impact_frames\":%u,\"crash_relief\":%.9g,\"jump_strength\":%.12g,\"crash_strength\":%.12g,\"cut_strength\":%.12g,\"airborne\":%s,\"impact_active\":%s}\n",
		(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame,
		contact.valid ? "true" : "false", contact.frontSupported ? "true" : "false", contact.rearSupported ? "true" : "false",
		contact.frontImpactFrames, contact.rearImpactFrames, m_riderComfort.CrashRelief(),
		m_comfortResult.jumpStrength, m_comfortResult.crashStrength, m_comfortResult.cutStrength,
		m_comfortResult.airborne ? "true" : "false", m_comfortResult.impactActive ? "true" : "false");
	const auto& menu = m_menuComfort.State();
	const char* phase = m_menuPhase == MenuComfort::Phase::Select ? "select" :
		m_menuPhase == MenuComfort::Phase::Intro ? "intro" :
		m_menuPhase == MenuComfort::Phase::RaceIntro ? "race_intro" :
		m_menuPhase == MenuComfort::Phase::Transmission ? "transmission" : "other";
	fprintf(file,
		"{\"type\":\"menu_comfort\",\"frame\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,\"ticks\":%llu,\"enabled\":%s,\"phase\":\"%s\",\"strength\":%.12g,\"effective_strength\":%.9g,\"input_edge\":%s,\"camera_moved\":%s,\"camera_valid\":%s,\"motion_active\":%s,\"pending_frames\":%u,\"stable_frames\":%u,\"missing_frames\":%u,\"navigation\":%d,\"steering\":%d,\"start\":%s,\"select_marker_a\":%s,\"select_marker_b\":%s,\"intro_marker_a\":%s,\"intro_marker_b\":%s,\"camera_viewport\":%u,\"status\":\"%s\"}\n",
		(unsigned long long)m_cameraTraceFrame, (unsigned long long)m_sceneFrame, (unsigned long long)m_sceneEpoch,
		(unsigned long long)m_menuComfortTicks, m_stereoMenuComfort ? "true" : "false", phase,
		menu.strength, double(CinematicVignetteStrength()), menu.inputEdge ? "true" : "false",
		menu.cameraMoved ? "true" : "false", menu.cameraValid ? "true" : "false", menu.motionActive ? "true" : "false",
		menu.pendingFrames, menu.stableFrames, menu.missingFrames, m_menuNavigation, m_menuSteering,
		m_menuStart ? "true" : "false", m_menuSelectMarkerA ? "true" : "false", m_menuSelectMarkerB ? "true" : "false",
		m_menuIntroMarkerA ? "true" : "false", m_menuIntroMarkerB ? "true" : "false", m_menuCameraViewport, menu.status);
	fprintf(file,
		"{\"type\":\"menu_layout\",\"frame\":%llu,\"classifier\":\"harley_viewport_layout_v3\",\"matched\":%s,\"viewport_count\":%llu,\"world_viewports\":%u,\"clock_viewports\":%u,\"clock_height\":%.9g,\"native_ui_viewports\":%u,\"race_layout\":%s,\"race_map_settled\":%s}\n",
		(unsigned long long)m_cameraTraceFrame, m_menuLayoutMatch ? "true" : "false", (unsigned long long)m_nodes.size(),
		m_menuWorldViewports, m_menuClockViewports, double(m_menuClockHeight), m_menuNativeUI,
		m_menuRaceLayout ? "true" : "false", m_menuRaceSettled ? "true" : "false");
	size_t cameraRecords = 0;
	for (const auto& node : m_nodes) {
		if (cameraRecords++ >= 16) break;
		const auto& vp = node.viewport;
		const bool applied = eyeScene && !FlatIntroActive() && m_stereoHorizonLock && vp.cameraValid && (!m_stereoUI || vp.presentationWorld);
		fprintf(file,
			"{\"type\":\"camera\",\"frame\":%llu,\"viewport\":%u,\"priority\":%d,\"matrix_base\":%u,\"root\":%u,\"root_matrix\":%u,\"root_flags\":%u,\"root_branches\":%u,\"world_candidate\":%s,\"camera_valid\":%s,\"horizon_applied\":%s,\"status\":\"%s\",\"native_rect\":[%.9g,%.9g,%.9g,%.9g],\"game_view\":",
			(unsigned long long)m_cameraTraceFrame, vp.sourceAddress, vp.priority, vp.matrixBaseAddress,
			vp.rootAddress, vp.rootMatrixIndex, vp.rootFlags, vp.cameraRootBranches,
			vp.worldCandidate ? "true" : "false", vp.cameraValid ? "true" : "false", applied ? "true" : "false",
			vp.cameraStatus, double(vp.vpX), double(vp.vpY), double(vp.vpWidth), double(vp.vpHeight));
		TraceFloats(file, vp.gameCamera, 16);
		fprintf(file, ",\"presentation_world\":%s,\"native_screen_camera\":%s",
			(m_stereoUI ? vp.presentationWorld : vp.cameraValid) ? "true" : "false",
			FlatIntroActive() || (m_stereoUI && !vp.presentationWorld) ? "true" : "false");
		fprintf(file, ",\"camera_source_root\":%u,\"camera_source_matrix\":%u", vp.cameraSourceRoot, vp.cameraSourceMatrix);
		fputs(",\"level_view\":", file);
		if (vp.cameraValid) TraceFloats(file, vp.levelCamera, 16); else fputs("null", file);
		fputs(",\"horizon_correction\":", file);
		if (vp.cameraValid) TraceFloats(file, vp.horizonCorrection, 16); else fputs("null", file);
		fprintf(file, ",\"rider_anchor_requested\":%s,\"rider_anchor_applied\":%s,\"rider_anchor_status\":\"%s\",\"rider_object_node\":%u,\"rider_body_world\":",
			m_stereoRiderAnchor ? "true" : "false", applied && vp.riderAnchorApplied ? "true" : "false", vp.riderAnchorStatus, vp.riderObjectNode);
		if (vp.riderAnchorApplied) TraceFloats(file, vp.riderBodyWorld, 16); else fputs("null", file);
		fprintf(file, ",\"rider_vertical_offset\":%.12g", vp.riderAnchorApplied ? m_comfortResult.verticalOffset : 0.0);
		fprintf(file, ",\"full_bike_requested\":%s,\"full_bike_applied\":%s,\"rider_source_model\":%u,\"rider_character_node\":%u,\"rider_hidden_models\":%u,\"rider_arm_models\":%u",
			m_stereoFullBike ? "true" : "false", applied && vp.fullBikeApplied ? "true" : "false",
			vp.riderSourceModel, vp.riderCharacterNode, vp.riderHiddenModels, vp.riderArmModels);
		fprintf(file, ",\"stable_chase_requested\":%s,\"stable_chase_applied\":%s,\"stable_chase_height\":%.9g,\"stable_chase_back\":%.9g,\"rider_driving_layout\":%s",
			m_stereoStableChase ? "true" : "false", applied && vp.stableChaseApplied ? "true" : "false",
			double(m_stereoChaseHeight), double(m_stereoChaseBack), vp.riderDrivingLayout ? "true" : "false");
		fputs(",\"rider_view\":", file);
		if (vp.riderAnchorApplied) TraceFloats(file, vp.riderCamera, 16); else fputs("null", file);
		fputs(",\"rider_correction\":", file);
		if (vp.riderAnchorApplied) TraceFloats(file, vp.riderCorrection, 16); else fputs("null", file);
		fputs(",\"effective_view\":", file);
		const Mat4& effective = applied ? (vp.riderAnchorApplied ? vp.riderCamera : vp.levelCamera) : vp.gameCamera;
		TraceFloats(file, effective, 16);
		fputs(",\"effective_position\":", file);
		Mat4 effectiveInverse;
		if (vp.cameraValid && InvertRigidCamera(effective, effectiveInverse)) TraceFloats(file, effectiveInverse.currentMatrix + 12, 3);
		else fputs("null", file);
		fputs(",\"game_position\":", file);
		Mat4 inverse;
		if (vp.cameraValid && InvertRigidCamera(vp.gameCamera, inverse)) TraceFloats(file, inverse.currentMatrix + 12, 3);
		else fputs("null", file);
		const float slopes[] = { vp.angle_left, vp.angle_right, vp.angle_bottom, vp.angle_top };
		fputs(",\"game_slopes\":", file);
		TraceFloats(file, slopes, 4);
		fputs(",\"effective_slopes\":", file);
		if (eyeScene) {
			float outputSlopes[4];
			GetEyeSlopes(vp, outputSlopes);
			TraceFloats(file, outputSlopes, 4);
		}
		else TraceFloats(file, slopes, 4);
		fprintf(file, ",\"wide_eye_world\":%s", eyeScene && HasWideEyeViewport(vp) ? "true" : "false");
		fputs("}\n", file);
	}

	if (!m_cameraTraceModelsWritten) {
		size_t modelCount = 0, eligibleModels = 0;
		for (const auto& node : m_nodes) {
			const auto& vp = node.viewport;
			if (!vp.cameraValid) continue;
			Mat4 inverse;
			if (!InvertRigidCamera(vp.gameCamera, inverse)) continue;
			eligibleModels += node.models.size();
			for (const auto& model : node.models) {
				if (modelCount >= 4096) break;
				++modelCount;
				Mat4 world = inverse;
				world.MultMatrix(model.modelMat);
				size_t vertices = 0;
				for (const auto& mesh : *model.meshes) vertices += mesh.vertexCount;
				fprintf(file,
					"{\"type\":\"model\",\"frame\":%llu,\"viewport\":%u,\"source\":%u,\"dynamic\":%s,\"mesh_count\":%llu,\"vertex_count\":%llu,\"scale\":%.9g,\"alpha\":%.9g,\"path\":[",
					(unsigned long long)m_cameraTraceFrame, vp.sourceAddress, model.sourceAddress, model.dynamic ? "true" : "false",
					(unsigned long long)model.meshes->size(), (unsigned long long)vertices, double(model.scale), double(model.alpha));
				for (size_t i = 0; i < model.diagnosticNodePath.size(); ++i) {
					if (i) fputc(',', file);
					fprintf(file, "%u", model.diagnosticNodePath[i]);
				}
				fprintf(file, "],\"rider_object_node\":%u,\"hidden_in_rider_eyes\":%s,\"root_relative\":",
					model.riderObjectNode, model.hideInRiderEyes ? "true" : "false");
				TraceFloats(file, world, 16);
				if (traceAllModels) {
					SceneInterpolation::Pose pose;
					float scale = 1;
					const bool rigid = !model.dynamic && model.meshes && !model.meshes->empty() && SceneInterpolation::ReadModelRigid(world, pose, scale);
					const bool unitRigid = rigid && SceneInterpolation::ReadRigid(world, pose);
					fprintf(file, ",\"interpolation_signature\":\"%016llx\",\"interpolation_rigid\":%s,\"interpolation_unit_rigid\":%s,\"interpolation_scale\":%.9g,\"presentation_root\":%u,\"world_viewport\":%s",
						(unsigned long long)InterpolationSignature(model), rigid ? "true" : "false", unitRigid ? "true" : "false", double(scale), model.presentationRoot,
						vp.presentationWorld ? "true" : "false");
				}
				fputs("}\n", file);
			}
		}
		if (eligibleModels) {
			m_cameraTraceModelsWritten = true;
			fprintf(file, "{\"type\":\"models_complete\",\"frame\":%llu,\"written\":%llu,\"eligible\":%llu,\"truncated\":%s}\n",
				(unsigned long long)m_cameraTraceFrame, (unsigned long long)modelCount,
				(unsigned long long)eligibleModels, eligibleModels > modelCount ? "true" : "false");
		}
	}
	TraceCameraLandmarks(file);
	if (fflush(file) != 0 || ferror(file)) {
		ErrorLog("Writing stereo camera trace failed: '%s'.", m_cameraTracePath.c_str());
		m_cameraTraceFailed = true;
	}
	if (m_cameraTraceFailed || m_cameraTraceSamples >= m_cameraTraceLimit) {
		fclose(m_cameraTraceFile);
		m_cameraTraceFile = nullptr;
	}
}

void CNew3D::UpdateEyeLayout()
{
	m_eyeLayout = DesktopEyeLayout::Calculate(int(m_totalXRes), int(m_totalYRes), m_wideScreen && !FlatIntroActive(), m_stereoMonoPreview);
	m_eyeSourceX = m_eyeLayout.world.x;
	m_eyeSourceY = m_eyeLayout.world.y;
	m_eyeSourceWidth = m_eyeLayout.world.width;
	m_eyeSourceHeight = m_eyeLayout.world.height;
}

bool CNew3D::HasWideEyeViewport(const Viewport& vp) const
{
	return !FlatIntroActive() && m_wideScreen && m_eyeLayout.HorizontalExpansion() > 1.0f && vp.cameraValid && vp.worldCandidate &&
		(!m_stereoUI || vp.presentationWorld);
}

void CNew3D::GetEyeSlopes(const Viewport& vp, float (&slopes)[4]) const
{
	slopes[0] = vp.angle_left; slopes[1] = vp.angle_right;
	slopes[2] = vp.angle_bottom; slopes[3] = vp.angle_top;
	if (m_cinemaCaptureStereo && vp.cameraValid && vp.presentationWorld) {
		const auto eye = StereoCinema::CaptureEye(m_stereoTheaterDepth, m_cinemaCaptureEye,
			m_cinemaUnits, m_cinemaAngle, slopes[0], slopes[1]);
		if (eye.stereo) { slopes[0] += eye.slopeShift; slopes[1] += eye.slopeShift; }
	}
	if (!FlatIntroActive() && m_stereoHorizonLock && vp.cameraValid && (!m_stereoUI || vp.presentationWorld)) {
		const float horizontal = std::tan(m_stereoHorizonFov * 0.00872664626f);
		const float vertical = horizontal * 384.0f / 496.0f;
		slopes[0] = -horizontal; slopes[1] = horizontal;
		slopes[2] = -vertical; slopes[3] = vertical;
	}
	if (HasWideEyeViewport(vp))
		DesktopEyeLayout::ExpandHorizontal(m_eyeLayout.HorizontalExpansion(), slopes[0], slopes[1]);
}

Viewport CNew3D::GetRenderViewport(const Viewport& original, bool interpolate)
{
	Viewport vp = original;
	if (!m_diagnosticEyeActive) {
		CalcViewport(&vp);
		return vp;
	}
	if (m_headsetEyeActive) {
		// A runtime eye is its own lens and render area. Never inherit the
		// desktop aspect, artificial eye spacing or the native camera's frustum.
		vp.x = vp.y = 0;
		vp.width = m_headsetEyeWidth;
		vp.height = m_headsetEyeHeight;
		vp.projectionMatrix = m_headsetProjection;
		if (m_xRatio > 0 && m_yRatio > 0) {
			const float xRatio = m_headsetEyeWidth / 496.0f;
			const float yRatio = m_headsetEyeHeight / 384.0f;
			vp.spotEllipse[0] = (original.spotEllipse[0] - m_xOffs) * xRatio / m_xRatio;
			vp.spotEllipse[1] = (original.spotEllipse[1] - m_yOffs) * yRatio / m_yRatio;
			vp.spotEllipse[2] *= xRatio / m_xRatio;
			vp.spotEllipse[3] *= yRatio / m_yRatio;
		}
		const Mat4 eyeTransform = GetEyeTransform(original, interpolate);
		for (int row = 0; row < 3; ++row)
			vp.lightingParams[row] = eyeTransform[row] * original.lightingParams[0] +
				eyeTransform[4 + row] * original.lightingParams[1] + eyeTransform[8 + row] * original.lightingParams[2];
		return vp;
	}

	// Native overlays remain centered at their original aspect. A validated
	// world viewport can fill a wider eye without changing its vertical frustum.
	const auto& native = m_eyeLayout.nativeUI;
	const float xRatio = native.width / 496.0f;
	const float yRatio = native.height / 384.0f;
	vp.x = native.x + int(vp.vpX * xRatio);
	vp.y = native.y + int((384.0f - vp.vpY - vp.vpHeight) * yRatio);
	vp.width = int(vp.vpWidth * xRatio);
	vp.height = int(vp.vpHeight * yRatio);
	if (HasWideEyeViewport(original)) {
		vp.x = m_eyeSourceX;
		vp.width = m_eyeSourceWidth;
	}
	float slopes[4];
	GetEyeSlopes(original, slopes);
	vp.projectionMatrix.LoadIdentity();
	vp.projectionMatrix.FrustumRZ(slopes[0], slopes[1], slopes[2], slopes[3], NEAR_PLANE);

	if (m_xRatio > 0 && m_yRatio > 0) {
		vp.spotEllipse[0] = native.x + (original.spotEllipse[0] - m_xOffs) * xRatio / m_xRatio;
		vp.spotEllipse[1] = native.y + (original.spotEllipse[1] - m_yOffs) * yRatio / m_yRatio;
		vp.spotEllipse[2] *= xRatio / m_xRatio;
		vp.spotEllipse[3] *= yRatio / m_yRatio;
	}
	// Sun and normals must be expressed in the same rotated camera space.
	const Mat4 eyeTransform = GetEyeTransform(original, interpolate);
	for (int row = 0; row < 3; ++row) {
		vp.lightingParams[row] = eyeTransform[row] * original.lightingParams[0] +
			eyeTransform[4 + row] * original.lightingParams[1] +
			eyeTransform[8 + row] * original.lightingParams[2];
	}
	return vp;
}

void CNew3D::DestroyEyeTargets()
{
	m_hudPanelValid = false;
	if (m_hudTarget.framebuffer) glDeleteFramebuffers(1, &m_hudTarget.framebuffer);
	if (m_hudTarget.texture) glDeleteTextures(1, &m_hudTarget.texture);
	m_hudTarget = EyeTarget{};
	m_preparedSceneValid = false;
	m_eyeImagesValid = false;
	for (auto& target : m_eyeTargets) {
		if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
		if (target.texture) glDeleteTextures(1, &target.texture);
		target = EyeTarget{};
	}
}

bool CNew3D::EnsureEyeTargets()
{
	if (m_eyeTargetsFailed) return false;
	if (m_eyeTargets[0].framebuffer && m_eyeTargets[1].framebuffer) return true;
	if (m_totalXRes < 2 || m_totalYRes == 0) return false;

	GLint previousTarget = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousTarget);
	for (auto& target : m_eyeTargets) {
		glGenTextures(1, &target.texture);
		glBindTexture(GL_TEXTURE_2D, target.texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_totalXRes, m_totalYRes, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glGenFramebuffers(1, &target.framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			ErrorLog("Stereo diagnostic framebuffer creation failed (0x%x); displaying center view.", status);
			DestroyEyeTargets();
			m_eyeTargetsFailed = true;
			glBindFramebuffer(GL_FRAMEBUFFER, previousTarget);
			return false;
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, previousTarget);
	UpdateEyeLayout();
	return true;
}

void CNew3D::LogDiagnosticScene(size_t centerModels)
{
	if (m_stereoRiderAnchor) {
		for (const auto& node : m_nodes) {
			const auto& vp = node.viewport;
			if (!vp.worldCandidate) continue;
			if (m_riderLastStatus != vp.riderAnchorStatus) {
				m_riderLastStatus = vp.riderAnchorStatus;
				InfoLog("Rider anchor prototype: status=%s applied=%d source=0x%x object_node=0x%x full_bike=%d character_node=0x%x native_height=11.5 up=%g back=%g game_units; upright offsets remove lean displacement; optional comfort mode filters vertical body motion.",
					vp.riderAnchorStatus, int(vp.riderAnchorApplied), vp.riderSourceModel, vp.riderObjectNode, int(vp.fullBikeApplied), vp.riderCharacterNode, m_stereoRiderUp, m_stereoRiderBack);
			}
			break;
		}
	}
	if (!m_diagnosticLogPending) return;
	size_t models = 0, meshes = 0, vertices = 0;
	for (const auto& node : m_nodes) {
		models += node.models.size();
		for (const auto& model : node.models) {
			meshes += model.meshes->size();
			for (const auto& mesh : *model.meshes) vertices += mesh.vertexCount;
		}
	}
	if (!models) return; // Delay evidence until the first actual 3D scene.
	InfoLog("Stereo diagnostic: render_serial=%llu center_models=%llu shared_eye_models=%llu meshes=%llu draw_vertices=%llu viewports=%llu separation=%g game_units yaw=%g pitch=%g frustum_bypass=%d eye_world_lod_distance=%g center_LOD=original 2D=%s convergence=infinity.",
		(unsigned long long)m_diagnosticRenderSerial, (unsigned long long)centerModels,
		(unsigned long long)models, (unsigned long long)meshes, (unsigned long long)vertices,
		(unsigned long long)m_nodes.size(), m_stereoEyeSeparation, m_stereoYaw, m_stereoPitch, int(m_stereoCullBypass), m_stereoCullBypass ? m_stereoLodDistance : 1.0f,
		m_stereoUI ? "native_layers_per_eye" : "omitted");
	InfoLog("Stereo diagnostic: %d presentation eye(s) reuse this single decoded scene and texture snapshot; center LOS is rendered first with original culling. Render serial is not an emulated frame ID.", m_stereoMonoPreview ? 1 : 2);
	if (m_stereoHorizonLock) {
		InfoLog("Horizon prototype: yaw-only game orientation, symmetric horizontal FOV=%g; HUD/map and invalid world roots omitted. Base position follows game camera unless the optional validated rider anchor applies. Optional rider comfort mode filters body Y and adds a peripheral vignette during large motion; full crash isolation remains unfinished.", m_stereoHorizonFov);
	}
	if (m_stereoUI)
		InfoLog("Stereo UI: dominant validated scene camera supplies the immersive view; other viewports keep their native camera. Prepared bottom/top tiles are drawn per eye. Cinematic vignette masks the scene before native-screen lettering and top tiles; physical panel depth remains a headset calibration task.");
	if (m_stereoStableChase)
		InfoLog("Stable chase prototype: validated Fat Boy driving layout and rider group; world-up height=%g back=%g game units; yaw follows upright chassis, native camera shake is removed, full rider retained. Physical chassis motion remains, with the existing optional vertical comfort filter.", m_stereoChaseHeight, m_stereoChaseBack);
	for (size_t i = 0; i < m_nodes.size() && i < 32; ++i) {
		const auto& vp = m_nodes[i].viewport;
		if (m_stereoHorizonLock && vp.worldCandidate) {
			InfoLog("Horizon viewport 0x%x: root_matrix=%u sibling_roots=%u status=%s.",
				vp.sourceAddress, vp.rootMatrixIndex, vp.cameraRootBranches, vp.cameraStatus);
		}
		InfoLog("Stereo diagnostic viewport[%llu]: priority=%d number=%d models=%llu native_rect=(%g,%g,%g,%g) slopes=(%g,%g,%g,%g) reverse_z_near=%g.",
			(unsigned long long)i, vp.priority, vp.number, (unsigned long long)m_nodes[i].models.size(),
			vp.vpX, vp.vpY, vp.vpWidth, vp.vpHeight,
			vp.angle_left, vp.angle_right, vp.angle_bottom, vp.angle_top, double(NEAR_PLANE));
	}
	// Immersive headset preparation skips DrawStereoDiagnostic, which normally
	// clears this one-shot flag after logging desktop eye matrices. Retire it
	// here once the scene report is complete; otherwise every native frame
	// reopens the log file for the entire report. Desktop/theater logging keeps
	// its existing eye-matrix evidence and clears the flag on the original path.
	if (m_headsetPresentation && !TheaterIntroActive()) m_diagnosticLogPending = false;
}

Mat4 CNew3D::PanelProjection(int width, int height) const
{
	// Use the same dominant world and lens as the scene behind the dashboard.
	// Standalone StereoVRHUD need not enable the horizon camera: its native
	// projection may be asymmetric or use a different FOV from that prototype.
	const Node* primary = nullptr;
	for (const auto& node : m_nodes) {
		if (node.viewport.cameraValid && node.viewport.presentationWorld && !node.models.empty() &&
			(!primary || node.models.size() > primary->models.size())) primary = &node;
	}
	if (primary) {
		float slopes[4];
		GetEyeSlopes(primary->viewport, slopes);
		if (std::all_of(std::begin(slopes), std::end(slopes), [](float value) { return std::isfinite(value); }) &&
			slopes[0] < slopes[1] && slopes[2] < slopes[3]) {
			Mat4 projection;
			projection.FrustumRZ(slopes[0], slopes[1], slopes[2], slopes[3], NEAR_PLANE);
			return projection;
		}
	}
	// No usable world camera is available; retain a bounded diagnostic lens.
	const float vertical = std::tan(m_stereoHorizonFov * 0.00872664626f) * 384.0f / 496.0f;
	const float horizontal = vertical * float(width) / std::max(1, height);
	Mat4 projection;
	projection.FrustumRZ(-horizontal, horizontal, -vertical, vertical, NEAR_PLANE);
	return projection;
}

bool CNew3D::EnsureHUDTarget()
{
	if (m_hudTarget.framebuffer) return true;
	GLint previousFramebuffer, previousTexture;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
	glGenTextures(1, &m_hudTarget.texture);
	glBindTexture(GL_TEXTURE_2D, m_hudTarget.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_totalXRes, m_totalYRes, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glGenFramebuffers(1, &m_hudTarget.framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_hudTarget.framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hudTarget.texture, 0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindTexture(GL_TEXTURE_2D, previousTexture);
	glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
	if (!complete) {
		ErrorLog("Unable to create spatial UI panel.");
		glDeleteFramebuffers(1, &m_hudTarget.framebuffer);
		glDeleteTextures(1, &m_hudTarget.texture);
		m_hudTarget = EyeTarget{};
	}
	return complete;
}

void CNew3D::PrepareHUDPanel()
{
	LiveFrameTrace::OwnerRenderScope trace("prep.hud", m_sceneFrame, m_sceneEpoch);
	if (m_hudPanelValid || !EnsureHUDTarget()) return;
	m_hudDepthCertified = (ImmersiveOverlayPanel() || m_stereoBikeHUD) && CertifyHUDDepth();
	const auto& r = m_eyeLayout.nativeUI;
	{
	LiveFrameTrace::OwnerRenderScope stage("prep.hud_clear", m_sceneFrame, m_sceneEpoch);
	glBindFramebuffer(GL_FRAMEBUFFER, m_hudTarget.framebuffer);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	}
	const bool tiles = m_tileRenderer && m_tileRenderer->HasPreparedLayers();
	const bool ownedState = tiles && m_headsetPresentation &&
		m_config["HeadsetOwnedHUDState"].ValueAsDefault<bool>(false);
	CRender2D::PreparedLayerState tileState;
	if (ownedState) {
		LiveFrameTrace::OwnerRenderScope stage("prep.hud_state", m_sceneFrame, m_sceneEpoch);
		// Capture arbitrary incoming state once, before the bottom draw. In
		// particular, preserve nondefault scissor and blend equations; they are
		// not reset by the layered 3D compositor and cannot be guessed.
		tileState = CRender2D::PreparedLayerState::Capture();
	}
	if (tiles) {
		LiveFrameTrace::OwnerRenderScope stage("prep.hud_bottom", m_sceneFrame, m_sceneEpoch);
		if (ownedState)
			m_tileRenderer->DrawPreparedLayerOwned(true, m_hudTarget.framebuffer, r.x, r.y, r.width, r.height, true, tileState);
		else m_tileRenderer->DrawPreparedLayer(true, m_hudTarget.framebuffer, r.x, r.y, r.width, r.height, true);
	}
	m_viewportPass = ViewportPass::Overlay;
	if (!m_uiOnlyScene) {
		const bool renderedPriority = ownedState && std::any_of(m_nodes.begin(), m_nodes.end(), [this](const Node& node) {
			return node.viewport.priority < 4 && !node.models.empty() && ShouldDrawViewport(node.viewport);
		});
		LiveFrameTrace::OwnerRenderScope stage("prep.hud_overlay", m_sceneFrame, m_sceneEpoch);
		DrawPreparedScene(false, m_hudTarget.framebuffer, true);
		if (ownedState) {
			// Exact DrawPreparedSceneOnce / R3DFrameBuffers::Draw postconditions.
			// Scissor enable/box and blend equations survive the whole pass.
			// Stencil survives only when no priority entered SetRenderStates.
			tileState.framebuffer = 0; tileState.drawBuffer = GL_BACK;
			tileState.program = tileState.vao = 0;
			tileState.viewport[0] = tileState.viewport[1] = 0;
			tileState.viewport[2] = m_totalXRes; tileState.viewport[3] = m_totalYRes;
			tileState.activeTexture = GL_TEXTURE0;
			tileState.texture = m_r3dFrameBuffers.ColourTexture();
			tileState.srcRGB = tileState.srcAlpha = GL_SRC_ALPHA;
			tileState.dstRGB = tileState.dstAlpha = GL_ONE_MINUS_SRC_ALPHA;
			tileState.depth = tileState.cull = tileState.blend = GL_FALSE;
			if (renderedPriority) tileState.stencil = GL_FALSE;
		}
	}
	if (tiles) {
		LiveFrameTrace::OwnerRenderScope stage("prep.hud_top", m_sceneFrame, m_sceneEpoch);
		const bool verify = ownedState &&
			!m_config["HeadsetRenderTest"].ValueAsDefault<std::string>("").empty() &&
			!m_config["HeadsetTestPerformance"].ValueAsDefault<bool>(false);
		const bool validContract = !verify || CRender2D::PreparedLayerState::Capture().Equals(tileState);
		if (!validContract) ErrorLog("Headset owned HUD state contract failed before top tile draw.");
		if (ownedState && validContract)
			m_tileRenderer->DrawPreparedLayerOwned(false, m_hudTarget.framebuffer, r.x, r.y, r.width, r.height, true, tileState);
		else m_tileRenderer->DrawPreparedLayer(false, m_hudTarget.framebuffer, r.x, r.y, r.width, r.height, true);
	}
	m_viewportPass = ViewportPass::All;
	m_hudPanelValid = true;
	if (ownedState && m_config["HeadsetHUDFlush"].ValueAsDefault<bool>(false)) {
		LiveFrameTrace::OwnerRenderScope stage("prep.hud_flush", m_sceneFrame, m_sceneEpoch);
		// Submit the assembled HUD before subsequent CPU work. This does not
		// wait for completion or replace the final OpenXR submission flush.
		glFlush();
	}
}

bool CNew3D::CertifyHUDDepth() const
{
	// Native priorities clear depth rather than comparing unrelated cameras.
	// Borrowing the final opaque snapshot is correct only when every visible
	// world model shares one priority and no high-priority polygon clears it.
	int priority = -1;
	for (const auto& node : m_nodes) {
		if (!node.viewport.presentationWorld || node.models.empty()) continue;
		if (node.viewport.priority < 0 || node.viewport.priority > 3) return false;
		if (priority >= 0 && priority != node.viewport.priority) return false;
		priority = node.viewport.priority;
		for (const auto& model : node.models) {
			if (model.hideInRiderEyes || !model.meshes) continue;
			for (const auto& mesh : *model.meshes)
				if (mesh.highPriority) return false;
		}
	}
	return priority >= 0;
}

bool CNew3D::DrawBikeHUD(int x, int y, int width, int height, const Mat4& view, const Mat4& projection,
	float unitsPerMeter, GLuint depth, const SpatialPanel::State* knownState)
{
	if (!m_stereoBikeHUD || !DrivingHUDScene() || m_bodyExploring) return false;
	const Viewport* anchor = nullptr;
	for (const auto& node : m_nodes) if (node.viewport.presentationWorld &&
		node.viewport.riderAnchorApplied && node.viewport.fullBikeApplied) { anchor = &node.viewport; break; }
	if (!anchor) return false;
	// Chassis-local instrument screens. The same native rider transform as the
	// bike supplies their position; independent head motion is applied only by
	// 'view'. Native forward is +Z, while the panel helper looks down -Z.
	Mat4 chassis = view;
	chassis.MultMatrix(anchor->riderCamera);
	chassis.MultMatrix(anchor->riderBodyWorld);
	chassis.Rotate(180.0f, 0, 1, 0);
	const auto& r = m_eyeLayout.nativeUI;
	for (const auto& instrument : BikeHUD::Instruments) {
		Mat4 panel = chassis;
		panel.Translate(instrument.x * unitsPerMeter, 0, 0);
		const float u0 = (r.x + r.width * instrument.left / 496.0f) / m_totalXRes;
		const float u1 = (r.x + r.width * instrument.right / 496.0f) / m_totalXRes;
		const float v0 = (r.y + r.height * (1.0f - instrument.bottom / 384.0f)) / m_totalYRes;
		const float v1 = (r.y + r.height * (1.0f - instrument.top / 384.0f)) / m_totalYRes;
		if (!m_spatialPanel.Draw(m_hudTarget.texture, x, y, width, height, panel, projection,
			instrument.halfWidth * unitsPerMeter, instrument.HalfHeight() * unitsPerMeter,
			instrument.distance * unitsPerMeter, instrument.y * unitsPerMeter,
			u0, v0, u1, v1, 0.0f, knownState, depth)) return false;
	}
	return true;
}

void CNew3D::DrawHUDPanel(GLuint target, const Mat4& headView)
{
	const auto& r = m_eyeLayout.nativeUI;
	const Mat4 projection = PanelProjection(m_eyeSourceWidth, m_eyeSourceHeight);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	const GLuint depth = m_hudDepthCertified ? m_r3dFrameBuffers.OpaqueDepthRenderbuffer() : 0;
	if (DrawBikeHUD(m_eyeSourceX, m_eyeSourceY, m_eyeSourceWidth, m_eyeSourceHeight,
		headView, projection, 16.0f, depth)) return;
	// Driving uses a dashboard below the forward horizon. Selection/ending
	// lettering stays on a transparent, centered screen in the forward frame.
	// Neither plane follows mouse/head rotation or acquires scene-camera motion.
	const bool dashboard = DrivingHUDScene();
	const bool sideBoard = !dashboard && !depth;
	Mat4 panelView = headView;
	if (sideBoard) panelView.Translate(-18.4f, 0, 0);
	const float halfWidth = dashboard ? 16.0f : (sideBoard ? 10.4f : TheaterHalfWidth());
	if (!m_spatialPanel.Draw(m_hudTarget.texture, m_eyeSourceX, m_eyeSourceY, m_eyeSourceWidth, m_eyeSourceHeight,
		panelView, projection, halfWidth, halfWidth * 384.0f / 496.0f, sideBoard ? 20.0f : 40.0f, dashboard ? -14.0f : 0.0f,
		float(r.x) / m_totalXRes, float(r.y) / m_totalYRes,
		float(r.x + r.width) / m_totalXRes, float(r.y + r.height) / m_totalYRes,
		dashboard ? 0.08f : 0.0f, nullptr, dashboard ? 0 : depth))
	{
		ErrorLog("Spatial UI panel draw failed.");
		// A native full-screen fallback would attach text to the viewer's eyes.
		if (GeneralTheaterEnabled()) return;
		if (m_tileRenderer && m_tileRenderer->HasPreparedLayers())
			m_tileRenderer->DrawPreparedLayer(true, target, r.x, r.y, r.width, r.height);
		m_viewportPass = ViewportPass::Overlay;
		DrawPreparedScene(false, target);
		if (m_tileRenderer && m_tileRenderer->HasPreparedLayers())
			m_tileRenderer->DrawPreparedLayer(false, target, r.x, r.y, r.width, r.height);
	}
}

void CNew3D::DrawStereoDiagnostic(const Mat4* headView, bool composite)
{
	UpdateEyeLayout();
	m_panelHeadView.LoadIdentity();
	if (headView) m_panelHeadView = *headView;
	else {
		m_panelHeadView.Rotate(-m_stereoPitch, 1.0f, 0.0f, 0.0f);
		m_panelHeadView.Rotate(-m_stereoYaw, 0.0f, 1.0f, 0.0f);
	}
	const auto& nativeUI = m_eyeLayout.nativeUI;
	m_stereoCinematicVignette = m_config["StereoCinematicVignette"].ValueAsDefault<bool>(false);
	const float cinematicStrength = CinematicVignetteStrength();
	const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	m_diagnosticEyeActive = true;
	bool hasModels = false;
	for (const auto& node : m_nodes) hasModels |= !node.models.empty();
	const bool tiles = m_stereoUI && m_tileRenderer && m_tileRenderer->HasPreparedLayers();
	const bool hideHUD = m_bodyExploring || (DrivingHUDScene() && m_stereoHideHUD);
	const bool panelRequested = !hideHUD && (ImmersiveOverlayPanel() ||
		(DrivingHUDScene() && (m_stereoVRHUD || GeneralTheaterEnabled())));
	if (panelRequested) PrepareHUDPanel();
	const bool spatialHUD = panelRequested && m_hudPanelValid;
	const bool separateHUD = hideHUD || spatialHUD || (GeneralTheaterEnabled() && panelRequested);
	const bool splitUI = m_stereoUI && (cinematicStrength > 0.0f || separateHUD);
	bool hasScreenModels = false;
	for (const auto& node : m_nodes)
		if (!node.viewport.presentationWorld && !node.models.empty()) hasScreenModels = true;
	// The desktop mono compositor samples only eye0, except an optional stereo
	// movie must also prepare eye1 for the headset. Both movie images are made
	// from this one native scene/texture snapshot and reused at headset cadence.
	const bool stereoTheater = StereoTheaterActive();
	const int eyeCount = stereoTheater ? 2 : (m_stereoMonoPreview || TheaterIntroActive() ? 1 : 2);
	m_cinemaCaptureStereo = stereoTheater;
	for (int eye = 0; eye < eyeCount; ++eye) {
		m_cinemaCaptureEye = eye;
		const auto eyeProfile = m_renderProfiler.Measure(eye == 0 ? RenderProfiler::Stage::LeftEye : RenderProfiler::Stage::RightEye);
		const float eyePosition = FlatIntroActive() ? 0.0f : (eye == 0 ? -0.5f : 0.5f) * m_stereoEyeSeparation;
		// Invert a head pose of yaw about +Y then pitch about +X. Parallel eyes
		// translate along the resulting head-local X axis; no toe-in or image shift.
		m_eyeView.LoadIdentity();
		m_eyeView.Translate(-eyePosition, 0.0f, 0.0f);
		if (!FlatIntroActive() && headView) m_eyeView.MultMatrix(*headView);
		else if (!FlatIntroActive()) {
			m_eyeView.Rotate(-m_stereoPitch, 1.0f, 0.0f, 0.0f);
			m_eyeView.Rotate(-m_stereoYaw, 0.0f, 1.0f, 0.0f);
		}
		if (m_diagnosticLogPending && hasModels) {
			const float* m = m_eyeView;
			InfoLog("Stereo diagnostic %s eye: position_x=%g view_rows=[%g,%g,%g,%g][%g,%g,%g,%g][%g,%g,%g,%g][%g,%g,%g,%g].",
				eye == 0 ? "left" : "right", eyePosition,
				m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14],m[3],m[7],m[11],m[15]);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, m_eyeTargets[eye].framebuffer);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (tiles && !separateHUD) m_tileRenderer->DrawPreparedLayer(true, m_eyeTargets[eye].framebuffer,
			nativeUI.x, nativeUI.y, nativeUI.width, nativeUI.height);
		m_viewportPass = splitUI ? ViewportPass::World : ViewportPass::All;
		if (!m_uiOnlyScene) DrawPreparedScene(false, m_eyeTargets[eye].framebuffer);
		if (splitUI) {
			// Mask only the moving scene. Native-camera 3D lettering and tile
			// instructions are drawn afterward so menus remain readable.
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_eyeTargets[eye].framebuffer);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			DrawComfortVignette(m_eyeSourceX, m_eyeSourceY, m_eyeSourceWidth, m_eyeSourceHeight, true, cinematicStrength);
			if (!m_uiOnlyScene && hasScreenModels && !separateHUD) {
				m_viewportPass = ViewportPass::Overlay;
				DrawPreparedScene(false, m_eyeTargets[eye].framebuffer);
			}
		}
		if (tiles && !separateHUD) m_tileRenderer->DrawPreparedLayer(false, m_eyeTargets[eye].framebuffer,
			nativeUI.x, nativeUI.y, nativeUI.width, nativeUI.height);
		if (spatialHUD) DrawHUDPanel(m_eyeTargets[eye].framebuffer, m_eyeView);
		if (TheaterIntroActive()) {
			// The cinema image is an opaque completed picture. Legacy blending
			// may leave fractional alpha even on its opaque background.
			GLboolean mask[4];
			glGetBooleanv(GL_COLOR_WRITEMASK, mask);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_eyeTargets[eye].framebuffer);
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			glColorMask(mask[0], mask[1], mask[2], mask[3]);
		}
	}
	m_viewportPass = ViewportPass::All;
	m_cinemaCaptureStereo = false;
	m_diagnosticEyeActive = false;
	m_eyeImagesValid = tiles || (TheaterIntroActive() && hasModels);
	for (const auto& node : m_nodes)
		if (node.viewport.cameraValid && !node.models.empty()) m_eyeImagesValid = true;
	m_eyeImageFrame = m_sceneFrame;
	m_eyeImageEpoch = m_sceneEpoch;
	m_eyeImagesTheater = TheaterIntroActive();
	m_eyeImagesStereoTheater = stereoTheater;
	if (composite) CompositeStereoEyes();
	if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
	if (hasModels) m_diagnosticLogPending = false;
}

bool CNew3D::CanReuseStereoEyes() const
{
	// A brief desktop transition fallback only, never a head-tracked VR path.
	return m_eyeImagesValid && m_eyeImagesTheater == TheaterIntroActive() && m_eyeImageEpoch == m_sceneEpoch &&
		m_sceneFrame >= m_eyeImageFrame && m_sceneFrame - m_eyeImageFrame <= 12;
}

void CNew3D::CompositeStereoEyes()
{
	m_stereoCinematicVignette = m_config["StereoCinematicVignette"].ValueAsDefault<bool>(false);
	const float cinematicStrength = CinematicVignetteStrength();
	const auto compositeProfile = m_renderProfiler.Measure(RenderProfiler::Stage::Composite);
	const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	// Preserve the prepared world's aspect, adding bars only when necessary.
	// Wide-screen eyes expand scenery; their native UI remains centered within.
	// Mono preview displays eye 0 across the desktop; stereo uses equal halves.
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_aaTarget);
	glDrawBuffer(m_aaTarget ? GL_COLOR_ATTACHMENT0 : GL_BACK);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	const int halfWidth = int(m_totalXRes) / 2;
	if (m_bodyBlackout) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glReadBuffer(GL_BACK);
		if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
		return;
	}
	const bool monoPreview = m_stereoMonoPreview;
	const int displayWidth = monoPreview ? int(m_totalXRes) : halfWidth;
	if (TheaterIntroActive()) {
		// A narrow desktop eye must still show the whole screen at center.
		// This is the virtual room's lens, independent of the native movie lens.
		const float aspect = float(displayWidth) / m_totalYRes;
		// Keep the room lens fixed as size changes. Otherwise enlarging the
		// projection to fit each new size would cancel the user's adjustment.
		const float vertical = std::max(std::tan(m_stereoHorizonFov * 0.00872664626f) * 384.0f / 496.0f,
			std::max(28.0f * 384.0f / 496.0f / 40.0f / 0.86f, 28.0f / 40.0f / 0.86f / aspect));
		Mat4 projection;
		projection.FrustumRZ(-vertical * aspect, vertical * aspect, -vertical, vertical, NEAR_PLANE);
		bool drawn = true;
		for (int eye = 0; eye < (monoPreview ? 1 : 2); ++eye) {
			Mat4 view;
			view.Translate((eye == 0 ? 0.5f : -0.5f) * m_stereoEyeSeparation, 0, 0);
			view.MultMatrix(m_panelHeadView);
			if (!m_spatialPanel.Draw(m_eyeTargets[m_eyeImagesStereoTheater ? eye : 0].texture, eye * displayWidth, 0, displayWidth, int(m_totalYRes),
				view, projection, TheaterHalfWidth(), TheaterHalfWidth() * 384.0f / 496.0f, 40.0f, 0,
				float(m_eyeSourceX) / m_totalXRes, float(m_eyeSourceY) / m_totalYRes,
				float(m_eyeSourceX + m_eyeSourceWidth) / m_totalXRes, float(m_eyeSourceY + m_eyeSourceHeight) / m_totalYRes, 1.0f))
				drawn = false;
		}
		if (drawn) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glReadBuffer(GL_BACK);
			if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
			return;
		}
		ErrorLog("Theater screen draw failed; retaining black surroundings.");
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glReadBuffer(GL_BACK);
		if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
		return;
	}
	const float fit = std::min(float(displayWidth) / m_eyeSourceWidth, float(m_totalYRes) / m_eyeSourceHeight);
	const int width = std::max(1, int(m_eyeSourceWidth * fit));
	const int height = std::max(1, int(m_eyeSourceHeight * fit));
	const int y = (int(m_totalYRes) - height) / 2;
	for (int eye = 0; eye < (monoPreview ? 1 : 2); ++eye) {
		const int x = eye * displayWidth + (displayWidth - width) / 2;
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_eyeTargets[TheaterIntroActive() ? 0 : eye].framebuffer);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glBlitFramebuffer(m_eyeSourceX, m_eyeSourceY, m_eyeSourceX + m_eyeSourceWidth, m_eyeSourceY + m_eyeSourceHeight,
			x, y, x + width, y + height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		DrawComfortVignette(x, y, width, height);
		if (cinematicStrength > 0.0f && !m_stereoUI) DrawComfortVignette(x, y, width, height, true, cinematicStrength);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glReadBuffer(GL_BACK);
	if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
}

void CNew3D::RenderFrame(void)
{
	// Model storage can be reused during this native update. Never carry pose
	// overrides across decoding, including an unchanged-clock scene rebuild.
	m_bodyArmModels = {};
	m_sceneInterpolationActive = false;
	LiveFrameTrace::OwnerRenderScope renderTrace("prep.render3d", m_sceneFrame, m_sceneEpoch);
	if (renderTrace.Active()) renderTrace.c = m_headsetPresentation ? 1 : 0;
	m_hudPanelValid = false;
	if (m_romCacheResetPending) {
		m_sceneModelBuffers.ClearScene(m_nodes);
		m_romMap.clear();
		m_polyBufferRom.clear();
		m_vbo.Reset();
		m_romCacheResetPending = false;
	}
	// No early-return path may accidentally publish the previous decoded scene
	// as a fresh head-tracked scene (blank/flash, empty root, failed upload, etc.).
	m_preparedSceneValid = false;
	m_uiOnlyScene = false;
	m_hasImmersiveWorld = false;
	const auto frameProfile = m_renderProfiler.Frame(m_config["FrameProfileFrameId"].ValueAsDefault<unsigned>(0),
		m_sceneFrame, m_sceneEpoch, m_diagnosticRenderSerial + 1);
	UpdateDiagnosticSettings();
	++m_diagnosticRenderSerial;
	m_diagnosticCenterFallback = m_diagnosticCachedPair = false;
	{
		std::lock_guard<std::mutex> guard(m_losMutex);
		std::swap(m_losBack, m_losFront);
		for (int i = 0; i < 4; i++) m_losBack->value[i] = 0;
	}
	if (m_blockCulling && !m_noWhiteFlash) {
		// A tile-only native frame has no geometry decode to overlap. Its current
		// surfaces must be ready before the blank/theater eligibility query.
		if (m_tileRenderer) m_tileRenderer->CompletePreparedLayers();
		if (m_stereoDiagnostic && m_stereoUI && m_tileRenderer &&
			m_tileRenderer->HasPreparedLayers() && EnsureEyeTargets()) {
			// Real3D is blank but the tile generator still owns valid menu/text
			// surfaces. Publish only those, never stale world geometry.
			m_sceneModelBuffers.ClearScene(m_nodes);
			m_uiOnlyScene = true;
			UpdateMenuComfort();
			if (m_stereoRiderComfort)
				m_comfortResult = m_riderComfort.Update(m_sceneFrame, m_sceneEpoch, false, {}, {}, m_gamePresentationState.motion);
			const bool useHeadView = m_presentationPoseSet &&
				(!m_presentationPoseIdentity || m_stereoYaw != 0.0f || m_stereoPitch != 0.0f);
			if (m_headsetPresentation) PrepareHeadsetScene();
			else {
				DrawStereoDiagnostic(useHeadView ? &m_presentationHeadView : nullptr);
				m_preparedSceneValid = m_eyeImagesValid;
			}
			m_preparedSceneFrame = m_sceneFrame;
			m_preparedSceneEpoch = m_sceneEpoch;
			if (m_sceneInterpolationEnabled) PrepareSceneInterpolation(SceneInterpolationNow());
			TraceCameraScene();
			return;
		}
		if (m_stereoRiderComfort) {
			m_comfortResult = m_riderComfort.Update(m_sceneFrame, m_sceneEpoch, false, {}, {}, m_gamePresentationState.motion);
			if (!GeneralTheaterEnabled() && CanReuseStereoEyes()) {
				CompositeStereoEyes();
				return;
			}
		}
		if (GeneralTheaterEnabled()) {
			m_eyeImagesValid = false;
			ClearPresentationTarget();
			return;
		}
		// There is no new scene on a hardware blank/flash request. Without a
		// recent pair, use neutral gray in comfort mode rather than black/white.
		if (m_aaTarget) glBindFramebuffer(GL_FRAMEBUFFER, m_aaTarget);
		const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
		if (m_stereoRiderComfort) glDisable(GL_SCISSOR_TEST);
		const float value = m_stereoRiderComfort ? 0.35f : 1.0f;
		glClearColor(value, value, value, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (scissor && m_stereoRiderComfort) glEnable(GL_SCISSOR_TEST);
		if (m_aaTarget) glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return;
	}

	// The normal camera pass is also the authoritative hardware LOS calculation.
	// A diagnostic culling bypass must never substitute an eye image for it.
	Vertex prevBefore[4]{};
	UINT16 coordsBefore[4][2]{};
	const UINT32 colorTableBefore = m_colorTableAddr;
	if (m_stereoDiagnostic && m_stereoCullBypass) {
		memcpy(prevBefore, m_prev, sizeof(m_prev));
		memcpy(coordsBefore, m_prevTexCoords, sizeof(m_prevTexCoords));
	}
	m_bypassFrustum = false;
	{
		LiveFrameTrace::OwnerRenderScope trace("prep.center_decode", m_sceneFrame, m_sceneEpoch);
		const auto profile = m_renderProfiler.Measure(RenderProfiler::Stage::CenterDecode);
		StressROMCache(false);
		BuildScene();
	}
	{
		LiveFrameTrace::OwnerRenderScope trace("prep.center_upload", m_sceneFrame, m_sceneEpoch);
		const auto profile = m_renderProfiler.Measure(RenderProfiler::Stage::CenterUpload);
		if (!UploadScene()) {
			if (GeneralTheaterEnabled()) ClearPresentationTarget();
			return;
		}
	}
	if (m_recordModelPaths) TraceDrawStateDuplicates(false);
	{
		LiveFrameTrace::OwnerRenderScope trace("prep.center_draw", m_sceneFrame, m_sceneEpoch);
		const auto profile = m_renderProfiler.Measure(RenderProfiler::Stage::CenterDrawLos);
		// BuildScene above has classified this native snapshot and its world
		// viewports. Immersive headset eyes/HUD redraw their own targets, so the
		// center image is unused when hardware requests no LOS sample. Keep the
		// authoritative decode/upload and the normal zeroed LOS buffers intact;
		// any requested LOS, theater scene, or desktop presentation retains the
		// complete original center draw and feedback path.
		const bool unusedHeadsetCenter = m_headsetPresentation && GeneralTheaterEnabled() &&
			!TheaterIntroActive() && std::none_of(m_nodes.begin(), m_nodes.end(), [](const Node& node) {
				return node.viewport.losPosX != 0 || node.viewport.losPosY != 0;
			});
		if (trace.Active()) trace.c = unusedHeadsetCenter ? 0 : 1;
		if (!unusedHeadsetCenter) DrawPreparedScene(true, m_aaTarget);
	}
	if (!m_stereoDiagnostic || !EnsureEyeTargets()) {
		if (GeneralTheaterEnabled()) ClearPresentationTarget();
		m_geometryUploadFailed = false;
		TraceCameraScene();
		return;
	}

	size_t centerModels = 0;
	for (const auto& node : m_nodes) centerModels += node.models.size();
	if (m_stereoCullBypass && !FlatIntroActive()) {
		Vertex prevAfter[4];
		UINT16 coordsAfter[4][2];
		const UINT32 colorTableAfter = m_colorTableAddr;
		memcpy(prevAfter, m_prev, sizeof(m_prev));
		memcpy(coordsAfter, m_prevTexCoords, sizeof(m_prevTexCoords));
		memcpy(m_prev, prevBefore, sizeof(m_prev));
		memcpy(m_prevTexCoords, coordsBefore, sizeof(m_prevTexCoords));
		m_colorTableAddr = colorTableBefore;
		m_bypassFrustum = true;
		{
			LiveFrameTrace::OwnerRenderScope trace("prep.bypass_decode", m_sceneFrame, m_sceneEpoch);
			const auto profile = m_renderProfiler.Measure(RenderProfiler::Stage::BypassDecode);
			StressROMCache(true);
			BuildScene();
		}
		m_bypassFrustum = false;
		memcpy(m_prev, prevAfter, sizeof(m_prev));
		memcpy(m_prevTexCoords, coordsAfter, sizeof(m_prevTexCoords));
		m_colorTableAddr = colorTableAfter;
		{
			LiveFrameTrace::OwnerRenderScope trace("prep.bypass_upload", m_sceneFrame, m_sceneEpoch);
			const auto profile = m_renderProfiler.Measure(RenderProfiler::Stage::BypassUpload);
			if (!UploadScene()) {
				if (GeneralTheaterEnabled()) ClearPresentationTarget();
				return;
			}
		}
		if (m_recordModelPaths) TraceDrawStateDuplicates(true);
	}
	// Clear the error latch only after every upload required for this frame
	// succeeded, so a repeat bypass failure cannot flood the console.
	m_geometryUploadFailed = false;
	ApplyRiderComfort();
	if (m_stereoRiderComfort && !m_stereoUI) {
		bool drawableWorld = false;
		for (const auto& node : m_nodes)
			if (node.viewport.cameraValid && !node.models.empty()) drawableWorld = true;
		if (!drawableWorld) {
			// Do not overwrite useful eye textures with an empty omitted scene.
			// After a short transition, retain the native center/menu render.
			m_diagnosticCachedPair = CanReuseStereoEyes();
			m_diagnosticCenterFallback = !m_diagnosticCachedPair;
			if (m_diagnosticCachedPair) CompositeStereoEyes();
			TraceCameraScene();
			return;
		}
	}
	UpdateEyeLayout(); // The native sample can switch between flat and immersive.
	TraceCameraScene();
	LogDiagnosticScene(centerModels);
	// Preserve the original path when no live pose is supplied or when the
	// requested pose and configured diagnostic angles are all identity. A live
	// pose otherwise replaces the static diagnostic angles; it is not added to them.
	const bool useHeadView = m_presentationPoseSet &&
		(!m_presentationPoseIdentity || m_stereoYaw != 0.0f || m_stereoPitch != 0.0f);
	if (m_headsetPresentation) PrepareHeadsetScene();
	else {
		DrawStereoDiagnostic(useHeadView ? &m_presentationHeadView : nullptr);
		m_preparedSceneValid = m_eyeImagesValid;
	}
	m_preparedSceneFrame = m_sceneFrame;
	m_preparedSceneEpoch = m_sceneEpoch;
	if (m_sceneInterpolationEnabled) PrepareSceneInterpolation(SceneInterpolationNow());
}

void CNew3D::SetSceneClock(uint64_t frame, uint64_t epoch)
{
	if (epoch != m_sceneEpoch || frame < m_sceneFrame) ResetSceneInterpolation();
	if (epoch != m_sceneEpoch || frame < m_sceneFrame) ResetMenuComfort();
	if (frame != m_sceneFrame || epoch != m_sceneEpoch) m_preparedSceneValid = false;
	if (epoch != m_sceneEpoch) m_eyeImagesValid = false;
	m_sceneFrame = frame;
	m_sceneEpoch = epoch;
}

void CNew3D::SetGamePresentationState(const GamePresentationState& state)
{
	if (HarleyPresentation::Classify(state) != HarleyPresentation::Classify(m_gamePresentationState)) {
		m_eyeImagesValid = false;
		m_preparedSceneValid = false;
		m_hudPanelValid = false;
	}
	m_gamePresentationState = state;
}

void CNew3D::InvalidatePresentation()
{
	m_bodyExploring=false; m_bodyPosition={}; m_bodyYaw=0; m_bodyFloor.clear();
	m_bodyArmModels={}; m_bodyHands={}; m_bodyHandAligned[0]=m_bodyHandAligned[1]=false;
	m_bodyGrip.Reset(); m_bodyNearGrip[0]=m_bodyNearGrip[1]=false;
	m_bodyGripRider=m_bodyGripCharacter=0; m_bodyVignette=0; m_bodyBlackout=false;
	ResetSceneInterpolation();
	m_hudPanelValid = false;
	ResetMenuComfort();
	m_preparedSceneValid = false;
	m_eyeImagesValid = false;
}

bool CNew3D::PresentationSettingsMatch() const
{
	// Do not call UpdateDiagnosticSettings here: that updates trace bookkeeping
	// and can reset comfort history. A changed configuration needs a native frame.
	auto value = [this](const char* key, float fallback, float limit) {
		const float number = m_config[key].ValueAsDefault<float>(fallback);
		return std::isfinite(number) ? std::clamp(number, -limit, limit) : fallback;
	};
	return m_config["HeadsetSceneInterpolation"].ValueAsDefault<bool>(false) == m_sceneInterpolationEnabled &&
		m_config["StereoDiagnostic"].ValueAsDefault<bool>(false) == m_stereoDiagnostic &&
		m_config["StereoUI"].ValueAsDefault<bool>(false) == m_stereoUI &&
		m_config["StereoMonoPreview"].ValueAsDefault<bool>(false) == m_stereoMonoPreview &&
		m_config["StereoMenuComfort"].ValueAsDefault<bool>(false) == m_stereoMenuComfort &&
		m_config["StereoFlatIntro"].ValueAsDefault<bool>(false) == m_stereoFlatIntro &&
		(m_config["StereoTheaterIntro"].ValueAsDefault<bool>(false) ||
		 m_config["StereoTheater"].ValueAsDefault<bool>(false)) == m_stereoTheaterIntro &&
		m_stereoTheaterDepth == 0 &&
		StereoCinema::Units(m_config["OpenXRUnitsPerMeter"].ValueAsDefault<float>(16.0f)) == m_cinemaUnits &&
		(m_stereoTheaterDepth == 0 || TheaterScreenAngle() == m_cinemaAngle) &&
		m_config["StereoVRHUD"].ValueAsDefault<bool>(false) == m_stereoVRHUD &&
		m_config["StereoBikeHUD"].ValueAsDefault<bool>(false) == m_stereoBikeHUD &&
		m_config["StereoHideHUD"].ValueAsDefault<bool>(false) == m_stereoHideHUD &&
		m_config["StereoCullBypass"].ValueAsDefault<bool>(false) == m_stereoCullBypass &&
		std::clamp(value("StereoLodDistance", 1.0f, 4.0f), 1.0f, 4.0f) == m_stereoLodDistance &&
		m_config["StereoHorizonLock"].ValueAsDefault<bool>(false) == m_stereoHorizonLock &&
		m_config["StereoRiderAnchor"].ValueAsDefault<bool>(false) == m_stereoRiderAnchor &&
		m_config["StereoFullBike"].ValueAsDefault<bool>(false) == m_stereoFullBike &&
		m_config["StereoStableChase"].ValueAsDefault<bool>(false) == m_stereoStableChase &&
		std::clamp(value("StereoChaseHeight", 20.0f, 60.0f), 0.0f, 60.0f) == m_stereoChaseHeight &&
		std::clamp(value("StereoChaseBack", 40.0f, 100.0f), 10.0f, 100.0f) == m_stereoChaseBack &&
		m_config["StereoRiderComfort"].ValueAsDefault<bool>(false) == m_stereoRiderComfort &&
		value("StereoEyeSeparation", 1.0f, 1000.0f) == m_stereoEyeSeparation &&
		value("StereoYaw", 0.0f, 180.0f) == m_stereoYaw &&
		value("StereoPitch", 0.0f, 89.0f) == m_stereoPitch &&
		std::clamp(value("StereoHorizonFov", 90.0f, 140.0f), 30.0f, 140.0f) == m_stereoHorizonFov &&
		std::clamp(value("StereoRiderUp", 0.0f, 30.0f), -10.0f, 30.0f) == m_stereoRiderUp &&
		std::clamp(value("StereoRiderBack", 0.0f, 30.0f), -20.0f, 30.0f) == m_stereoRiderBack &&
		std::clamp(value("StereoVignetteScale", 1.0f, 1.0f), 0.0f, 1.0f) == m_stereoVignetteScale &&
		m_config["WideScreen"].ValueAsDefault<bool>(false) == m_wideScreen &&
		m_config["NoWhiteFlash"].ValueAsDefault<bool>(false) == m_noWhiteFlash &&
		m_config["QuadRendering"].ValueAsDefault<bool>(false) == (m_numPolyVerts == 4);
}

bool CNew3D::CanPresentPreparedScene() const
{
	// SyncSnapshots may already have published the next native frame's block
	// culling flag. Only RenderFrame may apply that policy to the prepared scene;
	// the existing snapshot remains usable until its clock or resources change.
	return m_preparedSceneValid && m_stereoDiagnostic &&
		m_preparedSceneFrame == m_sceneFrame && m_preparedSceneEpoch == m_sceneEpoch &&
		m_eyeTargets[0].framebuffer && m_eyeTargets[1].framebuffer &&
		PresentationSettingsMatch();
}

bool CNew3D::GetPresentationState(PresentationState& state) const
{
	if (!CanPresentPreparedScene()) return false;
	state.sceneFrame = m_preparedSceneFrame;
	state.sceneEpoch = m_preparedSceneEpoch;
	state.verticalOffset = m_comfortResult.verticalOffset;
	state.vignetteStrength = m_comfortResult.vignetteStrength;
	state.menuVignetteStrength = m_menuComfort.State().strength;
	state.menuComfortTicks = m_menuComfortTicks;
	return true;
}

const Viewport* CNew3D::BodyAnchor() const
{
	if (!m_bodyOptIn || !CanPresentPreparedScene() || !DrivingHUDScene()) return nullptr;
	const Viewport* fallback = nullptr;
	for (const auto& node:m_nodes) if (node.viewport.riderAnchorApplied && node.viewport.fullBikeApplied &&
		node.viewport.presentationWorld && node.viewport.riderCharacterNode) {
		if (node.viewport.riderArmModels == 6) return &node.viewport;
		if (!fallback) fallback = &node.viewport;
	}
	return fallback;
}

bool CNew3D::GetBodyStatus(BodyStatus& state) const
{
	state = {};
	if (!m_bodyOptIn) return false;
	state.sceneFrame=m_preparedSceneFrame; state.sceneEpoch=m_preparedSceneEpoch;
	const auto* anchor=BodyAnchor();
	state.eligible=anchor!=nullptr;
	state.armsReady=anchor && (anchor->riderSourceModel==0x96FD45 || anchor->riderSourceModel==0x97439D);
	state.exploring=m_bodyExploring;
	for (int i=0;i<3;++i) state.positionMeters[i]=m_bodyPosition[i]/m_bodyUnits;
	state.yawDegrees=m_bodyYaw;
	state.walkingVignette=m_bodyVignette;
	for (unsigned hand=0;hand<2;++hand) {
		state.handLatched[hand]=m_bodyGrip.Latched(hand);
		state.handNearGrip[hand]=m_bodyNearGrip[hand];
		state.gripPositionMeters[hand][0]=float(m_bodyGripPositions[hand].x/m_bodyUnits);
		state.gripPositionMeters[hand][1]=float(m_bodyGripPositions[hand].y/m_bodyUnits);
		state.gripPositionMeters[hand][2]=float(m_bodyGripPositions[hand].z/m_bodyUnits);
	}
	for (const auto& arm:m_bodyArmModels) state.trackedArmParts+=arm.source!=nullptr;
	for (unsigned hand=0;hand<2;++hand) if (m_bodyArmModels[hand*3].source) {
		state.wristPositionMeters[hand][0]=float(m_bodyWrists[hand].x/m_bodyUnits);
		state.wristPositionMeters[hand][1]=float(m_bodyWrists[hand].y/m_bodyUnits);
		state.wristPositionMeters[hand][2]=float(m_bodyWrists[hand].z/m_bodyUnits);
		state.wristErrorMeters[hand]=m_bodyWristError[hand];
		state.wristClamped[hand]=m_bodyWristClamped[hand];
	}
	return true;
}

bool CNew3D::IsBodyArm(const Viewport& viewport, const Model& model) const
{
	if (!m_bodyOptIn || !viewport.riderCharacterNode || model.riderObjectNode!=viewport.riderCharacterNode) return false;
	const auto* profile=HarleyRiders::Find(viewport.riderSourceModel,true);
	return profile && HarleyRiders::ArmBit(*profile,model.sourceAddress)!=0;
}

Mat4 CNew3D::BodyNavigationView() const
{
	PresentationPose pose;
	std::copy(m_bodyPosition.begin(),m_bodyPosition.end(),pose.position);
	const float radians=m_bodyYaw*0.00872664625997f;
	pose.orientation[1]=std::sin(radians); pose.orientation[3]=std::cos(radians);
	Mat4 view; MakePresentationView(pose,view); return view;
}

bool CNew3D::BuildInspectionFloor()
{
	m_bodyFloor.clear();
	const auto* anchor=BodyAnchor(); if (!anchor) return false;
	if (m_numPolyVerts!=3 && m_numPolyVerts!=4) return false;
	const float range=6*m_bodyUnits;
	size_t visits=0;
	for (const auto& node:m_nodes) {
		if (!node.viewport.presentationWorld || !node.viewport.riderAnchorApplied) continue;
		for (const auto& model:node.models) {
			if (model.dynamic || !model.meshes || model.alpha!=1 ||
				model.riderObjectNode==anchor->riderObjectNode || model.riderObjectNode==anchor->riderCharacterNode) continue;
			Mat4 matrix=node.viewport.riderCorrection; matrix.MultMatrix(model.modelMat);
			for (const auto& mesh:*model.meshes) {
				if (mesh.polyAlpha || mesh.textureAlpha || mesh.alphaTest || mesh.highPriority) continue;
				const int begin=mesh.vboOffset;
				if (begin<0 || mesh.vertexCount<0 || size_t(begin)>m_polyBufferRom.size() ||
					size_t(mesh.vertexCount)>m_polyBufferRom.size()-size_t(begin) || mesh.vertexCount%m_numPolyVerts) return false;
				for (int v=0;v<mesh.vertexCount;v+=m_numPolyVerts) {
					if (++visits>500000) { m_bodyFloor.clear(); return false; }
					std::array<InspectionGeometry::Point,4> p{};
					for (int j=0;j<m_numPolyVerts;++j) {
						const float* q=m_polyBufferRom[size_t(begin+v+j)].base.pos;
						for (int k=0;k<3;++k) p[j][k]=matrix[k]*q[0]+matrix[k+4]*q[1]+matrix[k+8]*q[2]+matrix[k+12];
						if (!InspectionGeometry::Finite(p[j])) return false;
					}
					float minX=p[0][0],maxX=minX,minZ=p[0][2],maxZ=minZ;
					for (int j=1;j<m_numPolyVerts;++j) { minX=std::min(minX,p[j][0]); maxX=std::max(maxX,p[j][0]); minZ=std::min(minZ,p[j][2]); maxZ=std::max(maxZ,p[j][2]); }
					if (minX>range||maxX< -range||minZ>range||maxZ< -range) continue;
					if (m_bodyFloor.size()+2>65536) { m_bodyFloor.clear(); return false; }
					m_bodyFloor.push_back({p[0],p[1],p[2]});
					if (m_numPolyVerts==4) m_bodyFloor.push_back({p[0],p[2],p[3]});
				}
			}
		}
	}
	return InspectionGeometry::Ground(m_bodyFloor,0,0,-3*m_bodyUnits,-.25f*m_bodyUnits,m_bodyGround);
}

bool CNew3D::SetBodyMode(bool arms, bool exploring, float unitsPerMeter)
{
	if (!m_bodyOptIn || !std::isfinite(unitsPerMeter)||unitsPerMeter<1||unitsPerMeter>100) return false;
	if (arms!=m_bodyArms || unitsPerMeter!=m_bodyUnits) {
		m_bodyHandAligned[0]=m_bodyHandAligned[1]=false; m_bodyArmModels={}; m_bodyGrip.Reset();
	}
	m_bodyArms=arms; m_bodyUnits=unitsPerMeter;
	if (!exploring) {
		if (m_bodyExploring) m_bodyGrip.Reset();
		m_bodyExploring=false; m_bodyPosition={}; m_bodyYaw=0; m_bodyFloor.clear(); return true;
	}
	if (m_bodyExploring) return m_bodyFrame==m_preparedSceneFrame&&m_bodyEpoch==m_preparedSceneEpoch;
	if (!BuildInspectionFloor()) return false;
	// Entry is stationary. Smooth stick movement then moves away from
	// the bike; the native seated eye height is retained to avoid an unsolicited
	// standing-height jump. World elevation subsequently follows known ground.
	m_bodyPosition={}; m_bodyYaw=0; m_bodyFrame=m_preparedSceneFrame; m_bodyEpoch=m_preparedSceneEpoch;
	m_bodyExploring=true; m_sceneInterpolationActive=false; m_bodyGrip.Reset();
	return true;
}

void CNew3D::SetBodyVignette(float strength)
{
	m_bodyVignette=m_bodyOptIn && std::isfinite(strength) ? std::clamp(strength,0.0f,1.0f) : 0.0f;
}

void CNew3D::SetBodyGripButtons(bool left, bool right, bool active)
{
	m_bodyGrip.Buttons(left,right,m_bodyOptIn && m_bodyArms && !m_bodyExploring && active);
}

bool CNew3D::MoveBody(float rightMeters, float forwardMeters, float yawDegrees)
{
	if (!m_bodyExploring || m_bodyFrame!=m_preparedSceneFrame||m_bodyEpoch!=m_preparedSceneEpoch||
		!std::isfinite(rightMeters)||!std::isfinite(forwardMeters)||!std::isfinite(yawDegrees)||
		std::hypot(rightMeters,forwardMeters)>.501f||std::abs(yawDegrees)>30.01f) return false;
	// Turning is independent of a blocked translation. Integrate translation
	// using the midpoint heading so simultaneous small stick deltas remain
	// smooth. The caller limits delta time; this path never advances emulation.
	const float previousYaw=m_bodyYaw;
	m_bodyYaw=std::remainder(m_bodyYaw+yawDegrees,360.0f);
	if (rightMeters==0 && forwardMeters==0) return true;
	const float r=(previousYaw+.5f*yawDegrees)*.01745329251994f,c=std::cos(r),s=std::sin(r);
	InspectionGeometry::Point from={m_bodyPosition[0],m_bodyGround,m_bodyPosition[2]};
	InspectionGeometry::Point next={from[0]+(c*rightMeters-s*forwardMeters)*m_bodyUnits,from[1],
		from[2]+(-s*rightMeters-c*forwardMeters)*m_bodyUnits};
	if (std::hypot(next[0],next[2])>4*m_bodyUnits) return yawDegrees!=0;
	float ground=0;
	if (!InspectionGeometry::Ground(m_bodyFloor,next[0],next[2],m_bodyGround-.3f*m_bodyUnits,m_bodyGround+.3f*m_bodyUnits,ground)) return yawDegrees!=0;
	next[1]=ground;
	if (!InspectionGeometry::ClearStep(m_bodyFloor,from,next,m_bodyUnits)) return yawDegrees!=0;
	m_bodyPosition[0]=next[0]; m_bodyPosition[2]=next[2]; m_bodyPosition[1]+=ground-m_bodyGround; m_bodyGround=ground;
	return true;
}

const float* CNew3D::BodyArmMatrix(const Model& model) const
{
	if (!m_bodyArms || !m_diagnosticEyeActive) return nullptr;
	for (const auto& arm:m_bodyArmModels) if (arm.source==&model) return arm.matrix.data();
	return nullptr;
}

void CNew3D::PrepareTrackedArms(int64_t displayTimeNs)
{
	m_bodyArmModels={};
	m_bodyNearGrip[0]=m_bodyNearGrip[1]=false;
	const auto* anchor=BodyAnchor();
	if (!m_bodyArms || !anchor || (anchor->riderSourceModel!=0x96FD45 && anchor->riderSourceModel!=0x97439D)) {
		m_bodyGrip.Reset(); m_bodyGripRider=m_bodyGripCharacter=0; return;
	}
	if (m_bodyGripRider!=anchor->riderSourceModel || m_bodyGripCharacter!=anchor->riderCharacterNode) {
		m_bodyGrip.Reset(); m_bodyHandAligned[0]=m_bodyHandAligned[1]=false;
		m_bodyGripRider=anchor->riderSourceModel; m_bodyGripCharacter=anchor->riderCharacterNode;
	}
	for (unsigned hand=0;hand<2;++hand) {
		const auto ids=TrackedArms::FatBoySources(hand ? TrackedArms::Hand::Right : TrackedArms::Hand::Left);
		TrackedArms::Models models; const Model* parts[3]={}; bool duplicate=false;
		for (const auto& node:m_nodes) {
			if (&node.viewport!=anchor) continue;
			for (const auto& model:node.models) if (model.riderObjectNode==anchor->riderCharacterNode) {
				for (unsigned part=0;part<3;++part) if (model.sourceAddress==ids[part]) {
					if (parts[part]) { duplicate=true; continue; }
					parts[part]=&model; Mat4 matrix=anchor->riderCorrection; matrix.MultMatrix(model.modelMat);
					std::copy_n(matrix.currentMatrix,16,models[part].begin());
				}
			}
		}
		TrackedArms::BindPose bind;
		if (duplicate||!parts[0]||!parts[1]||!parts[2]||!TrackedArms::BuildFatBoyBind(hand ? TrackedArms::Hand::Right : TrackedArms::Hand::Left,models,bind)) {
			m_bodyGrip.Prepare(hand,false,false,false); continue;
		}
		m_bodyGripPositions[hand]=bind.wrist;
		const auto& pose=m_bodyHands.pose[hand];
		TrackedArms::Quaternion grip{pose.orientation[0],pose.orientation[1],pose.orientation[2],pose.orientation[3]},normalized;
		bool tracked=m_bodyHands.displayTimeNs==displayTimeNs && m_bodyHands.valid[hand] && TrackedArms::Normalize(grip,normalized);
		for (float value:pose.position) tracked=tracked && std::isfinite(value) && std::abs(value)<=10;
		TrackedArms::Target target;
		if (tracked) {
			const auto offset=TrackedArms::Rotate(normalized,{0,0,.05});
			target.wrist={(pose.position[0]+offset.x)*m_bodyUnits,(pose.position[1]+offset.y)*m_bodyUnits,(pose.position[2]+offset.z)*m_bodyUnits};
			m_bodyNearGrip[hand]=!m_bodyExploring && TrackedArms::Length(target.wrist-bind.wrist)<=.20*m_bodyUnits;
		}
		const bool latched=m_bodyGrip.Prepare(hand,tracked,m_bodyNearGrip[hand],!m_bodyExploring);
		if (latched) {
			// The original six meshes already pose the arms onto the handlebars.
			// Reusing them exactly keeps a latched hand attached as the bike turns.
			m_bodyWrists[hand]=bind.wrist; m_bodyWristError[hand]=0; m_bodyWristClamped[hand]=false;
			for (unsigned part=0;part<3;++part) m_bodyArmModels[hand*3+part]={parts[part],models[part]};
			continue;
		}
		if (!tracked) continue;
		if (!m_bodyHandAligned[hand]) {
			TrackedArms::Quaternion native;
			if (!TrackedArms::Rotation(models[2],native)) continue;
			m_bodyHandAlignment[hand]=TrackedArms::Multiply(TrackedArms::Conjugate(normalized),native);
			m_bodyHandAligned[hand]=true;
		}
		target.handRotation=TrackedArms::Multiply(normalized,m_bodyHandAlignment[hand]);
		TrackedArms::Result result;
		if (!TrackedArms::SolveWithRootReach(bind,target,m_bodyUnits,.5,result)) continue;
		// Report the wrist from the matrix actually sent to drawing, rather than
		// echoing the requested IK target. The integration test checks alignment.
		const auto pivot=TrackedArms::FatBoyLocalPivots(hand ? TrackedArms::Hand::Right : TrackedArms::Hand::Left)[2];
		const auto& matrix=result.models[2];
		m_bodyWrists[hand]={matrix[0]*pivot.x+matrix[4]*pivot.y+matrix[8]*pivot.z+matrix[12],
			matrix[1]*pivot.x+matrix[5]*pivot.y+matrix[9]*pivot.z+matrix[13],
			matrix[2]*pivot.x+matrix[6]*pivot.y+matrix[10]*pivot.z+matrix[14]};
		m_bodyWristError[hand]=float(std::hypot(m_bodyWrists[hand].x-target.wrist.x,m_bodyWrists[hand].y-target.wrist.y,m_bodyWrists[hand].z-target.wrist.z)/m_bodyUnits);
		m_bodyWristClamped[hand]=result.reachClamped;
		for (unsigned part=0;part<3;++part) m_bodyArmModels[hand*3+part]={parts[part],result.models[part]};
	}
}

bool CNew3D::SetPresentationPose(const PresentationPose& pose)
{
	Mat4 headView;
	if (!MakePresentationView(pose, headView)) return false;
	m_presentationHeadView = headView;
	m_presentationPoseSet = true;
	m_presentationPoseIdentity = pose.orientation[0] == 0.0f &&
		pose.orientation[1] == 0.0f && pose.orientation[2] == 0.0f &&
		pose.position[0] == 0.0f && pose.position[1] == 0.0f && pose.position[2] == 0.0f;
	return true;
}

bool CNew3D::RenderPresentation(const PresentationPose& pose)
{
	Mat4 headView;
	if (!CanPresentPreparedScene() || !MakePresentationView(pose, headView)) return false;
	// m_nodes owns decoded viewport/model/mesh data; vertex/texture reads below
	// use already-uploaded GL objects only. In particular, do not call BeginFrame,
	// BuildScene, UploadScene, ApplyRiderComfort or ProcessLos on this path.
	if (m_bodyOptIn) PrepareTrackedArms(0);
	DrawStereoDiagnostic(&headView);
	return true;
}

void CNew3D::SetHeadsetPresentation(bool enabled)
{
	if (m_headsetPresentation == enabled) return;
	m_headsetPresentation = enabled;
	if (m_tileRenderer) m_tileRenderer->SetDeferredUploads(enabled);
	InvalidatePresentation();
}

bool CNew3D::GetHapticState(HapticState& state) const
{
	state = {};
	if (!CanPresentPreparedScene()) return false;
	state.sceneFrame = m_sceneFrame;
	state.sceneEpoch = m_sceneEpoch;
	state.driving = HarleyPresentation::Classify(m_gamePresentationState) == HarleyPresentation::Mode::Driving;
	state.motion = m_gamePresentationState.motion;
	if (state.driving) for (const auto& node : m_nodes) {
		if (!node.viewport.riderAnchorApplied) continue;
		state.anchorValid = true;
		for (int i = 0; i < 3; ++i) {
			state.body[i] = node.viewport.riderBodyWorld[12 + i];
			state.anchorValid = state.anchorValid && std::isfinite(state.body[i]);
		}
		break;
	}
	return true;
}

void CNew3D::PrepareHeadsetScene()
{
	LiveFrameTrace::OwnerRenderScope trace("prep.headset_scene", m_sceneFrame, m_sceneEpoch);
	// CPU scene decoding can overlap outstanding previous-eye GPU work before
	// this first tile consumer. Complete this same native snapshot before any
	// panel state capture or immutable headset redraw can sample the surfaces.
	if (m_tileRenderer) m_tileRenderer->CompletePreparedLayers();
	UpdateEyeLayout();
	if (TheaterIntroActive()) {
		if (trace.Active()) trace.c = 1; // Theater movie captured once at native cadence.
		// Render the original-camera movie once per game frame. Repeated headset
		// refreshes only place this immutable picture in the virtual room.
		DrawStereoDiagnostic(nullptr, false);
		m_preparedSceneValid = m_eyeImagesValid;
		return;
	}
	m_eyeImagesValid = false;
	const bool hideHUD = m_bodyExploring || (DrivingHUDScene() && m_stereoHideHUD);
	const bool panelRequested = !hideHUD && (ImmersiveOverlayPanel() ||
		(DrivingHUDScene() && (m_stereoVRHUD || GeneralTheaterEnabled())));
	if (trace.Active()) trace.d = panelRequested ? 1 : 0;
	if (panelRequested) {
		m_diagnosticEyeActive = true;
		PrepareHUDPanel();
		m_diagnosticEyeActive = false;
	}
	m_preparedSceneValid = m_hasImmersiveWorld;
}

void CNew3D::DestroyHeadsetTargets()
{
	m_headsetEyeActive = false;
	m_r3dShader.SetVisibilityTexture(0);
	m_visibilityMasks.DestroyTargets();
	m_headsetFrameBuffers.reset();
	if (m_headsetTarget.framebuffer) glDeleteFramebuffers(1, &m_headsetTarget.framebuffer);
	if (m_headsetTarget.texture) glDeleteTextures(1, &m_headsetTarget.texture);
	m_headsetTarget = EyeTarget{};
	m_headsetTargetWidth = m_headsetTargetHeight = 0;
}

bool CNew3D::PrepareHeadsetTargets(int width, int height)
{
	// Startup allocation only: no scene decoding, rendering, runtime image
	// acquisition, or frame submission. Internal targets are not bound by the
	// caller here; preserve the caller-owned bindings touched by construction.
	struct Bindings {
		GLint activeTexture = 0, texture = 0, read = 0, draw = 0, renderbuffer = 0, vao = 0;
		Bindings() {
			glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
			glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
		}
		~Bindings() {
			glActiveTexture(activeTexture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
			glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
			glBindVertexArray(vao);
		}
	} bindings;
	HeadsetGpuProbe().Configure(this, HeadsetGpuRequested());
	return EnsureHeadsetTargets(width, height);
}

bool CNew3D::EnsureHeadsetTargets(int width, int height)
{
	if (m_headsetTarget.framebuffer && m_headsetFrameBuffers &&
		width == m_headsetTargetWidth && height == m_headsetTargetHeight) return true;
	GLint maxTexture = 0, maxRenderbuffer = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbuffer);
	if (width <= 0 || height <= 0 || width > std::min(maxTexture, maxRenderbuffer) ||
		height > std::min(maxTexture, maxRenderbuffer)) return false;
	DestroyHeadsetTargets();
	m_headsetFrameBuffers = std::make_unique<R3DFrameBuffers>();
	if (m_headsetFrameBuffers->CreateFBO(width, height) != Result::OKAY) {
		DestroyHeadsetTargets();
		ErrorLog("Unable to allocate headset layered render target (%dx%d).", width, height);
		return false;
	}
	glGenTextures(1, &m_headsetTarget.texture);
	glBindTexture(GL_TEXTURE_2D, m_headsetTarget.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glGenFramebuffers(1, &m_headsetTarget.framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_headsetTarget.framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_headsetTarget.texture, 0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		DestroyHeadsetTargets();
		ErrorLog("Unable to allocate headset color target (%dx%d).", width, height);
		return false;
	}
	m_headsetTargetWidth = width;
	m_headsetTargetHeight = height;
	InfoLog("Headset renderer: allocated %dx%d per-eye geometry target; exact runtime lens, no desktop image stretch.", width, height);
	return true;
}

bool CNew3D::DrawHeadsetPanel(bool theater, int width, int height, const Mat4& view,
	const Mat4& projection, float unitsPerMeter, const SpatialPanel::State* knownState, int theaterEye)
{
	const auto& r = m_eyeLayout.nativeUI;
	const bool dashboard = !theater && DrivingHUDScene();
	const GLuint depth = !theater && m_hudDepthCertified ? m_headsetFrameBuffers->OpaqueDepthRenderbuffer() : 0;
	if (dashboard && DrawBikeHUD(0, 0, width, height, view, projection, unitsPerMeter, depth, knownState)) return true;
	const bool sideBoard = !theater && !dashboard && !depth;
	Mat4 panelView = view;
	if (sideBoard) panelView.Translate(-1.15f * unitsPerMeter, 0, 0);
	const float distance = (sideBoard ? 1.25f : 2.5f) * unitsPerMeter;
	const float halfWidth = dashboard ? unitsPerMeter : (sideBoard ? .65f * unitsPerMeter : distance * std::tan(TheaterScreenAngle() * 0.00872664626f));
	const int movieEye = m_eyeImagesStereoTheater && theaterEye == 1 ? 1 : 0;
	return m_spatialPanel.Draw(theater ? m_eyeTargets[movieEye].texture : m_hudTarget.texture,
		0, 0, width, height, panelView, projection, halfWidth, halfWidth * 384.0f / 496.0f,
		distance, dashboard ? -0.875f * unitsPerMeter : 0.0f,
		float(r.x) / m_totalXRes, float(r.y) / m_totalYRes,
		float(r.x + r.width) / m_totalXRes, float(r.y + r.height) / m_totalYRes,
		theater ? 1.0f : (dashboard ? 0.08f : 0.0f), knownState, !theater && !dashboard ? depth : 0);
}

void CNew3D::SetUserMenuPanel(unsigned texture, bool visible, uint64_t revision)
{
	const bool active = visible && texture != 0;
	if (texture == m_userMenuTexture && active == m_userMenuVisible && revision == m_userMenuRevision) return;
	if (!active || !m_userMenuVisible) m_userMenuAnchorValid = false;
	m_userMenuTexture = texture;
	m_userMenuVisible = active;
	m_userMenuRevision = revision;
}

void CNew3D::AnchorUserMenu(const PresentationEye (&eyes)[2], const Mat4 (&views)[2])
{
	// EyeMatrices already validated the pair. Its rotation is eye-to-base
	// transposed: row 2 is the eye's backward axis in recentered tracking space.
	// Average both eyes to avoid putting the panel relative to either lens/IPD.
	for (int i = 0; i < 3; ++i)
		m_userMenuPositionMeters[i] = 0.5f * (eyes[0].pose.position[i] + eyes[1].pose.position[i]);
	float yawX = views[0][2] + views[1][2];
	float yawZ = views[0][10] + views[1][10];
	if (yawX * yawX + yawZ * yawZ < 0.000001f) {
		// Looking vertically leaves forward yaw undefined. Use the projected
		// right axis instead, keeping the panel upright without a pitch/roll tilt.
		yawX = -(views[0][8] + views[1][8]);
		yawZ = views[0][0] + views[1][0];
	}
	m_userMenuYawDegrees = (yawX * yawX + yawZ * yawZ >= 0.000001f)
		? std::atan2(yawX, yawZ) * 57.2957795131f : 0.0f;
	m_userMenuAnchorValid = true;
}

bool CNew3D::DrawUserMenu(int width, int height, const Mat4& view,
	const Mat4& projection, float unitsPerMeter)
{
	if (!m_userMenuVisible || !m_userMenuTexture || !m_userMenuAnchorValid) return true;
	Mat4 panelView = view;
	panelView.Translate(m_userMenuPositionMeters[0] * unitsPerMeter,
		m_userMenuPositionMeters[1] * unitsPerMeter, m_userMenuPositionMeters[2] * unitsPerMeter);
	panelView.Rotate(m_userMenuYawDegrees, 0, 1, 0);
	// Fixed in tracked space after opening; fresh eye matrices give independent
	// head movement even while the native game snapshot is paused. No depth
	// attachment or full-screen backdrop: menu texel alpha defines its bounds.
	return m_spatialPanel.Draw(m_userMenuTexture, 0, 0, width, height, panelView, projection,
		0.6f * unitsPerMeter, 0.45f * unitsPerMeter, 1.5f * unitsPerMeter, 0,
		0, 0, 1, 1, 0.0f);
}

bool CNew3D::RenderHeadsetEyes(const PresentationEye (&eyes)[2], float gameUnitsPerMeter)
{
	Mat4 views[2], projections[2];
	HeadsetVignette::Transform apertures[2];
	if (!CanPresentPreparedScene() || !GeneralTheaterEnabled() ||
		!HeadsetGeometry::EyeMatrices(eyes[0], gameUnitsPerMeter, views[0], projections[0]) ||
		!HeadsetGeometry::EyeMatrices(eyes[1], gameUnitsPerMeter, views[1], projections[1]) ||
		!HeadsetVignette::Build(eyes, apertures)) return false;
	HeadsetGpuTiming::Metadata gpuMetadata;
	gpuMetadata.sceneFrame=m_sceneFrame;gpuMetadata.sceneEpoch=m_sceneEpoch;
	gpuMetadata.nativeFrame=LiveFrameTrace::OwnerEnabled()?LiveFrameTrace::Get().ownerFrame:0;
	gpuMetadata.displayTimeNs=eyes[0].displayTimeNs;
	gpuMetadata.dimensions={eyes[0].width,eyes[0].height,eyes[1].width,eyes[1].height};
	gpuMetadata.theater=TheaterIntroActive();
	auto gpuPair=HeadsetGpuProbe().Begin(this,HeadsetGpuRequested(),gpuMetadata);
	if (m_userMenuVisible && !m_userMenuAnchorValid) AnchorUserMenu(eyes, views);
	// The caller acquires both swapchain images before entry and submits neither
	// if this returns false. Validate the pair before modifying either image.
	GLint savedDraw, savedRead, savedDrawBuffer, savedReadBuffer, savedViewport[4], savedScissor[4], savedEqRGB, savedEqAlpha;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedDraw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &savedRead);
	glGetIntegerv(GL_DRAW_BUFFER, &savedDrawBuffer);
	glGetIntegerv(GL_READ_BUFFER, &savedReadBuffer);
	glGetIntegerv(GL_VIEWPORT, savedViewport);
	glGetIntegerv(GL_SCISSOR_BOX, savedScissor);
	// World rendering changes blend factors but preserves the equations. Read
	// them once at entry, before world submission, for the post-world contract.
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &savedEqRGB);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &savedEqAlpha);
	GLboolean savedMask[4];
	glGetBooleanv(GL_COLOR_WRITEMASK, savedMask);
	const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
	auto restore = [&]() {
		m_headsetEyeActive = false;
		m_r3dShader.SetVisibilityTexture(0);
		m_diagnosticEyeActive = false;
		m_viewportPass = ViewportPass::All;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, savedDraw);
		glDrawBuffer(savedDrawBuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, savedRead);
		glReadBuffer(savedReadBuffer);
		glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
		glScissor(savedScissor[0], savedScissor[1], savedScissor[2], savedScissor[3]);
		glColorMask(savedMask[0], savedMask[1], savedMask[2], savedMask[3]);
		if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
		if (srgb) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
	};
	for (const auto& eye : eyes) {
		if (!glIsFramebuffer(eye.framebuffer)) { restore(); return false; }
		glBindFramebuffer(GL_FRAMEBUFFER, eye.framebuffer);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { restore(); return false; }
	}
	glDisable(GL_SCISSOR_TEST);
	// Existing game shaders output display-encoded colors. Avoid applying an
	// extra gamma conversion when writing them into an sRGB runtime swapchain.
	glDisable(GL_FRAMEBUFFER_SRGB);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (!EnsureHeadsetTargets(std::max(eyes[0].width, eyes[1].width), std::max(eyes[0].height, eyes[1].height))) {
		restore();
		return false;
	}
	const bool theater = TheaterIntroActive();
	const bool hideHUD = m_bodyExploring || (DrivingHUDScene() && m_stereoHideHUD);
	const bool panelRequested = !theater && !hideHUD && (ImmersiveOverlayPanel() ||
		(DrivingHUDScene() && (m_stereoVRHUD || GeneralTheaterEnabled())));
	// Preparation happened at the native snapshot boundary. Never recapture a
	// HUD or movie using eye poses here; both eyes must consume the same texture.
	if ((theater && (!m_eyeImagesValid || !m_eyeImagesTheater)) || (panelRequested && !m_hudPanelValid)) {
		restore();
		return false;
	}
	m_stereoCinematicVignette = m_config["StereoCinematicVignette"].ValueAsDefault<bool>(false);
	const float cinematicStrength = CinematicVignetteStrength();
	const bool verifyOverlayState = !m_config["HeadsetRenderTest"].ValueAsDefault<std::string>("").empty() &&
		!m_config["HeadsetTestPerformance"].ValueAsDefault<bool>(false);
	const bool equalEyeSizes = eyes[0].width == eyes[1].width && eyes[0].height == eyes[1].height;
	if (m_sceneInterpolationEnabled && !m_bodyExploring) {
		SampleSceneInterpolationForEyes(SceneInterpolationNow(), eyes);
		const auto at = LiveFrameTrace::OwnerNow();
		++m_interpolationPair;
		LiveFrameTrace::Event("scene.interpolation", at, at, m_interpolationPair, m_sceneFrame,
			m_interpolationSample.sceneTime, int64_t(m_interpolationSample.alpha * 1000000.0), m_interpolationMatched);
		LiveFrameTrace::Event("scene.interpolation_coverage", at, at, m_interpolationPair, m_sceneEpoch,
			m_interpolationFallback, m_interpolationAnchored, m_sceneInterpolationActive);
		LiveFrameTrace::Event("scene.interpolation_display", at, at, m_interpolationPair,
			eyes[0].displayTimeNs, m_interpolationSample.displayDelta,
			int64_t(m_interpolationSample.reason), eyes[1].displayTimeNs);
	}
	if (m_bodyExploring) m_sceneInterpolationActive=false;
	if (m_bodyOptIn) PrepareTrackedArms(eyes[0].displayTimeNs);
	bool success = true;
	for (int index = 0; index < 2 && success; ++index) {
		const auto& eye = eyes[index];
		// The layered composite uses absolute gl_FragCoord and a full internal
		// viewport. Only a certified exact-sized target can replace the final
		// RGBA8 image: offsets, larger attachments and asymmetric sizes retain
		// the original copy so pixels outside the eye rectangle stay untouched.
		const bool direct = eye.directColorTarget && equalEyeSizes && eye.x == 0 && eye.y == 0 &&
			eye.targetWidth == eye.width && eye.targetHeight == eye.height &&
			eye.width == m_headsetTargetWidth && eye.height == m_headsetTargetHeight;
		HeadsetGpuProbe().Eye(this,unsigned(index),direct);
		const GLuint outputTarget = direct ? eye.framebuffer : m_headsetTarget.framebuffer;
		m_eyeView = views[index];
		m_headsetProjection = projections[index];
		m_headsetEyeWidth = eye.width;
		m_headsetEyeHeight = eye.height;
		glBindFramebuffer(GL_FRAMEBUFFER, outputTarget);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		if (theater) {
			HeadsetGpuProbe().GeometryEnd(this); // No world geometry in flat cinema.
			success = DrawHeadsetPanel(true, eye.width, eye.height, views[index], projections[index], gameUnitsPerMeter, nullptr, index);
			HeadsetGpuProbe().LayerEnd(this); // This interval is the theater panel.
			HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftComfortEnd);
			HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftHUDEnd);
		} else {
			m_headsetEyeActive = true;
			m_diagnosticEyeActive = true;
			m_viewportPass = ViewportPass::World;
			GLuint visibilityTexture = 0;
			if (m_r3dShader.VisibilityMaskSupported())
				visibilityTexture = m_visibilityMasks.Get(unsigned(index), eye, projections[index],
					m_headsetTargetWidth, m_headsetTargetHeight);
			{
				HeadsetVisibilityMask::WorldBinding visibilityBinding(visibilityTexture != 0);
				m_r3dShader.SetVisibilityTexture(visibilityTexture, visibilityBinding.Sampler());
				DrawPreparedScene(false, outputTarget);
				m_r3dShader.SetVisibilityTexture(0);
			}
			if (m_duplicateAuditFailed) { success = false; break; }
			glBindFramebuffer(GL_FRAMEBUFFER, outputTarget);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			// DrawPreparedScene ends with the layered composite's program/VAO 0,
			// unit 0 colour texture, full internal-FBO viewport, default alpha
			// factors and disabled depth/cull/blend. Guarantee stencil disabled
			// even if a future empty world skips all geometry priorities.
			glDisable(GL_STENCIL_TEST);
			SpatialPanel::State worldState;
			worldState.viewport[2] = m_headsetTargetWidth;
			worldState.viewport[3] = m_headsetTargetHeight;
			std::copy(savedScissor, savedScissor + 4, worldState.scissorBox);
			worldState.texture0 = m_headsetFrameBuffers->ColourTexture();
			worldState.eqRGB = savedEqRGB; worldState.eqAlpha = savedEqAlpha;
			if (verifyOverlayState && !SpatialPanel::State::Capture().Equals(worldState)) {
				ErrorLog("Headset post-world overlay state contract failed for eye %d.", index);
				success = false; break;
			}
			DrawComfortVignette(0, 0, eye.width, eye.height, false, 1.0f, apertures[index].fromNdc, &worldState);
			if (cinematicStrength > 0) DrawComfortVignette(0, 0, eye.width, eye.height, true, cinematicStrength, apertures[index].fromNdc, &worldState);
			HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftComfortEnd);
			if (panelRequested)
				success = DrawHeadsetPanel(false, eye.width, eye.height, views[index], projections[index], gameUnitsPerMeter, &worldState);
			HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftHUDEnd);
			m_headsetEyeActive = false;
			m_diagnosticEyeActive = false;
		}
		if (!success) break;
		if (m_bodyBlackout) { glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); }
		// Keep settings legible above every game overlay, comfort mask and body
		// transition. Closed menus add no state queries, uploads or draw calls.
		if (m_userMenuVisible && !DrawUserMenu(eye.width, eye.height, views[index], projections[index], gameUnitsPerMeter)) {
			success = false;
			break;
		}
		HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftMenuEnd);
		// Submission is opaque. Preserve the completed color while fixing any
		// fractional alpha left by legacy game transparency passes.
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftAlphaEnd);
		if (!direct) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, m_headsetTarget.framebuffer);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, eye.framebuffer);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glBlitFramebuffer(0, 0, eye.width, eye.height, eye.x, eye.y, eye.x + eye.width, eye.y + eye.height,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		HeadsetGpuProbe().EyeMark(this,HeadsetGpuTiming::Marker::LeftCopyEnd);
	}
	restore();
	gpuPair.Complete(success);
	return success;
}

void CNew3D::BeginFrame(void)
{
}

void CNew3D::EndFrame(void)
{
}

/******************************************************************************
Real3D Address Translation

Functions that interpret word-granular Real3D addresses and return pointers.
******************************************************************************/

// Translates 24-bit culling RAM addresses
const UINT32* CNew3D::TranslateCullingAddress(UINT32 addr)
{
	addr &= 0x00FFFFFF;	// caller should have done this already

	if ((addr >= 0x800000) && (addr < 0x840000)) {
		return &m_cullingRAMHi[addr & 0x3FFFF];
	}
	else if (addr < 0x100000) {
		return &m_cullingRAMLo[addr];
	}

	return nullptr;
}

// Translates model references
const UINT32* CNew3D::TranslateModelAddress(UINT32 modelAddr)
{
	modelAddr &= 0x00FFFFFF;	// caller should have done this already

	if (modelAddr < 0x100000) {
		return &m_polyRAM[modelAddr];
	}
	else {
		return &m_vrom[modelAddr];
	}
}

bool CNew3D::DrawModel(UINT32 modelAddr)
{
	bool cached = false;

	const UINT32* const modelAddress = TranslateModelAddress(modelAddr);

	// create a new model to push onto the vector
	m_nodes.back().models.emplace_back();

	// get the last model in the array
	Model* const m = &m_nodes.back().models.back();
	m->sourceAddress = modelAddr;
	if (m_sceneInterpolationEnabled && !m_diagnosticNodePath.empty()) m->presentationRoot = m_diagnosticNodePath.front();
	if ((m_stereoFullBike || m_stereoStableChase) && m_diagnosticNodePath.size() >= 2) m->riderObjectNode = m_diagnosticNodePath[1];
	if (m_recordModelPaths) m->diagnosticNodePath = m_diagnosticNodePath;
	if (m_stereoRiderAnchor && HarleyRiders::Find(modelAddr, m_stereoFullBike || m_stereoStableChase) && m_riderMatrixPath.size() >= 2) {
		RiderCandidate candidate;
		candidate.viewport = m_nodes.back().viewport.sourceAddress;
		candidate.objectNode = m_diagnosticNodePath[1];
		candidate.sourceModel = modelAddr;
		candidate.topologyValid = m_riderMatrixNodePath[1];
		candidate.bodyView = m_riderMatrixPath[1];
		candidate.modelView.LoadMatrix(m_modelMat.currentMatrix);
		m_riderCandidates.push_back(candidate);
	}

	if (IsVROMModel(modelAddr) && !IsDynamicModel((UINT32*)modelAddress)) {

		// try to find meshes in the rom cache

		m->meshes = m_romMap[modelAddr];	// will create an entry with a null pointer if empty

		if (m->meshes) {
			cached = true;
		}
		else {
			m->meshes = std::make_shared<std::vector<Mesh>>();
			m_romMap[modelAddr] = m->meshes;		// store meshes in our rom map here
		}

		m->dynamic = false;
	}
	else {
		m->meshes = std::make_shared<std::vector<Mesh>>();
	}

	// copy current model matrix
	for (int i = 0; i < 16; i++) {
		m->modelMat[i] = m_modelMat.currentMatrix[i];
	}

	// update texture offsets
	m->textureOffsetX	= m_nodeAttribs.currentTexOffsetX;
	m->textureOffsetY	= m_nodeAttribs.currentTexOffsetY;
	m->page				= m_nodeAttribs.currentPage;
	m->scale			= m_nodeAttribs.currentModelScale;
	m->alpha			= m_nodeAttribs.currentModelAlpha;

	if (!cached) {
		CacheModel(m, modelAddress);
	}

	return true;
}

/*
	0x00:   x------- -------- -------- --------	Is UF ref
			-x------ -------- -------- --------	Is 3D model
			--x----- -------- -------- --------	Is point
			---x---- -------- -------- --------	Is point ref
			----x--- -------- -------- --------	Is animation
			-----x-- -------- -------- --------	Is billboard
			------x- -------- -------- --------	Child is billboard
			-------x -------- -------- --------	Extra child pointer needed
			-------- xxxxx--- -------- --------	Spare (unknown if used)
			-------- -----xxx xxxxxx-- --------	Node ID
			-------- -------- ------x- --------	Discard 1
			-------- -------- -------x --------	Discard 2

			-------- -------- -------- x-------	Reset matrix
			-------- -------- -------- -x------	Use child pointer
			-------- -------- -------- --x-----	Use sibling pointer
			-------- -------- -------- ---x----	No matrix
			-------- -------- -------- ----x---	Indirect child
			-------- -------- -------- -----x--	Valid color table
			-------- -------- -------- ------xx	Node type(0 = viewport, 1 = root node, 2 = culling node)

	0x01, 0x02 only present on Step 1.5+

	0x01:   xxxxxxxx xxxxxxxx xxxxxxxx xxxxxx--	Model scale (float32) last 2 bits are control words
			-------- -------- -------- ------x- Disable culling
			-------- -------- -------- -------x	Valid model scale

	0x02 :	-------- -------- x------- --------	Texture replace
			-------- -------- -x------ --------	Switch bank
			-------- -------- --xxxxxx x-------	X offset
			-------- -------- -------- -xxxxxxx	Y offset

	0x03 :	xxxxxxxx xxxxx--- -------- --------	Color table address 1
			-------- -----xxx xxxx---- --------	LOD table pointer
			-------- -------- ----xxxx xxxxxxxx	Node matrix

	0x04:   Translation X coordinate
	0x05:   Translation Y coordinate
	0x06:   Translation Z coordinate

	0x07:   xxxx---- -------- -------- -------- Color table address 2
			-----x-- -------- -------- -------- Sibling table
			------x- -------- -------- -------- Point
			-------x -------- -------- -------- Leaf node
			-------- xxxxxxxx xxxxxxxx xxxxxxxx Child pointer

	0x08:   xxxxxxx- -------- -------- -------- Color table address 3
			-------x -------- -------- -------- Null sibling
			-------- xxxxxxxx xxxxxxxx xxxxxxxx Sibling pointer

	0x09:   xxxxxxxx xxxxxxxx -------- -------- Blend radius
			-------- -------- xxxxxxxx xxxxxxxx Culling radius
*/

void CNew3D::DescendCullingNode(UINT32 addr)
{
	enum class NodeType { undefined = -1, viewport = 0, rootNode = 1, cullingNode = 2 };

	const UINT32	*node, *lodPtr;
	UINT32			matrixOffset, child1Ptr, sibling2Ptr;
	UINT16			uCullRadius;
	float			fCullRadius;
	UINT16			uBlendRadius;
	float			fBlendRadius;
	UINT8			lodTablePointer;
	NodeType		nodeType;
	bool			resetMatrix;

	if (m_nodeAttribs.StackLimit()) {
		return;
	}

	node = TranslateCullingAddress(addr);

	if (NULL == node) {
		return;
	}

	// Extract known fields
	nodeType		= (NodeType)(node[0x00] & 3);
	child1Ptr		= node[0x07 - m_offset] & 0x7FFFFFF;	// mask colour table bits
	sibling2Ptr		= node[0x08 - m_offset] & 0x1FFFFFF;	// mask colour table bits
	matrixOffset	= node[0x03 - m_offset] & 0xFFF;
	resetMatrix		= (node[0x0] & 0x80) > 0;
	lodTablePointer = (node[0x03 - m_offset] >> 12) & 0x7F;

	// check our node type
	if (nodeType == NodeType::viewport) {
		return;												// viewport nodes aren't rendered
	}

	// node discard
	if ((0x300 & node[0]) == 0x300) {						// This allows to set node transparency. If both bits are set object is completely invisible.
		return;
	}

	// parse siblings 
	if ((node[0x00] & 0x07) != 0x06) {						// colour table seems to indicate no siblings
		if (!(sibling2Ptr & 0x1000000) && sibling2Ptr) {
			DescendCullingNode(sibling2Ptr);				// no need to mask bit, would already be zero
		}
	}

	if ((node[0x00] & 0x04)) {
		m_colorTableAddr = ((node[0x03 - m_offset] >> 19) << 0) | ((node[0x07 - m_offset] >> 28) << 13) | ((node[0x08 - m_offset] >> 25) << 17);
		m_colorTableAddr &= 0x000FFFFF; // clamp to 4MB (in words) range
	}

	m_nodeAttribs.Push();	// save current attribs
	m_diagnosticNodePath.push_back(addr);

	if (!m_offset) {		// Step 1.5+

		if (node[0x01] & 1)
			m_nodeAttribs.currentModelScale = Util::Uint32AsFloat(node[0x01] & ~3);	// mask out control bits

		if (node[0x01] & 2)
			m_nodeAttribs.currentDisableCulling = true;

		// apply texture offsets, else retain current ones
		if ((node[0x02] & 0x8000))	{
			int tx = 32 * ((node[0x02] >> 7) & 0x3F);
			int ty = 32 * (node[0x02] & 0x1F);
			m_nodeAttribs.currentTexOffsetX	= tx;
			m_nodeAttribs.currentTexOffsetY = ty;
			m_nodeAttribs.currentPage = (node[0x02] & 0x4000) >> 14;
		}
	}

	// Apply matrix and translation
	m_modelMat.PushMatrix();

	// apply translation vector
	if (node[0x00] & 0x10) {
		float centroid_x = Util::Uint32AsFloat(node[0x04 - m_offset]);
		float centroid_y = Util::Uint32AsFloat(node[0x05 - m_offset]);
		float centroid_z = Util::Uint32AsFloat(node[0x06 - m_offset]);
		m_modelMat.Translate(centroid_x, centroid_y, centroid_z);
	}
	// multiply matrix, if specified
	else if (matrixOffset) {
		MultMatrix(matrixOffset,m_modelMat);
	}

	if (resetMatrix) {
		ResetMatrix(m_modelMat);
	}
	CaptureRootCamera(addr, node[0], matrixOffset, sibling2Ptr);
	if (m_stereoRiderAnchor) {
		// Ancestry is already in this outer path; retain only the current matrix.
		m_riderMatrixPath.emplace_back();
		m_riderMatrixPath.back().LoadMatrix(m_modelMat.currentMatrix);
		m_riderMatrixNodePath.push_back(matrixOffset != 0 && !(node[0] & (0x10 | 0x80)));
	}

	float& x = m_modelMat.currentMatrix[12];
	float& y = m_modelMat.currentMatrix[13];
	float& z = m_modelMat.currentMatrix[14];

	uCullRadius = node[9 - m_offset] & 0xFFFF;
	fCullRadius = R3DFloat::GetFloat16(uCullRadius) * m_nodeAttribs.currentModelScale;;

	uBlendRadius = node[9 - m_offset] >> 16;
	fBlendRadius = R3DFloat::GetFloat16(uBlendRadius) * m_nodeAttribs.currentModelScale;;

	bool outsideFrustum = false;
	if ((z * m_planes.bnlu - x * m_planes.bnlv * m_planes.correction) > fCullRadius ||
		(z * m_planes.bntu + y * m_planes.bntw) > fCullRadius ||
		(z * m_planes.bnru - x * m_planes.bnrv * m_planes.correction) > fCullRadius ||
		(z * m_planes.bnbu + y * m_planes.bnbw) > fCullRadius)
	{
		outsideFrustum = true;
	}

	float LODscale = m_nodeAttribs.currentDisableCulling ? std::numeric_limits<float>::max() : (fBlendRadius / std::hypot(x, y, z));
	const LOD * const lod = m_LODBlendTable->table[lodTablePointer].lod;
	// The optional distance extension affects only the shared eye scene's world
	// viewport. The native center pass keeps the hardware's original LOD, alpha
	// and LOS results. Explicit hidden flags and geometry absent from the game
	// scene remain authoritative; this only extends existing distance thresholds.
	if (m_bypassFrustum && m_nodes.back().viewport.worldCandidate &&
		!m_nodeAttribs.currentDisableCulling && m_stereoLodDistance > 1.0f) {
		LODscale *= m_stereoLodDistance;
	}

	LODscale = std::clamp(LODscale, 0.0f, std::numeric_limits<float>::max());

	if (m_nodeAttribs.currentDisableCulling || ((!outsideFrustum || m_bypassFrustum) && LODscale >= lod[3].deleteSize)) {

		// Descend down first link
		if ((node[0x00] & 0x08))	// 4-element LOD table
		{
			lodPtr = TranslateCullingAddress(child1Ptr);

			if (nullptr != lodPtr)
			{
				int modelLOD;
				for (modelLOD = 0; modelLOD < 3; modelLOD++)
				{
					if (LODscale >= lod[modelLOD].deleteSize && lodPtr[modelLOD] & 0x1000000)
						break;
				}

				float tempAlpha = m_nodeAttribs.currentModelAlpha;

				float nodeAlpha = lod[modelLOD].blendFactor * (LODscale - lod[modelLOD].deleteSize);
				nodeAlpha = std::clamp(nodeAlpha, 0.0f, 1.0f);
				if (nodeAlpha > (float)(31.0 / 32.0))		// shader discards pixels below 1/32 alpha
					nodeAlpha = 1.0f;
				else if (nodeAlpha < (float)(1.0 / 32.0))
					nodeAlpha = 0.0f;
				m_nodeAttribs.currentModelAlpha *= nodeAlpha;	// alpha of each node multiples by the alpha of its parent
				
				if ((node[0x03 - m_offset] & 0x20000000)) {
					DescendCullingNode(lodPtr[modelLOD] & 0xFFFFFF);

					if (nodeAlpha < 1.0f && modelLOD != 3)
					{
						m_nodeAttribs.currentModelAlpha = (1.0f - nodeAlpha) * tempAlpha;
						DescendCullingNode(lodPtr[modelLOD+1] & 0xFFFFFF);
					}
				}
				else {
					DrawModel(lodPtr[modelLOD] & 0xFFFFFF);

					if (nodeAlpha < 1.0f && modelLOD != 3)
					{
						m_nodeAttribs.currentModelAlpha = (1.0f - nodeAlpha) * tempAlpha;
						DrawModel(lodPtr[modelLOD + 1] & 0xFFFFFF);
					}
				}
			}
		}
		else {

			float nodeAlpha = lod[3].blendFactor * (LODscale - lod[3].deleteSize);
			nodeAlpha = std::clamp(nodeAlpha, 0.0f, 1.0f);
			m_nodeAttribs.currentModelAlpha *= nodeAlpha;	// alpha of each node multiples by the alpha of its parent

			DescendNodePtr(child1Ptr);
		}

	}

	m_modelMat.PopMatrix();

	// Restore old texture offsets
	m_nodeAttribs.Pop();
	m_diagnosticNodePath.pop_back();
	if (m_stereoRiderAnchor) {
		m_riderMatrixPath.pop_back();
		m_riderMatrixNodePath.pop_back();
	}
}

void CNew3D::DescendNodePtr(UINT32 nodeAddr)
{
	// Ignore null links
	if ((nodeAddr & 0x00FFFFFF) == 0) {
		return;
	}

	switch ((nodeAddr >> 24) & 0x5)		// pointer type encoded in upper 8 bits
	{
	case 0x00:
		DescendCullingNode(nodeAddr & 0xFFFFFF);
		break;
	case 0x01:
		DrawModel(nodeAddr & 0xFFFFFF);
		break;
	case 0x04:
		DescendPointerList(nodeAddr & 0xFFFFFF);
		break;
	default:
		break;
	}
}

void CNew3D::DescendPointerList(UINT32 addr)
{
	const UINT32* const list = TranslateCullingAddress(addr);

	if (nullptr == list) {
		return;
	}

	int index = 0;

	while (true) {

		if (list[index] & 0x01000000) {
			break;	// empty list
		}

		UINT32 nodeAddr = list[index] & 0x00FFFFFF;	// clear upper 8 bits to ensure this is processed as a culling node

		DescendCullingNode(nodeAddr);

		if (list[index] & 0x02000000) {
			break;	// list end
		}

		index++;
	}
}


/******************************************************************************
Matrix Stack
******************************************************************************/

// Macro to generate column-major (OpenGL) index from y,x subscripts
#define CMINDEX(y,x)	(x*4+y)

/*
* MultMatrix():
*
* Multiplies the matrix stack by the specified Real3D matrix. The matrix
* index is a 12-bit number specifying a matrix number relative to the base.
* The base matrix MUST be set up before calling this function.
*/
void CNew3D::MultMatrix(UINT32 matrixOffset, Mat4& mat)
{
	GLfloat		m[4*4];
	const float	*src = &m_matrixBasePtr[matrixOffset * 12];

	if (m_matrixBasePtr == NULL)	// LA Machineguns
		return;

	m[CMINDEX(0, 0)] = src[3];
	m[CMINDEX(0, 1)] = src[4];
	m[CMINDEX(0, 2)] = src[5];
	m[CMINDEX(0, 3)] = src[0];
	m[CMINDEX(1, 0)] = src[6];
	m[CMINDEX(1, 1)] = src[7];
	m[CMINDEX(1, 2)] = src[8];
	m[CMINDEX(1, 3)] = src[1];
	m[CMINDEX(2, 0)] = src[9];
	m[CMINDEX(2, 1)] = src[10];
	m[CMINDEX(2, 2)] = src[11];
	m[CMINDEX(2, 3)] = src[2];
	m[CMINDEX(3, 0)] = 0.0;
	m[CMINDEX(3, 1)] = 0.0;
	m[CMINDEX(3, 2)] = 0.0;
	m[CMINDEX(3, 3)] = 1.0;

	mat.MultMatrix(m);
}

/*
* InitMatrixStack():
*
* Initializes the modelview (model space -> view space) matrix stack and
* Real3D coordinate system. These are the last transforms to be applied (and
* the first to be defined on the stack) before projection.
*
* Model 3 games tend to define the following unusual base matrix:
*
*		0	0	-1	0
*		1	0	0	0
*		0	-1	0	0
*		0	0	0	1
*
* When this is multiplied by a column vector, the output is:
*
*		-Z
*		X
*		-Y
*		1
*
* My theory is that the Real3D GPU accepts vectors in Z,X,Y order. The games
* store everything as X,Y,Z and perform the translation at the end. The Real3D
* also has Y and Z coordinates opposite of the OpenGL convention. This
* function inserts a compensating matrix to undo these things.
*
* NOTE: This function assumes we are in GL_MODELVIEW matrix mode.
*/

void CNew3D::InitMatrixStack(UINT32 matrixBaseAddr, Mat4& mat)
{
	GLfloat m[4 * 4];

	// This matrix converts vectors back from the weird Model 3 Z,X,Y ordering
	// and also into OpenGL viewspace (-Y,-Z)
	m[CMINDEX(0, 0)] = 0.0;	m[CMINDEX(0, 1)] = 1.0;	m[CMINDEX(0, 2)] = 0.0;	m[CMINDEX(0, 3)] = 0.0;
	m[CMINDEX(1, 0)] = 0.0;	m[CMINDEX(1, 1)] = 0.0;	m[CMINDEX(1, 2)] =-1.0; m[CMINDEX(1, 3)] = 0.0;
	m[CMINDEX(2, 0)] =-1.0; m[CMINDEX(2, 1)] = 0.0;	m[CMINDEX(2, 2)] = 0.0;	m[CMINDEX(2, 3)] = 0.0;
	m[CMINDEX(3, 0)] = 0.0;	m[CMINDEX(3, 1)] = 0.0;	m[CMINDEX(3, 2)] = 0.0;	m[CMINDEX(3, 3)] = 1.0;

	mat.LoadMatrix(m);

	// Set matrix base address and apply matrix #0 (coordinate system matrix)
	m_matrixBasePtr = (float *)TranslateCullingAddress(matrixBaseAddr);
	MultMatrix(0, mat);
}

// what this does is to set the rotation back to zero, whilst keeping the position and scale of the current matrix
void CNew3D::ResetMatrix(Mat4& mat) const
{
	float m[16];
	memcpy(m, mat.currentMatrix, 16 * 4);

	// transpose the top 3x3 of the matrix (this effectively inverts the rotation). When we multiply our new matrix it'll effectively cancel out the rotations.
	std::swap(m[1], m[4]);
	std::swap(m[2], m[8]);
	std::swap(m[6], m[9]);

	// set position to zero
	m[12] = 0;
	m[13] = 0;
	m[14] = 0;
	m[15] = 1;

	// normalise columns, this removes the scaling, otherwise we'll apply it twice
	float s1 = std::sqrt((m[0] * m[0]) + (m[1] * m[1]) + (m[2] * m[2]));
	float s2 = std::sqrt((m[4] * m[4]) + (m[5] * m[5]) + (m[6] * m[6]));
	float s3 = std::sqrt((m[8] * m[8]) + (m[9] * m[9]) + (m[10] * m[10]));

	m[0] /= s1;		m[4] /= s2;		m[8] /= s3;
	m[1] /= s1;		m[5] /= s2;		m[9] /= s3;
	m[2] /= s1;		m[6] /= s2;		m[10] /= s3;

	mat.MultMatrix(m);
}

// Draws viewports of the given priority
void CNew3D::RenderViewport(UINT32 addr)
{
	static const GLfloat	color[8][3] =
	{											// RGB1 color translation
		{ 0.0f, 0.0f, 0.0f },	// off
		{ 0.0f, 0.0f, 1.0f },	// blue
		{ 0.0f, 1.0f, 0.0f },	// green
		{ 0.0f, 1.0f, 1.0f },	// cyan
		{ 1.0f, 0.0f, 0.0f }, 	// red
		{ 1.0f, 0.0f, 1.0f },	// purple
		{ 1.0f, 1.0f, 0.0f },	// yellow
		{ 1.0f, 1.0f, 1.0f }	// white
	};

	if ((addr & 0x00FFFFFF) == 0) {
		return;
	}

	// Translate address and obtain pointer
	const uint32_t * const vpnode = TranslateCullingAddress(addr);

	if (nullptr == vpnode) {
		return;
	}

	bool vpDisabled = vpnode[0] & 0x20;						// only if viewport enabled

	{
		// create node object 
		m_nodes.emplace_back(Node());
		m_sceneModelBuffers.PrepareModels(m_nodes.back().models);

		// get pointer to its viewport
		Viewport* vp = &m_nodes.back().viewport;
		vp->sourceAddress = addr & 0xFFFFFF;
		vp->rootAddress = vpnode[0x02] & 0xFFFFFF;

		vp->priority	= (vpnode[0] >> 3) & 0x3;
		vp->select		= (vpnode[0] >> 8) & 0x3;
		vp->number		= (vpnode[0] >> 10);

		// Fetch viewport parameters
		vp->vpX			= ((vpnode[0x1A] & 0xFFFF) * (float)(1.0 / 16.0)) ;		// viewport X (12.4 fixed point)
		vp->vpY			= ((vpnode[0x1A] >> 16) * (float)(1.0 / 16.0));			// viewport Y (12.4)
		vp->vpWidth		= ((vpnode[0x14] & 0xFFFF) * (float)(1.0 / 4.0));		// width (14.2)
		vp->vpHeight	= ((vpnode[0x14] >> 16) * (float)(1.0 / 4.0));			// height (14.2)

		uint32_t matrixBase = vpnode[0x16] & 0xFFFFFF;							// matrix base address
		vp->matrixBaseAddress = matrixBase;
		vp->worldCandidate = vp->priority == 1 && vp->vpX == 0.0f && vp->vpY == 0.0f &&
			vp->vpWidth >= 495.0f && vp->vpWidth <= 496.0f &&
			vp->vpHeight >= 383.0f && vp->vpHeight <= 384.0f;
		if (vp->worldCandidate) vp->cameraStatus = vpDisabled ? "viewport_disabled" : "root_not_captured";

		m_LODBlendTable = (LODBlendTable*)TranslateCullingAddress(vpnode[0x17] & 0xFFFFFF);

		float cv = Util::Uint32AsFloat(vpnode[0x8]);	// 1/(left-right)
		float cw = Util::Uint32AsFloat(vpnode[0x9]);	// 1/(top-bottom)
		float io = Util::Uint32AsFloat(vpnode[0xa]);	// top / bottom (ratio) - ish
		float jo = Util::Uint32AsFloat(vpnode[0xb]);	// left / right (ratio)

		// clipping plane normals
		m_planes.bnlu = Util::Uint32AsFloat(vpnode[0xc]);
		m_planes.bnlv = Util::Uint32AsFloat(vpnode[0xd]);
		m_planes.bntu = Util::Uint32AsFloat(vpnode[0xe]);
		m_planes.bntw = Util::Uint32AsFloat(vpnode[0xf]);
		m_planes.bnru = Util::Uint32AsFloat(vpnode[0x10]);
		m_planes.bnrv = Util::Uint32AsFloat(vpnode[0x11]);
		m_planes.bnbu = Util::Uint32AsFloat(vpnode[0x12]);
		m_planes.bnbw = Util::Uint32AsFloat(vpnode[0x13]);
		m_planes.correction = 1.0f;		// might get changed by the calc viewport method

		vp->angle_left		= (0.0f - jo) / cv;
		vp->angle_right		= (1.0f - jo) / cv;
		vp->angle_bottom	= -(1.0f - io) / cw;
		vp->angle_top		= -(0.0f - io) / cw;

		vp->cota = Util::Uint32AsFloat(vpnode[0x3]);

		CalcViewport(vp);

		// Lighting (note that sun vector points toward sun -- away from vertex)
		vp->lightingParams[0] = Util::Uint32AsFloat(vpnode[0x05]);							// sun X
		vp->lightingParams[1] = -Util::Uint32AsFloat(vpnode[0x06]);							// sun Y (- to convert to ogl cordinate system)
		vp->lightingParams[2] = -Util::Uint32AsFloat(vpnode[0x04]);							// sun Z (- to convert to ogl cordinate system)
		vp->lightingParams[3] = std::max(0.f, std::min(Util::Uint32AsFloat(vpnode[0x07]), 1.0f));	// sun intensity (clamp to 0-1)
		vp->lightingParams[4] = (float)((vpnode[0x24] >> 8) & 0xFF) * (float)(1.0 / 255.0);	// ambient intensity
		vp->lightingParams[5] = 0.0f;	// reserved

		vp->sunClamp = m_sunClamp;
		vp->intensityClamp = (m_step == 0x10);		// just step 1.0 ?
		vp->hardwareStep = m_step;

		// Spotlight
		int spotColorIdx = (vpnode[0x20] >> 11) & 7;									// spotlight color index
		int spotFogColorIdx = (vpnode[0x20] >> 8) & 7;									// spotlight on fog color index
		vp->spotEllipse[0] = (float)(INT16)(vpnode[0x1E] & 0xFFFF) * (float)(1.0 / 8.0);// spotlight X position (13.3 fixed point)
		vp->spotEllipse[1] = (float)(INT16)(vpnode[0x1D] & 0xFFFF) * (float)(1.0 / 8.0);// spotlight Y
		vp->spotEllipse[2] = (float)((vpnode[0x1E] >> 16) & 0xFFFF);					// spotlight X size (16-bit)
		vp->spotEllipse[3] = (float)((vpnode[0x1D] >> 16) & 0xFFFF);					// spotlight Y size

		vp->spotRange[0] = 1.0f / Util::Uint32AsFloat(vpnode[0x21]);					// spotlight start
		vp->spotRange[1] = Util::Uint32AsFloat(vpnode[0x1F]);							// spotlight extent

		vp->spotColor[0] = color[spotColorIdx][0];										// spotlight color
		vp->spotColor[1] = color[spotColorIdx][1];
		vp->spotColor[2] = color[spotColorIdx][2];

		vp->spotFogColor[0] = color[spotFogColorIdx][0];								// spotlight color on fog
		vp->spotFogColor[1] = color[spotFogColorIdx][1];
		vp->spotFogColor[2] = color[spotFogColorIdx][2];

		// spotlight is specified in terms of physical resolution
		vp->spotEllipse[1] = 384.0f - vp->spotEllipse[1];								// flip Y position

		// Avoid division by zero
		vp->spotEllipse[2] = std::max(1.0f, vp->spotEllipse[2]);
		vp->spotEllipse[3] = std::max(1.0f, vp->spotEllipse[3]);

		vp->spotEllipse[2] = std::roundf(2047.0f / vp->spotEllipse[2]);
		vp->spotEllipse[3] = std::roundf(2047.0f / vp->spotEllipse[3]);

		// Scale the spotlight to the OpenGL viewport
		vp->spotEllipse[0] = vp->spotEllipse[0] * m_xRatio + (float)m_xOffs;
		vp->spotEllipse[1] = vp->spotEllipse[1] * m_yRatio + (float)m_yOffs;
		vp->spotEllipse[2] *= m_xRatio;
		vp->spotEllipse[3] *= m_yRatio;

		// Line of sight position
		vp->losPosX = (int)(((vpnode[0x1c] & 0xFFFF) / 16.0f) + 0.5f);					// x position
		vp->losPosY = (int)(((vpnode[0x1c] >> 16) / 16.0f) + 0.5f);						// y position 0 starts from the top

		// Fog
		vp->fogColour[0]	= (float)((vpnode[0x22] >> 16) & 0xFF) * (float)(1.0 / 255.0);	// fog color R
		vp->fogColour[1]	= (float)((vpnode[0x22] >> 8) & 0xFF) * (float)(1.0 / 255.0);	// fog color G
		vp->fogColour[2]	= (float)((vpnode[0x22] >> 0) & 0xFF) * (float)(1.0 / 255.0);	// fog color B
		vp->fogDensity		= std::abs(Util::Uint32AsFloat(vpnode[0x23]));					// fog density	- ocean hunter uses negative values, but looks the same
		vp->fogStart		= (float)(INT16)(vpnode[0x25] & 0xFFFF) * (float)(1.0 / 255.0);	// fog start
		vp->fogAttenuation	= (float)((vpnode[0x24] >> 16) & 0xFF) * (float)(1.0 / 255.0);	// fog attenuation
		vp->fogAmbient		= (float)((vpnode[0x25] >> 16) & 0xFF) * (float)(1.0 / 255.0);	// fog ambient

		// Avoid Infinite and NaN values for Star Wars Trilogy
		if (std::isinf(vp->fogDensity) || std::isnan(vp->fogDensity)) {
			vp->fogDensity = 0;
		}

		vp->scrollFog = (float)(vpnode[0x20] & 0xFF) * (float)(1.0 / 255.0);				// scroll fog
		vp->scrollAtt = (float)(vpnode[0x24] & 0xFF) * (float)(1.0 / 255.0);				// scroll attenuation

		// Clear texture offsets before proceeding
		m_nodeAttribs.Reset();

		// Set up coordinate system and base matrix
		InitMatrixStack(matrixBase, m_modelMat);

		// Descend down the node link. Need to start with a culling node because that defines our culling radius.
		if (!vpDisabled) {
			auto childptr = vpnode[0x02];
			if (((childptr >> 24) & 0x5) == 0) {
				DescendNodePtr(vpnode[0x02]);
			}
		}
	}
	
	// render next viewport
	if (vpnode[0x01] != 0x01000000) {
		RenderViewport(vpnode[0x01]);
	}
}

void CNew3D::CopyVertexData(const R3DPoly& r3dPoly, std::vector<FVertex>& vertexArray)
{
	// both lemans 24 and dirt devils are rendering some totally transparent polys as the first object in each viewport
	// in dirt devils it's parallel to the camera so is completely invisible, but breaks our depth calculation
	// in lemans 24 its a sort of diamond shape, but never leaves a hole in the transparent geometry so must be being skipped by the h/w
	if (r3dPoly.faceColour[3] == 0) {
		return;
	}

	if (m_numPolyVerts==4) {
		if (r3dPoly.number == 4) {
			vertexArray.emplace_back(r3dPoly, 0);		// construct directly inside container without copy
			vertexArray.emplace_back(r3dPoly, 1);
			vertexArray.emplace_back(r3dPoly, 2);
			vertexArray.emplace_back(r3dPoly, 3);

			// check for identical points (i.e. forced triangle), as the quad code can be inprecise for these:
			// the quad shader detects them and draws just the triangle
			FVertex* v = (&vertexArray.back()) - 3;

			for (int i = 0; i < 4; i++) {

				int next1 = (i + 1) % 4;

				if (FVertex::Equal(v[i], v[next1])) {
					v[next1] = v[i];
					std::rotate(v, v + (next1 + 1) % 4, v + 4);	// copy to slot 3, after its original in slot 2, as the quad shader looks for it there
					break;
				}
			}
		}
		else {
			vertexArray.emplace_back(r3dPoly, 0);
			vertexArray.emplace_back(r3dPoly, 1);
			vertexArray.emplace_back(r3dPoly, 2);
			vertexArray.emplace_back(r3dPoly, 2);		// last point is a copy of 2 (the quad shader detects this and draws just the triangle)
		}
	}
	else {
		vertexArray.emplace_back(r3dPoly, 0);
		vertexArray.emplace_back(r3dPoly, 1);
		vertexArray.emplace_back(r3dPoly, 2);

		if (r3dPoly.number == 4) {
			vertexArray.emplace_back(r3dPoly, 0);
			vertexArray.emplace_back(r3dPoly, 2);
			vertexArray.emplace_back(r3dPoly, 3);
		}
	}
}

void CNew3D::GetCoordinates(int width, int height, UINT16 uIn, UINT16 vIn, float uvScale, float& uOut, float& vOut) const
{
	uOut = (uIn * uvScale) / width;
	vOut = (vIn * uvScale) / height;
}

int CNew3D::GetTexFormat(int originalFormat, bool contour) const
{
	if (!contour) {
		return originalFormat;	// the same
	}

	switch (originalFormat)
	{
	case 1:
	case 2:
	case 3:
	case 4:
		return originalFormat + 7;		// these formats are identical to 1-4, except they lose the 4 bit alpha part when contour is enabled
	default:
		return originalFormat;
	}
}

void CNew3D::SetMeshValues(SortingMesh *currentMesh, PolyHeader &ph)
{
	//copy attributes
	currentMesh->textured		= ph.TexEnabled();
	currentMesh->alphaTest		= ph.AlphaTest();
	currentMesh->textureAlpha	= ph.TextureAlpha();
	currentMesh->polyAlpha		= ph.PolyAlpha();
	currentMesh->lighting		= ph.LightEnabled();
	currentMesh->fixedShading	= ph.FixedShading() && !ph.SmoothShading();
	currentMesh->highPriority	= ph.HighPriority();
	currentMesh->transLSelect	= ph.TranslucencyPatternSelect();
	currentMesh->layered		= ph.Layered();
	currentMesh->specular		= ph.SpecularEnabled();
	currentMesh->shininess		= ph.Shininess();
	currentMesh->specularValue	= ph.SpecularValue();
	currentMesh->fogIntensity	= ph.LightModifier();
	currentMesh->translatorMap	= ph.TranslatorMap();
	currentMesh->noLosReturn	= ph.NoLosReturn();
	currentMesh->smoothShading	= ph.SmoothShading();

	if (currentMesh->textured) {

		currentMesh->format = GetTexFormat(ph.TexFormat(), ph.AlphaTest());

		if (currentMesh->format == 7) {
			currentMesh->alphaTest = false;	// alpha test is a 1 bit test, this format needs a lower threshold, since it has 16 levels of transparency
		}

		currentMesh->x				= ph.X();
		currentMesh->y				= ph.Y();
		currentMesh->width			= ph.TexWidth();
		currentMesh->height			= ph.TexHeight();
		currentMesh->microTexture	= ph.MicroTexture();
		currentMesh->inverted		= ph.TranslatorMapOffset() == 2;
		currentMesh->page			= ph.Page();

		{
			bool smoothU = ph.TexSmoothU();
			bool smoothV = ph.TexSmoothV();

			if (ph.AlphaTest()) {
				smoothU = false;	// smooth wrap makes no sense for alpha tested polys with pixel dilate
				smoothV = false;
			}

			if (ph.TexUMirror()) {
				if (smoothU)	currentMesh->wrapModeU = Mesh::TexWrapMode::mirror;
				else			currentMesh->wrapModeU = Mesh::TexWrapMode::mirrorClamp;
			}
			else {
				if (smoothU)	currentMesh->wrapModeU = Mesh::TexWrapMode::repeat;
				else			currentMesh->wrapModeU = Mesh::TexWrapMode::repeatClamp;
			}

			if (ph.TexVMirror()) {
				if (smoothV)	currentMesh->wrapModeV = Mesh::TexWrapMode::mirror;
				else			currentMesh->wrapModeV = Mesh::TexWrapMode::mirrorClamp;
			}
			else {
				if (smoothV)	currentMesh->wrapModeV = Mesh::TexWrapMode::repeat;
				else			currentMesh->wrapModeV = Mesh::TexWrapMode::repeatClamp;
			}
		}

		if (currentMesh->microTexture) {

			currentMesh->microTextureID = ph.MicroTextureID();
			currentMesh->microTextureMinLOD = (float)ph.MicroTextureMinLOD();
		}
	}
}

void CNew3D::CacheModel(Model *m, const UINT32 *data)
{
	if (data == nullptr)
		return;

	UINT16			texCoords[4][2];
	PolyHeader		ph;
	UINT64			lastHash	= -1;
	SortingMesh*	currentMesh = nullptr;
	
	std::unordered_map<UINT64, SortingMesh> sMap;

	ph = data; 
	int numTriangles = ph.NumTrianglesTotal();

	// Cache all polygons
	do {

		R3DPoly		p;					// current polygon
		float		uvScale;

		if (ph.header[6] == 0) {
			break;
		}

		// create a hash value based on poly attributes -todo add more attributes
		auto hash = ph.Hash();

		if (hash != lastHash) {

			if (sMap.count(hash) == 0) {

				currentMesh = &sMap.insert({hash, SortingMesh()}).first->second;

				//make space for our vertices
				currentMesh->verts.reserve(numTriangles * 3);

				//set mesh values
				SetMeshValues(currentMesh, ph);
			}
			else
				currentMesh = &sMap[hash];
		}

		// Obtain basic polygon parameters
		p.number	= ph.NumVerts();
		uvScale		= ph.UVScale();

		ph.FaceNormal(p.faceNormal);

		// Fetch reused vertices according to bitfield, then new verts
		int j = 0;
		for (int i = 0; i < 4; i++)		// up to 4 reused vertices
		{
			if (ph.SharedVertex(i))
			{
				p.v[j] = m_prev[i];

				texCoords[j][0] = m_prevTexCoords[i][0];
				texCoords[j][1] = m_prevTexCoords[i][1];

				//check if we need to recalc tex coords - will only happen if tex tiles are different + sharing vertices
				if (hash != lastHash) {
					if (currentMesh->textured) {
						GetCoordinates(currentMesh->width, currentMesh->height, texCoords[j][0], texCoords[j][1], uvScale, p.v[j].texcoords[0], p.v[j].texcoords[1]);
					}
				}

				j++;
			}
		}

		lastHash = hash;

		// copy face attributes

		if (!ph.PolyColor()) {
			int colorIdx = ph.ColorIndex();
			p.faceColour[2] = (m_polyRAM[m_colorTableAddr + colorIdx] & 0xFF);
			p.faceColour[1] = ((m_polyRAM[m_colorTableAddr + colorIdx] >> 8) & 0xFF);
			p.faceColour[0] = ((m_polyRAM[m_colorTableAddr + colorIdx] >> 16) & 0xFF);
		}
		else {
			p.faceColour[0] = ((ph.header[4] >> 24));
			p.faceColour[1] = ((ph.header[4] >> 16) & 0xFF);
			p.faceColour[2] = ((ph.header[4] >> 8) & 0xFF);
		}

		p.faceColour[3] = ph.Transparency();

		if (ph.Discard1() && !ph.Discard2()) {
			p.faceColour[3] /= 2;
		}

		// if we have flat shading, we can't re-use normals from shared vertices
		for (int i = 0; i < p.number && !ph.SmoothShading(); i++) {
			p.v[i].normal[0] = p.faceNormal[0];
			p.v[i].normal[1] = p.faceNormal[1];
			p.v[i].normal[2] = p.faceNormal[2];
		}

		p.textureNP = ph.TextureNP() * m_textureNPFactor;

		UINT32* vData = ph.StartOfData();	// vertex data starts here

		// remaining vertices are new and defined here
		for (; j < p.number; j++)	
		{
			// Fetch vertices
			UINT32 ix = vData[0];
			UINT32 iy = vData[1];
			UINT32 iz = vData[2];
			UINT32 it = vData[3];

			// Decode vertices
			p.v[j].pos[0] = (((INT32)ix) >> 8) * m_vertexFactor;
			p.v[j].pos[1] = (((INT32)iy) >> 8) * m_vertexFactor;
			p.v[j].pos[2] = (((INT32)iz) >> 8) * m_vertexFactor;
			p.v[j].pos[3] = 1.0f;

			// Per vertex normals
			if (ph.SmoothShading()) {
				p.v[j].normal[0] = BYTE_TO_FLOAT((INT8)(ix & 0xFF));
				p.v[j].normal[1] = BYTE_TO_FLOAT((INT8)(iy & 0xFF));
				p.v[j].normal[2] = BYTE_TO_FLOAT((INT8)(iz & 0xFF));
			}

			if (ph.FixedShading() && !ph.SmoothShading()) {			// fixed shading seems to be disabled if actual normals are set

				//==========
				float shade;
				//==========

				// If specular is enabled fixed shading values are treated as unsigned
				if (ph.SpecularEnabled()) {
					shade = (ix & 0xFF) * (float)(1.0 / 255.0);
				}
				else {
					shade = BYTE_TO_FLOAT((INT8)(ix & 0xFF));
				}
				
				p.v[j].fixedShade = shade;
			}

			float texU = 0;
			float texV = 0;

			// tex coords
			if (currentMesh->textured) {
				GetCoordinates(currentMesh->width, currentMesh->height, (UINT16)(it >> 16), (UINT16)(it & 0xFFFF), uvScale, texU, texV);
			}

			p.v[j].texcoords[0] = texU;
			p.v[j].texcoords[1] = texV;

			//cache un-normalised tex coordinates
			texCoords[j][0] = (UINT16)(it >> 16);
			texCoords[j][1] = (UINT16)(it & 0xFFFF);

			vData += 4;
		}

		// check if we need to double up vertices for two sided lighting
		if (ph.DoubleSided() && !ph.Discard()) {

			R3DPoly tempP = p;

			// flip normals
			V3::inverse(tempP.faceNormal);

			for (int i2 = 0; i2 < tempP.number; i2++) {
				V3::inverse(tempP.v[i2].normal);
			}

			CopyVertexData(tempP, currentMesh->verts);
		}

		// Copy this polygon into the model buffer
		if (!ph.Discard()) {
			CopyVertexData(p, currentMesh->verts);
		}
		
		// Copy current vertices into previous vertex array
		for (int i = 0; i < 4; i++) {
			m_prev[i] = p.v[i];
			m_prevTexCoords[i][0] = texCoords[i][0];
			m_prevTexCoords[i][1] = texCoords[i][1];
		}

	} while (ph.NextPoly());

	//sorted the data, now copy to main data structures

	// we know how many meshes we have to reserve appropriate space
	m->meshes->reserve(sMap.size());

	for (auto& it : sMap) {

		if (m->dynamic) {

			// calculate VBO values for current mesh
			it.second.vboOffset		= (int)m_polyBufferRam.size() + MAX_ROM_VERTS;
			it.second.vertexCount	= (int)it.second.verts.size();

			// copy poly data to main buffer
			m_polyBufferRam.insert(m_polyBufferRam.end(), it.second.verts.begin(), it.second.verts.end());
		}
		else {
			// calculate VBO values for current mesh
			it.second.vboOffset		= (int)m_polyBufferRom.size();
			it.second.vertexCount	= (int)it.second.verts.size();

			// copy poly data to main buffer
			m_polyBufferRom.insert(m_polyBufferRom.end(), it.second.verts.begin(), it.second.verts.end());
		}

		//copy the temp mesh into the model structure
		//this will lose the associated vertex data, which is now copied to the main buffer anyway
		m->meshes->push_back(it.second);
	}
}

bool CNew3D::IsDynamicModel(UINT32 *data) const
{
	if (data == nullptr) {
		return false;
	}

	PolyHeader p(data);

	do {

		if ((p.header[1] & 2) == 0) {		// model has rgb colour palette 
			return true;
		}

	} while (p.NextPoly());

	return false;
}

bool CNew3D::IsVROMModel(UINT32 modelAddr) const
{
	return modelAddr >= 0x100000;
}

void CNew3D::CalcViewport(Viewport* vp)
{
	float l = vp->angle_left;	// we need to calc the shape of the projection frustum for culling
	float r = vp->angle_right;
	float t = vp->angle_top;
	float b = vp->angle_bottom;

	vp->projectionMatrix.LoadIdentity();	// reset matrix

	// Diagnostic presentation widens only its eye pass. The native center and
	// its culling/LOS projection must remain the game's original 496:384 view.
	if (m_wideScreen && !m_stereoDiagnostic && (vp->vpX == 0) && (vp->vpWidth >= 495) && (vp->vpY == 0) && (vp->vpHeight >= 383)) {

		/*
		 * Compute aspect ratio correction factor. "Window" refers to the full GL
		 * viewport (i.e., totalXRes x totalYRes). "Viewable area" is the effective
		 * Model 3 screen (xRes x yRes). In non-wide-screen, non-stretch mode, this
		 * is intended to replicate the 496x384 display and may in general be 
		 * smaller than the window. The rest of the window appears to have a
		 * border, which is created by a scissor box.
		 *
		 * In wide-screen mode, we want to expand the frustum horizontally to fill
		 * the window. We want the aspect ratio to be correct. To accomplish this,
		 * the viewable area is set *the same* as in non-wide-screen mode (e.g.,
		 * often smaller than the window) but glScissor() is set by the OSD layer's
		 * screen setup code to reveal the entire window.
		 *
		 * In stretch mode, the window and viewable area are both set the same,
		 * which means there will be no aspect ratio correction and the display
		 * will stretch to fill the entire window while keeping the view frustum
		 * the same as a 496x384 Model 3 display. The display will be distorted.
		 */
		float windowAR = (float)m_totalXRes / (float)m_totalYRes;
		float viewableAreaAR = (float)m_xRes / (float)m_yRes;
		
		// Will expand horizontal frustum planes only in non-stretch mode (wide-
		// screen and non-wide-screen modes have identical resolution parameters
		// and only their scissor box differs)
		float correction = windowAR / viewableAreaAR;
		m_planes.correction = 1.0f / correction;

		vp->x		= 0;
		vp->y		= m_yOffs + (int)((384.0f - (vp->vpY + vp->vpHeight))*m_yRatio);
		vp->width	= m_totalXRes;
		vp->height = (int)(vp->vpHeight*m_yRatio);

		vp->projectionMatrix.FrustumRZ(l*correction, r*correction, b, t, NEAR_PLANE);
	}
	else {

		vp->x		= m_xOffs + (int)(vp->vpX*m_xRatio);
		vp->y		= m_yOffs + (int)((384.0f - (vp->vpY + vp->vpHeight))*m_yRatio);
		vp->width	= (int)(vp->vpWidth*m_xRatio);
		vp->height	= (int)(vp->vpHeight*m_yRatio);

		vp->projectionMatrix.FrustumRZ(l, r, b, t, NEAR_PLANE);
	}
}

void CNew3D::TranslateTexture(unsigned& x, unsigned& y, int width, int height, int& page) const
{
	page = y / 1024;

	// remove page from y coordinate
	y -= (page * 1024);
}

void CNew3D::SetSunClamp(bool enable)
{
	m_sunClamp = enable;
}

void CNew3D::SetBlockCulling(bool enable)
{
	m_blockCulling = enable;
}

bool CNew3D::CanDeferNativePreparation()
{
	// Only this owner writes the pending LOS buffer. Prepared headset redraws
	// do not change it. If the next swap publishes identical bits, postponing
	// that swap for one redraw cannot alter a concurrent PPC register read.
	// Release the short lock before any presentation or GL work.
	if (!m_preparedSceneValid) return false;
	std::lock_guard<std::mutex> guard(m_losMutex);
	return std::memcmp(m_losFront->value, m_losBack->value, sizeof(m_losFront->value)) == 0;
}

float CNew3D::GetLosValue(int layer)
{
	// we always write to the 'back' buffer, and the software reads from the front
	// then they get swapped
	std::lock_guard<std::mutex> guard(m_losMutex);
	return m_losFront->value[layer];
}

void CNew3D::TranslateLosPosition(int inX, int inY, int& outX, int& outY) const
{
	// remap real3d 496x384 to our new viewport
	inY = 384 - inY;

	outX = m_xOffs + int(inX * m_xRatio);
	outY = m_yOffs + int(inY * m_yRatio);
}

bool CNew3D::ProcessLos(int priority)
{
	for (const auto &n : m_nodes) {
		if (n.viewport.priority == priority) {
			if (n.viewport.losPosX || n.viewport.losPosY) {

				int losX, losY;
				TranslateLosPosition(n.viewport.losPosX, n.viewport.losPosY, losX, losY);

				float range[2];
				{
					LiveFrameTrace::OwnerRenderScope trace("prep.los_readback", m_sceneFrame, m_sceneEpoch);
					if (trace.Active()) { trace.c = priority; trace.d = 1; }
					glReadPixels(losX, losY, 1, 1, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, range);
				}
				GLubyte stencilVal = Util::FloatAsInt32(range[1]);

				float zVal = range[0] / NEAR_PLANE;

				// apply our mask to stencil, because layered poly attributes use the lower bits
				stencilVal &= 0x80;

				// if the stencil val is zero that means we've hit sky or whatever, if it hits a 1 we've hit geometry
				// the real3d returns 1 in the top bit of the float if the line of sight test passes (ie doesn't hit geometry)

				auto zValP = reinterpret_cast<unsigned char*>(&zVal);	// this is legal in c++, casting to int technically isn't

				if (stencilVal == 0) {
					zValP[0] |= 1;		// set first bit to 1
				}
				else {
					zValP[0] &= 0xFE;	// set first bit to zero
				}

				m_losBack->value[priority] = zVal;

				return true;
			}
		}
	}

	return false;
}

} // New3D
