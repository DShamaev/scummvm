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

#include "engines/stark/gfx/openglssurface.h"

#include "engines/stark/gfx/opengls.h"
#include "engines/stark/gfx/bitmap.h"
#include "engines/stark/gfx/color.h"

#include "engines/stark/scene.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/settings.h"

#include "common/config-manager.h"

#if defined(USE_OPENGL_SHADERS)

#include "graphics/opengl/shader.h"

namespace Stark {
namespace Gfx {

OpenGLSSurfaceRenderer::OpenGLSSurfaceRenderer(OpenGLSDriver *gfx) :
		SurfaceRenderer(),
		_gfx(gfx) {
	_shader = _gfx->createSurfaceShaderInstance();
	_shaderDepth = _gfx->createSurfaceDepthShaderInstance();
	_shaderFill = _gfx->createSurfaceFillShaderInstance();
}

OpenGLSSurfaceRenderer::~OpenGLSSurfaceRenderer() {
	delete _shaderFill;
	delete _shaderDepth;
	delete _shader;
}

void OpenGLSSurfaceRenderer::render(const Bitmap *bitmap, const Common::Point &dest) {
	render(bitmap, dest, bitmap->width(), bitmap->height());
}

void OpenGLSSurfaceRenderer::render(const Bitmap *bitmap, const Common::Point &dest, uint width, uint height) {
	// Destination rectangle with given width and height
	_gfx->start2DMode();

	bool useDepth = _depthBitmap != nullptr &&
	                StarkSettings->getBoolSetting(Settings::kDepthMaps);
	OpenGL::Shader *shader = useDepth ? _shaderDepth : _shader;

	shader->use();
	shader->setUniform1f("fadeLevel", _fadeLevel);
	shader->setUniform("snapToGrid", _snapToGrid ? 1 : 0);
	shader->setUniform("verOffsetXY", offsetVertex(dest));
	if (_noScalingOverride) {
		shader->setUniform("verSizeWH", normalizeCurrentCoordinates(width, height));
	} else {
		shader->setUniform("verSizeWH", normalizeOriginalCoordinates(width, height));
	}

	Common::Rect nativeViewport = _gfx->getViewport();
	shader->setUniform("viewport", Math::Vector2d(nativeViewport.width(), nativeViewport.height()));

	if (useDepth) {
		shader->setUniform("tex", 0);
		shader->setUniform("depthTex", 1);
		shader->setUniform("debugShowDepth", ConfMan.getBool("debug_show_depth") ? 1 : 0);
		shader->setUniform1f("depthZMin", _depthZMin);
		shader->setUniform1f("depthZMax", _depthZMax);
		shader->setUniform1f("nearClip", StarkScene->getNearClipPlane());
		shader->setUniform1f("farClip", StarkScene->getFarClipPlane());
		// Per-location bias from the depth JSON, falling back to the global
		// setting. Complex multi-level scenes (e.g. the Academy) need a lower
		// bias than simple rooms so foreground walls still occlude.
		float bias = getDepthBias() >= 0.0f
				? getDepthBias()
				: CLIP(ConfMan.getInt("depth_bias"), 0, 50) / 100.0f;
		shader->setUniform1f("depthBias", bias);

		// Publish this background's depth range for the depth fog
		StarkScene->setBackgroundDepthRange(_depthZMin, _depthZMax);

		glActiveTexture(GL_TEXTURE1);
		_depthBitmap->bind();
		glActiveTexture(GL_TEXTURE0);

		// Write the depth of the surface pixels so the 3D items rendered
		// afterwards are occluded by the closer parts of the surface.
		// LEQUAL makes overlay props with depth maps only paint over
		// pixels they are actually in front of (per-pixel prop occlusion).
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_TRUE);
	}

	bitmap->bind();
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	if (useDepth) {
		glDepthFunc(GL_LESS);
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
	}

	shader->unbind();
	_gfx->end2DMode();
}

void OpenGLSSurfaceRenderer::fill(const Color &color, const Common::Point &dest, uint width, uint height) {
	// Destination rectangle with given width and height
	_gfx->start2DMode();

	_shaderFill->use();
	_shaderFill->setUniform1f("fadeLevel", _fadeLevel);
	_shaderFill->setUniform("snapToGrid", _snapToGrid ? 1 : 0);
	_shaderFill->setUniform("verOffsetXY", normalizeOriginalCoordinates(dest.x, dest.y));
	if (_noScalingOverride) {
		_shaderFill->setUniform("verSizeWH", normalizeCurrentCoordinates(width, height));
	} else {
		_shaderFill->setUniform("verSizeWH", normalizeOriginalCoordinates(width, height));
	}

	Common::Rect nativeViewport = _gfx->getViewport();
	_shaderFill->setUniform("viewport", Math::Vector2d(nativeViewport.width(), nativeViewport.height()));

	_shaderFill->setUniform("color", Math::Vector4d(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f));

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	_shaderFill->unbind();
	_gfx->end2DMode();
}

Math::Vector2d OpenGLSSurfaceRenderer::normalizeOriginalCoordinates(int x, int y) const {
	Common::Rect viewport = _gfx->getUnscaledViewport();
	return Math::Vector2d(x / (float)viewport.width(), y / (float)viewport.height());
}

Math::Vector2d OpenGLSSurfaceRenderer::offsetVertex(const Common::Point &dest) const {
	Common::Rect viewport = _gfx->getUnscaledViewport();
	return Math::Vector2d((dest.x + _vertexOffsetX) / (float)viewport.width(),
	                      (dest.y + _vertexOffsetY) / (float)viewport.height());
}

Math::Vector2d OpenGLSSurfaceRenderer::normalizeCurrentCoordinates(int x, int y) const {
	Common::Rect viewport = _gfx->getViewport();
	return Math::Vector2d(x / (float)viewport.width(), y / (float)viewport.height());
}

} // End of namespace Gfx
} // End of namespace Stark

#endif // if defined(USE_OPENGL_SHADERS)
