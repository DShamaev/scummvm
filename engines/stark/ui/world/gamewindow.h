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

#ifndef STARK_UI_GAME_WINDOW_H
#define STARK_UI_GAME_WINDOW_H

#include "engines/stark/gfx/faderenderer.h"
#include "engines/stark/gfx/renderentry.h"
#include "engines/stark/gfx/surfacerenderer.h"
#include "engines/stark/ui/window.h"

#include "common/scummsys.h"
#include "common/rect.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"

namespace Stark {

class ActionMenu;
class InventoryWindow;
class VisualText;

class GameWindow : public Window {
public:
	GameWindow(Gfx::Driver *gfx, Cursor *cursor, ActionMenu *actionMenu, InventoryWindow *inventory);
	virtual ~GameWindow();

	/** Clear the location dependent state */
	void reset();

	/** Update when the screen resolution has changed */
	void onScreenChanged();

	/** Toggle the display of exit locations */
	void toggleExitDisplay() { _displayExit = !_displayExit; }

protected:
	void onMouseMove(const Common::Point &pos) override;
	void onClick(const Common::Point &pos) override;
	void onRightClick(const Common::Point &pos) override;
	void onDoubleClick(const Common::Point &pos) override;
	void onRender() override;

	void checkObjectAtPos(const Common::Point &pos, int16 selectedInventoryItem, int16 &singlePossibleAction, bool &isDefaultAction);

	/** Draw pulsing markers over the interactive items of the location */
	void renderHotspotMarkers();

	/** (Re)build the four action-tinted marker textures from the current palette setting */
	void buildMarkerBitmaps();

	/** Build a procedural soft dot texture with the given tint */
	Gfx::Bitmap *createMarkerBitmap(float tintR, float tintG, float tintB);

	/** Get or create the cached label visual for a hotspot title */
	VisualText *getHotspotLabel(const Common::String &title);

	/** Free the cached hotspot labels */
	void clearTextCaches();


	ActionMenu *_actionMenu;
	InventoryWindow *_inventory;

	Gfx::RenderEntryArray _renderEntries;
	Resources::ItemVisual *_objectUnderCursor;
	Common::Point _objectRelativePosition;

	Gfx::FadeRenderer *_fadeRenderer;
	Gfx::SurfaceRenderer *_surfaceRenderer;
	Gfx::Bitmap *_hotspotMarkers[4];
	bool _markerColorblindCache;

	Common::HashMap<Common::String, VisualText *> _hotspotLabels;

	VisualImageXMG *_exitArrow, *_exitArrowLeft, *_exitArrowRight;
	int _exitLeftBoundary, _exitRightBoundary;

	bool _displayExit;
};

} // End of namespace Stark

#endif // STARK_UI_GAME_WINDOW_H
