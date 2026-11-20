#ifndef FBO_H
#define FBO_H

#include <GL/glew.h>
#include "GLSLShader.h"
#include "Model.h"

namespace New3D {

class R3DFrameBuffers {

public:
	R3DFrameBuffers();
	~R3DFrameBuffers();

	void	Draw(bool preserveAlpha = false);	// composite; optional premultiplied RGBA destination
	
	Result	CreateFBO(int width, int height);
	void	DestroyFBO();

	void	BindTexture(Layer layer);
	void	SetFBO(Layer layer);
	void	StoreDepth();
	void	RestoreDepth();
	GLuint ColourTexture() const { return m_texIDs[0]; }
	// Valid as a complete presentation depth only when the caller certifies
	// one world priority without high-priority overlays. StoreDepth retains
	// opaque depth before either transparency layer changes the working depth.
	GLuint OpaqueDepthRenderbuffer() const { return m_renderBufferIDCopy; }

private:

	Result	CreateFBODepthCopy(int width, int height);
	GLuint	CreateTexture(int width, int height);
	void	AllocShaderTrans();
	void	AllocShaderBase();

	void	DrawBaseLayer();
	void	DrawAlphaLayer();

	GLuint m_frameBufferID;
	GLuint m_renderBufferID;
	GLuint m_texIDs[3];
	GLuint m_frameBufferIDCopy;
	GLuint m_renderBufferIDCopy;
	Layer m_lastLayer;
	int m_width;
	int m_height;

	// shaders
	GLSLShader m_shaderBase;
	GLSLShader m_shaderTrans;

	// vao
	GLuint m_vao;	// this really needed if we don't actually use vertex attribs?
};

}

#endif
