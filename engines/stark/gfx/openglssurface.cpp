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

	bool depthMapsOn = StarkSettings->getBoolSetting(Settings::kDepthMaps);
	// _depthAllowed: depth participation is opt-in, enabled by RenderEntry around
	// WORLD draws only. UI draws (inventory, action menu, dialog panel) call
	// VisualImageXMG::render() directly and must take the plain path even when
	// their image carries a depth map - see SurfaceRenderer::setDepthAllowed.
	bool useDepth = _depthBitmap != nullptr && depthMapsOn && _depthAllowed;

	// Depth-only pre-pass: write this surface's depth WITHOUT touching colour, so
	// props that draw after the character (because they stand nearer than her) are
	// already in the depth buffer when her shadow drape runs mid-draw. Only
	// near-opaque pixels stamp, so a soft edge can't depth-reject her behind it.
	if (_depthOnly) {
		if (!_occludes || (!useDepth && !(_flatDepth > 0.0f && depthMapsOn && _depthAllowed))) {
			_gfx->end2DMode();   // nothing to contribute; start2DMode already ran
			return;
		}
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	}
	// Flat occlusion: no per-pixel depth map, but a plane depth was set for this
	// sprite (a floor-positioned foreground image) and per-pixel sprite occlusion
	// is enabled. Draw with the depth shader in flat mode so 3D items are occluded
	// per-pixel by this sprite's plane instead of by whole-sprite draw order.
	bool useFlat = !useDepth && _flatDepth > 0.0f && depthMapsOn && _depthAllowed;
	OpenGL::Shader *shader = (useDepth || useFlat) ? _shaderDepth : _shader;

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
		shader->setUniform("flatDepth", 0);
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

		// Hand the depth mask to the post pass so SSAO / DoF can sample it
		// (a normal texture) instead of copying the GL depth buffer. Pass the
		// on-screen area so the driver keeps the largest (full-screen background)
		// rather than a small overlay prop's depth map.
		//
		// Also pass how to map a viewport UV onto this mask. In scrolling locations
		// the background is drawn WIDER than the viewport at a scrolled offset, so
		// sampling the mask at plain viewport UV reads the wrong part of it - and the
		// error slides as the camera scrolls. The surface occupies [o, o+s] of the
		// viewport in normalised top-origin coords, so maskUv = (p - o) / s.
		Math::Vector2d o = offsetVertex(dest);
		Math::Vector2d s = _noScalingOverride
				? normalizeCurrentCoordinates(width, height)
				: normalizeOriginalCoordinates(width, height);
		float sx = ABS(s.getX()) > 0.0001f ? s.getX() : 1.0f;
		float sy = ABS(s.getY()) > 0.0001f ? s.getY() : 1.0f;
		Math::Vector2d uvScale(1.0f / sx, 1.0f / sy);
		Math::Vector2d uvOffset(-o.getX() / sx, -o.getY() / sy);
		_gfx->setWorldDepth(_depthBitmap, _depthZMin, _depthZMax, (int)width * (int)height,
		                    uvScale, uvOffset);

		glActiveTexture(GL_TEXTURE1);
		_depthBitmap->bind();
		glActiveTexture(GL_TEXTURE0);

		// Write the depth of the surface pixels so the 3D items rendered
		// afterwards are occluded by the closer parts of the surface.
		// LEQUAL makes overlay props with depth maps only paint over
		// pixels they are actually in front of (per-pixel prop occlusion).
		//
		// _occludes == false (the background plate) skips this entirely: start2DMode
		// already left GL_DEPTH_TEST off and glDepthMask(GL_FALSE), which is exactly
		// the vanilla no-depth-map path, so the colour still draws but nothing is
		// stamped into the depth buffer and no actor drawn afterwards can be rejected
		// by it. The uniform setup above has already published the mask + range for
		// the post pass and the drape. See SurfaceRenderer::setOccludes.
		if (_occludes) {
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_TRUE);
		}
	} else if (useFlat) {
		shader->setUniform("tex", 0);
		shader->setUniform("depthTex", 1);
		shader->setUniform("flatDepth", 1);
		shader->setUniform1f("flatDepthEye", _flatDepth);
		shader->setUniform("debugShowDepth", 0);
		shader->setUniform1f("depthZMin", 0.0f);
		shader->setUniform1f("depthZMax", 1.0f);
		shader->setUniform1f("depthBias", 0.0f);
		shader->setUniform1f("nearClip", StarkScene->getNearClipPlane());
		shader->setUniform1f("farClip", StarkScene->getFarClipPlane());

		// Write the plane depth (with an alpha discard in the shader) and depth-
		// test so this sprite occludes / is occluded per-pixel like a depth-mapped
		// prop, rather than winning or losing wholesale by paint order.
		//
		// _occludes == false (the background plate) skips this entirely: start2DMode
		// already left GL_DEPTH_TEST off and glDepthMask(GL_FALSE), which is exactly
		// the vanilla no-depth-map path, so the colour still draws but nothing is
		// stamped into the depth buffer and no actor can be rejected by it. The
		// uniform setup above has already published the mask + range for the post
		// pass and the drape. See SurfaceRenderer::setOccludes.
		if (_occludes) {
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_TRUE);
		}
	}

	// Only near-opaque pixels stamp depth during the pre-pass (see alphaCutoff in
	// stark_surface_depth.fragment); the normal draw keeps its ~0 cutoff.
	if (useDepth || useFlat) {
		shader->setUniform1f("alphaCutoff", _depthOnly ? 0.9f : 0.0f);
	}

	bitmap->bind();
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	if ((useDepth || useFlat) && _occludes) {
		glDepthFunc(GL_LESS);
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
	}
	if (_depthOnly) {
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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

void OpenGLSSurfaceRenderer::stampDepthPlane(const Bitmap *bitmap, const Common::Point &dest, uint width, uint height, float eyeDepth) {
	_gfx->start2DMode();

	_shaderDepth->use();
	_shaderDepth->setUniform1f("fadeLevel", 0.0f);
	_shaderDepth->setUniform("snapToGrid", _snapToGrid ? 1 : 0);
	_shaderDepth->setUniform("verOffsetXY", offsetVertex(dest));
	if (_noScalingOverride) {
		_shaderDepth->setUniform("verSizeWH", normalizeCurrentCoordinates(width, height));
	} else {
		_shaderDepth->setUniform("verSizeWH", normalizeOriginalCoordinates(width, height));
	}

	Common::Rect nativeViewport = _gfx->getViewport();
	_shaderDepth->setUniform("viewport", Math::Vector2d(nativeViewport.width(), nativeViewport.height()));

	_shaderDepth->setUniform("tex", 0);
	_shaderDepth->setUniform("debugShowDepth", 0);
	_shaderDepth->setUniform("flatDepth", 1);
	_shaderDepth->setUniform1f("flatDepthEye", eyeDepth);
	_shaderDepth->setUniform1f("depthZMin", 0.0f);
	_shaderDepth->setUniform1f("depthZMax", 1.0f);
	_shaderDepth->setUniform1f("depthBias", 0.0f);
	_shaderDepth->setUniform1f("nearClip", StarkScene->getNearClipPlane());
	_shaderDepth->setUniform1f("farClip", StarkScene->getFarClipPlane());

	// Depth-only: write the plane depth wherever the sprite is opaque, but touch
	// no colour (the frame is already composited). LEQUAL so a sprite only stamps
	// where it is at or nearer than what is already there - it never overwrites a
	// character (or nearer sprite) that is drawn in front of it, which would
	// corrupt that surface's depth for the post pass.
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);

	bitmap->bind();
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	_gfx->recordSpriteStamp(eyeDepth);

	// Restore the default 2D state (depth off, colour on).
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LESS);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	_shaderDepth->unbind();
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
