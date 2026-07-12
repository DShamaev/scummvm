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

#include "engines/stark/services/settings.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/archiveloader.h"

#include "common/config-manager.h"
#include "common/debug.h"

#include "audio/mixer.h"

#include "engines/advancedDetector.h"

namespace Stark {

Settings::Settings(Audio::Mixer *mixer, const ADGameDescription *gd) :
		_mixer(mixer),
		_isDemo(gd->flags & ADGF_DEMO),
		_language(gd->language) {
	// Initialize keys
	_boolKey[kHighModel] = "enable_high_resolution_models";
	_boolKey[kSubtitle] = "subtitles";
	_boolKey[kSpecialFX] = "enable_special_effects";
	_boolKey[kShadow] = "enable_shadows";
	_boolKey[kHighFMV] = "play_high_resolution_videos";
	_boolKey[kTimeSkip] = "enable_time_skip";
	_boolKey[kHighlightHotspots] = "highlight_hotspots";
	_boolKey[kSoftShadows] = "soft_shadows";
	_boolKey[kDepthMaps] = "enable_depth_maps";
	_boolKey[kAmbientMatching] = "ambient_matching";
	_boolKey[kNormalMapping] = "enable_normal_mapping";
	_boolKey[kDepthFog] = "enable_depth_fog";
	_intKey[kVoice] = "speech_volume";
	_intKey[kMusic] = "music_volume";
	_intKey[kSfx] = "sfx_volume";
	_intKey[kSaveLoadPage] = "saveload_lastpage";

	// Register default settings
	ConfMan.registerDefault(_boolKey[kHighModel], true);
	ConfMan.registerDefault(_boolKey[kSubtitle], true);
	ConfMan.registerDefault(_boolKey[kSpecialFX], true);
	ConfMan.registerDefault(_boolKey[kShadow], true);
	ConfMan.registerDefault(_boolKey[kHighFMV], true);
	ConfMan.registerDefault(_boolKey[kTimeSkip], false);
	ConfMan.registerDefault(_boolKey[kHighlightHotspots], false);
	ConfMan.registerDefault(_boolKey[kSoftShadows], true);
	ConfMan.registerDefault(_boolKey[kDepthMaps], true);
	ConfMan.registerDefault(_boolKey[kAmbientMatching], true);
	ConfMan.registerDefault(_boolKey[kNormalMapping], true);
	ConfMan.registerDefault(_boolKey[kDepthFog], false);
	ConfMan.registerDefault("fog_density", 45);   // percent

	// Post-processing pipeline (percent-based; 100 = neutral where noted)
	ConfMan.registerDefault("enable_post_processing", false);
	ConfMan.registerDefault("grade_brightness", 0);    // additive, 0 = neutral
	ConfMan.registerDefault("grade_contrast", 100);    // 100 = neutral
	ConfMan.registerDefault("grade_saturation", 100);  // 100 = neutral
	ConfMan.registerDefault("grade_tint_r", 100);      // 100 = neutral
	ConfMan.registerDefault("grade_tint_g", 100);
	ConfMan.registerDefault("grade_tint_b", 100);
	ConfMan.registerDefault("vignette_strength", 0);
	ConfMan.registerDefault("grain_strength", 0);
	ConfMan.registerDefault("sharpen_strength", 0);

	// Depth of field: focus on the character, soften by distance
	ConfMan.registerDefault("enable_depth_of_field", false);
	ConfMan.registerDefault("dof_strength", 3);    // max blur radius in texels (inline)
	ConfMan.registerDefault("dof_range", 200);     // falloff width, % of focus distance

	// Contact ambient occlusion (depth-only), filmic tonemap and bloom
	ConfMan.registerDefault("ssao_strength", 0);   // percent, 0 = off (per-scene base)
	ConfMan.registerDefault("ssao_master", 100);   // master gain over per-scene SSAO, %
	ConfMan.registerDefault("ssao_radius", 18);    // sample radius in texels
	ConfMan.registerDefault("tonemap_strength", 0);// percent ACES mix, 0 = off
	ConfMan.registerDefault("bloom_strength", 0);  // percent, 0 = off
	ConfMan.registerDefault("bloom_threshold", 70);// percent luminance cutoff
	ConfMan.registerDefault("auto_scene_post", true); // derive per-scene defaults
	ConfMan.registerDefault("post_master", 100);   // master grade intensity, % (100 = full)
	// Multi-pass FBO post: wide, smooth bloom (bright-pass + separable Gaussian)
	// instead of the single-pass inline bloom. Now that FBOs work on this stack.
	ConfMan.registerDefault("enable_hq_post", true);
	// SSAO/DoF use the real GL depth buffer (includes the character) rather than
	// the background depth mask. Confirmed working on Apple GL-over-Metal (the
	// earlier "hang" was the pause-key bug); mask is the fallback if a stack
	// rejects the depth copy.
	ConfMan.registerDefault("enable_depth_copy", true);
	// Stamp floor-positioned foreground sprites (furniture, doors) that lack a
	// per-pixel depth map into the depth buffer at their camera distance, so the
	// post pass sees them at true depth instead of the background behind them.
	// Depth-only (never changes colour), so it is safe to leave on.
	ConfMan.registerDefault("enable_sprite_depth", true);
	// Take the sprite depth further: also write it during the colour render (with
	// a depth test) so characters are occluded per-pixel by flat foreground props
	// and walls, instead of whole-sprite draw order. Off by default - it changes
	// actual rendering, so it is opt-in until validated per scene.
	ConfMan.registerDefault("enable_sprite_occlusion", false);
	ConfMan.registerDefault(_intKey[kSaveLoadPage], 0);
	ConfMan.registerDefault("replacement_png_premultiply_alpha", false);
	ConfMan.registerDefault("debug_show_depth", false);
	ConfMan.registerDefault("post_debug_view", 0);   // 0=off,1=depth,2=dyn mask,3=bg mask
	ConfMan.registerDefault("debug_show_normals", false);
	ConfMan.registerDefault("scene_lighting_strength", 60);   // percent
	// Lowest the ambient-matching may dim a character (percent). Lower = the
	// character goes darker in dark rooms (less "spotlit"); higher = more
	// readable but can look lit independently of a dark scene.
	ConfMan.registerDefault("character_min_light", 40);
	// Master switch for the enhanced per-pixel actor additions (specular, rim,
	// normal mapping). Off falls back to plain per-pixel diffuse.
	ConfMan.registerDefault("enhanced_actor_light", true);
	// Global specular intensity dial (percent, 100 = tuned defaults). Lower to
	// tame highlight twinkle on animating low-poly meshes; 0 = no specular.
	ConfMan.registerDefault("specular_scale", 100);
	ConfMan.registerDefault("marker_scale", 100);          // percent
	ConfMan.registerDefault("marker_colorblind", false);
	ConfMan.registerDefault("subtitle_scale", 100);        // percent
	ConfMan.registerDefault("subtitle_autoscroll", true);  // scroll long subtitles in sync with the voice
	ConfMan.registerDefault("subtitle_scroll_ms_per_char", 55); // scroll pacing fallback
	ConfMan.registerDefault("subtitle_language", "");      // "" = original baked-in text
	// Broad-coverage fallback font for subtitle packs that don't ship their own.
	// Drop a TTF with this name in fonts/ or loc/ and it is used automatically
	// for any selected pack whose script the stylised game font can't render.
	ConfMan.registerDefault("subtitle_fallback_font", "subtitle_fallback.ttf");
	ConfMan.registerDefault("stark_autosave_on_travel", true);
	ConfMan.registerDefault("texture_anisotropy", true);
	ConfMan.registerDefault("texture_anisotropy_level", 16);

	// Soft shadow quality: number of jittered passes (each redraws the mesh).
	// 8 keeps the penumbra smooth while halving the cost of the 16-pass version,
	// which matters a lot with the high-poly enhanced meshes.
	ConfMan.registerDefault("shadow_passes", 8);

	// Depth occlusion bias: how far the background is pushed back (percent of
	// the scene depth range) so estimation noise doesn't eat the character.
	ConfMan.registerDefault("depth_bias", 6);
	ConfMan.registerDefault("ignore_font_settings", true);

	// Use the FunCom logo video to check low-resolution fmv
	Common::SeekableReadStream *lowResFMV = StarkArchiveLoader->getExternalFile("1402_lo_res.bbb", "Global/");
	_hasLowRes = lowResFMV;
	delete lowResFMV;
}

void Settings::setIntSetting(IntSettingIndex index, int value) {
	ConfMan.setInt(_intKey[index], value);

	Audio::Mixer::SoundType type;
	switch (index) {
		case kVoice:
			type = Audio::Mixer::kSpeechSoundType;
			break;
		case kMusic:
			type = Audio::Mixer::kMusicSoundType;
			break;
		case kSfx:
			type = Audio::Mixer::kSFXSoundType;
			break;
		default:
			return;
	}

	_mixer->setVolumeForSoundType(type, value);
}

bool Settings::isAssetsModEnabled() const {
	return ConfMan.getBool("enable_assets_mod");
}

bool Settings::shouldPreMultiplyReplacementPNGs() const {
	return ConfMan.getBool("replacement_png_premultiply_alpha");
}

Gfx::Bitmap::SamplingFilter Settings::getImageSamplingFilter() const {
	return ConfMan.getBool("use_linear_filtering") ? Gfx::Bitmap::kLinear : Gfx::Bitmap::kNearest;
}

bool Settings::isFontAntialiasingEnabled() const {
	return ConfMan.getBool("enable_font_antialiasing");
}

Common::CodePage Settings::getTextCodePage() const {
	switch (_language) {
	case Common::PL_POL:
		return Common::kWindows1250;
	case Common::RU_RUS:
		return Common::kWindows1251;
	case Common::HE_ISR:
		return Common::kWindows1255;
	default:
		return Common::kWindows1252;
	}
}

bool Settings::shouldIgnoreFontSettings() const {
	return ConfMan.getBool("ignore_font_settings") && _language == Common::EN_ANY;
}

Common::Language Settings::getLanguage() const {
	return _language;
}

} // End of namespace Stark
