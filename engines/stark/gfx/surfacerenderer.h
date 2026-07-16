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

#ifndef STARK_GFX_SURFACE_RENDERER_H
#define STARK_GFX_SURFACE_RENDERER_H

#include "common/rect.h"

namespace Stark {
namespace Gfx {

class Bitmap;
struct Color;

/**
 * A renderer to draw textures as two dimensional surfaces to the current viewport
 */
class SurfaceRenderer {
public:
	SurfaceRenderer();
	virtual ~SurfaceRenderer();

	/**
	 * Draw a 2D surface from the specified bitmap
	 */
	virtual void render(const Bitmap *bitmap, const Common::Point &dest) = 0;

	/**
	 * Draw a 2D surface from the specified bitmap with given width and height
	 */
	virtual void render(const Bitmap *bitmap, const Common::Point &dest, uint width, uint height) = 0;

	/**
	 * Draw a filled 2D rectangle using the specified color
	 */
	virtual void fill(const Color &color, const Common::Point &dest, uint width, uint height) = 0;

	/**
	 * Stamp a single constant eye-space depth for this surface into the depth
	 * buffer only (no colour is written). Used so floor-positioned sprites that
	 * lack a per-pixel depth map still occupy the depth buffer, which lets the
	 * post-processing pass tell them apart from the background behind them.
	 * Transparent pixels are skipped. Backends without depth support ignore it.
	 */
	virtual void stampDepthPlane(const Bitmap *bitmap, const Common::Point &dest, uint width, uint height, float eyeDepth) {}

	/**
	 * When this is set to true, the texture size is expected to be in current
	 * coordinates, and is to be drawn without scaling.
	 *
	 * This setting does not affect the destination point coordinates
	 */
	void setNoScalingOverride(bool noScalingOverride);

	/**
	 * The fade level is added to the color value of each pixel
	 *
	 * It is a value between -1 and 1
	 */
	void setFadeLevel(float fadeLevel);

	/**
	 * Align vertex coordinates to the native pixel grid
	 */
	void setSnapToGrid(bool snapToGrid);

	/**
	 * An extra destination offset, in original (fractional) coordinates, added
	 * to the destination point. Because the destination point is an integer,
	 * this allows sub-unit positioning - e.g. smooth pixel-accurate scrolling -
	 * that snapToGrid then rounds to whole screen pixels. Backends that don't
	 * implement it simply ignore the offset.
	 */
	void setVertexOffset(float x, float y);

	/**
	 * Set a depth map for the rendered surface.
	 *
	 * When set, renderers supporting it write per-pixel depth so 3D items
	 * can be occluded by parts of the surface. The bitmap contains 16-bit
	 * normalized eye-space depth packed in the R (high) and G (low) channels,
	 * spanning the [zMin, zMax] eye-space range.
	 */
	void setDepthBitmap(const Bitmap *bitmap, float zMin, float zMax, float bias);

	/** Per-location depth bias, or a negative value to use the global setting */
	float getDepthBias() const { return _depthBias; }

	/**
	 * Set a single constant eye-space depth for the next render, used for flat
	 * foreground sprites that lack a per-pixel depth map. When set (and no depth
	 * map is bound), supporting renderers write this plane depth with a depth
	 * test during the colour pass, so 3D items are occluded per-pixel by the
	 * sprite instead of by whole-sprite draw order. 0 disables it.
	 */
	void setFlatDepth(float eyeDepth) { _flatDepth = eyeDepth; }

	/**
	 * Depth-only pre-pass: write this surface's depth without touching colour, and
	 * only from near-opaque pixels. Props nearer than the character are drawn AFTER
	 * her, so without this their depth doesn't exist yet when her shadow drape runs
	 * mid-draw and no shadow can land on them.
	 */
	void setDepthOnly(bool depthOnly) { _depthOnly = depthOnly; }

	/**
	 * Allow this draw to participate in the depth system at all (per-pixel
	 * depth map, flat plane depth, world-depth publication for the post pass).
	 *
	 * Default FALSE: depth participation is opt-in, enabled by RenderEntry
	 * around WORLD draws only. Depth maps are looked up by image filename
	 * (resources/image.cpp), so an image drawn in the UI can carry one too -
	 * e.g. an inventory icon that shares its art with a scene overlay. The UI
	 * calls VisualImageXMG::render() directly, and letting such a draw take
	 * the depth path is categorically wrong: it depth-TESTS the icon against
	 * the 3D scene behind the panel (clipping it), depth-WRITES scene-frame
	 * eye distances in the middle of UI rendering, publishes its mask as a
	 * world-depth candidate, and clobbers the scene's background depth range.
	 */
	void setDepthAllowed(bool allowed) { _depthAllowed = allowed; }

	/**
	 * Should this surface's depth map REJECT 3D items drawn after it?
	 *
	 * False for the pre-rendered background plate. Layer3D::listRenderEntries()
	 * excludes kItemBackground from the sort and paints it first, unconditionally,
	 * behind everything - so in the original engine the background could never
	 * occlude the character, and the art was authored on that guarantee. Anything
	 * that must occlude her is a separate sorted item (e.g. common room_wallfar,
	 * prop10_pillarleft). Letting the background's depth map depth-test her is a
	 * power it never had, and since that depth is a monocular estimate with a
	 * collapsed far field, it eats her: at the 16/00 far doorway she stands at
	 * eye 1424 while the estimated wall behind her reads 1247, so everything above
	 * her boots is rejected. (Her boots survive only because the pixels there are
	 * the exact rasterised floor.)
	 *
	 * When false the surface still draws its colour and still publishes its depth
	 * mask + range, so the post pass (SSAO/DoF/fog) and the shadow drape are
	 * unaffected - both sample the mask TEXTURE, not the GL depth buffer, and the
	 * drape's min(mask, realDepth) simply falls back to the mask.
	 */
	void setOccludes(bool occludes) { _occludes = occludes; }

protected:
	bool _noScalingOverride;
	float _fadeLevel;
	bool _snapToGrid;
	float _vertexOffsetX;
	float _vertexOffsetY;

	const Bitmap *_depthBitmap;
	float _depthZMin;
	float _depthZMax;
	float _depthBias;
	float _flatDepth;
	bool _depthOnly;
	bool _occludes;
	bool _depthAllowed;
};

} // End of namespace Gfx
} // End of namespace Stark

#endif // STARK_GFX_SURFACE_RENDERER_H
