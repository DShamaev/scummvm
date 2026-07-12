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

#include "engines/stark/ui/world/gamewindow.h"

#include "engines/engine.h"

#include "engines/stark/console.h"
#include "engines/stark/scene.h"
#include "engines/stark/gfx/driver.h"
#include "engines/stark/resources/anim.h"
#include "engines/stark/resources/knowledgeset.h"
#include "engines/stark/resources/image.h"
#include "engines/stark/resources/item.h"
#include "engines/stark/resources/location.h"
#include "engines/stark/resources/layer.h"
#include "engines/stark/services/global.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/staticprovider.h"
#include "engines/stark/services/fontprovider.h"
#include "engines/stark/services/gameinterface.h"
#include "engines/stark/services/settings.h"
#include "engines/stark/services/userinterface.h"
#include "engines/stark/gfx/bitmap.h"
#include "engines/stark/gfx/color.h"
#include "engines/stark/gfx/surfacerenderer.h"

#include "common/config-manager.h"
#include "common/system.h"
#include "graphics/surface.h"
#include "engines/stark/ui/cursor.h"
#include "engines/stark/ui/world/actionmenu.h"
#include "engines/stark/ui/world/inventorywindow.h"
#include "engines/stark/visual/text.h"
#include "engines/stark/visual/image.h"

namespace Stark {

GameWindow::GameWindow(Gfx::Driver *gfx, Cursor *cursor, ActionMenu *actionMenu, InventoryWindow *inventory) :
		Window(gfx, cursor),
	_actionMenu(actionMenu),
	_inventory(inventory),
	_objectUnderCursor(nullptr),
	_displayExit(false) {
	_position = Common::Rect(Gfx::Driver::kGameViewportWidth, Gfx::Driver::kGameViewportHeight);
	_position.translate(0, Gfx::Driver::kTopBorderHeight);
	_visible = true;

	_fadeRenderer = _gfx->createFadeRenderer();
	_surfaceRenderer = _gfx->createSurfaceRenderer();

	buildMarkerBitmaps();

	_exitArrow = StarkStaticProvider->getUIElement(StaticProvider::kExitArrow);
	_exitArrowLeft = StarkStaticProvider->getUIElement(StaticProvider::kExitArrowLeft);
	_exitArrowRight = StarkStaticProvider->getUIElement(StaticProvider::kExitArrowRight);

	_exitLeftBoundary = 5;
	_exitRightBoundary = Gfx::Driver::kGameViewportWidth - _exitArrowRight->getWidth() - 5;
}

GameWindow::~GameWindow() {
	delete _fadeRenderer;
	delete _surfaceRenderer;
	for (int i = 0; i < 4; i++) {
		delete _hotspotMarkers[i];
	}
	clearTextCaches();
}

void GameWindow::buildMarkerBitmaps() {
	for (int i = 0; i < 4; i++) {
		delete _hotspotMarkers[i];
		_hotspotMarkers[i] = nullptr;
	}

	if (ConfMan.getBool("marker_colorblind")) {
		// Colorblind-safe: distinct by brightness and hue spacing (blue,
		// yellow, vermilion), the Okabe-Ito-inspired safe set
		_hotspotMarkers[0] = createMarkerBitmap(1.0f, 1.0f, 1.0f);
		_hotspotMarkers[1] = createMarkerBitmap(0.35f, 0.60f, 1.0f); // look: blue
		_hotspotMarkers[2] = createMarkerBitmap(0.90f, 0.60f, 0.00f); // use: orange-yellow
		_hotspotMarkers[3] = createMarkerBitmap(0.95f, 0.35f, 0.10f); // talk: vermilion
	} else {
		_hotspotMarkers[0] = createMarkerBitmap(1.0f, 1.0f, 1.0f);
		_hotspotMarkers[1] = createMarkerBitmap(0.45f, 0.9f, 1.0f);
		_hotspotMarkers[2] = createMarkerBitmap(1.0f, 0.75f, 0.35f);
		_hotspotMarkers[3] = createMarkerBitmap(0.55f, 1.0f, 0.55f);
	}

	_markerColorblindCache = ConfMan.getBool("marker_colorblind");
}

Gfx::Bitmap *GameWindow::createMarkerBitmap(float tintR, float tintG, float tintB) {
	static const int kMarkerSize = 32;
	const float center = (kMarkerSize - 1) / 2.0f;
	const float radius = kMarkerSize / 2.0f;

	Graphics::Surface surface;
	surface.create(kMarkerSize, kMarkerSize, Gfx::Driver::getRGBAPixelFormat());

	for (int y = 0; y < kMarkerSize; y++) {
		uint32 *dst = (uint32 *) surface.getBasePtr(0, y);
		for (int x = 0; x < kMarkerSize; x++) {
			float dx = x - center;
			float dy = y - center;
			float d = sqrtf(dx * dx + dy * dy) / radius;

			// Soft dot: dark halo fading out at the edge, bright tinted core
			float halo = CLIP(1.0f - d, 0.0f, 1.0f);
			halo = halo * halo * (3.0f - 2.0f * halo); // smoothstep
			float core = CLIP(1.0f - d / 0.55f, 0.0f, 1.0f);
			core = core * core * (3.0f - 2.0f * core);

			// Pre-multiplied alpha, as expected by the surface renderer
			byte alpha = (byte) (halo * 210.0f);
			byte intensity = (byte) (core * alpha);
			*dst++ = surface.format.ARGBToColor(alpha,
					(byte) (intensity * tintR),
					(byte) (intensity * tintG),
					(byte) (intensity * tintB));
		}
	}

	Gfx::Bitmap *bitmap = _gfx->createBitmap(&surface);
	bitmap->setSamplingFilter(Gfx::Bitmap::kLinear);

	surface.free();
	return bitmap;
}

VisualText *GameWindow::getHotspotLabel(const Common::String &title) {
	if (_hotspotLabels.contains(title)) {
		return _hotspotLabels[title];
	}

	VisualText *text = new VisualText(_gfx);
	text->setText(title);
	text->setColor(Gfx::Color(0xFF, 0xFF, 0xFF));
	text->setBackgroundColor(Gfx::Color(0x00, 0x00, 0x00, 0x50));
	text->setFont(FontProvider::kSmallFont);

	_hotspotLabels[title] = text;
	return text;
}

void GameWindow::clearTextCaches() {
	for (Common::HashMap<Common::String, VisualText *>::iterator it = _hotspotLabels.begin();
			it != _hotspotLabels.end(); ++it) {
		delete it->_value;
	}
	_hotspotLabels.clear();

}

void GameWindow::onRender() {
	// Advance the all-locations dump crawl, if one is running
	Console *console = static_cast<Console *>(g_engine->getDebugger());
	if (console) {
		console->tickDumpCrawl();
	}

	// List the items to render
	Resources::Location *location = StarkGlobal->getCurrent()->getLocation();
	_renderEntries = location->listRenderEntries();
	Gfx::LightEntryArray lightEntries = location->listLightEntries();

	// Track the focus subject's depth for depth of field
	Resources::ModelItem *april = StarkGlobal->getCurrent()->getInteractive();
	if (april) {
		Math::Vector3d eye = april->getPosition3D();
		StarkScene->getViewMatrix().transform(&eye, true);
		StarkScene->setFocusDepth(eye.length());
	}

	// Render all the scene items
	Gfx::RenderEntryArray::iterator element = _renderEntries.begin();
	while (element != _renderEntries.end()) {
		// Draw the current element
		(*element)->render(lightEntries);

		// Go for the next one
		element++;
	}

	// Depth pass: stamp foreground sprites (floor-positioned images without a
	// per-pixel depth map) into the depth buffer at their camera distance. This
	// writes depth only - the colour frame is already composited and untouched -
	// so the post-processing pass sees these sprites at their true depth instead
	// of the background behind them (fixes SSAO/DoF bleeding through them).
	if (ConfMan.getBool("enable_sprite_depth")) {
		for (element = _renderEntries.begin(); element != _renderEntries.end(); element++) {
			(*element)->stampDepth();
		}
	}

	if (_displayExit) {
		Common::Array<Common::Point> exitPositions = StarkGameInterface->listExitPositions();

		for (uint i = 0; i < exitPositions.size(); ++i) {
			Common::Point pos = exitPositions[i];
			VisualImageXMG *exitImage = nullptr;

			if (pos.x < _exitLeftBoundary) {
				pos.x = _exitLeftBoundary;
				exitImage = _exitArrowLeft;
			} else if (pos.x > _exitRightBoundary) {
				pos.x = _exitRightBoundary;
				exitImage = _exitArrowRight;
			} else {
				exitImage = _exitArrow;
			}

			exitImage->render(pos, false);
		}
	}

	if (StarkSettings->getBoolSetting(Settings::kHighlightHotspots)) {
		renderHotspotMarkers();
	}

	float fadeLevel = StarkScene->getFadeLevel();
	if ((1.0f - fadeLevel) > 0.00001f) {
		_fadeRenderer->render(fadeLevel);
	}
}

void GameWindow::renderHotspotMarkers() {
	Common::Array<Resources::Item::Hotspot> hotspots = StarkGameInterface->listHotspots();

	// Rebuild the palette if the colorblind setting was toggled
	if (ConfMan.getBool("marker_colorblind") != _markerColorblindCache) {
		buildMarkerBitmaps();
	}

	// Pulse the marker size over a one second cycle, scaled by the setting
	float scale = CLIP(ConfMan.getInt("marker_scale"), 50, 250) / 100.0f;
	uint32 cycle = g_system->getMillis() % 1000;
	float pulse = (cycle < 500 ? cycle : 1000 - cycle) / 500.0f;
	int size = (int) ((14 + 6.0f * pulse) * scale);

	for (uint i = 0; i < hotspots.size(); ++i) {
		Common::Point pos = hotspots[i].position;

		// The game's hotspot points are label anchors, placed above the object
		// so the tooltip text does not cover it. Nudge the dot down onto the
		// object; the label still sits above (see labelPos below).
		Common::Point markerPos = pos;
		markerPos.y += 16;

		// Keep the markers inside the viewport
		markerPos.x = CLIP<int16>(markerPos.x, size / 2, Gfx::Driver::kGameViewportWidth - size / 2);
		markerPos.y = CLIP<int16>(markerPos.y, size / 2, Gfx::Driver::kGameViewportHeight - size / 2);

		// Tint the marker by the hotspot's default action
		Gfx::Bitmap *marker = _hotspotMarkers[0];
		switch (hotspots[i].defaultAction) {
			case Resources::PATTable::kActionLook:
				marker = _hotspotMarkers[1];
				break;
			case Resources::PATTable::kActionUse:
				marker = _hotspotMarkers[2];
				break;
			case Resources::PATTable::kActionTalk:
				marker = _hotspotMarkers[3];
				break;
			default:
				break;
		}

		_surfaceRenderer->render(marker, Common::Point(markerPos.x - size / 2, markerPos.y - size / 2), size, size);

		// Name label next to the marker, kept at the original anchor (above)
		if (!hotspots[i].title.empty()) {
			VisualText *label = getHotspotLabel(hotspots[i].title);
			Common::Rect rect = label->getRect();
			Common::Point labelPos(pos.x + 12, pos.y - 16);
			labelPos.x = CLIP<int16>(labelPos.x, 0, Gfx::Driver::kGameViewportWidth - rect.width());
			labelPos.y = CLIP<int16>(labelPos.y, 0, Gfx::Driver::kGameViewportHeight - rect.height());
			label->render(labelPos);
		}
	}
}


void GameWindow::onMouseMove(const Common::Point &pos) {
	_renderEntries = StarkGlobal->getCurrent()->getLocation()->listRenderEntries();

	if (!StarkUserInterface->isInteractive()) {
		_objectUnderCursor = nullptr;
		_cursor->setCursorType(Cursor::kPassive);
		_cursor->setMouseHint("");
		return;
	}

	int16 selectedInventoryItem = _inventory->getSelectedInventoryItem();
	int16 singlePossibleAction = -1;
	bool defaultAction = false;
	bool itemActive = false;

	checkObjectAtPos(pos, selectedInventoryItem, singlePossibleAction, defaultAction);

	if (selectedInventoryItem != -1 && !defaultAction) {
		VisualImageXMG *cursorImage = StarkGameInterface->getCursorImage(selectedInventoryItem);
		_cursor->setCursorImage(cursorImage);
		itemActive = singlePossibleAction == selectedInventoryItem;
	} else if (_objectUnderCursor) {
		switch (singlePossibleAction) {
			case -1:
				_cursor->setCursorType(Cursor::kActive);
				break;
			case Resources::PATTable::kActionLook:
				_cursor->setCursorType(Cursor::kEye);
				break;
			case Resources::PATTable::kActionTalk:
				_cursor->setCursorType(Cursor::kMouth);
				break;
			case Resources::PATTable::kActionUse:
				_cursor->setCursorType(Cursor::kHand);
				break;
			default:
				VisualImageXMG *cursorImage = StarkGameInterface->getCursorImage(singlePossibleAction);
				_cursor->setCursorImage(cursorImage);
				break;
		}
	} else {
		// Not an object
		_cursor->setCursorType(Cursor::kDefault);
	}
	_cursor->setItemActive(itemActive);

	Common::String mouseHint;
	if (_objectUnderCursor) {
		mouseHint = StarkGameInterface->getItemTitleAt(_objectUnderCursor, _objectRelativePosition);
	}
	_cursor->setMouseHint(mouseHint);
}

void GameWindow::onClick(const Common::Point &pos) {
	if (!StarkGlobal->getCurrent()) {
		return; // No level is loaded yet, interaction is impossible
	}

	if (!StarkUserInterface->isInteractive()) {
		StarkUserInterface->markInteractionDenied();
		return;
	}

	_actionMenu->close();

	int16 selectedInventoryItem = _inventory->getSelectedInventoryItem();
	int16 singlePossibleAction = -1;
	bool defaultAction;

	checkObjectAtPos(pos, selectedInventoryItem, singlePossibleAction, defaultAction);

	if (_objectUnderCursor) {
		if (singlePossibleAction != -1) {
			StarkGameInterface->itemDoActionAt(_objectUnderCursor, singlePossibleAction, _objectRelativePosition);
		} else if (selectedInventoryItem == -1) {
			_actionMenu->open(_objectUnderCursor, _objectRelativePosition);
		}
	} else {
		// The walk code expects unscaled absolute mouse coordinates
		StarkGameInterface->walkTo(_cursor->getMousePosition(true));
	}
}

void GameWindow::onRightClick(const Common::Point &pos) {
	if (!StarkUserInterface->isInteractive()) {
		return;
	}

	int16 selectedInventoryItem = _inventory->getSelectedInventoryItem();

	if (selectedInventoryItem == -1) {
		_inventory->open();
	} else {
		_inventory->setSelectedInventoryItem(-1);
	}
}

void GameWindow::onDoubleClick(const Common::Point &pos) {
	if (!StarkUserInterface->isInteractive()) {
		StarkUserInterface->markInteractionDenied();
		return;
	}

	if (StarkGameInterface->isAprilWalking()) {
		StarkGameInterface->setAprilRunning();
	}
}

void GameWindow::checkObjectAtPos(const Common::Point &pos, int16 selectedInventoryItem, int16 &singlePossibleAction, bool &isDefaultAction) {
	_objectUnderCursor = nullptr;
	singlePossibleAction = -1;
	isDefaultAction = false;

	Math::Ray ray = StarkScene->makeRayFromMouse(_cursor->getMousePosition(true));

	Common::Rect cursorRect;
	if (selectedInventoryItem != -1) {
		cursorRect = _cursor->getHotRectangle();
		cursorRect.translate(pos.x, pos.y);
	}

	// Render entries are sorted from the farthest to the camera to the nearest
	// Loop in reverse order
	for (int i = _renderEntries.size() - 1; i >= 0; i--) {
		if (_renderEntries[i]->containsPoint(pos, _objectRelativePosition, cursorRect)
		    || _renderEntries[i]->intersectRay(ray)) {
			_objectUnderCursor = _renderEntries[i]->getOwner();
			break;
		}
	}

	if (!_objectUnderCursor || !StarkGameInterface->itemHasActionAt(_objectUnderCursor, _objectRelativePosition, -1)) {
		// Only consider items with runnable scripts
		_objectUnderCursor = nullptr;
		return;
	}

	int32 defaultAction = StarkGameInterface->itemGetDefaultActionAt(_objectUnderCursor, _objectRelativePosition);
	if (defaultAction != -1) {
		// Use the default action if there is one
		singlePossibleAction = defaultAction;
		isDefaultAction = true;
	} else if (selectedInventoryItem != -1) {
		// Use the selected inventory item if there is one
		if (StarkGameInterface->itemHasActionAt(_objectUnderCursor, _objectRelativePosition, selectedInventoryItem)) {
			singlePossibleAction = selectedInventoryItem;
		}
	} else {
		// Otherwise, use stock actions
		Resources::ActionArray actionsPossible = StarkGameInterface->listStockActionsPossibleForObjectAt(
				_objectUnderCursor, _objectRelativePosition);

		if (actionsPossible.size() == 1) {
			singlePossibleAction = actionsPossible[0];
		}
	}
}

void GameWindow::reset() {
	_renderEntries.clear();
	_objectUnderCursor = nullptr;
	_objectRelativePosition.x = 0;
	_objectRelativePosition.y = 0;

	// Hotspot titles are location specific
	clearTextCaches();
}

void GameWindow::onScreenChanged() {
	// May be called when resources have not been loaded
	if (!StarkGlobal->getCurrent()) {
		return;
	}

	Resources::Location *location = StarkGlobal->getCurrent()->getLocation();
	Common::Array<Resources::ImageText *> images = location->listChildrenRecursive<Resources::ImageText>(Resources::Image::kImageText);

	for (uint i = 0; i < images.size(); i++) {
		images[i]->resetVisual();
	}
}

} // End of namespace Stark
