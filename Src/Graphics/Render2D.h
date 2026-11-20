/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2012 Bart Trzynadlowski, Nik Henson
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
  * Render2D.h
  *
  * Header file defining the CRender2D class: OpenGL tile generator graphics.
  */

#ifndef INCLUDED_RENDER2D_H
#define INCLUDED_RENDER2D_H

#include <GL/glew.h>
#include <memory>
#include "Supermodel.h"
#include "Util/NewConfig.h"
#include "New3D/GLSLShader.h"
#include "FBO.h"
#include "../Model3/TileGenBuffer.h"
#include "TileUploadTracker.h"

  /*
   * CRender2D:
   *
   * Tile generator graphics engine. This must be constructed and initialized
   * before being attached to any objects that want to make use of it. Apart from
   * the constructor, all members assume that a global GL device
   * context is available and that GL functions may be called.
   */
class CRender2D
{
public:
	/*
	 * BeginFrame(void):
	 *
	 * Prepare to render a new frame. Must be called once per frame prior to
	 * drawing anything.
	 */
	void BeginFrame(void);

	/*
	 * PreRenderFrame(void):
	 *
	 * Draws the all top layers (above 3D graphics) and bottom layers (below 3D
	 * graphics) but does not yet display them. May send data to the GPU.
	 */
	void PreRenderFrame(void);

	// Native owner-thread upload scheduling. Headset preparation may postpone
	// the transfer until CPU scene decoding finishes, but never beyond EndFrame
	// or a draw-buffer replacement. Frozen presentation methods never upload.
	void SetDeferredUploads(bool enabled);
	void CompletePreparedLayers(void);

	/*
	 * RenderFrameBottom(void):
	 *
	 * Overwrites the color buffer with bottom surface that was pre-rendered by
	 * the last call to PreRenderFrame().
	 */
	void RenderFrameBottom(void);

	/*
	 * RenderFrameTop(void):
	 *
	 * Draws the top surface (if it exists) that was pre-rendered by the last
	 * call to PreRenderFrame(). Previously drawn graphics layers will be visible
	 * through transparent regions.
	 */
	void RenderFrameTop(void);

	// Draw a previously uploaded tile surface into an explicit eye target.
	// Does not clear, upload emulated memory, or select the native AA target.
	void DrawPreparedLayer(bool bottom, GLuint target, int x, int y, int width, int height, bool preserveAlpha = false);
	// Exact caller-owned state for native HUD assembly. This does not replace
	// the generic, state-preserving entry point above. Capture once before the
	// bottom layer, then derive the post-overlay state from the 3D compositor's
	// documented postconditions instead of querying immediately after its draws.
	struct PreparedLayerState {
		GLint framebuffer = 0, drawBuffer = GL_BACK, program = 0, vao = 0;
		GLint viewport[4]{}, scissorBox[4]{}, activeTexture = GL_TEXTURE0, texture = 0;
		GLint srcRGB = GL_SRC_ALPHA, dstRGB = GL_ONE_MINUS_SRC_ALPHA;
		GLint srcAlpha = GL_SRC_ALPHA, dstAlpha = GL_ONE_MINUS_SRC_ALPHA;
		GLint eqRGB = GL_FUNC_ADD, eqAlpha = GL_FUNC_ADD;
		GLboolean depth = GL_FALSE, cull = GL_FALSE, blend = GL_FALSE;
		GLboolean stencil = GL_FALSE, scissor = GL_FALSE;
		static PreparedLayerState Capture();
		void Restore() const;
		bool Equals(const PreparedLayerState& other) const;
	};
	void DrawPreparedLayerOwned(bool bottom, GLuint target, int x, int y, int width, int height,
		bool preserveAlpha, const PreparedLayerState& restoreState);
	bool HasPreparedLayers() const { return m_surfacePrepared[0] && m_surfacePrepared[1]; }

	/*
	 * EndFrame(void):
	 *
	 * Signals the end of rendering for this frame. Must be called last during
	 * the frame.
	 */
	void EndFrame(void);

	/*
	 * WriteVRAM(addr, data):
	 *
	 * Indicates what will be written next to the tile generator's RAM. The
	 * VRAM address must not have yet been updated, to allow the renderer to
	 * check for changes. Data is accepted in the same form as the tile
	 * generator: the MSB is what was written to addr+3. This function is
	 * intended to facilitate on-the-fly decoding of tiles and palette data.
	 *
	 * Parameters:
	 *    addr  Address in tile generator RAM. Caller must ensure it is
	 *          clamped to the range 0x000000 to 0x11FFFF because this
	 *          function does not.
	 *    data  The data to write.
	 */
	void WriteVRAM(unsigned addr, uint32_t data);

	/*
	 * AttachRegisters(regPtr):
	 *
	 * Attaches tile generator registers. This must be done prior to any
	 * rendering otherwise the program may crash with an access violation.
	 *
	 * Parameters:
	 *    regPtr  Pointer to the base of the tile generator registers. There
	 *        are assumed to be 64 in all.
	 */
	void AttachRegisters(const uint32_t* regPtr);

	/*
	 * AttachPalette(palPtr):
	 *
	 * Attaches tile generator palettes. This must be done prior to any
	 * rendering.
	 *
	 * Parameters:
	 *    palPtr  Pointer to two palettes. The first is for layers A/A' and
	 *        the second is for B/B'.
	 */
	void AttachPalette(const uint32_t* palPtr[2]);

	/*
	 * AttachVRAM(vramPtr):
	 *
	 * Attaches tile generator RAM. This must be done prior to any rendering
	 * otherwise the program may crash with an access violation.
	 *
	 * Parameters:
	 *    vramPtr   Pointer to the base of the tile generator RAM (0x120000
	 *              bytes). VRAM is assumed to be in little endian format.
	 */
	void AttachVRAM(const uint8_t* vramPtr);

	/*
	* AttachDrawBuffers(bottom,top):
	*
	* The tilegen is double buffered (for threading). We attach the current draw buffer for drawing.
	*
	* Parameters:
	*    bottom		Bottom surface
	*    top		Top surface	
	*/
	void AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top);

	/*
	 * Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes);
	 *
	 * One-time initialization of the context. Must be called before any other
	 * members (meaning it should be called even before being attached to any
	 * other objects that want to use it).
	 *
	 * Parameters:
	 *    xOffset   X offset of the viewable area within OpenGL display
	 *              surface, in pixels.
	 *    yOffset   Y offset.
	 *    xRes      Horizontal resolution of the viewable area.
	 *    yRes      Vertical resolution.
	 *    totalXRes Horizontal resolution of the complete display area.
	 *    totalYRes Vertical resolution.
	 *
	 * Returns:
	 *    OKAY is successful, otherwise FAILED if a non-recoverable error
	 *    occurred. Prints own error messages.
	 */
	Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget, UpscaleMode upscaleMode);

	/*
	 * CRender2D(config):
	 * ~CRender2D(void):
	 *
	 * Constructor and destructor.
	 *
	 * Parameters:
	 *    config  Run-time configuration.
	 */
	CRender2D(const Util::Config::Node& config);
	~CRender2D(void);

private:

	void	Setup2D			(bool isBottom);
	void	DrawSurface		(GLuint textureID);

	// Run-time configuration
	const Util::Config::Node& m_config;

	// OpenGL data
	unsigned  m_xPixels = 496;  // display surface resolution
	unsigned  m_yPixels = 384;  // ...
	unsigned  m_xOffset = 0;    // offset
	unsigned  m_yOffset = 0;
	unsigned  m_totalXPixels = 0;   // total display surface resolution
	unsigned  m_totalYPixels = 0;
	unsigned  m_correction = 0;
	GLuint m_aaTarget = 0;
	UpscaleMode m_upscaleMode = UpscaleMode::Biquintic;

	GLuint m_vao;
	GLuint m_textureIDs[2];
	bool m_surfacePrepared[2] = { false, false };
	GLSLShader m_drawShader;
	std::shared_ptr<TileGenBuffer> m_drawBuffers[2];
	TileUploadTracker m_tileUploadTracker[2]; // append only: preserve all existing member offsets
	bool m_deferTileUploads = false;
	bool m_tileUploadPending = false;
};


#endif  // INCLUDED_RENDER2D_H
