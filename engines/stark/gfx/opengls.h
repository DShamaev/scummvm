/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef STARK_GFX_OPENGLS_H
#define STARK_GFX_OPENGLS_H

#include "common/system.h"

#if defined(USE_OPENGL_SHADERS)

#include "engines/stark/gfx/driver.h"

#include "graphics/opengl/system_headers.h"

namespace OpenGL {
class Shader;
}

namespace Stark {
namespace Gfx {

class OpenGLSDriver : public Driver {
public:
	OpenGLSDriver();
	~OpenGLSDriver();

	void init() override;

	void setScreenViewport(bool noScaling) override;
	void setViewport(const Common::Rect &rect) override;

	void clearScreen() override;
	void flipBuffer() override;

	Texture *createTexture() override;
	Bitmap *createBitmap(const Graphics::Surface *surface = nullptr, const byte *palette = nullptr) override;
	VisualActor *createActorRenderer() override;
	VisualProp *createPropRenderer() override;
	SurfaceRenderer *createSurfaceRenderer() override;
	FadeRenderer *createFadeRenderer() override;

	OpenGL::Shader *createActorShaderInstance();
	OpenGL::Shader *createSurfaceShaderInstance();
	OpenGL::Shader *createSurfaceDepthShaderInstance();
	OpenGL::Shader *createSurfaceFillShaderInstance();
	OpenGL::Shader *createFadeShaderInstance();
	OpenGL::Shader *createShadowShaderInstance();

	void start2DMode();
	void end2DMode();
	void set3DMode() override;
	bool computeLightsEnabled() override;

	/**
	 * Post-processing: render the frame into an offscreen buffer, then
	 * composite it to the screen through the post-process shader.
	 * beginPostProcess binds the buffer; endPostProcess resolves it.
	 * Returns false if post-processing is unavailable or disabled, in
	 * which case rendering proceeds directly to the screen as before.
	 */
	bool beginPostProcess();
	void endPostProcess();
	void applyPostProcess() override;

	/**
	 * Register the current location's background depth-mask texture (a normal
	 * texture, already loaded for depth occlusion) so the post pass can sample
	 * it for SSAO / depth-of-field - avoiding a GL depth-buffer copy, which
	 * hangs on Apple's GL-over-Metal stack.
	 */
	void setWorldDepth(const Bitmap *depth, float zMin, float zMax);

	void getPostDepthState(bool &glDepthCopy, bool &worldMask, bool &contactMode) const override;

	/** Record a sprite depth-stamp (eye-space) this frame, for diagnostics. */
	void recordSpriteStamp(float eyeDepth);
	/** Report last frame's stamp count and eye-depth range (postInfo). */
	void getSpriteStampInfo(int &count, float &minEye, float &maxEye) const override;

	Common::Rect getViewport() const;
	Common::Rect getUnscaledViewport() const;

	Graphics::Surface *getViewportScreenshot() const override;

private:
	Common::Rect _viewport;
	Common::Rect _unscaledViewport;

	OpenGL::Shader *_surfaceShader;
	OpenGL::Shader *_surfaceDepthShader;
	OpenGL::Shader *_surfaceFillShader;
	OpenGL::Shader *_actorShader;
	OpenGL::Shader *_fadeShader;
	OpenGL::Shader *_shadowShader;
	GLuint _surfaceVBO;
	GLuint _fadeVBO;

	// Post-processing offscreen buffer
	OpenGL::Shader *_postShader;
	GLuint _postVBO;
	GLuint _postFBO;
	GLuint _postColorTex;
	GLuint _postDepthRBO;
	int _postWidth;
	int _postHeight;
	bool _postActive;
	int _renderScale;   // supersample factor for the in-game FBO (1 = off)

	// Copy-path post-processing + detail magnifier. Instead of rendering the
	// scene into an FBO (which hangs on Apple's GL-over-Metal stack), the frame
	// is drawn normally then copied from the back buffer into these textures and
	// run through the post shader. Depth is copied too for depth-of-field, but
	// only if this GL stack accepts the copy - otherwise DoF disables itself.
	GLuint _magTex;
	int _magWidth;
	int _magHeight;

	// The current location's background depth-mask texture (owned elsewhere),
	// sampled by SSAO / DoF. Reset each frame; only set when a depth map exists.
	const Bitmap *_worldDepthBitmap;
	float _worldDepthZMin;
	float _worldDepthZMax;
	// Optional GL depth-buffer copy (real depth, includes the character), used
	// when 'enable_depth_copy' is on and the stack accepts it.
	GLuint _postDepthCopyTex;

	// Diagnostics: state of the last post pass' depth setup (read by postInfo).
	bool _postDbgGLDepth;
	bool _postDbgWorldMask;
	bool _postDbgContact;

	// Diagnostics: foreground sprite depth stamps this frame.
	int _spriteStampCount;
	int _spriteStampCountFrame;   // accumulates during a frame, published on clear
	float _spriteStampMinEye;
	float _spriteStampMaxEye;
	float _spriteStampMinEyeFrame;
	float _spriteStampMaxEyeFrame;

	void ensurePostResources(int width, int height);
	void freePostResources();
};

} // End of namespace Gfx
} // End of namespace Stark

#endif // defined(USE_OPENGL_SHADERS)

#endif // STARK_GFX_OPENGLS_H
