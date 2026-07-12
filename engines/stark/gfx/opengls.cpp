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

#include "engines/stark/gfx/opengls.h"

#include "common/config-manager.h"
#include "common/system.h"
#include "common/util.h"
#include "common/events.h"

#include "math/matrix4.h"
#include "math/vector2d.h"
#include "math/vector3d.h"

#if defined(USE_OPENGL_SHADERS)

#include "engines/stark/scene.h"
#include "engines/stark/services/services.h"

#include "engines/stark/gfx/openglsactor.h"
#include "engines/stark/gfx/openglbitmap.h"
#include "engines/stark/gfx/openglsprop.h"
#include "engines/stark/gfx/openglssurface.h"
#include "engines/stark/gfx/openglsfade.h"
#include "engines/stark/gfx/opengltexture.h"

#include "graphics/surface.h"
#include "graphics/opengl/shader.h"

namespace Stark {
namespace Gfx {

static const GLfloat surfaceVertices[] = {
	// XS   YT
	0.0f, 0.0f,
	1.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
};

static const GLfloat fadeVertices[] = {
	// XS   YT
	-1.0f,  1.0f,
	 1.0f,  1.0f,
	-1.0f, -1.0f,
	 1.0f, -1.0f,
};

// Fullscreen quad for post-processing: position (NDC) + texcoord
static const GLfloat postVertices[] = {
	// X      Y      U     V
	-1.0f,  1.0f,  0.0f, 1.0f,
	 1.0f,  1.0f,  1.0f, 1.0f,
	-1.0f, -1.0f,  0.0f, 0.0f,
	 1.0f, -1.0f,  1.0f, 0.0f,
};

OpenGLSDriver::OpenGLSDriver() :
	_surfaceShader(nullptr),
	_surfaceDepthShader(nullptr),
	_surfaceFillShader(nullptr),
	_actorShader(nullptr),
	_fadeShader(nullptr),
	_shadowShader(nullptr),
	_surfaceVBO(0),
	_fadeVBO(0),
	_postShader(nullptr),
	_postVBO(0),
	_postFBO(0),
	_postColorTex(0),
	_postDepthRBO(0),
	_postWidth(0),
	_postHeight(0),
	_postActive(false),
	_renderScale(1),
	_magTex(0),
	_magWidth(0),
	_magHeight(0),
	_worldDepthBitmap(nullptr),
	_worldDepthZMin(0.0f),
	_worldDepthZMax(0.0f),
	_postDepthCopyTex(0),
	_postDbgGLDepth(false),
	_postDbgWorldMask(false),
	_postDbgContact(false),
	_spriteStampCount(0),
	_spriteStampCountFrame(0),
	_spriteStampMinEye(0.0f),
	_spriteStampMaxEye(0.0f),
	_spriteStampMinEyeFrame(0.0f),
	_spriteStampMaxEyeFrame(0.0f) {
}

OpenGLSDriver::~OpenGLSDriver() {
	OpenGL::Shader::freeBuffer(_surfaceVBO);
	OpenGL::Shader::freeBuffer(_fadeVBO);
	OpenGL::Shader::freeBuffer(_postVBO);
	freePostResources();
	if (_magTex) { glDeleteTextures(1, &_magTex); _magTex = 0; }
	if (_postDepthCopyTex) { glDeleteTextures(1, &_postDepthCopyTex); _postDepthCopyTex = 0; }
	delete _postShader;
	delete _surfaceFillShader;
	delete _surfaceDepthShader;
	delete _surfaceShader;
	delete _actorShader;
	delete _fadeShader;
	delete _shadowShader;
}

void OpenGLSDriver::init() {
	computeScreenViewport();

	static const char* attributes[] = { "position", "texcoord", nullptr };
	_surfaceShader = OpenGL::Shader::fromFiles("stark_surface", attributes);
	_surfaceVBO = OpenGL::Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(surfaceVertices), surfaceVertices);
	_surfaceShader->enableVertexAttribute("position", _surfaceVBO, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(float), 0);
	_surfaceShader->enableVertexAttribute("texcoord", _surfaceVBO, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(float), 0);

	_surfaceDepthShader = OpenGL::Shader::fromFiles("stark_surface_depth", attributes);
	_surfaceDepthShader->enableVertexAttribute("position", _surfaceVBO, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(float), 0);
	_surfaceDepthShader->enableVertexAttribute("texcoord", _surfaceVBO, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(float), 0);

	static const char* fillAttributes[] = { "position", nullptr };
	_surfaceFillShader = OpenGL::Shader::fromFiles("stark_surface_fill", fillAttributes);
	_surfaceFillShader->enableVertexAttribute("position", _surfaceVBO, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(float), 0);

	static const char* actorAttributes[] = { "position1", "position2", "bone1", "bone2", "boneWeight", "normal", "texcoord", nullptr };
	_actorShader = OpenGL::Shader::fromFiles("stark_actor", actorAttributes);

	static const char* shadowAttributes[] = { "position1", "position2", "bone1", "bone2", "boneWeight", nullptr };
	_shadowShader = OpenGL::Shader::fromFiles("stark_shadow", shadowAttributes);

	static const char* fadeAttributes[] = { "position", nullptr };
	_fadeShader = OpenGL::Shader::fromFiles("stark_fade", fadeAttributes);
	_fadeVBO = OpenGL::Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(fadeVertices), fadeVertices);
	_fadeShader->enableVertexAttribute("position", _fadeVBO, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(float), 0);

	static const char* postAttributes[] = { "position", "texcoord", nullptr };
	_postShader = OpenGL::Shader::fromFiles("stark_post", postAttributes);
	_postVBO = OpenGL::Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(postVertices), postVertices);
	_postShader->enableVertexAttribute("position", _postVBO, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	_postShader->enableVertexAttribute("texcoord", _postVBO, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 2 * sizeof(float));

	// Diagnostic: report the OpenGL context SDL/macOS actually handed us, so we
	// can tell whether we are on the legacy 2.1 stack (GLSL 1.20) or a modern
	// core profile - which determines whether an "OpenGL upgrade" is even an
	// option here or would require porting every shader.
	const char *glVer = (const char *)glGetString(GL_VERSION);
	const char *glslVer = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	const char *glRend = (const char *)glGetString(GL_RENDERER);
	warning("Stark: OpenGL %s | GLSL %s | renderer %s",
	        glVer ? glVer : "?", glslVer ? glslVer : "?", glRend ? glRend : "?");
}

void OpenGLSDriver::setScreenViewport(bool noScaling) {
	if (noScaling) {
		_viewport = Common::Rect(g_system->getWidth(), g_system->getHeight());
		_unscaledViewport = _viewport;
	} else {
		_viewport = _screenViewport;
		_unscaledViewport = Common::Rect(kOriginalWidth, kOriginalHeight);
	}

	int rs = _postActive ? _renderScale : 1;
	glViewport(_viewport.left * rs, _viewport.top * rs, _viewport.width() * rs, _viewport.height() * rs);
}

void OpenGLSDriver::setViewport(const Common::Rect &rect) {
	_viewport = Common::Rect(
			_screenViewport.width() * rect.width() / kOriginalWidth,
			_screenViewport.height() * rect.height() / kOriginalHeight
			);

	_viewport.translate(
			_screenViewport.left + _screenViewport.width() * rect.left / kOriginalWidth,
			_screenViewport.top + _screenViewport.height() * rect.top / kOriginalHeight
			);

	_unscaledViewport = rect;

	int rs = _postActive ? _renderScale : 1;
	glViewport(_viewport.left * rs, (g_system->getHeight() - _viewport.bottom) * rs,
	           _viewport.width() * rs, _viewport.height() * rs);
}

void OpenGLSDriver::clearScreen() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	// The depth mask is re-registered when this frame's background renders; clear
	// it so a location without a depth map doesn't reuse the previous one.
	_worldDepthBitmap = nullptr;

	// Publish the previous frame's sprite-stamp diagnostics, then reset for this
	// frame's stamp pass (which runs later, during the game window render).
	_spriteStampCount = _spriteStampCountFrame;
	_spriteStampMinEye = _spriteStampMinEyeFrame;
	_spriteStampMaxEye = _spriteStampMaxEyeFrame;
	_spriteStampCountFrame = 0;
	_spriteStampMinEyeFrame = 0.0f;
	_spriteStampMaxEyeFrame = 0.0f;
}

void OpenGLSDriver::recordSpriteStamp(float eyeDepth) {
	if (_spriteStampCountFrame == 0) {
		_spriteStampMinEyeFrame = eyeDepth;
		_spriteStampMaxEyeFrame = eyeDepth;
	} else {
		_spriteStampMinEyeFrame = MIN(_spriteStampMinEyeFrame, eyeDepth);
		_spriteStampMaxEyeFrame = MAX(_spriteStampMaxEyeFrame, eyeDepth);
	}
	_spriteStampCountFrame++;
}

void OpenGLSDriver::getSpriteStampInfo(int &count, float &minEye, float &maxEye) const {
	count = _spriteStampCount;
	minEye = _spriteStampMinEye;
	maxEye = _spriteStampMaxEye;
}

void OpenGLSDriver::setWorldDepth(const Bitmap *depth, float zMin, float zMax) {
	_worldDepthBitmap = depth;
	_worldDepthZMin = zMin;
	_worldDepthZMax = zMax;
}

void OpenGLSDriver::getPostDepthState(bool &glDepthCopy, bool &worldMask, bool &contactMode) const {
	glDepthCopy = _postDbgGLDepth;
	worldMask   = _postDbgWorldMask;
	contactMode = _postDbgContact;
}

Common::String OpenGLSDriver::testFramebuffer() {
	// 1) Create an FBO with a colour texture attachment.
	GLuint fbo = 0, tex = 0;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

	// Save state we are about to change, so the diagnostic doesn't disturb the
	// live frame (else the screen shows a stray viewport / clear colour).
	GLint savedViewport[4] = { 0, 0, 0, 0 };
	GLfloat savedClear[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_VIEWPORT, savedViewport);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, savedClear);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	Common::String result;
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		result = Common::String::format("FBO INCOMPLETE (status 0x%04x) - cannot use FBOs", (uint)status);
	} else {
		// 2) Render into it: a clear, then the textured post quad (the operation
		//    that historically hung). readback forces the GPU to actually finish.
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glViewport(0, 0, 64, 64);
		glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		bool drewQuad = false;
		if (_postShader && _magTex) {
			_postShader->use();
			_postShader->setUniform("sceneTex", 0);
			_postShader->setUniform1f("magnify", 1.0f);
			_postShader->setUniform1f("debugView", 0.0f);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, _magTex);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			glBindTexture(GL_TEXTURE_2D, 0);
			_postShader->unbind();
			drewQuad = true;
		}

		unsigned char px[4] = { 0, 0, 0, 0 };
		glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		GLenum err = glGetError();
		result = Common::String::format(
				"FBO OK - render+readback returned (%d,%d,%d,%d)%s glError=0x%04x. "
				"If you can read this, the FBO path did NOT hang.",
				px[0], px[1], px[2], px[3], drewQuad ? " [drew post quad]" : " [clear only]", (uint)err);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &tex);

	// Restore the state we touched so the live frame is unaffected.
	glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
	glClearColor(savedClear[0], savedClear[1], savedClear[2], savedClear[3]);
	return result;
}

void OpenGLSDriver::flipBuffer() {
	g_system->updateScreen();
}

void OpenGLSDriver::ensurePostResources(int width, int height) {
	if (_postFBO && _postWidth == width && _postHeight == height) {
		return;
	}

	freePostResources();
	_postWidth = width;
	_postHeight = height;

	glGenFramebuffers(1, &_postFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, _postFBO);

	glGenTextures(1, &_postColorTex);
	glBindTexture(GL_TEXTURE_2D, _postColorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _postColorTex, 0);

	// Depth + stencil renderbuffer (not a texture): the depth-stencil texture
	// path hangs on the macOS GL stack. This is the proven, portable setup.
	glGenRenderbuffers(1, &_postDepthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, _postDepthRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _postDepthRBO);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _postDepthRBO);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		warning("Post-processing framebuffer incomplete, disabling");
		freePostResources();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLSDriver::freePostResources() {
	if (_postColorTex) { glDeleteTextures(1, &_postColorTex); _postColorTex = 0; }
	if (_postDepthRBO) { glDeleteRenderbuffers(1, &_postDepthRBO); _postDepthRBO = 0; }
	if (_postFBO)      { glDeleteFramebuffers(1, &_postFBO); _postFBO = 0; }
	_postWidth = _postHeight = 0;
}

bool OpenGLSDriver::beginPostProcess() {
	// Rendering the scene into an FBO hangs on Apple's GL-over-Metal stack, so
	// the scene now always renders straight to the back buffer; post-processing
	// is applied afterwards from a back-buffer copy (see applyPostProcess).
	return false;
}

void OpenGLSDriver::endPostProcess() {
	if (!_postActive) {
		return;
	}
	_postActive = false;

	// Composite the offscreen frame to the screen through the post shader
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, g_system->getWidth(), g_system->getHeight());

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);

	_postShader->use();
	_postShader->setUniform("sceneTex", 0);
	_postShader->setUniform("texelSize", Math::Vector2d(1.0f / _postWidth, 1.0f / _postHeight));
	_postShader->setUniform1f("time", (g_system->getMillis() % 10000) / 1000.0f);

	// Detail magnifier: zoom into the (supersampled) frame around the cursor so
	// fine geometry - e.g. skinning spikes - can be inspected. 100 = off.
	float magnify = CLIP(ConfMan.hasKey("magnify") ? ConfMan.getInt("magnify") : 100, 100, 800) / 100.0f;
	_postShader->setUniform1f("magnify", magnify);

	// Zoom centre follows the mouse cursor (UV, y flipped to texture space).
	Common::Point mouse = g_system->getEventManager()->getMousePos();
	float cx = CLIP((float)mouse.x / (float)g_system->getWidth(), 0.0f, 1.0f);
	float cy = 1.0f - CLIP((float)mouse.y / (float)g_system->getHeight(), 0.0f, 1.0f);
	_postShader->setUniform("magnifyCenter", Math::Vector2d(cx, cy));

	// Color grading
	_postShader->setUniform1f("gradeBrightness", ConfMan.getInt("grade_brightness") / 100.0f);
	_postShader->setUniform1f("gradeContrast", ConfMan.getInt("grade_contrast") / 100.0f);
	_postShader->setUniform1f("gradeSaturation", ConfMan.getInt("grade_saturation") / 100.0f);
	_postShader->setUniform("gradeTint", Math::Vector3d(
			ConfMan.getInt("grade_tint_r") / 100.0f,
			ConfMan.getInt("grade_tint_g") / 100.0f,
			ConfMan.getInt("grade_tint_b") / 100.0f));

	// Effects
	_postShader->setUniform1f("vignetteStrength", ConfMan.getInt("vignette_strength") / 100.0f);
	_postShader->setUniform1f("grainStrength", ConfMan.getInt("grain_strength") / 100.0f);
	_postShader->setUniform1f("sharpenStrength", ConfMan.getInt("sharpen_strength") / 100.0f);

	// Depth of field is disabled: sampling the FBO depth attachment hangs on
	// this GL stack. Kept as a no-op uniform until an MRT depth pass is added.
	_postShader->setUniform1f("dofStrength", 0.0f);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _postColorTex);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindTexture(GL_TEXTURE_2D, 0);

	_postShader->unbind();

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void OpenGLSDriver::applyPostProcess() {
	int magnifyPercent = ConfMan.hasKey("magnify") ? ConfMan.getInt("magnify") : 100;
	bool magnifying = magnifyPercent > 100;
	bool grading    = ConfMan.getBool("enable_post_processing");
	// Depth source for SSAO / DoF: the real GL depth buffer (includes the
	// character - best quality) when enabled and available, else the background
	// depth-mask texture the engine already loads (no character, but never hangs).
	bool useGLDepth = ConfMan.getBool("enable_depth_copy");
	bool haveDepth  = useGLDepth || _worldDepthBitmap != nullptr;
	bool dof        = ConfMan.getBool("enable_depth_of_field") && haveDepth;
	// Effective SSAO = per-scene base (from post_scenes.json, or the global
	// ssao_strength for un-analysed scenes) scaled by the master gain the menu
	// slider controls. The master always has an effect, even where a per-scene
	// value would otherwise fully override the global.
	int ssaoEff     = CLIP(StarkScene->getPostSetting("ssao_strength") *
	                       CLIP(ConfMan.getInt("ssao_master"), 0, 300) / 100, 0, 100);
	bool ssao       = ssaoEff > 0 && haveDepth;
	// The depth debug views need the depth source bound even when SSAO/DoF are
	// off, so they always reflect the true depth setup rather than stale/unbound
	// samplers (which read as "everything is background").
	int debugView   = CLIP(ConfMan.getInt("post_debug_view"), 0, 4);
	bool wantDepth  = dof || ssao || debugView > 0;

	// Nothing to apply: leave the already-rendered frame as-is.
	if (!magnifying && !grading && !dof && !ssao && debugView == 0) {
		return;
	}

	// Restrict to the 3D game viewport (exclude the top/bottom UI border strips)
	// and point the GL viewport at that region for the composite draw.
	setViewport(Common::Rect(0, Gfx::Driver::kTopBorderHeight,
	                         Gfx::Driver::kOriginalWidth,
	                         Gfx::Driver::kTopBorderHeight + Gfx::Driver::kGameViewportHeight));
	Common::Rect vp = _viewport;
	int vw = vp.width();
	int vh = vp.height();
	if (vw <= 0 || vh <= 0) {
		return;
	}
	int glY = g_system->getHeight() - vp.bottom; // GL framebuffer is bottom-left origin

	// (Re)create the colour capture texture when the region size changes.
	if (!_magTex || _magWidth != vw || _magHeight != vh) {
		if (_magTex) {
			glDeleteTextures(1, &_magTex);
		}
		glGenTextures(1, &_magTex);
		glBindTexture(GL_TEXTURE_2D, _magTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vw, vh, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		_magWidth = vw;
		_magHeight = vh;
	}

	// Copy the finished world region from the back buffer (no FBO bound).
	glBindTexture(GL_TEXTURE_2D, _magTex);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp.left, glY, vw, vh);

	// Optionally copy the real GL depth buffer (includes the 3D character) for
	// higher-quality SSAO/DoF. This is the operation that hung during earlier
	// tests - which were confounded by the pause-key bug, so it is worth retesting.
	if (wantDepth && useGLDepth) {
		if (!_postDepthCopyTex) {
			glGenTextures(1, &_postDepthCopyTex);
			glBindTexture(GL_TEXTURE_2D, _postDepthCopyTex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		glBindTexture(GL_TEXTURE_2D, _postDepthCopyTex);
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, vp.left, glY, vw, vh, 0);
	}

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);

	_postShader->use();
	_postShader->setUniform("sceneTex", 0);
	_postShader->setUniform("texelSize", Math::Vector2d(1.0f / vw, 1.0f / vh));
	_postShader->setUniform1f("time", (g_system->getMillis() % 10000) / 1000.0f);

	// Cursor-centred magnifier (1.0 = off), in region-local UV.
	_postShader->setUniform1f("magnify", CLIP(magnifyPercent, 100, 800) / 100.0f);
	Common::Point mouse = g_system->getEventManager()->getMousePos();
	float cx = CLIP((float)(mouse.x - vp.left) / (float)vw, 0.0f, 1.0f);
	float cy = 1.0f - CLIP((float)(mouse.y - vp.top) / (float)vh, 0.0f, 1.0f);
	_postShader->setUniform("magnifyCenter", Math::Vector2d(cx, cy));

	// Colour grade + screen effects. Values resolve through the per-scene helper
	// so they can be auto-tuned per background; neutral when post is off.
	if (grading) {
		// Master intensity: blend every graded value toward its neutral, so one
		// slider dials the whole look up or down (100 = per-scene as authored,
		// 0 = no grade). Bloom threshold is a cutoff, not scaled.
		float pm = CLIP(ConfMan.getInt("post_master"), 0, 200) / 100.0f;
		_postShader->setUniform1f("gradeBrightness", (StarkScene->getPostSetting("grade_brightness") / 100.0f) * pm);
		_postShader->setUniform1f("gradeContrast", 1.0f + (StarkScene->getPostSetting("grade_contrast") / 100.0f - 1.0f) * pm);
		_postShader->setUniform1f("gradeSaturation", 1.0f + (StarkScene->getPostSetting("grade_saturation") / 100.0f - 1.0f) * pm);
		_postShader->setUniform("gradeTint", Math::Vector3d(
				1.0f + (StarkScene->getPostSetting("grade_tint_r") / 100.0f - 1.0f) * pm,
				1.0f + (StarkScene->getPostSetting("grade_tint_g") / 100.0f - 1.0f) * pm,
				1.0f + (StarkScene->getPostSetting("grade_tint_b") / 100.0f - 1.0f) * pm));
		_postShader->setUniform1f("vignetteStrength", (StarkScene->getPostSetting("vignette_strength") / 100.0f) * pm);
		_postShader->setUniform1f("grainStrength", (StarkScene->getPostSetting("grain_strength") / 100.0f) * pm);
		_postShader->setUniform1f("sharpenStrength", (StarkScene->getPostSetting("sharpen_strength") / 100.0f) * pm);
		_postShader->setUniform1f("tonemapStrength", CLIP(StarkScene->getPostSetting("tonemap_strength"), 0, 100) / 100.0f * pm);
		_postShader->setUniform1f("bloomStrength", CLIP(StarkScene->getPostSetting("bloom_strength"), 0, 300) / 100.0f * pm);
		_postShader->setUniform1f("bloomThreshold", CLIP(StarkScene->getPostSetting("bloom_threshold"), 0, 100) / 100.0f);
	} else {
		_postShader->setUniform1f("gradeBrightness", 0.0f);
		_postShader->setUniform1f("gradeContrast", 1.0f);
		_postShader->setUniform1f("gradeSaturation", 1.0f);
		_postShader->setUniform("gradeTint", Math::Vector3d(1.0f, 1.0f, 1.0f));
		_postShader->setUniform1f("vignetteStrength", 0.0f);
		_postShader->setUniform1f("grainStrength", 0.0f);
		_postShader->setUniform1f("sharpenStrength", 0.0f);
		_postShader->setUniform1f("tonemapStrength", 0.0f);
		_postShader->setUniform1f("bloomStrength", 0.0f);
		_postShader->setUniform1f("bloomThreshold", 1.0f);
	}

	// Contact ambient occlusion (depth-based), independent of colour grading.
	if (ssao) {
		_postShader->setUniform1f("ssaoStrength", ssaoEff / 100.0f);
		_postShader->setUniform1f("ssaoRadius", (float)CLIP(StarkScene->getPostSetting("ssao_radius"), 1, 64));
	} else {
		_postShader->setUniform1f("ssaoStrength", 0.0f);
		_postShader->setUniform1f("ssaoRadius", 1.0f);
	}

	// Depth of field: blur by distance from the character's focus plane.
	if (dof) {
		float focus = StarkScene->getFocusDepth();
		float rangePct = CLIP(ConfMan.getInt("dof_range"), 1, 300) / 100.0f;
		_postShader->setUniform1f("dofStrength", (float)CLIP(ConfMan.getInt("dof_strength"), 0, 64));
		_postShader->setUniform1f("dofFocus", focus);
		_postShader->setUniform1f("dofRange", MAX(focus * rangePct, 0.001f));
	} else {
		_postShader->setUniform1f("dofStrength", 0.0f);
	}

	// Bind the depth source(s) for SSAO / DoF. Primary depth on unit 1:
	//   mode 1 = GL window depth (real, includes the character)
	//   mode 0 = packed eye-space background depth mask
	// When BOTH the GL depth and the background mask exist, the mask is also
	// bound on unit 2 so SSAO can separate dynamic occluders (character/props,
	// which stand in front of the baked background) from static background edges
	// - the latter are already shaded in the pre-rendered art and must not halo.
	if (wantDepth) {
		bool dynamicMask = false;

		glActiveTexture(GL_TEXTURE1);
		if (useGLDepth && _postDepthCopyTex) {
			glBindTexture(GL_TEXTURE_2D, _postDepthCopyTex);
			_postShader->setUniform1f("depthMode", 1.0f);
			_postShader->setUniform1f("depthNear", StarkScene->getNearClipPlane());
			_postShader->setUniform1f("depthFar", StarkScene->getFarClipPlane());

			if (_worldDepthBitmap) {
				glActiveTexture(GL_TEXTURE2);
				_worldDepthBitmap->bind();
				glActiveTexture(GL_TEXTURE1);
				_postShader->setUniform("bgDepthTex", 2);
				_postShader->setUniform1f("depthZMin", _worldDepthZMin);
				_postShader->setUniform1f("depthZMax", _worldDepthZMax);
				dynamicMask = true;
			}
		} else if (_worldDepthBitmap) {
			_worldDepthBitmap->bind();
			_postShader->setUniform1f("depthMode", 0.0f);
			_postShader->setUniform1f("depthZMin", _worldDepthZMin);
			_postShader->setUniform1f("depthZMax", _worldDepthZMax);
		}
		glActiveTexture(GL_TEXTURE0);
		_postShader->setUniform("depthTex", 1);

		// Contact-mode SSAO (shade only static background behind dynamic
		// occluders, never the character itself) is possible only when the
		// character can be told apart from the background, i.e. both depth
		// sources are present. Otherwise fall back to plain crease AO.
		_postShader->setUniform1f("ssaoDynamicOnly", dynamicMask ? 1.0f : 0.0f);

		// Record for the postInfo diagnostic.
		_postDbgGLDepth  = useGLDepth && _postDepthCopyTex != 0;
		_postDbgWorldMask = _worldDepthBitmap != nullptr;
		_postDbgContact  = dynamicMask;
	}

	// Optional debug view of the depth setup (0 = off). Driven by ConfMan so it
	// can be toggled live from the console: setInt post_debug_view <0..3>.
	_postShader->setUniform1f("debugView", (float)debugView);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _magTex);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	_postShader->unbind();

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

Texture *OpenGLSDriver::createTexture() {
	return new OpenGlTexture();
}

Bitmap *OpenGLSDriver::createBitmap(const Graphics::Surface *surface, const byte *palette) {
	OpenGlBitmap *bitmap = new OpenGlBitmap();

	if (surface) {
		bitmap->update(surface, palette);
	}

	return bitmap;
}

VisualActor *OpenGLSDriver::createActorRenderer() {
	return new OpenGLSActorRenderer(this);
}

VisualProp *OpenGLSDriver::createPropRenderer() {
	return new OpenGLSPropRenderer(this);
}

SurfaceRenderer *OpenGLSDriver::createSurfaceRenderer() {
	return new OpenGLSSurfaceRenderer(this);
}

FadeRenderer *OpenGLSDriver::createFadeRenderer() {
	return new OpenGLSFadeRenderer(this);
}

void OpenGLSDriver::start2DMode() {
	// Enable alpha blending
	glEnable(GL_BLEND);
	//glBlendEquation(GL_FUNC_ADD); // It's the default

	// This blend mode prevents color fringes due to filtering.
	// It requires the textures to have their color values pre-multiplied
	// with their alpha value. This is the "Premultiplied Alpha" technique.
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
}

void OpenGLSDriver::end2DMode() {
	// Disable alpha blending
	glDisable(GL_BLEND);

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void OpenGLSDriver::set3DMode() {
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	// Blending and stencil test are only used in rendering shadows
	// They are manually enabled and disabled there
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glStencilFunc(GL_EQUAL, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
}

bool OpenGLSDriver::computeLightsEnabled() {
	return false;
}

Common::Rect OpenGLSDriver::getViewport() const {
	return _viewport;
}

Common::Rect OpenGLSDriver::getUnscaledViewport() const {
	return _unscaledViewport;
}

OpenGL::Shader *OpenGLSDriver::createActorShaderInstance() {
	return _actorShader->clone();
}

OpenGL::Shader *OpenGLSDriver::createSurfaceShaderInstance() {
	return _surfaceShader->clone();
}

OpenGL::Shader *OpenGLSDriver::createSurfaceDepthShaderInstance() {
	return _surfaceDepthShader->clone();
}

OpenGL::Shader *OpenGLSDriver::createSurfaceFillShaderInstance() {
	return _surfaceFillShader->clone();
}

OpenGL::Shader *OpenGLSDriver::createFadeShaderInstance() {
	return _fadeShader->clone();
}

OpenGL::Shader *OpenGLSDriver::createShadowShaderInstance() {
	return _shadowShader->clone();
}

Graphics::Surface *OpenGLSDriver::getViewportScreenshot() const {
	Graphics::Surface *s = new Graphics::Surface();
	s->create(_viewport.width(), _viewport.height(), getRGBAPixelFormat());

	glReadPixels(_viewport.left, g_system->getHeight() - _viewport.bottom, _viewport.width(), _viewport.height(),
	             GL_RGBA, GL_UNSIGNED_BYTE, s->getPixels());

	flipVertical(s);

	return s;
}

} // End of namespace Gfx
} // End of namespace Stark

#endif // defined(USE_OPENGL_SHADERS)
