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

#ifndef STARK_UI_MENU_SETTINGS_MENU_H
#define STARK_UI_MENU_SETTINGS_MENU_H

#include "engines/stark/ui/menu/locationscreen.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/settings.h"
#include "engines/stark/visual/text.h"

namespace Stark {

class VisualImageXMG;

/**
 * Manager of test sound
 */
class TestSoundManager {
public:
	TestSoundManager();
	~TestSoundManager() {}

	/** Load sounds **/
	void load();

	/** Close the sound manager and reset pointers **/
	void close();

	/** play a specific sound in a loop */
	void play(int index);

	/** request to end the playing loop */
	void endLoop();

	/** stop any currently playing sound */
	void stop();

	/** update on game frame */
	void update();

private:
	Resources::Sound *_currentSound;
	Resources::Sound *_sounds[3];
	bool _isLopping;
};

/**
 * The setting menu of the game
 */
class SettingsMenuScreen : public StaticLocationScreen {
public:
	SettingsMenuScreen(Gfx::Driver *gfx, Cursor *cursor);
	virtual ~SettingsMenuScreen();

	// StaticLocationScreen API
	void open() override;
	void close() override;
	void onGameLoop() override;

	void handleMouseUp();

private:
	enum HelpTextIndex {
		kHighRes = 5,
		kSubtitles = 7,
		kSpecialFX = 9,
		kShadows = 11,
		kHighResFMV = 13,
		kVoice = 16,
		kMusic = 18,
		kSfx = 20,
		kAllowFF = 22
	};

	enum WidgetIndex {
		kWidgetVoice = 15,
		kWidgetMusic = 17,
		kWidgetSfx = 19
	};

	template<HelpTextIndex N>
	void textHandler(StaticLocationWidget &widget, const Common::Point &mousePos);

	template<Settings::BoolSettingIndex N>
	void flipSettingHandler();

	/** Flip a plain boolean ConfMan key (for engine-added settings) */
	template<int N>
	void flipBoolKeyHandler();

	void backHandler();

	/** Build each page's widgets */
	void buildSettingsPage();
	void buildEnhancementsPage();

	/** Switch between the two pages, rebuilding the widgets */
	void showEnhancementsPage();
	void showSettingsPage();

private:
	const Gfx::Color _textColorHovered = Gfx::Color(0x1E, 0x1E, 0x96);
	const Gfx::Color _textColorDefault = Gfx::Color(0x00, 0x00, 0x00);

	TestSoundManager _soundManager;
	bool _enhancementsPage;
	bool _pendingRebuild;
};

/**
 * Widget with a checkbox
 */
class CheckboxWidget : public StaticLocationWidget {
public:
	CheckboxWidget(const char *renderEntryName, bool isChecked,
				   WidgetOnClickCallback *onClickCallback,
	               WidgetOnMouseMoveCallback *onMouseMoveCallback);
	virtual ~CheckboxWidget() {};

	// StaticLocationWidget API
	void render() override;
	bool isMouseInside(const Common::Point &mousePos) const override;
	void onClick() override;

private:
	VisualImageXMG *_currentImage;
	VisualImageXMG *_checkBoxImage[2];
	Common::Point _position;
	int _checkboxWidth, _checkboxHeight;
	bool _isChecked;

	bool isMouseInsideCheckbox(const Common::Point &mousePos) const;
};

/**
 * Checkbox widget with an engine-provided label, not bound to a location render entry.
 *
 * Used for settings added by the engine which have no widget in the game's
 * original settings location.
 */
class CustomCheckboxWidget : public StaticLocationWidget {
public:
	CustomCheckboxWidget(Gfx::Driver *gfx, const Common::String &text,
	                     const Common::Point &textPosition, bool isChecked,
	                     WidgetOnClickCallback *onClickCallback);

	/** Self-managing variant: reads/writes a boolean ConfMan key directly */
	CustomCheckboxWidget(Gfx::Driver *gfx, const Common::String &text,
	                     const Common::Point &textPosition, const Common::String &confKey);
	virtual ~CustomCheckboxWidget() {};

	// StaticLocationWidget API
	void render() override;
	bool isMouseInside(const Common::Point &mousePos) const override;
	void onClick() override;
	void onMouseMove(const Common::Point &mousePos) override;
	void onScreenChanged() override;

private:
	void init(const Common::String &text, const Common::Point &textPosition, bool isChecked);

	const Gfx::Color _textColorHovered = Gfx::Color(0x1E, 0x1E, 0x96);
	const Gfx::Color _textColorDefault = Gfx::Color(0x00, 0x00, 0x00);

	Common::String _confKey;   // empty for the callback variant
	VisualText _text;
	Common::Point _textPosition;
	Common::Point _checkboxPosition;
	VisualImageXMG *_currentImage;
	VisualImageXMG *_checkBoxImage[2];
	int _checkboxWidth, _checkboxHeight;
	bool _isChecked;
};

/**
 * Engine-added widget that cycles an integer ConfMan setting through a set
 * of preset values on click, showing "Label: value%".
 */
class CustomCycleWidget : public StaticLocationWidget {
public:
	CustomCycleWidget(Gfx::Driver *gfx, const Common::String &label,
	                  const Common::Point &textPosition, const Common::String &confKey,
	                  const Common::Array<int> &values, const Common::String &suffix);
	virtual ~CustomCycleWidget() {};

	// StaticLocationWidget API
	void render() override;
	bool isMouseInside(const Common::Point &mousePos) const override;
	void onClick() override;
	void onMouseMove(const Common::Point &mousePos) override;
	void onScreenChanged() override;

private:
	void refreshText();

	const Gfx::Color _textColorHovered = Gfx::Color(0x1E, 0x1E, 0x96);
	const Gfx::Color _textColorDefault = Gfx::Color(0x00, 0x00, 0x00);

	Gfx::Driver *_gfx;
	VisualText _text;
	Common::String _label;
	Common::String _suffix;
	Common::String _confKey;
	Common::Point _textPosition;
	Common::Array<int> _values;
	bool _hovered;
};

/**
 * Engine-added widget that cycles the subtitle language through the detected
 * localization packs, showing "Label: <language name>".
 */
class LanguageWidget : public StaticLocationWidget {
public:
	LanguageWidget(Gfx::Driver *gfx, const Common::String &label, const Common::Point &textPosition);
	virtual ~LanguageWidget() {};

	// StaticLocationWidget API
	void render() override;
	bool isMouseInside(const Common::Point &mousePos) const override;
	void onClick() override;
	void onMouseMove(const Common::Point &mousePos) override;
	void onScreenChanged() override;

private:
	void refreshText();

	const Gfx::Color _textColorHovered = Gfx::Color(0x1E, 0x1E, 0x96);
	const Gfx::Color _textColorDefault = Gfx::Color(0x00, 0x00, 0x00);

	Gfx::Driver *_gfx;
	VisualText _text;
	Common::String _label;
	Common::Point _textPosition;
	bool _hovered;
};

/**
 * A clickable text button not bound to a location render entry.
 * Used to navigate between the settings page and the enhancements page.
 */
class CustomButtonWidget : public StaticLocationWidget {
public:
	CustomButtonWidget(Gfx::Driver *gfx, const Common::String &text,
	                   const Common::Point &textPosition, WidgetOnClickCallback *onClickCallback);
	virtual ~CustomButtonWidget() {};

	// StaticLocationWidget API
	void render() override;
	bool isMouseInside(const Common::Point &mousePos) const override;
	void onClick() override;
	void onMouseMove(const Common::Point &mousePos) override;
	void onScreenChanged() override;

private:
	const Gfx::Color _textColorHovered = Gfx::Color(0x1E, 0x1E, 0x96);
	const Gfx::Color _textColorDefault = Gfx::Color(0x00, 0x00, 0x00);

	VisualText _text;
	Common::Point _textPosition;
};

/**
 * Widget with a dragged slider for twisting the volume
 */
class VolumeWidget : public StaticLocationWidget {
public:
	VolumeWidget(const char *renderEntryName, Cursor *cursor,
				 TestSoundManager &soundManager, int soundIndex,
				 Settings::IntSettingIndex settingIndex,
				 WidgetOnMouseMoveCallback *onMouseMoveCallback);
	virtual ~VolumeWidget() {};

	// StaticLocationWidget API
	void render() override;
	bool isMouseInside(const Common::Point &mousePos) const override;
	void onClick() override;
	void onMouseMove(const Common::Point &mousePos) override;
	void onMouseUp() override;

private:
	const Gfx::Color _textColorBgHovered = Gfx::Color(0xFF, 0xFF, 0xFF);
	static const int _maxVolume = 256;

	VisualImageXMG *_sliderImage;
	VisualImageXMG *_bgImage;

	Cursor *_cursor;

	TestSoundManager &_soundManager;
	const int _soundIndex;

	Common::Point _sliderPosition, _bgPosition;
	int _bgWidth, _bgHeight, _sliderWidth, _minX, _maxX;

	bool _isDragged;
	const Settings::IntSettingIndex _settingIndex;

	bool isMouseInsideBg(const Common::Point &mousePos) const;

	int volumeToX(int volume) {
		return volume * (_maxX - _minX) / _maxVolume + _minX;
	}

	int xToVolume(int x) {
		return (x - _minX) * _maxVolume / (_maxX - _minX);
	}
};

} // End of namespace Stark

#endif // STARK_UI_MENU_SETTING_MENU_H
