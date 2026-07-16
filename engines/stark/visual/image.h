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

#ifndef STARK_VISUAL_IMAGE_H
#define STARK_VISUAL_IMAGE_H

#include "engines/stark/visual/visual.h"

#include "common/rect.h"
#include "common/stream.h"

namespace Graphics {
struct Surface;
}

namespace Stark {

namespace Gfx {
class Driver;
class SurfaceRenderer;
class Bitmap;
}

/**
 * XMG (still image) renderer
 */
class VisualImageXMG : public Visual {
public:
	static const VisualType TYPE = Visual::kImageXMG;

	explicit VisualImageXMG(Gfx::Driver *gfx);
	~VisualImageXMG() override;

	/**
	 * Load the pixel data from a XMG image
	 */
	void load(Common::ReadStream *stream);

	/**
	 * Load the size from an XMG image
	 */
	void readOriginalSize(Common::ReadStream *stream);

	/**
	 * Load the pixel data from a PNG image
	 */
	bool loadPNG(Common::SeekableReadStream *stream);

	/**
	 * Load a depth map from a PNG image
	 *
	 * The image contains 16-bit normalized eye-space depth packed in the
	 * R (high) and G (low) channels, spanning the [zMin, zMax] range.
	 */
	bool loadDepthPNG(Common::SeekableReadStream *stream, float zMin, float zMax, float bias);

	void render(const Common::Point &position, bool useOffset);
	void render(const Common::Point &position, bool useOffset, bool unscaled);

	/** True if this image carries a per-pixel depth map (already writes depth) */
	bool hasDepthMap() const { return _depthBitmap != nullptr; }

	/** Eye-space range of the depth map (only valid when hasDepthMap()) */
	float getDepthZMin() const { return _depthZMin; }
	float getDepthZMax() const { return _depthZMax; }

	/** The colour bitmap, for passes that need to sample this image directly */
	Gfx::Bitmap *getBitmap() const { return _bitmap; }

	/** The depth-map bitmap (16-bit eye depth in R/G), when hasDepthMap() */
	Gfx::Bitmap *getDepthBitmap() const { return _depthBitmap; }

	/**
	 * Stamp a single constant eye-space depth for this image into the depth
	 * buffer (no colour), so a floor-positioned sprite without a depth map still
	 * occupies the depth buffer for the post-processing pass.
	 */
	/** Colour-masked, opaque-only draw that only contributes per-pixel depth. */
	void renderDepthOnly(const Common::Point &position, bool useOffset);
	void stampDepth(const Common::Point &position, bool useOffset, float eyeDepth);

	/**
	 * Set a constant eye-space plane depth used by the next render() so a flat
	 * sprite occludes 3D items per-pixel via the depth buffer. 0 disables it.
	 */
	void setOcclusionDepth(float eyeDepth);

	/** Should this image's depth map reject 3D items? False for the background
	 *  plate, which the original engine always painted behind everything.
	 *  See Gfx::SurfaceRenderer::setOccludes. */
	void setOccludes(bool occludes);

	/**
	 * Allow this draw to take the depth path (map / flat plane / world-depth
	 * publication). Enabled by RenderEntry around WORLD draws only; UI draws
	 * must stay on the plain path even when the image carries a depth map.
	 * See SurfaceRenderer::setDepthAllowed.
	 */
	void setDepthAllowed(bool allowed);

	/** Render the image stretched to an explicit width and height */
	void renderScaledToSize(const Common::Point &position, uint width, uint height);

	/** Set an offset used when rendering */
	void setHotSpot(const Common::Point &hotspot);
	Common::Point getHotspot() const { return _hotspot; }

	/**
	 * The fade level is added to the color value of each pixel
	 *
	 * It is a value between -1 and 1
	 */
	void setFadeLevel(float fadeLevel);

	/** Perform a transparency hit test on an image point */
	bool isPointSolid(const Common::Point &point) const;

	/** Get the width in pixels */
	int getWidth() const;

	/** Get the height in pixels */
	int getHeight() const;

	/** Get a read only pointer to the surface backing the image */
	const Graphics::Surface *getSurface() const;

private:
	Graphics::Surface *multiplyColorWithAlpha(const Graphics::Surface *source);

	Gfx::Driver *_gfx;
	Gfx::SurfaceRenderer *_surfaceRenderer;
	Gfx::Bitmap *_bitmap;
	Gfx::Bitmap *_depthBitmap;
	float _depthZMin;
	float _depthZMax;
	Graphics::Surface *_surface;
	Common::Point _hotspot;
	uint _originalWidth;
	uint _originalHeight;
};

} // End of namespace Stark

#endif // STARK_VISUAL_IMAGE_H
