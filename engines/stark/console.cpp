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

#include "engines/stark/console.h"

#include "engines/stark/formats/xarc.h"
#include "engines/stark/resources/object.h"
#include "engines/stark/resources/anim.h"
#include "engines/stark/resources/level.h"
#include "engines/stark/resources/location.h"
#include "engines/stark/resources/level.h"
#include "engines/stark/resources/knowledge.h"
#include "engines/stark/resources/root.h"
#include "engines/stark/resources/script.h"
#include "engines/stark/resources/knowledgeset.h"
#include "engines/stark/resources/item.h"
#include "engines/stark/resources/textureset.h"
#include "engines/stark/services/archiveloader.h"
#include "engines/stark/services/dialogplayer.h"
#include "engines/stark/services/global.h"
#include "engines/stark/services/resourceprovider.h"
#include "engines/stark/services/userinterface.h"
#include "engines/stark/services/fontprovider.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/staticprovider.h"
#include "engines/stark/tools/decompiler.h"

#include "engines/stark/gfx/renderentry.h"
#include "engines/stark/gfx/driver.h"
#include "engines/stark/model/model.h"
#include "engines/stark/resources/bonesmesh.h"
#include "engines/stark/resources/camera.h"
#include "engines/stark/resources/floor.h"
#include "engines/stark/resources/floorface.h"
#include "engines/stark/resources/image.h"
#include "engines/stark/scene.h"
#include "engines/stark/services/global.h"
#include "engines/stark/visual/image.h"

#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "image/png.h"

namespace Stark {

Console::Console() :
		GUI::Debugger(),
		_crawlActive(false),
		_crawlScenes(false),
		_crawlModels(false),
		_crawlForce(false),
		_crawlWait(0) {
	registerCmd("dumpArchive",          WRAP_METHOD(Console, Cmd_DumpArchive));
	registerCmd("dumpRoot",             WRAP_METHOD(Console, Cmd_DumpRoot));
	registerCmd("dumpStatic",           WRAP_METHOD(Console, Cmd_DumpStatic));
	registerCmd("dumpGlobal",           WRAP_METHOD(Console, Cmd_DumpGlobal));
	registerCmd("dumpLevel",            WRAP_METHOD(Console, Cmd_DumpLevel));
	registerCmd("dumpKnowledge",        WRAP_METHOD(Console, Cmd_DumpKnowledge));
	registerCmd("dumpLocation",         WRAP_METHOD(Console, Cmd_DumpLocation));
	registerCmd("dumpSceneData",        WRAP_METHOD(Console, Cmd_DumpSceneData));
	registerCmd("depthViz",             WRAP_METHOD(Console, Cmd_DepthViz));
	registerCmd("toggle",               WRAP_METHOD(Console, Cmd_Toggle));
	registerCmd("setInt",               WRAP_METHOD(Console, Cmd_SetInt));
	registerCmd("setBool",              WRAP_METHOD(Console, Cmd_SetBool));
	registerCmd("postInfo",             WRAP_METHOD(Console, Cmd_PostInfo));
	registerCmd("renderEntries",        WRAP_METHOD(Console, Cmd_RenderEntries));
	registerCmd("postPreset",           WRAP_METHOD(Console, Cmd_PostPreset));
	registerCmd("dumpModels",           WRAP_METHOD(Console, Cmd_DumpModels));
	registerCmd("dumpModelsOriginal",   WRAP_METHOD(Console, Cmd_DumpModelsOriginal));
	registerCmd("dumpAll",              WRAP_METHOD(Console, Cmd_DumpAll));
	registerCmd("listScripts",          WRAP_METHOD(Console, Cmd_ListScripts));
	registerCmd("enableScript",         WRAP_METHOD(Console, Cmd_EnableScript));
	registerCmd("forceScript",          WRAP_METHOD(Console, Cmd_ForceScript));
	registerCmd("decompileScript",      WRAP_METHOD(Console, Cmd_DecompileScript));
	registerCmd("testDecompiler",       WRAP_METHOD(Console, Cmd_TestDecompiler));
	registerCmd("listAnimations",       WRAP_METHOD(Console, Cmd_ListAnimations));
	registerCmd("forceAnimation",       WRAP_METHOD(Console, Cmd_ForceAnimation));
	registerCmd("listInventoryItems",   WRAP_METHOD(Console, Cmd_ListInventoryItems));
	registerCmd("listLocations",        WRAP_METHOD(Console, Cmd_ListLocations));
	registerCmd("location",             WRAP_METHOD(Console, Cmd_Location));
	registerCmd("chapter",              WRAP_METHOD(Console, Cmd_Chapter));
	registerCmd("changeLocation",       WRAP_METHOD(Console, Cmd_ChangeLocation));
	registerCmd("changeChapter",        WRAP_METHOD(Console, Cmd_ChangeChapter));
	registerCmd("changeKnowledge",      WRAP_METHOD(Console, Cmd_ChangeKnowledge));
	registerCmd("enableInventoryItem",  WRAP_METHOD(Console, Cmd_EnableInventoryItem));
	registerCmd("extractAllTextures",   WRAP_METHOD(Console, Cmd_ExtractAllTextures));
}

Console::~Console() {
}

bool Console::Cmd_DumpArchive(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Extract all the files from a game archive\n");
		debugPrintf("The destination folder, named 'dump', is in the location ScummVM was launched from\n");
		debugPrintf("Usage :\n");
		debugPrintf("dumpArchive [path to archive]\n");
		return true;
	}

	Formats::XARCArchive xarc;
	if (!xarc.open(argv[1])) {
		debugPrintf("Can't open archive with name '%s'\n", argv[1]);
		return true;
	}

	Common::ArchiveMemberList members;
	xarc.listMembers(members);

	for (Common::ArchiveMemberList::const_iterator it = members.begin(); it != members.end(); it++) {
		Common::Path fileName(Common::String::format("dump/%s", it->get()->getName().c_str()));

		// Open the output file
		Common::DumpFile outFile;
		if (!outFile.open(fileName, true)) {
			debugPrintf("Unable to open file '%s' for writing\n", fileName.toString().c_str());
			return true;
		}

		// Copy the archive content to the output file using a temporary buffer
		Common::SeekableReadStream *inStream = it->get()->createReadStream();
		uint8 *buf = new uint8[inStream->size()];

		inStream->read(buf, inStream->size());
		outFile.write(buf, inStream->size());

		delete[] buf;
		delete inStream;
		outFile.close();

		debugPrintf("Extracted '%s'\n", it->get()->getName().c_str());
	}

	return true;
}

static Common::String sanitizeFileName(const Common::String &in) {
	Common::String out;
	for (uint i = 0; i < in.size(); i++) {
		char c = in[i];
		out += (Common::isAlnum(c) || c == '-' || c == '.') ? c : '_';
	}
	return out;
}

bool Console::Cmd_DumpSceneData(int argc, const char **argv) {
	if (!StarkGlobal->getCurrent()) {
		debugPrintf("Only available in-game, once a location is loaded\n");
		return true;
	}

	dumpCurrentSceneData();
	return true;
}

void Console::dumpCurrentSceneData() {
	Current *current = StarkGlobal->getCurrent();

	Resources::Level *level = current->getLevel();
	Resources::Location *location = current->getLocation();
	Resources::Floor *floor = current->getFloor();

	if (!level || !location) {
		debugPrintf("Scene not fully loaded, skipping dump\n");
		return;
	}

	Common::String dumpDir = Common::String::format("dump/scene_%02x_%02x",
			level->getIndex(), location->getIndex());

	// -- Dump the background (and other still) images currently loaded
	Common::Array<Resources::Image *> images = location->listChildrenRecursive<Resources::Image>();
	Gfx::RenderEntryArray renderEntries = location->listRenderEntries();
	int dumpedImages = 0;
	Common::String manifest = "[\n";

	for (uint i = 0; i < images.size(); i++) {
		Visual *visual = images[i]->getVisual();
		VisualImageXMG *imageVisual = visual ? visual->get<VisualImageXMG>() : nullptr;
		if (!imageVisual || !imageVisual->getSurface()) {
			continue;
		}

		Resources::Item *parentItem = images[i]->findParent<Resources::Item>();
		bool isBackground = parentItem && parentItem->getSubType() == Resources::Item::kItemBackground;

		Common::String imageName = sanitizeFileName(images[i]->getFilename().baseName());
		Common::Path outPath(Common::String::format("%s/%s%s.png",
				dumpDir.c_str(), isBackground ? "background_" : "", imageName.c_str()));

		Common::DumpFile outFile;
		if (!outFile.open(outPath, true)) {
			debugPrintf("Unable to open '%s' for writing\n", outPath.toString().c_str());
			continue;
		}

		Image::writePNG(outFile, *imageVisual->getSurface());
		outFile.close();
		dumpedImages++;

		// Print where a replacement / depth map for this image needs to be placed
		Common::String pngName = images[i]->getFilename().baseName();
		if (pngName.hasSuffixIgnoreCase(".xmg")) {
			pngName = Common::String(pngName.c_str(), pngName.size() - 4);
		}
		Common::Path modPath = StarkArchiveLoader->getExternalFilePath(
				Common::Path(pngName + "-depth.png"), images[i]->getArchiveName());
		debugPrintf("%s -> %s (depth map target: %s)\n",
				imageName.c_str(), outPath.toString().c_str(), modPath.toString().c_str());

		// Locate the item's current on-screen position for overlay props, plus
		// its eye-space distance (|sort key|) - the anchor a prop depth map is
		// calibrated around.
		bool positioned = false;
		Common::Point itemPosition;
		float itemDistance = 0.0f;
		for (uint j = 0; j < renderEntries.size(); j++) {
			if (renderEntries[j]->getOwner() == parentItem) {
				itemPosition = renderEntries[j]->getPosition();
				itemDistance = ABS(renderEntries[j]->getSortKey());
				positioned = true;
				break;
			}
		}

		manifest += Common::String::format(
				"%s\t{\"dumped\": \"%s%s.png\", \"background\": %s, \"depthTarget\": \"%s\", "
				"\"positioned\": %s, \"position\": [%d, %d], \"size\": [%d, %d], \"eyeDistance\": %f}",
				dumpedImages > 1 ? ",\n" : "",
				isBackground ? "background_" : "", imageName.c_str(),
				isBackground ? "true" : "false",
				modPath.toString('/').c_str(),
				positioned ? "true" : "false",
				itemPosition.x, itemPosition.y,
				imageVisual->getWidth(), imageVisual->getHeight(),
				itemDistance);
	}

	manifest += "\n]\n";

	Common::Path manifestPath(dumpDir + "/manifest.json");
	Common::DumpFile manifestFile;
	if (manifestFile.open(manifestPath, true)) {
		manifestFile.writeString(manifest);
		manifestFile.close();
	}

	// -- Dump the camera and floor geometry as JSON
	Common::Path geoPath(dumpDir + "/geometry.json");
	Common::DumpFile geoFile;
	if (!geoFile.open(geoPath, true)) {
		debugPrintf("Unable to open '%s' for writing\n", geoPath.toString().c_str());
		return;
	}

	Math::Matrix4 view = StarkScene->getViewMatrix();
	Math::Matrix4 projection = StarkScene->getProjectionMatrix();

	// Scroll offset and full scene size, so a scrolling location's floor anchors
	// can be mapped onto the full background image (not just the current window).
	Common::Rect sceneViewport = StarkScene->getSceneViewport();
	Common::Rect sceneSize = StarkScene->getSceneSize();

	Common::String json = "{\n";
	json += Common::String::format("\t\"level\": \"%02x\",\n\t\"location\": \"%02x\",\n",
			level->getIndex(), location->getIndex());
	json += Common::String::format("\t\"scroll\": [%d, %d],\n", sceneViewport.left, sceneViewport.top);
	json += Common::String::format("\t\"sceneWidth\": %d,\n\t\"sceneHeight\": %d,\n",
			sceneSize.width(), sceneSize.height());

	json += "\t\"viewMatrix\": [";
	for (int i = 0; i < 16; i++) {
		json += Common::String::format("%s%f", i ? ", " : "", view.getData()[i]);
	}
	json += "],\n\t\"projectionMatrix\": [";
	for (int i = 0; i < 16; i++) {
		json += Common::String::format("%s%f", i ? ", " : "", projection.getData()[i]);
	}
	json += "],\n";

	// Anchor points: floor vertices with their world position, screen position
	// in original game coordinates, and eye space position
	json += "\t\"floorVertices\": [\n";
	uint32 numVertices = floor ? floor->getNumVertices() : 0;
	for (uint32 i = 0; i < numVertices; i++) {
		Math::Vector3d world = floor->getVertex(i);
		Common::Point screen = StarkScene->convertPosition3DToGameScreenOriginal(world);

		Math::Vector3d eye = world;
		view.transform(&eye, true);

		json += Common::String::format(
				"\t\t{\"world\": [%f, %f, %f], \"screen\": [%d, %d], \"eye\": [%f, %f, %f]}%s\n",
				world.x(), world.y(), world.z(),
				screen.x, screen.y,
				eye.x(), eye.y(), eye.z(),
				(i == numVertices - 1) ? "" : ",");
	}
	json += "\t],\n";

	json += "\t\"floorFaces\": [\n";
	uint32 numFaces = floor ? floor->getNumFaces() : 0;
	for (uint32 i = 0; i < numFaces; i++) {
		Resources::FloorFace *face = floor->getFace(i);
		json += Common::String::format("\t\t[%d, %d, %d]%s\n",
				face->getVertexIndex(0), face->getVertexIndex(1), face->getVertexIndex(2),
				(i == numFaces - 1) ? "" : ",");
	}
	json += "\t]\n}\n";

	geoFile.writeString(json);
	geoFile.close();

	debugPrintf("Dumped %d images, %d floor vertices, %d floor faces to '%s'\n",
			dumpedImages, numVertices, numFaces, dumpDir.c_str());
}

bool Console::Cmd_Toggle(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Flip a boolean enhancement setting\n");
		debugPrintf("Usage :\n");
		debugPrintf("toggle [setting]\n");
		debugPrintf("Settings: soft_shadows, enable_depth_maps, ambient_matching, enable_normal_mapping, enable_depth_fog, highlight_hotspots, debug_show_depth\n");
		return true;
	}

	bool newValue = !ConfMan.getBool(argv[1]);
	ConfMan.setBool(argv[1], newValue);
	ConfMan.flushToDisk();
	debugPrintf("%s: %s\n", argv[1], newValue ? "on" : "off");
	return true;
}

bool Console::Cmd_DumpModels(int argc, const char **argv) {
	if (!StarkGlobal->getCurrent()) {
		debugPrintf("Only available in-game, once a location is loaded\n");
		return true;
	}

	dumpCurrentModels(false);
	return true;
}

bool Console::Cmd_DumpModelsOriginal(int argc, const char **argv) {
	if (!StarkGlobal->getCurrent()) {
		debugPrintf("Only available in-game, once a location is loaded\n");
		return true;
	}

	// Dump the ORIGINAL meshes (ignoring any enhanced overrides), so re-dumping
	// with the assets mod on never captures an already-subdivided mesh.
	dumpCurrentModels(true);
	return true;
}

void Console::dumpCurrentModels(bool original) {
	Current *current = StarkGlobal->getCurrent();

	Resources::Location *location = current->getLocation();
	if (!location) {
		debugPrintf("Scene not fully loaded, skipping model dump\n");
		return;
	}

	// Only dump items that are actually being rendered. Enumerating every
	// item recursively also picks up inactive/global items whose animation
	// hierarchy was freed on location exit (dangling, non-null pointers),
	// which crash when queried. The render-entry set is exactly the active,
	// valid, on-screen items.
	Common::Array<Resources::ModelItem *> items;
	Gfx::RenderEntryArray renderEntries = location->listRenderEntries();
	for (uint i = 0; i < renderEntries.size(); i++) {
		Resources::ItemVisual *owner = renderEntries[i]->getOwner();
		if (owner && owner->getSubType() == Resources::Item::kItemModel) {
			items.push_back(static_cast<Resources::ModelItem *>(owner));
		}
	}

	int dumpedCount = 0;

	for (uint itemIndex = 0; itemIndex < items.size(); itemIndex++) {
		Resources::BonesMesh *bonesMesh = items[itemIndex]->findBonesMesh();
		if (!bonesMesh) {
			continue;
		}

		// In "original" mode load a fresh copy straight from the archive, so a
		// re-dump never captures an already-enhanced (overridden) mesh.
		Model *model = original ? bonesMesh->loadOriginalModel() : bonesMesh->getModel();
		if (!model) {
			continue;
		}

		Common::Path outPath(Common::String::format("dump/models/%s_%s.json",
				sanitizeFileName(items[itemIndex]->getName()).c_str(),
				sanitizeFileName(bonesMesh->getName()).c_str()));

		Common::DumpFile out;
		if (!out.open(outPath, true)) {
			debugPrintf("Unable to open '%s' for writing\n", outPath.toString().c_str());
			if (original) delete model;
			continue;
		}

		const Common::Array<BoneNode *> &bones = model->getBones();
		const Common::Array<VertNode *> &verts = model->getVertices();
		const Common::Array<Face *> &faces = model->getFaces();
		const Common::Array<Material *> &mats = model->getMaterials();

		// A model with no bones cannot be skinned or dumped meaningfully
		if (bones.empty()) {
			debugPrintf("Skipping '%s': model has no bones\n", items[itemIndex]->getName().c_str());
			if (original) delete model;
			continue;
		}

		// Validate that every vertex references bone indices within range,
		// and that face indices are within the vertex array. Some locations
		// contain malformed or partially-loaded meshes that would otherwise
		// crash the dumper.
		bool valid = true;
		for (uint i = 0; i < verts.size() && valid; i++) {
			if (verts[i]->_bone1 >= bones.size() || verts[i]->_bone2 >= bones.size()) {
				valid = false;
			}
		}
		for (uint i = 0; i < faces.size() && valid; i++) {
			for (uint j = 0; j < faces[i]->vertexIndices.size() && valid; j++) {
				if (faces[i]->vertexIndices[j] >= verts.size()) {
					valid = false;
				}
			}
		}
		if (!valid) {
			debugPrintf("Skipping '%s': mesh has out-of-range bone or vertex indices\n",
					items[itemIndex]->getName().c_str());
			if (original) delete model;
			continue;
		}

		Common::Path replacementTarget = StarkArchiveLoader->getExternalFilePath(
				bonesMesh->getFilename(), bonesMesh->getArchiveName());

		out.writeString(Common::String::format(
				"{\n\"item\": \"%s\",\n\"mesh\": \"%s\",\n"
				"\"meshName\": \"%s\",\n\"u2\": %f,\n"
				"\"filename\": \"%s\",\n\"replacementTarget\": \"%s\",\n",
				items[itemIndex]->getName().c_str(), bonesMesh->getName().c_str(),
				model->getName().c_str(), model->getU2(),
				bonesMesh->getFilename().toString('/').c_str(),
				replacementTarget.toString('/').c_str()));

		// Bones with their current model-space transforms
		out.writeString("\"bones\": [\n");
		for (uint i = 0; i < bones.size(); i++) {
			out.writeString(Common::String::format(
					"{\"name\": \"%s\", \"parent\": %d, \"u1\": %f, \"pos\": [%f, %f, %f], \"rot\": [%f, %f, %f, %f]}%s\n",
					bones[i]->_name.c_str(), bones[i]->_parent, bones[i]->_u1,
					bones[i]->_animPos.x(), bones[i]->_animPos.y(), bones[i]->_animPos.z(),
					bones[i]->_animRot.x(), bones[i]->_animRot.y(), bones[i]->_animRot.z(), bones[i]->_animRot.w(),
					(i == bones.size() - 1) ? "" : ","));
		}
		out.writeString("],\n");

		out.writeString("\"materials\": [\n");
		for (uint i = 0; i < mats.size(); i++) {
			out.writeString(Common::String::format(
					"{\"name\": \"%s\", \"texture\": \"%s\", \"color\": [%f, %f, %f], \"doubleSided\": %s}%s\n",
					mats[i]->name.c_str(), mats[i]->texture.c_str(),
					mats[i]->r, mats[i]->g, mats[i]->b,
					mats[i]->doubleSided ? "true" : "false",
					(i == mats.size() - 1) ? "" : ","));
		}
		out.writeString("],\n");

		// Vertices skinned to the current pose:
		// px py pz nx ny nz u v bone1 bone2 weight1
		out.writeString("\"vertices\": [\n");
		for (uint i = 0; i < verts.size(); i++) {
			const VertNode *v = verts[i];

			Math::Vector3d p1 = v->_pos1;
			bones[v->_bone1]->_animRot.transform(p1);
			p1 += bones[v->_bone1]->_animPos;

			Math::Vector3d p2 = v->_pos2;
			bones[v->_bone2]->_animRot.transform(p2);
			p2 += bones[v->_bone2]->_animPos;

			Math::Vector3d p = p1 * v->_boneWeight + p2 * (1.0f - v->_boneWeight);

			Math::Vector3d n = v->_normal;
			bones[v->_bone1]->_animRot.transform(n);

			// Note: the renderers negate texS
			out.writeString(Common::String::format(
					"[%f, %f, %f, %f, %f, %f, %f, %f, %d, %d, %f]%s\n",
					p.x(), p.y(), p.z(), n.x(), n.y(), n.z(),
					-v->_texS, v->_texT,
					v->_bone1, v->_bone2, v->_boneWeight,
					(i == verts.size() - 1) ? "" : ","));
		}
		out.writeString("],\n");

		out.writeString("\"faces\": [\n");
		for (uint i = 0; i < faces.size(); i++) {
			out.writeString(Common::String::format("{\"material\": %d, \"indices\": [", faces[i]->materialId));
			for (uint j = 0; j < faces[i]->vertexIndices.size(); j++) {
				out.writeString(Common::String::format("%s%d", j ? ", " : "", faces[i]->vertexIndices[j]));
			}
			out.writeString(Common::String::format("]}%s\n", (i == faces.size() - 1) ? "" : ","));
		}
		out.writeString("]\n}\n");

		out.close();
		dumpedCount++;

		debugPrintf("%s: %d bones, %d vertices, %d faces -> %s\n",
				items[itemIndex]->getName().c_str(),
				(int) bones.size(), (int) verts.size(), (int) faces.size(),
				outPath.toString().c_str());

		if (original) delete model;
	}

	debugPrintf("Dumped %d %smodel(s)\n", dumpedCount, original ? "original " : "");
}

static Common::String crawlDoneMarker(uint16 level, uint16 location) {
	return Common::String::format("dump/crawl/done_%02x_%02x", level, location);
}

static Common::String crawlSkipMarker(uint16 level, uint16 location) {
	return Common::String::format("dump/crawl/skip_%02x_%02x", level, location);
}

static bool crawlFileExists(const Common::String &path) {
	return Common::FSNode(Common::Path(path)).exists();
}

static void crawlTouchFile(const Common::String &path, const Common::String &contents) {
	Common::DumpFile file;
	if (file.open(Common::Path(path), true)) {
		file.writeString(contents);
		file.close();
	}
}

bool Console::Cmd_DumpAll(int argc, const char **argv) {
	bool force = argc == 3 && strcmp(argv[2], "force") == 0;
	if (argc < 2 || argc > 3 ||
			(strcmp(argv[1], "scenes") && strcmp(argv[1], "models") && strcmp(argv[1], "both")) ||
			(argc == 3 && !force)) {
		debugPrintf("Visit every location in the game and dump its data for the\n");
		debugPrintf("enhancement pipeline. Takes a few minutes; progress is printed\n");
		debugPrintf("to the terminal. Save your game first: this trashes game state.\n");
		debugPrintf("Add 'force' to re-dump locations already done in a previous run\n");
		debugPrintf("(e.g. after the dump format changed); blacklisted crashers are\n");
		debugPrintf("still skipped - clear dump/crawl to retry those too.\n");
		debugPrintf("Usage :\n");
		debugPrintf("dumpAll [scenes|models|both] [force]\n");
		return true;
	}

	_crawlForce = force;
	_crawlScenes = strcmp(argv[1], "models") != 0;
	_crawlModels = strcmp(argv[1], "scenes") != 0;

	// If a previous crawl died mid-location, blacklist the offender
	Common::FSNode lastNode(Common::Path("dump/crawl/last"));
	if (lastNode.exists()) {
		Common::SeekableReadStream *lastStream = lastNode.createReadStream();
		if (lastStream) {
			Common::String line = lastStream->readLine();
			delete lastStream;

			uint lastLevel = 0, lastLocation = 0;
			if (sscanf(line.c_str(), "%x %x", &lastLevel, &lastLocation) == 2 &&
			    !crawlFileExists(crawlDoneMarker(lastLevel, lastLocation))) {
				crawlTouchFile(crawlSkipMarker(lastLevel, lastLocation), line);
				debugPrintf("Previous crawl died in %02x %02x - blacklisted "
						"(delete dump/crawl/skip_%02x_%02x to retry it)\n",
						lastLevel, lastLocation, lastLevel, lastLocation);
			}
		}
	}

	// Enumerate all the locations using a temporary archive loader,
	// the same way listLocations does
	ArchiveLoader *archiveLoader = new ArchiveLoader();
	ArchiveLoader *gameArchiveLoader = StarkArchiveLoader;
	StarkArchiveLoader = archiveLoader;

	archiveLoader->load("x.xarc");
	Resources::Root *root = archiveLoader->useRoot<Resources::Root>("x.xarc");

	_crawlQueue.clear();
	Common::Array<Resources::Level *> levels = root->listChildren<Resources::Level>();
	for (uint i = 0; i < levels.size(); i++) {
		Common::Path levelArchive = archiveLoader->buildArchiveName(levels[i]);
		archiveLoader->load(levelArchive);
		Resources::Level *level = archiveLoader->useRoot<Resources::Level>(levelArchive);

		Common::Array<Resources::Location *> locations = level->listChildren<Resources::Location>();
		for (uint j = 0; j < locations.size(); j++) {
			CrawlTarget target;
			target.level = level->getIndex();
			target.location = locations[j]->getIndex();

			if (!_crawlForce && crawlFileExists(crawlDoneMarker(target.level, target.location))) {
				continue; // Already dumped in a previous run (ignored with 'force')
			}
			if (crawlFileExists(crawlSkipMarker(target.level, target.location))) {
				continue; // Blacklisted as a crasher
			}

			_crawlQueue.push_back(target);
		}

		archiveLoader->returnRoot(levelArchive);
		archiveLoader->unloadUnused();
	}

	StarkArchiveLoader = gameArchiveLoader;
	delete archiveLoader;

	if (_crawlQueue.empty()) {
		debugPrintf("Nothing left to crawl - all locations are dumped or blacklisted\n");
		return true;
	}

	debugPrintf("Crawling %d locations (already dumped ones are skipped)...\n", (int) _crawlQueue.size());

	StarkUserInterface->changeScreen(Screen::kScreenGame);
	if (!StarkGlobal->getRoot()) {
		StarkResourceProvider->initGlobal();
	}

	CrawlTarget first = _crawlQueue.back();
	_crawlQueue.pop_back();
	crawlTouchFile("dump/crawl/last", Common::String::format("%02x %02x\n", first.level, first.location));
	StarkResourceProvider->requestLocationChange(first.level, first.location);

	_crawlWait = 30;
	_crawlActive = true;

	// Close the console so the game loop can run
	return false;
}

void Console::tickDumpCrawl() {
	if (!_crawlActive) {
		return;
	}

	if (StarkResourceProvider->hasLocationChangeRequest()) {
		// Still loading the requested location
		_crawlWait = 30;
		return;
	}

	if (_crawlWait > 0) {
		// Grace period so scripts and animations settle after arrival
		_crawlWait--;
		return;
	}

	if (_crawlScenes) {
		dumpCurrentSceneData();
	}
	if (_crawlModels) {
		dumpCurrentModels(true); // always dump originals, never enhanced overrides
	}

	// Mark this location as done so interrupted crawls can resume
	Current *current = StarkGlobal->getCurrent();
	if (current) {
		crawlTouchFile(crawlDoneMarker(current->getLevel()->getIndex(),
				current->getLocation()->getIndex()), "done\n");
	}

	if (_crawlQueue.empty()) {
		_crawlActive = false;
		crawlTouchFile("dump/crawl/last", "done\n");
		debug("dumpAll: crawl complete");
		return;
	}

	CrawlTarget next = _crawlQueue.back();
	_crawlQueue.pop_back();
	debug("dumpAll: %d locations remaining, going to %02x %02x",
			(int) _crawlQueue.size(), next.level, next.location);

	crawlTouchFile("dump/crawl/last", Common::String::format("%02x %02x\n", next.level, next.location));
	StarkResourceProvider->requestLocationChange(next.level, next.location);
	_crawlWait = 30;
}

bool Console::Cmd_SetInt(int argc, const char **argv) {
	if (argc != 3) {
		debugPrintf("Set an integer enhancement setting\n");
		debugPrintf("Usage: setInt [key] [value]\n");
		debugPrintf("Keys: fog_density, marker_scale, subtitle_scale,\n");
		debugPrintf("      subtitle_scroll_ms_per_char, grade_brightness,\n");
		debugPrintf("      grade_contrast, grade_saturation, grade_tint_r/g/b,\n");
		debugPrintf("      vignette_strength, grain_strength, sharpen_strength\n");
		debugPrintf("(For on/off settings use setBool instead.)\n");
		return true;
	}

	ConfMan.setInt(argv[1], atoi(argv[2]));
	ConfMan.flushToDisk();

	// The subtitle/dialog font size is baked into the font at load time, so a
	// live change only takes effect once the fonts are rebuilt.
	if (scumm_stricmp(argv[1], "subtitle_scale") == 0) {
		StarkFontProvider->initFonts();
	}

	debugPrintf("%s: %d\n", argv[1], atoi(argv[2]));
	return true;
}

bool Console::Cmd_SetBool(int argc, const char **argv) {
	if (argc != 3) {
		debugPrintf("Set a boolean enhancement setting\n");
		debugPrintf("Usage: setBool [key] [true|false]\n");
		debugPrintf("Keys: subtitle_autoscroll, enable_assets_mod, marker_colorblind,\n");
		debugPrintf("      enable_post_processing, enable_depth_of_field, ...\n");
		return true;
	}

	Common::String v(argv[2]);
	bool value = v.equalsIgnoreCase("true") || v.equalsIgnoreCase("on")
	             || v.equalsIgnoreCase("yes") || v == "1";
	ConfMan.setBool(argv[1], value);
	ConfMan.flushToDisk();
	debugPrintf("%s: %s\n", argv[1], value ? "true" : "false");
	return true;
}

bool Console::Cmd_PostInfo(int argc, const char **argv) {
	debugPrintf("enable_post_processing: %s\n", ConfMan.getBool("enable_post_processing") ? "ON" : "off");
	debugPrintf("auto_scene_post:        %s\n", ConfMan.getBool("auto_scene_post") ? "ON" : "off");

	// Depth setup used by the last post pass. Contact-mode SSAO (no character
	// self-shadow, no bleed onto foreground sprites) needs BOTH the GL depth
	// copy and the background depth mask; if either is missing it silently
	// falls back to plain crease AO, which does bleed.
	bool glDepth = false, worldMask = false, contact = false;
	StarkGfx->getPostDepthState(glDepth, worldMask, contact);
	debugPrintf("enable_depth_copy:      %s\n", ConfMan.getBool("enable_depth_copy") ? "ON" : "off");
	debugPrintf("enable_depth_maps:      %s\n", ConfMan.getBool("enable_depth_maps") ? "ON" : "off");
	debugPrintf("  -> GL depth copied:      %s\n", glDepth ? "yes" : "no");
	debugPrintf("  -> world depth mask set: %s\n", worldMask ? "yes" : "no");
	debugPrintf("  -> contact-mode SSAO:    %s\n", contact ? "ACTIVE" : "no (fallback crease AO)");
	if (!ConfMan.getBool("enable_depth_maps")) {
		debugPrintf("  !! enable_depth_maps is OFF: SSAO falls back to bleeding crease AO.\n");
		debugPrintf("     Fix: setBool enable_depth_maps true  (or the Depth occlusion toggle)\n");
	}
	debugPrintf("post_debug_view:        %d  (setInt post_debug_view 1..4 to visualize)\n",
			ConfMan.getInt("post_debug_view"));

	// Sprite depth stamp (Route B): how many foreground sprites got stamped last
	// frame, and at what eye-space depth range, vs the scene's clip planes. If the
	// stamp range sits near farClip instead of between near and the mask, the sort
	// key isn't the eye-space depth we assume (scale mismatch).
	int stampCount = 0; float stampMin = 0.0f, stampMax = 0.0f;
	StarkGfx->getSpriteStampInfo(stampCount, stampMin, stampMax);
	debugPrintf("enable_sprite_depth:    %s\n", ConfMan.getBool("enable_sprite_depth") ? "ON" : "off");
	debugPrintf("  -> sprites stamped:      %d (eye depth %.1f .. %.1f)\n", stampCount, stampMin, stampMax);
	debugPrintf("  -> scene near/far clip:  %.1f / %.1f\n",
			StarkScene->getNearClipPlane(), StarkScene->getFarClipPlane());

	Current *current = StarkGlobal->getCurrent();
	if (current && current->getLevel() && current->getLocation()) {
		debugPrintf("current location key:   %02x/%02x\n",
				current->getLevel()->getIndex(), current->getLocation()->getIndex());
	} else {
		debugPrintf("current location key:   (none - not in a location)\n");
	}

	static const char *const keys[] = {
		"ssao_strength", "tonemap_strength", "bloom_strength", "bloom_threshold",
		"vignette_strength", "grade_saturation", "grade_contrast", "grade_brightness"
	};
	debugPrintf("effective post values (per-scene marked *):\n");
	for (uint i = 0; i < ARRAYSIZE(keys); i++) {
		int effective = StarkScene->getPostSetting(keys[i]);
		int global = ConfMan.getInt(keys[i]);
		debugPrintf("  %-18s = %-5d %s\n", keys[i], effective,
				effective != global ? "* (from post_scenes.json)" : "");
	}

	int ssaoBase = StarkScene->getPostSetting("ssao_strength");
	int ssaoMaster = ConfMan.getInt("ssao_master");
	debugPrintf("SSAO: base %d x master %d%% = %d effective\n",
			ssaoBase, ssaoMaster, CLIP(ssaoBase * CLIP(ssaoMaster, 0, 300) / 100, 0, 100));
	return true;
}

bool Console::Cmd_RenderEntries(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current || !current->getLocation()) {
		debugPrintf("Not in a location\n");
		return true;
	}

	Gfx::RenderEntryArray entries = current->getLocation()->listRenderEntries();
	debugPrintf("%-26s %-5s %-8s %-9s %-9s\n", "name", "image", "depthmap", "sortKey", "stampEye");
	for (uint i = 0; i < entries.size(); i++) {
		Gfx::RenderEntry *e = entries[i];
		VisualImageXMG *img = e->getImage();
		debugPrintf("%-26s %-5s %-8s %-9.1f %-9.1f\n",
				e->getName().c_str(),
				img ? "yes" : "no",
				img ? (img->hasDepthMap() ? "yes" : "no") : "-",
				e->getSortKey(),
				e->getStampEyeDepth());
	}
	debugPrintf("(%u entries; stamp needs image=yes, depthmap=no, and sortKey or stampEye > 0)\n",
			entries.size());
	return true;
}

bool Console::Cmd_PostPreset(int argc, const char **argv) {
	// A tasteful "cinematic" default so the effect is visible with one command
	ConfMan.setBool("enable_post_processing", true);
	ConfMan.setInt("grade_contrast", 108);
	ConfMan.setInt("grade_saturation", 108);
	ConfMan.setInt("vignette_strength", 35);
	ConfMan.setInt("grain_strength", 5);
	ConfMan.setInt("sharpen_strength", 25);
	ConfMan.flushToDisk();
	debugPrintf("Applied cinematic post-processing preset\n");
	return true;
}


bool Console::Cmd_DepthViz(int argc, const char **argv) {
	bool newValue = !ConfMan.getBool("debug_show_depth");
	ConfMan.setBool("debug_show_depth", newValue);
	ConfMan.flushToDisk();
	debugPrintf("Depth map visualization: %s\n", newValue ? "on" : "off");
	return true;
}

bool Console::Cmd_DumpRoot(int argc, const char **argv) {
	Resources::Root *root = StarkGlobal->getRoot();
	if (root) {
		root->print();
	} else {
		debugPrintf("The global root has not been loaded\n");
	}

	return true;
}

bool Console::Cmd_DumpGlobal(int argc, const char **argv) {
	Resources::Level *level = StarkGlobal->getLevel();
	if (level) {
		level->print();
	} else {
		debugPrintf("The global level has not been loaded\n");
	}

	return true;
}

bool Console::Cmd_DumpStatic(int argc, const char **argv) {
	// Static resources are initialized in the beginning of the running
	StarkStaticProvider->getLevel()->print();

	return true;
}

bool Console::Cmd_DumpLevel(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (current) {
		current->getLevel()->print();
	} else {
		debugPrintf("Game levels have not been loaded\n");
	}

	return true;
}

bool Console::Cmd_DumpKnowledge(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();

	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	Resources::Level *level = current->getLevel();
	Resources::Location *location = current->getLocation();
	Common::Array<Resources::Knowledge *> knowledge = level->listChildrenRecursive<Resources::Knowledge>();
	knowledge.insert_at(knowledge.size(), location->listChildrenRecursive<Resources::Knowledge>());
	Common::Array<Resources::Knowledge *>::iterator it;
	for (it = knowledge.begin(); it != knowledge.end(); ++it) {
		(*it)->print();
	}
	return true;
}

bool Console::Cmd_ChangeKnowledge(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();

	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	uint index = 0;
	char type = 0;

	if (argc >= 4) {
		index = atoi(argv[1]);
		type = argv[2][0];
		if (type == 'b' || type == 'i') {
			Resources::Level *level = current->getLevel();
			Resources::Location *location = current->getLocation();
			Common::Array<Resources::Knowledge *> knowledgeArr = level->listChildrenRecursive<Resources::Knowledge>();
			knowledgeArr.insert_at(knowledgeArr.size(), location->listChildrenRecursive<Resources::Knowledge>());
			if (index < knowledgeArr.size() ) {
				Resources::Knowledge *knowledge = knowledgeArr[index];
				if (type == 'b') {
					knowledge->setBooleanValue(atoi(argv[3]));
				} else if (type == 'i') {
					knowledge->setIntegerValue(atoi(argv[3]));
				}
				return true;
			} else {
				debugPrintf("Invalid index %d, only %d indices available\n", index, knowledgeArr.size());
			}
		} else {
			debugPrintf("Invalid type: %c, only b and i are available\n", type);
		}
	} else if (argc > 1 ) {
		debugPrintf("Too few args\n");
	}

	debugPrintf("Change the value of some knowledge. Use dumpKnowledge to get an id\n");
	debugPrintf("Usage :\n");
	debugPrintf("changeKnowledge [id] [type] [value]\n");
	debugPrintf("available types: b(inary), i(nteger)\n");
	return true;
}

Common::Array<Resources::Script *> Console::listAllLocationScripts() const {
	Common::Array<Resources::Script *> scripts;

	Resources::Level *level = StarkGlobal->getCurrent()->getLevel();
	Resources::Location *location = StarkGlobal->getCurrent()->getLocation();
	scripts.push_back(level->listChildrenRecursive<Resources::Script>());
	scripts.push_back(location->listChildrenRecursive<Resources::Script>());

	return scripts;
}

bool Console::Cmd_ListScripts(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	Common::Array<Resources::Script *> scripts = listAllLocationScripts();

	for (uint i = 0; i < scripts.size(); i++) {
		Resources::Script *script = scripts[i];

		debugPrintf("%d: %s - enabled: %d", i, script->getName().c_str(), script->isEnabled());

		// Print which resource is causing the script to wait
		if (script->isSuspended()) {
			Resources::Object *suspending = script->getSuspendingResource();
			if (suspending) {
				debugPrintf(", waiting for: %s (%s)", suspending->getName().c_str(), suspending->getType().getName());
			} else {
				debugPrintf(", paused");
			}
		}

		debugPrintf("\n");
	}

	return true;
}

bool Console::Cmd_EnableScript(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	uint index = 0;

	if (argc >= 2) {
		index = atoi(argv[1]);

		bool value = true;
		if (argc >= 3) {
			value = atoi(argv[2]);
		}

		Common::Array<Resources::Script *> scripts = listAllLocationScripts();
		if (index < scripts.size() ) {
			Resources::Script *script = scripts[index];
			script->enable(value);
			return true;
		} else {
			debugPrintf("Invalid index %d, only %d indices available\n", index, scripts.size());
		}
	}

	debugPrintf("Enable or disable a script. Use listScripts to get an id\n");
	debugPrintf("Usage :\n");
	debugPrintf("enableScript [id] (value)\n");
	return true;
}

bool Console::Cmd_ForceScript(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	uint index = 0;

	if (argc >= 2) {
		index = atoi(argv[1]);

		Common::Array<Resources::Script *> scripts = listAllLocationScripts();
		if (index < scripts.size() ) {
			Resources::Script *script = scripts[index];
			script->enable(true);
			script->goToNextCommand(); // Skip the begin command to avoid checks
			script->execute(Resources::Script::kCallModePlayerAction);
			return true;
		} else {
			debugPrintf("Invalid index %d, only %d indices available\n", index, scripts.size());
		}
	}

	debugPrintf("Force the execution of a script. Use listScripts to get an id\n");
	debugPrintf("Usage :\n");
	debugPrintf("forceScript [id]\n");
	return true;
}

bool Console::Cmd_DecompileScript(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	if (argc >= 2) {
		uint index = atoi(argv[1]);

		Common::Array<Resources::Script *> scripts = listAllLocationScripts();
		if (index < scripts.size()) {
			Resources::Script *script = scripts[index];

			Tools::Decompiler *decompiler = new Tools::Decompiler(script);
			if (decompiler->getError() != "") {
				debugPrintf("Decompilation failure: %s\n", decompiler->getError().c_str());
			}

			debug("Script %d - %s:", index, script->getName().c_str());
			decompiler->printDecompiled();

			delete decompiler;

			return true;
		} else {
			debugPrintf("Invalid index %d, only %d indices available\n", index, scripts.size());
		}
	}

	debugPrintf("Decompile a script. Use listScripts to get an id\n");
	debugPrintf("Usage :\n");
	debugPrintf("decompileScript [id]\n");
	return true;
}

class ArchiveVisitor {
public:
	virtual ~ArchiveVisitor() {}
	virtual void acceptLevelArchive(Resources::Level *level) = 0;
	virtual void acceptLocationArchive(Resources::Location *location) = 0;
};

void Console::walkAllArchives(ArchiveVisitor *visitor) {
	ArchiveLoader *archiveLoader = new ArchiveLoader();

	// Temporarily replace the global archive loader with our instance
	ArchiveLoader *gameArchiveLoader = StarkArchiveLoader;
	StarkArchiveLoader = archiveLoader;

	archiveLoader->load("x.xarc");
	Resources::Root *root = archiveLoader->useRoot<Resources::Root>("x.xarc");

	// Find all the levels
	Common::Array<Resources::Level *> levels = root->listChildren<Resources::Level>();

	// Loop over the levels
	for (uint i = 0; i < levels.size(); i++) {
		Resources::Level *level = levels[i];

		Common::Path levelArchive = archiveLoader->buildArchiveName(level);
		debug("%s - %s", levelArchive.toString(Common::Path::kNativeSeparator).c_str(), level->getName().c_str());

		// Load the detailed level archive
		archiveLoader->load(levelArchive);
		level = archiveLoader->useRoot<Resources::Level>(levelArchive);

		// Visit the level archive
		visitor->acceptLevelArchive(level);

		Common::Array<Resources::Location *> locations = level->listChildren<Resources::Location>();

		// Loop over the locations
		for (uint j = 0; j < locations.size(); j++) {
			Resources::Location *location = locations[j];

			Common::Path locationArchive = archiveLoader->buildArchiveName(level, location);
			debug("%s - %s", locationArchive.toString(Common::Path::kNativeSeparator).c_str(), location->getName().c_str());

			// Load the detailed location archive
			archiveLoader->load(locationArchive);
			location = archiveLoader->useRoot<Resources::Location>(locationArchive);

			// Visit the location archive
			visitor->acceptLocationArchive(location);

			archiveLoader->returnRoot(locationArchive);
			archiveLoader->unloadUnused();
		}

		archiveLoader->returnRoot(levelArchive);
		archiveLoader->unloadUnused();
	}

	// Restore the global archive loader
	StarkArchiveLoader = gameArchiveLoader;

	delete archiveLoader;
}

class DecompilingArchiveVisitor : public ArchiveVisitor {
public:
	DecompilingArchiveVisitor() :
	    _totalScripts(0),
	    _okScripts(0) {}

	void acceptLevelArchive(Resources::Level *level) override {
		decompileScriptChildren(level);
	}

	void acceptLocationArchive(Resources::Location *location) override {
		decompileScriptChildren(location);
	}

	int getTotalScripts() const { return _totalScripts; }
	int getOKScripts() const { return _okScripts; }

private:
	int _totalScripts;
	int _okScripts;

	void decompileScriptChildren(Resources::Object *resource) {
		Common::Array<Resources::Script *> scripts = resource->listChildrenRecursive<Resources::Script>();

		for (uint i = 0; i < scripts.size(); i++) {
			Resources::Script *script = scripts[i];

			Tools::Decompiler decompiler(script);
			_totalScripts++;

			Common::String result;
			if (decompiler.getError() == "") {
				result = "OK";
				_okScripts++;
			} else {
				result = decompiler.getError();
			}

			debug("%d - %s: %s", script->getIndex(), script->getName().c_str(), result.c_str());
		}
	}
};

bool Console::Cmd_TestDecompiler(int argc, const char **argv) {
	DecompilingArchiveVisitor visitor;
	walkAllArchives(&visitor);

	debugPrintf("Successfully decompiled %d scripts out of %d\n", visitor.getOKScripts(), visitor.getTotalScripts());

	return true;
}

class TextureExtractingArchiveVisitor : public ArchiveVisitor {
public:
	void acceptLevelArchive(Resources::Level *level) override {
		decompileScriptChildren(level);
	}

	void acceptLocationArchive(Resources::Location *location) override {
		decompileScriptChildren(location);
	}

private:
	void decompileScriptChildren(Resources::Object *resource) {
		Common::Array<Resources::TextureSet *> textureSets = resource->listChildrenRecursive<Resources::TextureSet>();

		for (uint i = 0; i < textureSets.size(); i++) {
			Resources::TextureSet *textureSet = textureSets[i];
			textureSet->extractArchive();
		}
	}
};

bool Console::Cmd_ExtractAllTextures(int argc, const char **argv) {
	TextureExtractingArchiveVisitor visitor;
	walkAllArchives(&visitor);

	return true;
}

Common::Array<Resources::Anim *> Console::listAllLocationAnimations() const {
	Common::Array<Resources::Anim *> animations;

	Resources::Level *level = StarkGlobal->getCurrent()->getLevel();
	Resources::Location *location = StarkGlobal->getCurrent()->getLocation();
	animations.push_back(level->listChildrenRecursive<Resources::Anim>());
	animations.push_back(location->listChildrenRecursive<Resources::Anim>());

	return animations;
}

bool Console::Cmd_ListAnimations(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current) {
		debugPrintf("This command is only available in game.\n");
		return true;
	}

	Common::Array<Resources::Anim *> animations = listAllLocationAnimations();

	for (uint i = 0; i < animations.size(); i++) {
		Resources::Anim *anim = animations[i];
		Resources::Item *item = anim->findParent<Resources::Item>();

		debugPrintf("%d: %s - %s - in use: %d\n", i, item->getName().c_str(), anim->getName().c_str(), anim->isInUse());
	}

	return true;
}

bool Console::Cmd_ForceAnimation(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();
	if (!current) {
		debugPrintf("This command is only available in game.\n");
		return true;
	}

	if (argc < 2) {
		debugPrintf("Force the execution of an animation. Use listAnimations to get an id\n");
		debugPrintf("Usage :\n");
		debugPrintf("forceAnimation [id]\n");
		return true;
	}

	uint index = atoi(argv[1]);

	Common::Array<Resources::Anim *> animations = listAllLocationAnimations();
	if (index >= animations.size() ) {
		debugPrintf("Invalid animation %d\n", index);
		return true;
	}

	Resources::Anim *anim = animations[index];
	Resources::Item *item = anim->findParent<Resources::Item>();
	Resources::ItemVisual *sceneItem = item->getSceneInstance();

	if (!sceneItem->isEnabled()) {
		sceneItem->setEnabled(true);
	}

	sceneItem->playActionAnim(anim);

	return false;
}

bool Console::Cmd_DumpLocation(int argc, const char **argv) {
	if (StarkStaticProvider->isStaticLocation()) {
		StarkStaticProvider->getLocation()->print();
		return true;
	}

	Current *current = StarkGlobal->getCurrent();
	if (current) {
		current->getLocation()->print();
	} else {
		debugPrintf("Locations have not been loaded\n");
	}

	return true;
}

bool Console::Cmd_ListInventoryItems(int argc, const char **argv) {
	Resources::KnowledgeSet *inventory = StarkGlobal->getInventory();

	if (!inventory) {
		debugPrintf("The inventory has not been loaded\n");
		return true;
	}

	Common::Array<Resources::Item*> inventoryItems = inventory->listChildren<Resources::Item>(Resources::Item::kItemInventory);
	Common::Array<Resources::Item*>::iterator it = inventoryItems.begin();
	for (int i = 0; it != inventoryItems.end(); ++it, i++) {
		debugPrintf("Item %d: %s%s\n", i, (*it)->getName().c_str(), (*it)->isEnabled() ? " (enabled)" : "");
	}

	return true;
}

bool Console::Cmd_EnableInventoryItem(int argc, const char **argv) {
	Resources::KnowledgeSet *inventory = StarkGlobal->getInventory();

	if (!inventory) {
		debugPrintf("The inventory has not been loaded\n");
		return true;
	}

	if (argc != 2) {
		debugPrintf("Enable a specific inventory item. Use listInventoryItems to get an id\n");
		debugPrintf("Usage :\n");
		debugPrintf("enableInventoryItem [id]\n");
		return true;
	}

	uint num = atoi(argv[1]);
	Common::Array<Resources::Item*> inventoryItems = inventory->listChildren<Resources::Item>(Resources::Item::kItemInventory);
	if (num < inventoryItems.size()) {
		inventoryItems[num]->setEnabled(true);
	} else {
		debugPrintf("Invalid index %d, only %d indices available\n", num, inventoryItems.size());
	}

	return true;
}

bool Console::Cmd_ListLocations(int argc, const char **argv) {
	ArchiveLoader *archiveLoader = new ArchiveLoader();

	// Temporarily replace the global archive loader with our instance
	ArchiveLoader *gameArchiveLoader = StarkArchiveLoader;
	StarkArchiveLoader = archiveLoader;

	archiveLoader->load("x.xarc");
	Resources::Root *root = archiveLoader->useRoot<Resources::Root>("x.xarc");

	// Find all the levels
	Common::Array<Resources::Level *> levels = root->listChildren<Resources::Level>();

	// Loop over the levels
	for (uint i = 0; i < levels.size(); i++) {
		Resources::Level *level = levels[i];

		Common::Path levelArchive = archiveLoader->buildArchiveName(level);
		debugPrintf("%s - %s\n", levelArchive.toString(Common::Path::kNativeSeparator).c_str(), level->getName().c_str());

		// Load the detailed level archive
		archiveLoader->load(levelArchive);
		level = archiveLoader->useRoot<Resources::Level>(levelArchive);

		Common::Array<Resources::Location *> locations = level->listChildren<Resources::Location>();

		// Loop over the locations
		for (uint j = 0; j < locations.size(); j++) {
			Resources::Location *location = locations[j];

			Common::Path roomArchive = archiveLoader->buildArchiveName(level, location);
			debugPrintf("%s - %s\n", roomArchive.toString(Common::Path::kNativeSeparator).c_str(), location->getName().c_str());
		}

		archiveLoader->returnRoot(levelArchive);
		archiveLoader->unloadUnused();
	}

	// Restore the global archive loader
	StarkArchiveLoader = gameArchiveLoader;

	delete archiveLoader;

	return true;
}

bool Console::Cmd_ChangeLocation(int argc, const char **argv) {
	if (argc >= 3) {
		// Assert indices
		Common::Path xarcFileName(Common::String::format("%s/%s/%s.xarc", argv[1], argv[2], argv[2]));
		if (!Common::File::exists(xarcFileName)) {
			debugPrintf("Invalid location %s %s. Use listLocations to get correct indices\n", argv[1], argv[2]);
			return true;
		}

		uint levelIndex = strtol(argv[1] , nullptr, 16);
		uint locationIndex = strtol(argv[2] , nullptr, 16);

		StarkUserInterface->changeScreen(Screen::kScreenGame);

		if (!StarkGlobal->getRoot()) {
			StarkResourceProvider->initGlobal();
		}

		StarkResourceProvider->requestLocationChange(levelIndex, locationIndex);

		return false;
	} else if (argc > 1) {
		debugPrintf("Too few args\n");
	}

	debugPrintf("Change the current location. Use listLocations to get indices\n");
	debugPrintf("Usage :\n");
	debugPrintf("changeLocation [level] [location]\n");
	return true;
}

bool Console::Cmd_ChangeChapter(int argc, const char **argv) {
	if (!StarkGlobal->getLevel()) {
		debugPrintf("The global level has not been loaded\n");
		return true;
	}

	if (argc != 2) {
		debugPrintf("Change the current chapter\n");
		debugPrintf("Usage :\n");
		debugPrintf("changeChapter [value]\n");
		return true;
	}

	char *endPtr = nullptr;
	long value = strtol(argv[1], &endPtr, 10);
	if (*endPtr == '\0' && value >= 0 && value <= INT_MAX)
		StarkGlobal->setCurrentChapter((int32) value);
	else
		debugPrintf("Invalid chapter\n");

	return true;
}

bool Console::Cmd_Location(int argc, const char **argv) {
	Current *current = StarkGlobal->getCurrent();

	if (!current) {
		debugPrintf("Game levels have not been loaded\n");
		return true;
	}

	if (argc != 1) {
		debugPrintf("Display the current location\n");
		debugPrintf("Usage :\n");
		debugPrintf("location\n");
		return true;
	}

	debugPrintf("location: %02x %02x\n", current->getLevel()->getIndex(), current->getLocation()->getIndex());

	return true;
}

bool Console::Cmd_Chapter(int argc, const char **argv) {
	if (!StarkGlobal->getLevel()) {
		debugPrintf("The global level has not been loaded\n");
		return true;
	}

	if (argc != 1) {
		debugPrintf("Display the current chapter\n");
		debugPrintf("Usage :\n");
		debugPrintf("chapter\n");
		return true;
	}

	int32 value = StarkGlobal->getCurrentChapter();

	debugPrintf("chapter: %d\n", value);

	return true;
}

} // End of namespace Stark
