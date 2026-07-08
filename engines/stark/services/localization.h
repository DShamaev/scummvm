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

#ifndef STARK_SERVICES_LOCALIZATION_H
#define STARK_SERVICES_LOCALIZATION_H

#include "common/str.h"
#include "common/str-enc.h"
#include "common/array.h"
#include "common/hashmap.h"

namespace Common {
class FSNode;
}

namespace Stark {

/**
 * Subtitle localization.
 *
 * The game's dialogue text is baked into the resource archives (one language
 * per install). This service adds optional, drop-in "subtitle packs" that
 * override the on-screen text at runtime without touching the voice track.
 *
 * Packs are plain-text files placed in a "loc" folder next to the game data
 * ("<gamedir>/loc/<code>.loc"). Each pack starts with a small header and then
 * a list of "<hash>=<text>" entries, where <hash> is the 32-bit FNV-1a hash of
 * the original phrase and <text> is the translation (with "\\n" for newlines):
 *
 *   !code=fr
 *   !name=Francais
 *   !codepage=1252
 *   1a2b3c4d=Bonjour, April.
 *
 * The picker on the Enhancements settings page lists "Original" plus every pack
 * found, and the selection is stored in the "subtitle_language" config key.
 */
class LocalizationProvider {
public:
	struct Language {
		Common::String code;   ///< "" for the original baked-in text
		Common::String name;   ///< display name shown in the picker
		Common::String file;   ///< pack filename, "" for the original
		Common::CodePage codePage; ///< byte encoding of this pack's text
		Common::String font;       ///< optional TTF for scripts the base font lacks
		uint32 fontSize;           ///< base point size for the pack font, 0 = default
		Language() : codePage(Common::kWindows1252), fontSize(0) {}
	};

	LocalizationProvider();

	/** Scan the loc folder and build the list of available languages */
	void init();

	/** All selectable languages, index 0 always being the original text */
	const Common::Array<Language> &getLanguages() const { return _languages; }

	/** True when at least one subtitle pack was found */
	bool hasPacks() const { return _languages.size() > 1; }

	/** Index of the currently selected language within getLanguages() */
	uint getSelectedIndex() const;

	/** Display name of the currently selected language */
	Common::String getSelectedName() const;

	/** Byte encoding subtitles should be decoded with for the current selection */
	Common::CodePage getActiveCodePage() const;

	/** Subtitle font supplied by the current pack, or "" to use the default */
	Common::String getActiveFontFile() const;

	/** Base point size for the current pack's font, or 0 for the default */
	uint32 getActiveFontSize() const;

	/** Select a language by index (wrapping), persisting it and loading the pack */
	void selectIndex(uint index);

	/** Advance to the next available language; returns its display name */
	Common::String cycleNext();

	/**
	 * Return the translation for a baked subtitle phrase, or the phrase itself
	 * when the original language is selected or no translation exists.
	 */
	Common::String translate(const Common::String &phrase);

private:
	bool readPackHeader(const Common::FSNode &node, Language &out) const;
	void ensurePackLoaded();
	void loadPack(const Common::String &file);

	Common::Array<Language> _languages;
	Common::String _loadedFile;                     ///< pack currently held in _map
	Common::HashMap<uint32, Common::String> _map;   ///< active override table
};

} // End of namespace Stark

#endif // STARK_SERVICES_LOCALIZATION_H
