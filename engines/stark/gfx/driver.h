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

#ifndef STARK_GFX_DRIVER_H
#define STARK_GFX_DRIVER_H

#include "common/rect.h"
#include "graphics/pixelformat.h"

namespace Graphics {
struct Surface;
}

namespace Stark {

class VisualActor;
class VisualProp;

namespace Gfx {

class SurfaceRenderer;
class FadeRenderer;
class Bitmap;
class Texture;

class Driver {
public:
	static Driver *create();

	virtual ~Driver() {}

	virtual void init() = 0;

	bool computeScreenViewport();
	virtual void setScreenViewport(bool noScaling) = 0; // deprecated

	virtual void setViewport(const Common::Rect &rect) = 0;

	/** Get the screen viewport in actual resolution */
	Common::Rect getScreenViewport() { return _screenViewport; }

	Common::Rect gameViewport() const;

	virtual void clearScreen() = 0;
	virtual void flipBuffer() = 0;

	/**
	 * Create a new texture for 3D
	 *
	 * The caller is responsible for freeing it.
	 *
	 */
	virtual Texture *createTexture() = 0;

	/**
	 * Create a new bitmap for 2D
	 *
	 * The caller is responsible for freeing it.
	 *
	 */
	virtual Bitmap *createBitmap(const Graphics::Surface *surface = nullptr, const byte *palette = nullptr) = 0;

	/**
	 * Create a new actor renderer
	 *
	 * The caller is responsible for freeing it.
	 */
	virtual VisualActor *createActorRenderer() = 0;

	/**
	 * Create a new prop renderer
	 *
	 * The caller is responsible for freeing it.
	 */
	virtual VisualProp *createPropRenderer() = 0;

	/**
	 * Create a new surface renderer
	 *
	 * The caller is responsible for freeing it.
	 */
	virtual SurfaceRenderer *createSurfaceRenderer() = 0;

	/**
	 * Create a new fade renderer
	 *
	 * The caller is responsible for freeing it.
	 */
	virtual FadeRenderer *createFadeRenderer() = 0;

	/**
	 * Post-processing hooks. The base implementation is a no-op (rendering
	 * goes directly to the screen); the shader-based driver overrides these
	 * to render into an offscreen buffer and composite through post shaders.
	 */
	virtual bool beginPostProcess() { return false; }
	virtual void endPostProcess() {}

	/**
	 * Supersampling resolve: if beginPostProcess redirected the frame into a
	 * supersampled buffer, downsample it back into the engine framebuffer.
	 * No-op otherwise. Called after the frame is rendered, before applyPostProcess.
	 */
	virtual void resolveSupersample() {}

	/**
	 * Is the current frame being rendered into the supersampling buffer?
	 *
	 * Decides WHERE the post pass runs. Normal frames apply it mid-render,
	 * right after the game window draws the world and before the in-viewport
	 * UI windows (inventory, action menu) - so scene-depth effects (SSAO/DoF)
	 * and the grade never composite over UI pixels. Supersampled frames can't
	 * do that (the copy coordinates assume the resolved backbuffer), so they
	 * keep the legacy end-of-frame site; SSAO/DoF are disabled there anyway.
	 */
	virtual bool isFrameSupersampled() const { return false; }

	/**
	 * Screen-space post-processing pass over the CURRENT viewport region: copy
	 * that region from the back buffer and redraw it through the post shader
	 * (colour grade, vignette, grain, sharpen, depth-of-field, cursor magnifier).
	 * Copying instead of rendering into an FBO keeps it working on GL stacks
	 * whose FBO path is unstable. Called from the game window so it affects only
	 * the 3D world, not the UI drawn around/after it.
	 */
	virtual void applyPostProcess() {}

	/**
	 * Report the depth state used by the last post-process pass, for diagnostics:
	 * whether the GL depth buffer was copied (character-aware depth), whether the
	 * background depth mask was available, and whether contact-mode SSAO (which
	 * needs both) was therefore active.
	 */
	virtual void getPostDepthState(bool &glDepthCopy, bool &worldMask, bool &contactMode) const {
		glDepthCopy = worldMask = contactMode = false;
	}

	/** Diagnostics: number and eye-depth range of foreground sprite depth stamps. */
	virtual void getSpriteStampInfo(int &count, float &minEye, float &maxEye) const {
		count = 0;
		minEye = maxEye = 0.0f;
	}

	/**
	 * Diagnostic: render into an offscreen framebuffer and read it back, to test
	 * whether the FBO path works on this GL stack (it historically hung on Apple's
	 * GL-over-Metal). Returns a human-readable result; if the stack is bad this
	 * call itself may hang, which is the answer.
	 */
	virtual Common::String testFramebuffer() { return "FBO test not supported by this backend"; }

	/** Checks if a screenpoint coord is within window bounds */
	bool isPosInScreenBounds(const Common::Point &point) const;

	/** Convert a coordinate from current to original resolution */
	Common::Point convertCoordinateCurrentToOriginal(const Common::Point &point) const;

	/** Scale a width value from original resolution to current resolution */
	uint scaleWidthOriginalToCurrent(uint width) const;

	/** Scale a height value from original resolution to current resolution */
	uint scaleHeightOriginalToCurrent(uint height) const;

	/** Scale a width value from current resolution to original resolution */
	uint scaleWidthCurrentToOriginal(uint width) const;

	/** Scale a height value from current resolution to original resolution */
	uint scaleHeightCurrentToOriginal(uint width) const;

	/**
	 * Textures are expected to be in the RGBA byte order
	 *
	 * That is to say bitmaps sent to OpenGL need to have the following layout:
	 * R G B A R G B A, ...
	 *
	 * This method can be used to retrieve what that means in terms
	 * of pixel format according to the current platform's endianness.
	 */
	static const Graphics::PixelFormat getRGBAPixelFormat();

	/** Grab a screenshot of the currently active viewport as defined by setViewport */
	virtual Graphics::Surface *getViewportScreenshot() const = 0;

	virtual void set3DMode() = 0;
	virtual bool computeLightsEnabled() = 0;

	virtual bool supportsModdedAssets() const { return true; }

	static const int32 kOriginalWidth = 640;
	static const int32 kOriginalHeight = 480;

	static const int32 kTopBorderHeight = 36;
	static const int32 kGameViewportHeight = 365;
	static const int32 kBottomBorderHeight = 79;

	static const int32 kGameViewportWidth = 640;

protected:
	static void flipVertical(Graphics::Surface *s);

	Common::Rect _screenViewport;
	bool         _computeLights;
};

} // End of namespace Gfx
} // End of namespace Stark

#endif // STARK_GFX_DRIVER_H
