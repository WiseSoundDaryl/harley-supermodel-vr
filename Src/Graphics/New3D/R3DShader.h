#ifndef _R3DSHADER_H_
#define _R3DSHADER_H_

#include <GL/glew.h>
#include "Util/NewConfig.h"
#include "Model.h"
#include <map>
#include <string>

namespace New3D {

class R3DShader
{
public:
	R3DShader(const Util::Config::Node &config);

	bool	LoadShader			(const char* vertexShader = nullptr, const char* fragmentShader = nullptr);
	void	UnloadShader		();
	void	SetMeshUniforms		(const Mesh* m);
	void	SetModelStates		(const Model* model, const float* matrixOverride = nullptr);
	void	SetViewportUniforms	(const Viewport *vp);
	void	Start				();
	void	SetShader			(bool enable = true);
	GLint	GetVertexAttribPos	(const std::string& attrib);
	void	DiscardAlpha		(bool discard);				// use to remove alpha from texture alpha only polys for 1st pass
	void	SetLayer			(Layer layer);
	// Optional owner-thread headset-world mask; zero preserves every fragment.
	void SetVisibilityTexture(GLuint texture, GLuint sampler = 0) { m_visibilityTexture = texture; m_visibilityRestoreSampler = sampler; }
	bool VisibilityMaskSupported() const { return m_locVisibilityEnabled >= 0; }


private:

	void PrintShaderResult(GLuint shader);
	void PrintProgramResult(GLuint program);

	// run-time config
	const Util::Config::Node &m_config;

	GLint m_locVisibilitySampler = -1, m_locVisibilityEnabled = -1;
	GLuint m_visibilityTexture = 0, m_visibilityRestoreSampler = 0;
	bool m_visibilityCompileDisabled = false;

	// shader IDs
	GLuint m_shaderProgram;
	GLuint m_vertexShader;
	GLuint m_geoShader;
	GLuint m_fragmentShader;

	// mesh uniform locations
	GLint m_locTextureBank[2];		// 2 banks
	GLint m_locTexture1Enabled;		// base texture
	GLint m_locTexture2Enabled;		// micro texture
	GLint m_locTexturePage;
	GLint m_locTextureAlpha;
	GLint m_locAlphaTest;
	GLint m_locMicroTexMinLOD;
	GLint m_locMicroTexID;
	GLint m_locBaseTexInfo;
	GLint m_locBaseTexType;
	GLint m_locTextureInverted;
	GLint m_locTexWrapMode;
	GLint m_locTranslatorMap;
	GLint m_locColourLayer;
	GLint m_locPolyAlpha;

	// cached mesh values
	bool	m_textured1;
	bool	m_textured2;
	bool	m_textureAlpha;		// use alpha in texture
	bool	m_alphaTest;		// discard fragment based on alpha (ogl does this with fixed function)
	float	m_fogIntensity;
	bool	m_lightEnabled;
	float	m_shininess;
	float	m_specularValue;
	bool	m_specularEnabled;
	bool	m_fixedShading;
	bool	m_smoothShading;
	bool	m_translatorMap;
	bool	m_polyAlpha;
	int		m_texturePage;

	bool	m_layered;
	bool	m_noLosReturn;
	float	m_microTexMinLOD;
	int		m_microTexID;
	int		m_baseTexInfo[4];
	int		m_baseTexType;
	int		m_texWrapMode[2];
	bool	m_textureInverted;

	// cached model values
	float	m_modelScale;
	float	m_nodeAlpha;
	int		m_transX;
	int		m_transY;
	int		m_transPage;

	// are our cache values dirty
	bool	m_dirtyMesh;
	bool	m_dirtyModel;

	// Only exact uniform payloads are cached: Viewport also contains unrelated
	// state and padding. A shader bind/relink invalidates every viewport value.
	struct ViewportUniformCache
	{
		float fogDensity, fogStart, fogColour[3], fogAttenuation, fogAmbient;
		float lighting[6];
		GLint sunClamp, intensityClamp;
		float spotEllipse[4], spotRange[2], spotColor[3], spotFogColor[3];
		float projection[16];
		GLint hardwareStep;
		float cota;
	} m_viewportUniforms;
	bool m_dirtyViewport;

	template<typename T> bool ViewportUniformChanged(T& cached, const T& incoming)
	{
		// Byte comparison preserves signed zero and NaN payload changes. The
		// first call never reads uninitialized cache storage.
		if (!m_dirtyViewport && std::memcmp(&cached, &incoming, sizeof(T)) == 0) return false;
		std::memcpy(&cached, &incoming, sizeof(T));
		return true;
	}

	// viewport uniform locations
	GLint m_locFogIntensity;
	GLint m_locFogDensity;
	GLint m_locFogStart;
	GLint m_locFogColour;
	GLint m_locFogAttenuation;
	GLint m_locFogAmbient;
	GLint m_locProjMat;
	GLint m_locCota;

	// lighting / other
	GLint m_locLighting;
	GLint m_locLightEnabled;
	GLint m_locSunClamp;
	GLint m_locIntensityClamp;
	GLint m_locShininess;
	GLint m_locSpecularValue;
	GLint m_locSpecularEnabled;
	GLint m_locFixedShading;
	GLint m_locSmoothShading;

	GLint m_locSpotEllipse;
	GLint m_locSpotRange;
	GLint m_locSpotColor;
	GLint m_locSpotFogColor;

	// model uniforms
	GLint m_locModelScale;
	GLint m_locNodeAlpha;
	GLint m_locModelMat;

	// global uniforms
	GLint m_locHardwareStep;
	GLint m_locDiscardAlpha;

	// vertex attribute position cache
	std::map<std::string, GLint> m_vertexLocCache;

};


} // New3D

#endif
