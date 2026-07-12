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

#include "math/matrix4.h"

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
	OpenGL::Shader *createShadowMapShaderInstance();

	/**
	 * Shadow mapping. renderShadowMapBegin binds an offscreen buffer and returns
	 * the size to render the caster into (depth-encoded-in-colour); the caller
	 * draws the caster with the shadow-map shader, then renderShadowMapEnd stores
	 * the result + light matrix and restores the engine framebuffer. Receivers
	 * read getShadowMapTexture()/getShadowLightViewProj(). No-op / invalid when
	 * shadow mapping is disabled or the FBO is incomplete.
	 */
	int renderShadowMapBegin();
	void renderShadowMapEnd(const Math::Matrix4 &lightViewProj);
	GLuint getShadowMapTexture() const { return _shadowValid ? _shadowTex : 0; }
	Math::Matrix4 getShadowLightViewProj() const { return _shadowLightVP; }
	bool isShadowMapValid() const { return _shadowValid; }
	/** Debug: draw the shadow map to the screen corner (shadow_map_debug). */
	void debugDrawShadowMap();

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
	bool beginPostProcess() override;
	void endPostProcess() override;
	void applyPostProcess() override;

	/**
	 * Supersampling (SSAA): beginPostProcess redirects the whole frame into an
	 * offscreen buffer rendered at _renderScale x, and resolveSupersample
	 * downsamples it back into the engine's framebuffer (the anti-aliasing).
	 * Both no-op when the 'supersample' setting is 100 (off).
	 */
	void resolveSupersample() override;

	/**
	 * Register the current location's background depth-mask texture (a normal
	 * texture, already loaded for depth occlusion) so the post pass can sample
	 * it for SSAO / depth-of-field - avoiding a GL depth-buffer copy, which
	 * hangs on Apple's GL-over-Metal stack.
	 */
	void setWorldDepth(const Bitmap *depth, float zMin, float zMax);

	void getPostDepthState(bool &glDepthCopy, bool &worldMask, bool &contactMode) const override;

	Common::String testFramebuffer() override;

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
	OpenGL::Shader *_shadowMapShader;

	// Shadow mapping: the caster (April) is rendered from the light into this
	// offscreen buffer as depth-encoded-in-colour, then receivers sample it.
	GLuint _shadowFbo;
	GLuint _shadowTex;
	GLuint _shadowDepthRBO;
	int _shadowSize;
	Math::Matrix4 _shadowLightVP;
	bool _shadowValid;
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
	GLint _sceneFbo;    // the engine framebuffer to resolve the supersampled frame back into
	bool _frameSupersampled;  // this frame was rendered into _postFBO at _renderScale x

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

	// High-quality bloom: bright-pass + separable Gaussian at half resolution in
	// an FBO (now that FBOs work on this stack), for a wide, smooth bloom instead
	// of the single-pass inline version.
	OpenGL::Shader *_blurShader;
	GLuint _bloomFbo;       // shared half-res FBO for the bloom and AO passes
	GLuint _bloomTexA;
	GLuint _bloomTexB;
	int _bloomW;
	int _bloomH;

	// High-quality SSAO: compute AO into its own half-res buffer and blur it
	// (denoise), so it can be pushed stronger/wider without speckle.
	OpenGL::Shader *_ssaoShader;
	GLuint _aoTexA;
	GLuint _aoTexB;

	// High-quality depth-of-field: a pre-blurred copy of the scene at a FIXED
	// coarse resolution (independent of the physical/Retina viewport) that the
	// composite cross-fades toward by circle-of-confusion, giving smooth bokeh
	// instead of the harsh single-pass ring over the sharp image.
	GLuint _dofTexA;
	GLuint _dofTexB;
	int _dofW;
	int _dofH;

	// The framebuffer the engine is actually rendering into at the start of the
	// post pass. ScummVM's OpenGL backend renders the game into its OWN FBO, not
	// the window default (0), so all the post/FBO passes must restore to THIS,
	// never a hardcoded 0 - otherwise the composite lands on the wrong target and
	// is discarded (every post effect + debug view silently does nothing).
	GLint _postDrawFbo;

	// Render one fullscreen pass with _blurShader from srcTex into dstTex (bound
	// to _bloomFbo), at w x h. mode 1 = bright-pass.
	void blurPass(GLuint srcTex, GLuint dstTex, int w, int h, float dirX, float dirY, float mode, float threshold);
	// Build the bloom texture (_bloomTexA) from the copied scene (_magTex).
	void buildBloom(int vw, int vh, float threshold);
	// Build the denoised AO texture (_aoTexA). Depth must already be bound/decided.
	void buildSSAO(int vw, int vh, float radius, float dynamicOnly);
	// Build the pre-blurred scene copy (_dofTexA) for depth-of-field.
	void buildDoF(int vw, int vh, int iterations);
	// Ensure the half-res ping-pong textures exist at vw/2 x vh/2.
	void ensureHalfResTargets(int vw, int vh);

	void ensurePostResources(int width, int height);
	void freePostResources();
};

} // End of namespace Gfx
} // End of namespace Stark

#endif // defined(USE_OPENGL_SHADERS)

#endif // STARK_GFX_OPENGLS_H
