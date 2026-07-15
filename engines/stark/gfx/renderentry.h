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

#ifndef STARK_GFX_RENDER_ENTRY_H
#define STARK_GFX_RENDER_ENTRY_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"

#include "math/ray.h"
#include "math/vector3d.h"

namespace Stark {

class Visual;
class VisualImageXMG;
class VisualText;

namespace Resources {
class ItemVisual;
}

namespace Gfx {

struct LightEntry {
	enum Type {
		kAmbient     = 0,
		kPoint       = 1,
		kDirectional = 2,
		kSpot        = 4
	};

	Type type;
	Math::Vector3d color;
	Math::Vector3d position;
	Math::Vector3d direction;
	Math::Angle innerConeAngle;
	Math::Angle outerConeAngle;
	float falloffNear;
	float falloffFar;
	Math::Vector4d worldPosition;
	Math::Vector4d eyePosition;
	Math::Vector3d eyeDirection;
};

typedef Common::Array<LightEntry *> LightEntryArray;

class RenderEntry {
public:
	RenderEntry(Resources::ItemVisual *owner, const Common::String &name);
	virtual ~RenderEntry() {}

	void render(const LightEntryArray &lights = LightEntryArray());

	/**
	 * Stamp this entry's flat depth into the depth buffer, but only for a
	 * floor-positioned image item that has no per-pixel depth map. Lets such
	 * foreground sprites occupy the depth buffer so post-processing can tell
	 * them apart from the background. No-op for actors, props, un-positioned
	 * images, and images that already carry a depth map.
	 */
	/**
	 * Contribute this entry's depth ahead of the actors, so the character's shadow
	 * drape (which runs mid actor-draw) sees props that stand NEARER than her and
	 * are therefore drawn after her. Depth-mapped props draw their real per-pixel
	 * depth colour-masked; the rest stamp their flat floor plane.
	 */
	/** Cast this entry's actor shadow, once every scene item has been drawn. */
	void castShadow();
	void prepassDepth();
	void stampDepth();

	/**
	 * Force a specific eye-space depth for stampDepth(), used for 2D foreground
	 * overlay layers that have no sort-key distance of their own. Positive value
	 * enables it; 0 (default) falls back to the floor sort key.
	 */
	void setStampEyeDepth(float eyeDepth) { _stampEyeDepth = eyeDepth; }

	void setVisual(Visual *visual);
	void setPosition(const Common::Point &position);
	void setPosition3D(const Math::Vector3d &position, float direction);
	void setSortKey(float sortKey);
	void setClickable(bool clickable);

	/** Gets the position */
	Common::Point getPosition() const { return _position; }
	Visual *getVisual() const { return _visual; }

	/** Gets the owner-object */
	Resources::ItemVisual *getOwner() const { return _owner; }

	/** Gets the entry's name */
	const Common::String &getName() const { return _name; }

	/** Diagnostics: sort key (camera distance for floor items) and stamp override */
	float getSortKey() const { return _sortKey; }
	float getStampEyeDepth() const { return _stampEyeDepth; }

	/** Obtain the underlying image visual, if any */
	VisualImageXMG *getImage() const;

	/** Obtain the underlying text visual, if any */
	VisualText *getText() const;

	/**
	 * Mouse picking test for 2D items
	 *
	 * @param position game window coordinates to test
	 * @param relativePosition successful hit item relative coordinates
	 * @param cursorRect cursor rectangle to be used to test small world items
	 * @return successful hit
	 */
	bool containsPoint(const Common::Point &position, Common::Point &relativePosition, const Common::Rect &cursorRect) const;

	/** Mouse picking test for 3D items */
	bool intersectRay(const Math::Ray &ray) const;

	/** Compare two render entries by their sort keys */
	static bool compare(const RenderEntry *x, const RenderEntry *y);

	/**
	 * Compute the 2D screen space bounding rect for the item,
	 * in original game view coordinates.
	 */
	Common::Rect getBoundingRect() const;

protected:
	/** Eye-space plane depth to stamp/occlude for a flat image, or 0 if none. */
	float imageEyeDepth(VisualImageXMG *image) const;

	Common::String _name;
	Resources::ItemVisual *_owner;

	Visual *_visual;
	Common::Point _position;
	Math::Vector3d _position3D;
	float _direction3D;
	float _sortKey;
	float _stampEyeDepth;
	bool _clickable;
};

typedef Common::Array<RenderEntry *> RenderEntryArray;

} // End of namespace Gfx
} // End of namespace Stark

#endif // STARK_GFX_RENDER_ENTRY_H
