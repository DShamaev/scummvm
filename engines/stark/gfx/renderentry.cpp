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

#include "engines/stark/gfx/renderentry.h"
#include "engines/stark/gfx/driver.h"

#include "common/config-manager.h"

#include "engines/stark/resources/item.h"

#include "engines/stark/visual/actor.h"
#include "engines/stark/visual/effects/bubbles.h"
#include "engines/stark/visual/effects/fireflies.h"
#include "engines/stark/visual/effects/fish.h"
#include "engines/stark/visual/image.h"
#include "engines/stark/visual/prop.h"
#include "engines/stark/visual/smacker.h"
#include "engines/stark/visual/text.h"
#include "engines/stark/visual/visual.h"

namespace Stark {
namespace Gfx {

RenderEntry::RenderEntry(Resources::ItemVisual *owner, const Common::String &name) :
		_name(name),
		_owner(owner),
		_visual(nullptr),
		_direction3D(0.0),
		_sortKey(0.0),
		_stampEyeDepth(0.0),
		_clickable(true) {
}

void RenderEntry::render(const LightEntryArray &lights) {
	if (!_visual) {
		// warning("No visual for render entry '%s'", _name.c_str());
		return;
	}

	VisualImageXMG *imageXMG = _visual->get<VisualImageXMG>();
	if (imageXMG) {
		// Optional per-pixel occlusion for flat foreground sprites: write a plane
		// depth during the colour render so 3D items are clipped by this sprite.
		float occ = ConfMan.getBool("enable_sprite_occlusion") ? imageEyeDepth(imageXMG) : 0.0f;
		if (occ > 0.0f) {
			imageXMG->setOcclusionDepth(occ);
			imageXMG->render(_position, true);
			imageXMG->setOcclusionDepth(0.0f);
		} else {
			imageXMG->render(_position, true);
		}
	}

	VisualActor *actor = _visual->get<VisualActor>();
	if (actor) {
		actor->render(_position3D, _direction3D, lights);
	}

	VisualProp *prop = _visual->get<VisualProp>();
	if (prop) {
		prop->render(_position3D, _direction3D, lights);
	}

	VisualSmacker *smacker = _visual->get<VisualSmacker>();
	if (smacker) {
		smacker->render(_position);
	}

	VisualText *text = _visual->get<VisualText>();
	if (text) {
		text->render(_position);
	}

	VisualEffectBubbles *bubbles = _visual->get<VisualEffectBubbles>();
	if (bubbles) {
		bubbles->render(_position);
	}

	VisualEffectFireFlies *fireflies = _visual->get<VisualEffectFireFlies>();
	if (fireflies) {
		fireflies->render(_position);
	}

	VisualEffectFish *fish = _visual->get<VisualEffectFish>();
	if (fish) {
		fish->render(_position);
	}
}

float RenderEntry::imageEyeDepth(VisualImageXMG *image) const {
	// Actors/props (no image) already write real depth; images that carry a
	// depth map already stamp per-pixel - neither needs a flat plane.
	if (!image || image->hasDepthMap()) {
		return 0.0f;
	}

	// An explicit foreground-plane override (2D overlay layers drawn in front of
	// the character), else the floor sort-key distance for floor-positioned
	// 3D-layer sprites. The sort key is the item's eye-space Z, which is NEGATIVE
	// in front of the camera, so use its magnitude. Un-positioned images (sort
	// key 0, no override) have no depth.
	float eye = _stampEyeDepth > 0.0f ? _stampEyeDepth : ABS(_sortKey);
	return eye > 0.0f ? eye : 0.0f;
}

void RenderEntry::castShadow() {
	if (!_visual) {
		return;
	}
	VisualActor *actor = _visual->get<VisualActor>();
	if (actor) {
		actor->castPendingShadow();
	}
}

void RenderEntry::prepassDepth() {
	if (!_visual) {
		return;
	}
	VisualImageXMG *image = _visual->get<VisualImageXMG>();
	if (!image) {
		return;   // actors write their own real depth when they draw
	}

	if (image->hasDepthMap()) {
		// Per-pixel depth from the prop's depth map, ahead of the actors.
		image->renderDepthOnly(_position, true);
		return;
	}

	// No depth map: the flat floor-distance plane (same rule as stampDepth).
	float eye = imageEyeDepth(image);
	if (eye > 0.0f) {
		image->stampDepth(_position, true, eye);
	}
}

void RenderEntry::stampDepth() {
	if (!_visual) {
		return;
	}

	VisualImageXMG *image = _visual->get<VisualImageXMG>();
	float eye = imageEyeDepth(image);
	if (eye <= 0.0f) {
		return;
	}

	image->stampDepth(_position, true, eye);
}

void RenderEntry::setVisual(Visual *visual) {
	_visual = visual;
}

void RenderEntry::setPosition(const Common::Point &position) {
	_position = position;
}

void RenderEntry::setPosition3D(const Math::Vector3d &position, float direction) {
	_position3D = position;
	_direction3D = direction;
}

void RenderEntry::setSortKey(float sortKey) {
	_sortKey = sortKey;
}

void RenderEntry::setClickable(bool clickable) {
	_clickable = clickable;
}

bool RenderEntry::compare(const RenderEntry *x, const RenderEntry *y) {
	if (x->_sortKey != y->_sortKey) {
		return x->_sortKey < y->_sortKey;
	} else if (x->_owner && y->_owner) {
		// The original used a stable sort. Common::sort is not.
		// This should ensure the items remain in the same order if they have the same sort key
		return x->_owner->getIndex() < y->_owner->getIndex();
	} else {
		return (x->_owner < y->_owner);
	}
}

bool RenderEntry::containsPoint(const Common::Point &position, Common::Point &relativePosition, const Common::Rect &cursorRect) const {
	if (!_visual || !_clickable) {
		return false;
	}

	VisualImageXMG *image = _visual->get<VisualImageXMG>();
	if (image) {
		Common::Rect imageRect = Common::Rect(image->getWidth(), image->getHeight());
		imageRect.translate(_position.x, _position.y);
		imageRect.translate(-image->getHotspot().x, -image->getHotspot().y);

		relativePosition.x = position.x - imageRect.left;
		relativePosition.y = position.y - imageRect.top;
		if (imageRect.contains(position) && image->isPointSolid(relativePosition)) {
			return true;
		}

		if (imageRect.width() < 32 && imageRect.height() < 32
				&& !cursorRect.isEmpty() && cursorRect.intersects(imageRect)) {
			// If the item in the scene is way smaller than the cursor,
			// use the whole cursor as a hit rectangle.
			relativePosition.x = 1 - image->getHotspot().x;
			relativePosition.y = 1 - image->getHotspot().y;

			return true;
		}
	}

	VisualSmacker *smacker = _visual->get<VisualSmacker>();
	if (smacker) {
		Common::Point smackerPosition = smacker->getPosition();
		smackerPosition -= _position;

		Common::Rect smackerRect = Common::Rect(smacker->getWidth(), smacker->getHeight());
		smackerRect.translate(smackerPosition.x, smackerPosition.y);

		relativePosition.x = position.x - smackerRect.left;
		relativePosition.y = position.y - smackerRect.top;
		if (smackerRect.contains(position) && smacker->isPointSolid(relativePosition)) {
			return true;
		}
	}

	VisualText *text = _visual->get<VisualText>();
	if (text) {
		Common::Rect textRect = text->getRect();
		textRect.translate(_position.x, _position.y);

		relativePosition.x = position.x - textRect.left;
		relativePosition.y = position.y - textRect.top;
		if (textRect.contains(position)) {
			return true;
		}
	}

	return false;
}

bool RenderEntry::intersectRay(const Math::Ray &ray) const {
	if (!_visual || !_clickable) {
		return false;
	}

	VisualActor *actor = _visual->get<VisualActor>();
	if (actor) {
		return actor->intersectRay(ray, _position3D, _direction3D);
	}

	VisualProp *prop = _visual->get<VisualProp>();
	if (prop) {
		return prop->intersectRay(ray, _position3D, _direction3D);
	}

	return false;
}

VisualImageXMG *RenderEntry::getImage() const {
	if (!_visual) {
		return nullptr;
	}
	return _visual->get<VisualImageXMG>();
}

VisualText *RenderEntry::getText() const {
	if (!_visual) {
		return nullptr;
	}
	return _visual->get<VisualText>();
}

Common::Rect RenderEntry::getBoundingRect() const {
	if (!_visual) {
		return Common::Rect();
	}

	VisualActor *actor = _visual->get<VisualActor>();
	if (actor) {
		return actor->getBoundingRect(_position3D, _direction3D);
	}

	warning("RenderEntry::getBoundingRect is not implemented for '%s'", _name.c_str());
	return Common::Rect();
}

} // End of namespace Gfx
} // End of namespace Stark
