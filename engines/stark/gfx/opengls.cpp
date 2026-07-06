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
	_postActive(false) {
}

OpenGLSDriver::~OpenGLSDriver() {
	OpenGL::Shader::freeBuffer(_surfaceVBO);
	OpenGL::Shader::freeBuffer(_fadeVBO);
	OpenGL::Shader::freeBuffer(_postVBO);
	freePostResources();
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
}

void OpenGLSDriver::setScreenViewport(bool noScaling) {
	if (noScaling) {
		_viewport = Common::Rect(g_system->getWidth(), g_system->getHeight());
		_unscaledViewport = _viewport;
	} else {
		_viewport = _screenViewport;
		_unscaledViewport = Common::Rect(kOriginalWidth, kOriginalHeight);
	}

	glViewport(_viewport.left, _viewport.top, _viewport.width(), _viewport.height());
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

	glViewport(_viewport.left, g_system->getHeight() - _viewport.bottom, _viewport.width(), _viewport.height());
}

void OpenGLSDriver::clearScreen() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
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
	if (!ConfMan.getBool("enable_post_processing")) {
		return false;
	}

	int width = g_system->getWidth();
	int height = g_system->getHeight();
	ensurePostResources(width, height);
	if (!_postFBO) {
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, _postFBO);
	_postActive = true;
	return true;
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
