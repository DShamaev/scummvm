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

#include "engines/stark/services/localization.h"

#include "engines/stark/services/services.h"
#include "engines/stark/services/settings.h"
#include "engines/stark/services/fontprovider.h"

#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/stream.h"

namespace Stark {

// Map a pack's "!codepage" value ("1252", "cp1251", "utf8", ...) to a CodePage.
static Common::CodePage parseCodePage(const Common::String &raw) {
	Common::String s = raw;
	s.toLowercase();
	if (s.hasPrefix("cp")) s = Common::String(s.c_str() + 2);
	else if (s.hasPrefix("windows-")) s = Common::String(s.c_str() + 8);
	else if (s.hasPrefix("windows")) s = Common::String(s.c_str() + 7);
	if (s == "utf8" || s == "utf-8") return Common::kUtf8;
	if (s == "ascii") return Common::kASCII;
	if (s == "1250") return Common::kWindows1250;
	if (s == "1251") return Common::kWindows1251;
	if (s == "1252") return Common::kWindows1252;
	if (s == "1253") return Common::kWindows1253;
	if (s == "1254") return Common::kWindows1254;
	if (s == "1255") return Common::kWindows1255;
	if (s == "1256") return Common::kWindows1256;
	if (s == "1257") return Common::kWindows1257;
	return Common::kWindows1252;
}

// 32-bit FNV-1a over the raw bytes of the phrase. Kept deliberately simple so
// the pack-building tool can reproduce the exact same keys.
static uint32 hashPhrase(const Common::String &s) {
	uint32 hash = 2166136261u;
	for (uint i = 0; i < s.size(); i++) {
		hash ^= (byte)s[i];
		hash *= 16777619u;
	}
	return hash;
}

// Turn "\\n" into a newline and "\\\\" into a backslash.
static Common::String unescape(const Common::String &in) {
	Common::String out;
	for (uint i = 0; i < in.size(); i++) {
		if (in[i] == '\\' && i + 1 < in.size()) {
			char n = in[i + 1];
			if (n == 'n') { out += '\n'; i++; continue; }
			if (n == '\\') { out += '\\'; i++; continue; }
		}
		out += in[i];
	}
	return out;
}

static Common::FSNode locFolder() {
	return Common::FSNode(ConfMan.getPath("path")).getChild("loc");
}

LocalizationProvider::LocalizationProvider() {
}

void LocalizationProvider::init() {
	_languages.clear();
	_loadedFile.clear();
	_map.clear();

	Language original;
	original.name = "Original";
	original.codePage = StarkSettings->getTextCodePage(); // the baked-in language
	_languages.push_back(original);

	Common::FSNode dir = locFolder();
	if (!dir.isDirectory()) {
		return;
	}

	Common::FSList list;
	if (!dir.getChildren(list, Common::FSNode::kListFilesOnly)) {
		return;
	}
	Common::sort(list.begin(), list.end());

	for (uint i = 0; i < list.size(); i++) {
		Common::String name = list[i].getName();
		if (!name.hasSuffixIgnoreCase(".loc")) {
			continue;
		}
		Language lang;
		if (readPackHeader(list[i], lang)) {
			_languages.push_back(lang);
			debug(1, "Localization: found subtitle pack '%s' (%s)", lang.name.c_str(), lang.code.c_str());
		}
	}
}

bool LocalizationProvider::readPackHeader(const Common::FSNode &node, Language &out) const {
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream) {
		return false;
	}

	out.file = node.getName();
	// Default code/name derived from the filename, overridden by header keys.
	out.code = node.getName();
	if (out.code.hasSuffixIgnoreCase(".loc")) {
		out.code = Common::String(out.code.c_str(), out.code.size() - 4);
	}
	out.name = out.code;

	while (!stream->eos()) {
		Common::String line = stream->readLine();
		if (line.empty()) {
			continue;
		}
		if (line[0] != '!') {
			break; // header ended
		}
		uint32 eq = line.findFirstOf('=');
		if (eq == Common::String::npos) {
			continue;
		}
		Common::String key(line.c_str() + 1, line.c_str() + eq);
		Common::String value(line.c_str() + eq + 1);
		key.trim();
		value.trim();
		if (key.equalsIgnoreCase("code")) {
			out.code = value;
		} else if (key.equalsIgnoreCase("name")) {
			out.name = value;
		} else if (key.equalsIgnoreCase("codepage")) {
			out.codePage = parseCodePage(value);
		} else if (key.equalsIgnoreCase("font")) {
			out.font = value;
		} else if (key.equalsIgnoreCase("fontsize")) {
			out.fontSize = (uint32)atoi(value.c_str());
		}
	}

	delete stream;
	return true;
}

void LocalizationProvider::loadPack(const Common::String &file) {
	_map.clear();
	_loadedFile = file;
	if (file.empty()) {
		return;
	}

	Common::FSNode node = locFolder().getChild(file);
	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream) {
		warning("Localization: unable to open subtitle pack '%s'", file.c_str());
		return;
	}

	while (!stream->eos()) {
		Common::String line = stream->readLine();
		if (line.empty() || line[0] == '!' || line[0] == '#' || line[0] == '/') {
			continue;
		}
		uint32 eq = line.findFirstOf('=');
		if (eq == Common::String::npos) {
			continue;
		}
		Common::String key(line.c_str(), line.c_str() + eq);
		Common::String value(line.c_str() + eq + 1);
		uint32 hash = (uint32)strtoul(key.c_str(), nullptr, 16);
		_map[hash] = unescape(value);
	}

	delete stream;
	debug(1, "Localization: loaded %u lines from '%s'", (uint)_map.size(), file.c_str());
}

uint LocalizationProvider::getSelectedIndex() const {
	Common::String code = ConfMan.hasKey("subtitle_language") ? ConfMan.get("subtitle_language") : "";
	for (uint i = 0; i < _languages.size(); i++) {
		if (_languages[i].code == code) {
			return i;
		}
	}
	return 0; // fall back to Original if the stored pack is gone
}

Common::String LocalizationProvider::getSelectedName() const {
	return _languages[getSelectedIndex()].name;
}

Common::CodePage LocalizationProvider::getActiveCodePage() const {
	return _languages[getSelectedIndex()].codePage;
}

Common::String LocalizationProvider::getActiveFontFile() const {
	uint index = getSelectedIndex();
	if (index == 0) {
		return ""; // Original: keep the game's own font
	}
	const Language &lang = _languages[index];
	if (!lang.font.empty()) {
		return lang.font; // pack ships its own font
	}
	// Pack without a font of its own: fall back to the bundled broad-coverage
	// font if one is configured/present (used only when the file actually loads).
	return ConfMan.hasKey("subtitle_fallback_font") ? ConfMan.get("subtitle_fallback_font") : "";
}

uint32 LocalizationProvider::getActiveFontSize() const {
	return _languages[getSelectedIndex()].fontSize;
}

void LocalizationProvider::selectIndex(uint index) {
	if (index >= _languages.size()) {
		index = 0;
	}
	ConfMan.set("subtitle_language", _languages[index].code);
	ConfMan.flushToDisk();
	loadPack(_languages[index].file);

	// Swap in the pack's subtitle font (or clear it) for the new selection.
	if (StarkFontProvider) {
		StarkFontProvider->setSubtitleFont(_languages[index].font, _languages[index].fontSize);
	}
}

Common::String LocalizationProvider::cycleNext() {
	uint next = (getSelectedIndex() + 1) % _languages.size();
	selectIndex(next);
	return _languages[next].name;
}

void LocalizationProvider::ensurePackLoaded() {
	const Language &lang = _languages[getSelectedIndex()];
	if (lang.file != _loadedFile) {
		loadPack(lang.file);
	}
}

Common::String LocalizationProvider::translate(const Common::String &phrase) {
	if (getSelectedIndex() == 0 || phrase.empty()) {
		return phrase; // original text selected
	}

	ensurePackLoaded();
	if (_map.empty()) {
		return phrase;
	}

	Common::HashMap<uint32, Common::String>::const_iterator it = _map.find(hashPhrase(phrase));
	if (it != _map.end()) {
		return it->_value;
	}
	return phrase; // untranslated line: keep the original
}

} // End of namespace Stark
