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

#include "engines/stark/ui/menu/settingsmenu.h"
#include "engines/stark/ui/cursor.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/userinterface.h"
#include "engines/stark/services/staticprovider.h"
#include "engines/stark/visual/image.h"
#include "engines/stark/resources/location.h"
#include "engines/stark/resources/sound.h"
#include "engines/stark/gfx/renderentry.h"
#include "engines/stark/services/fontprovider.h"

namespace Stark {

SettingsMenuScreen::SettingsMenuScreen(Gfx::Driver *gfx, Cursor *cursor) :
		StaticLocationScreen(gfx, cursor, "OptionLocation", Screen::kScreenSettingsMenu),
		_soundManager(),
		_enhancementsPage(false),
		_pendingRebuild(false) {
}

SettingsMenuScreen::~SettingsMenuScreen() {
}

void SettingsMenuScreen::open() {
	StaticLocationScreen::open();
	_soundManager.load();

	// Always start on the main settings page
	_enhancementsPage = false;
	buildSettingsPage();
}

void SettingsMenuScreen::showEnhancementsPage() {
	// Deferred: rebuilding here would free the very widget whose click
	// handler is running. The actual switch happens in onGameLoop.
	_enhancementsPage = true;
	_pendingRebuild = true;
}

void SettingsMenuScreen::showSettingsPage() {
	_enhancementsPage = false;
	_pendingRebuild = true;
}

void SettingsMenuScreen::buildSettingsPage() {
	_widgets.push_back(new StaticLocationWidget(
			// This is the background image
			"The Longest Journey",
			nullptr,
			nullptr));

	_widgets.push_back(new StaticLocationWidget(
			"Return",
			CLICK_HANDLER(SettingsMenuScreen, backHandler),
			nullptr));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"Back",
			CLICK_HANDLER(SettingsMenuScreen, backHandler),
			nullptr));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"GSettings",
			nullptr,
			nullptr));

	_widgets.push_back(new CheckboxWidget(
			"AprilHighRes",
			StarkSettings->getBoolSetting(Settings::kHighModel),
			CLICK_HANDLER(SettingsMenuScreen, flipSettingHandler<Settings::kHighModel>),
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kHighRes>)));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"HighResHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new CheckboxWidget(
			"Subtitles",
			StarkSettings->getBoolSetting(Settings::kSubtitle),
			CLICK_HANDLER(SettingsMenuScreen, flipSettingHandler<Settings::kSubtitle>),
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kSubtitles>)));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"SubtitleHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new CheckboxWidget(
			"SpecialFX",
			StarkSettings->getBoolSetting(Settings::kSpecialFX),
			CLICK_HANDLER(SettingsMenuScreen, flipSettingHandler<Settings::kSpecialFX>),
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kSpecialFX>)));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"SpecialFXHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new CheckboxWidget(
			"Shadows",
			StarkSettings->getBoolSetting(Settings::kShadow),
			CLICK_HANDLER(SettingsMenuScreen, flipSettingHandler<Settings::kShadow>),
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kShadows>)));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"ShadowsHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new CheckboxWidget(
			"HighResFMV",
			StarkSettings->getBoolSetting(Settings::kHighFMV),
			CLICK_HANDLER(SettingsMenuScreen, flipSettingHandler<Settings::kHighFMV>),
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kHighResFMV>)));
	_widgets.back()->setupSounds(3, 4);
	_widgets.back()->setVisible(StarkSettings->hasLowResFMV());

	_widgets.push_back(new StaticLocationWidget(
			"FMVHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new StaticLocationWidget(
			"VSettings",
			nullptr,
			nullptr));

	_widgets.push_back(new VolumeWidget(
			"Voice",
			_cursor,
			_soundManager, 0,
			Settings::kVoice,
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kVoice>)));

	_widgets.push_back(new StaticLocationWidget(
			"VoiceHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new VolumeWidget(
			"Music",
			_cursor,
			_soundManager, 2,
			Settings::kMusic,
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kMusic>)));

	_widgets.push_back(new StaticLocationWidget(
			"MusicHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new VolumeWidget(
			"Sfx",
			_cursor,
			_soundManager, 1,
			Settings::kSfx,
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kSfx>)));

	_widgets.push_back(new StaticLocationWidget(
			"SfxHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	_widgets.push_back(new CheckboxWidget(
			"AllowFF",
			StarkSettings->getBoolSetting(Settings::kTimeSkip),
			CLICK_HANDLER(SettingsMenuScreen, flipSettingHandler<Settings::kTimeSkip>),
			MOVE_HANDLER(SettingsMenuScreen, textHandler<kAllowFF>)));
	_widgets.back()->setupSounds(3, 4);

	_widgets.push_back(new StaticLocationWidget(
			"AllowFFHelp",
			nullptr,
			nullptr));
	_widgets.back()->setVisible(false);

	// A single button that opens the dedicated enhancements page, placed on
	// the blank right side of the book, clear of the hover-help text.
	Common::Point enhancementsButtonPos(430, 250);
	Gfx::RenderEntry *anchorEntry = StarkStaticProvider->getLocation()->getRenderEntryByName("AprilHighRes");
	if (anchorEntry) {
		enhancementsButtonPos = anchorEntry->getPosition();
		enhancementsButtonPos.x += 210;
		enhancementsButtonPos.y += 150;
	}
	_widgets.push_back(new CustomButtonWidget(
			_gfx, "Enhancements >>", enhancementsButtonPos,
			CLICK_HANDLER(SettingsMenuScreen, showEnhancementsPage)));
	_widgets.back()->setupSounds(3, 4);
}

void SettingsMenuScreen::buildEnhancementsPage() {
	_widgets.push_back(new StaticLocationWidget("The Longest Journey", nullptr, nullptr));

	// Back to the main settings page
	_widgets.push_back(new CustomButtonWidget(
			_gfx, "<< Settings", Common::Point(90, 55),
			CLICK_HANDLER(SettingsMenuScreen, showSettingsPage)));
	_widgets.back()->setupSounds(3, 4);

	// Page title (non-interactive)
	_widgets.push_back(new CustomButtonWidget(_gfx, "Enhancements", Common::Point(285, 55), nullptr));

	// Two columns kept in the upper part of the page, clear of the decorative
	// volume waveforms baked into the book art (which sit lower-right).
	// The left column aligns with the main settings page's checkbox column.
	int leftX = 150;
	Gfx::RenderEntry *anchorEntry = StarkStaticProvider->getLocation()->getRenderEntryByName("AprilHighRes");
	if (anchorEntry) {
		leftX = anchorEntry->getPosition().x;
	}
	const int rightX = leftX + 215;
	const int startY = 105, step = 24;
	int y = startY;

	// --- Left column: visual enhancement toggles
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Soft shadows", Common::Point(leftX, y), "soft_shadows"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Depth occlusion", Common::Point(leftX, y), "enable_depth_maps"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Scene lighting", Common::Point(leftX, y), "ambient_matching"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Normal mapping", Common::Point(leftX, y), "enable_normal_mapping"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Depth fog", Common::Point(leftX, y), "enable_depth_fog"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Post-processing", Common::Point(leftX, y), "enable_post_processing"));
	_widgets.back()->setupSounds(3, 4);

	// --- Right column: UI / accessibility toggles and sizes
	y = startY;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Highlight objects", Common::Point(rightX, y), "highlight_hotspots"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Colorblind markers", Common::Point(rightX, y), "marker_colorblind"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCheckboxWidget(_gfx, "Autosave on travel", Common::Point(rightX, y), "stark_autosave_on_travel"));
	_widgets.back()->setupSounds(3, 4);

	Common::Array<int> markerValues;
	markerValues.push_back(100); markerValues.push_back(125);
	markerValues.push_back(150); markerValues.push_back(200);
	Common::Array<int> subValues;
	subValues.push_back(100); subValues.push_back(125); subValues.push_back(150);
	subValues.push_back(200); subValues.push_back(250);

	y += step;
	_widgets.push_back(new CustomCycleWidget(_gfx, "Marker size", Common::Point(rightX, y), "marker_scale", markerValues, "%"));
	_widgets.back()->setupSounds(3, 4);
	y += step;
	_widgets.push_back(new CustomCycleWidget(_gfx, "Subtitle size", Common::Point(rightX, y), "subtitle_scale", subValues, "%"));
	_widgets.back()->setupSounds(3, 4);
}

void SettingsMenuScreen::close() {
	_soundManager.close();
	ConfMan.flushToDisk();
	StaticLocationScreen::close();
}

void SettingsMenuScreen::onGameLoop() {
	if (_pendingRebuild) {
		_pendingRebuild = false;
		freeWidgets();
		if (_enhancementsPage) {
			buildEnhancementsPage();
		} else {
			buildSettingsPage();
		}
	}

	_soundManager.update();
}

void SettingsMenuScreen::handleMouseUp() {
	// The volume sliders only exist on the main settings page
	if (!_enhancementsPage && _widgets.size() > kWidgetSfx) {
		_soundManager.endLoop();
		_widgets[kWidgetVoice]->onMouseUp();
		_widgets[kWidgetMusic]->onMouseUp();
		_widgets[kWidgetSfx]->onMouseUp();
	}
}

template<SettingsMenuScreen::HelpTextIndex N>
void SettingsMenuScreen::textHandler(StaticLocationWidget &widget, const Common::Point &mousePos) {
	if (widget.isVisible()) {
		if (widget.isMouseInside(mousePos)) {
			widget.setTextColor(_textColorHovered);
			_widgets[N]->setVisible(true);
		} else {
			widget.setTextColor(_textColorDefault);
			_widgets[N]->setVisible(false);
		}
	}
}

template<Settings::BoolSettingIndex N>
void SettingsMenuScreen::flipSettingHandler() {
	StarkSettings->flipSetting(N);
}

template<int N>
void SettingsMenuScreen::flipBoolKeyHandler() {
	// N selects which engine-added boolean key to flip
	const char *key = "marker_colorblind";
	ConfMan.setBool(key, !ConfMan.getBool(key));
}

void SettingsMenuScreen::backHandler() {
	StarkUserInterface->backPrevScreen();
}

CheckboxWidget::CheckboxWidget(const char *renderEntryName, bool isChecked,
							   WidgetOnClickCallback *onClickCallback,
	            			   WidgetOnMouseMoveCallback *onMouseMoveCallback) :
		StaticLocationWidget(renderEntryName, onClickCallback, onMouseMoveCallback),
		_isChecked(isChecked) {
	// Load images
	_checkBoxImage[0] = StarkStaticProvider->getUIElement(StaticProvider::kCheckMark, 0);
	_checkBoxImage[1] = StarkStaticProvider->getUIElement(StaticProvider::kCheckMark, 1);
	_checkboxWidth = _checkBoxImage[0]->getWidth();
	_checkboxHeight = _checkBoxImage[0]->getHeight();
	_currentImage = _checkBoxImage[_isChecked];

	// Set positions
	Common::Point textPosition = getPosition();
	_position.x = textPosition.x - _checkboxWidth - 8;
	_position.y = textPosition.y - 4;
}

void CheckboxWidget::render() {
	StaticLocationWidget::render();
	_currentImage->render(_position, true);
}

bool CheckboxWidget::isMouseInside(const Common::Point &mousePos) const {
	return StaticLocationWidget::isMouseInside(mousePos) || isMouseInsideCheckbox(mousePos);
}

void CheckboxWidget::onClick() {
	StaticLocationWidget::onClick();
	_isChecked = !_isChecked;
	_currentImage = _checkBoxImage[_isChecked];
}

bool CheckboxWidget::isMouseInsideCheckbox(const Common::Point &mousePos) const {
	return mousePos.x >= _position.x && mousePos.x <= _position.x + _checkboxWidth &&
		   mousePos.y >= _position.y && mousePos.y <= _position.y + _checkboxHeight;
}

CustomCheckboxWidget::CustomCheckboxWidget(Gfx::Driver *gfx, const Common::String &text,
										   const Common::Point &textPosition, bool isChecked,
										   WidgetOnClickCallback *onClickCallback) :
		StaticLocationWidget(nullptr, onClickCallback, nullptr),
		_text(gfx) {
	init(text, textPosition, isChecked);
}

CustomCheckboxWidget::CustomCheckboxWidget(Gfx::Driver *gfx, const Common::String &text,
										   const Common::Point &textPosition, const Common::String &confKey) :
		StaticLocationWidget(nullptr, nullptr, nullptr),
		_confKey(confKey),
		_text(gfx) {
	init(text, textPosition, ConfMan.getBool(confKey));
}

void CustomCheckboxWidget::init(const Common::String &text, const Common::Point &textPosition, bool isChecked) {
	_textPosition = textPosition;
	_isChecked = isChecked;

	_text.setText(text);
	_text.setColor(_textColorDefault);
	_text.setFont(FontProvider::kCustomFont, 3);

	// Load images
	_checkBoxImage[0] = StarkStaticProvider->getUIElement(StaticProvider::kCheckMark, 0);
	_checkBoxImage[1] = StarkStaticProvider->getUIElement(StaticProvider::kCheckMark, 1);
	_checkboxWidth = _checkBoxImage[0]->getWidth();
	_checkboxHeight = _checkBoxImage[0]->getHeight();
	_currentImage = _checkBoxImage[_isChecked];

	// Set positions, mirroring CheckboxWidget's layout
	_checkboxPosition.x = _textPosition.x - _checkboxWidth - 8;
	_checkboxPosition.y = _textPosition.y - 4;
}

void CustomCheckboxWidget::render() {
	_text.render(_textPosition);
	_currentImage->render(_checkboxPosition, true);
}

bool CustomCheckboxWidget::isMouseInside(const Common::Point &mousePos) const {
	Common::Rect textRect = const_cast<CustomCheckboxWidget *>(this)->_text.getRect();

	bool insideText = mousePos.x >= _textPosition.x && mousePos.x <= _textPosition.x + textRect.width() &&
	                  mousePos.y >= _textPosition.y && mousePos.y <= _textPosition.y + textRect.height();
	bool insideCheckbox = mousePos.x >= _checkboxPosition.x && mousePos.x <= _checkboxPosition.x + _checkboxWidth &&
	                      mousePos.y >= _checkboxPosition.y && mousePos.y <= _checkboxPosition.y + _checkboxHeight;

	return insideText || insideCheckbox;
}

void CustomCheckboxWidget::onClick() {
	StaticLocationWidget::onClick();
	_isChecked = !_isChecked;
	_currentImage = _checkBoxImage[_isChecked];

	// Self-managing variant writes the ConfMan key directly
	if (!_confKey.empty()) {
		ConfMan.setBool(_confKey, _isChecked);
	}
}

CustomButtonWidget::CustomButtonWidget(Gfx::Driver *gfx, const Common::String &text,
									   const Common::Point &textPosition, WidgetOnClickCallback *onClickCallback) :
		StaticLocationWidget(nullptr, onClickCallback, nullptr),
		_text(gfx),
		_textPosition(textPosition) {
	_text.setText(text);
	_text.setColor(_textColorDefault);
	_text.setFont(FontProvider::kCustomFont, 3);
}

void CustomButtonWidget::render() {
	_text.render(_textPosition);
}

bool CustomButtonWidget::isMouseInside(const Common::Point &mousePos) const {
	Common::Rect rect = const_cast<CustomButtonWidget *>(this)->_text.getRect();
	return mousePos.x >= _textPosition.x && mousePos.x <= _textPosition.x + rect.width() &&
	       mousePos.y >= _textPosition.y && mousePos.y <= _textPosition.y + rect.height();
}

void CustomButtonWidget::onClick() {
	StaticLocationWidget::onClick();
}

void CustomButtonWidget::onMouseMove(const Common::Point &mousePos) {
	_text.setColor(isMouseInside(mousePos) ? _textColorHovered : _textColorDefault);
}

void CustomButtonWidget::onScreenChanged() {
	_text.reset();
}

void CustomCheckboxWidget::onMouseMove(const Common::Point &mousePos) {
	_text.setColor(isMouseInside(mousePos) ? _textColorHovered : _textColorDefault);
}

void CustomCheckboxWidget::onScreenChanged() {
	_text.reset();
}

CustomCycleWidget::CustomCycleWidget(Gfx::Driver *gfx, const Common::String &label,
									 const Common::Point &textPosition, const Common::String &confKey,
									 const Common::Array<int> &values, const Common::String &suffix) :
		StaticLocationWidget(nullptr, nullptr, nullptr),
		_gfx(gfx),
		_text(gfx),
		_label(label),
		_suffix(suffix),
		_confKey(confKey),
		_textPosition(textPosition),
		_values(values),
		_hovered(false) {
	_text.setColor(_textColorDefault);
	_text.setFont(FontProvider::kCustomFont, 3);
	refreshText();
}

void CustomCycleWidget::refreshText() {
	int current = ConfMan.getInt(_confKey);
	_text.setText(Common::String::format("%s: %d%s", _label.c_str(), current, _suffix.c_str()));
}

void CustomCycleWidget::render() {
	_text.render(_textPosition);
}

bool CustomCycleWidget::isMouseInside(const Common::Point &mousePos) const {
	Common::Rect rect = const_cast<CustomCycleWidget *>(this)->_text.getRect();
	return mousePos.x >= _textPosition.x && mousePos.x <= _textPosition.x + rect.width() &&
	       mousePos.y >= _textPosition.y && mousePos.y <= _textPosition.y + rect.height();
}

void CustomCycleWidget::onClick() {
	StaticLocationWidget::onClick();

	// Advance to the next preset value, wrapping around
	int current = ConfMan.getInt(_confKey);
	int nextIndex = 0;
	for (uint i = 0; i < _values.size(); i++) {
		if (_values[i] == current) {
			nextIndex = (i + 1) % _values.size();
			break;
		}
	}
	ConfMan.setInt(_confKey, _values[nextIndex]);
	refreshText();
	_text.setColor(_hovered ? _textColorHovered : _textColorDefault);
}

void CustomCycleWidget::onMouseMove(const Common::Point &mousePos) {
	_hovered = isMouseInside(mousePos);
	_text.setColor(_hovered ? _textColorHovered : _textColorDefault);
}

void CustomCycleWidget::onScreenChanged() {
	_text.reset();
}

VolumeWidget::VolumeWidget(const char *renderEntryName, Cursor *cursor,
						   TestSoundManager &soundManager, int soundIndex,
						   Settings::IntSettingIndex settingIndex,
						   WidgetOnMouseMoveCallback *onMouseMoveCallback) :
		StaticLocationWidget(renderEntryName, nullptr, onMouseMoveCallback),
		_cursor(cursor),
		_soundManager(soundManager),
		_soundIndex(soundIndex),
		_settingIndex(settingIndex),
		_isDragged(false) {
	// Load images
	_sliderImage = StarkStaticProvider->getUIElement(StaticProvider::kVolume, 0);
	_bgImage = StarkStaticProvider->getUIElement(StaticProvider::kVolume, 1);
	_bgWidth = _bgImage->getWidth();
	_bgHeight = _bgImage->getHeight();
	_sliderWidth = _sliderImage->getWidth();

	// Set positions
	_bgPosition.x = 313;
	_bgPosition.y = 303 + _settingIndex * 51;
	_sliderPosition.y = _bgPosition.y;

	_minX = _bgPosition.x;
	_maxX = _bgPosition.x + _bgWidth - _sliderWidth;
}

void VolumeWidget::render() {
	StaticLocationWidget::render();

	_sliderPosition.x = volumeToX(StarkSettings->getIntSetting(_settingIndex));

	_sliderImage->render(_sliderPosition, false);
	_bgImage->render(_bgPosition, false);
}

bool VolumeWidget::isMouseInside(const Common::Point &mousePos) const {
	return StaticLocationWidget::isMouseInside(mousePos) || isMouseInsideBg(mousePos);
}

void VolumeWidget::onClick() {
	if (isMouseInsideBg(_cursor->getMousePosition())) {
		_isDragged = true;
		_soundManager.play(_soundIndex);
	}
}

void VolumeWidget::onMouseMove(const Common::Point &mousePos) {
	if (isMouseInsideBg(mousePos)) {
		setTextColor(_textColorBgHovered);
	} else {
		StaticLocationWidget::onMouseMove(mousePos);
	}

	if (_isDragged) {
		int posX = mousePos.x - _sliderWidth / 2;
		if (posX < _minX) {
			posX = _minX;
		}
		if (posX > _maxX) {
			posX = _maxX;
		}
		StarkSettings->setIntSetting(_settingIndex, xToVolume(posX));
	}
}

void VolumeWidget::onMouseUp() {
	_isDragged = false;
}

bool VolumeWidget::isMouseInsideBg(const Common::Point &mousePos) const {
	return mousePos.x >= _bgPosition.x && mousePos.x <= _bgPosition.x + _bgWidth &&
		   mousePos.y >= _bgPosition.y && mousePos.y <= _bgPosition.y + _bgHeight;
}

TestSoundManager::TestSoundManager() :
		_currentSound(nullptr),
		_isLopping(false) {
	for (int i = 0; i < 3; ++i) {
		_sounds[i] = nullptr;
	}
}

void TestSoundManager::load() {
	for (int i = 0; i < 3; ++i) {
		_sounds[i] = StarkStaticProvider->getLocationSound(i);
		_sounds[i]->setLooping(false);
	}
}

void TestSoundManager::close() {
	stop();
	for (int i = 0; i < 3; ++i) {
		_sounds[i] = nullptr;
	}
}

void TestSoundManager::play(int index) {
	stop();
	_currentSound = _sounds[index];
	if (_currentSound) {
		_currentSound->play();
		_isLopping = true;
	}
}

void TestSoundManager::endLoop() {
	_isLopping = false;
}

void TestSoundManager::stop() {
	if (_currentSound) {
		_currentSound->stop();
		_currentSound = nullptr;
	}
	_isLopping = false;
}

void TestSoundManager::update() {
	if (_currentSound && !_currentSound->isPlaying()) {
		if (_isLopping) {
			_currentSound->play();
		} else {
			_currentSound->stop();
			_currentSound = nullptr;
		}
	}
}

} // End of namespace Stark
