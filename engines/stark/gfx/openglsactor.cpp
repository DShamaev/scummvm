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

#include "engines/stark/gfx/openglsactor.h"

#include "engines/stark/model/model.h"
#include "engines/stark/model/animhandler.h"
#include "engines/stark/resources/location.h"

#include "common/config-manager.h"
#include "math/glmath.h"
#include "engines/stark/scene.h"
#include "engines/stark/services/global.h"
#include "engines/stark/services/services.h"
#include "engines/stark/services/settings.h"
#include "engines/stark/gfx/color.h"
#include "engines/stark/gfx/opengls.h"
#include "engines/stark/gfx/texture.h"

#if defined(USE_OPENGL_SHADERS)

#include "graphics/opengl/shader.h"

namespace Stark {
namespace Gfx {

// Classify a material from its name/texture into rough surface parameters, so
// skin and cloth stay matte while metal, armour and eyes get a real highlight,
// instead of the whole character sharing one plasticky specular. The default
// matches the previous uniform look, so unrecognised materials are unchanged.
static void classifyMaterial(const Common::String &name, const Common::String &texture,
                             float &specStrength, float &shininess, float &rim) {
	specStrength = 0.18f;
	shininess = 18.0f;
	rim = 0.25f;

	Common::String n = name + " " + texture;
	n.toLowercase();

	if (n.contains("eye")) {
		specStrength = 0.55f; shininess = 60.0f;
	} else if (n.contains("metal") || n.contains("armor") || n.contains("armour")
			|| n.contains("steel") || n.contains("gold") || n.contains("blade")
			|| n.contains("sword") || n.contains("helm") || n.contains("buckle")
			|| n.contains("chrome") || n.contains("glass")) {
		specStrength = 0.50f; shininess = 46.0f;
	} else if (n.contains("hair")) {
		specStrength = 0.12f; shininess = 22.0f;
	} else if (n.contains("cloth") || n.contains("dress") || n.contains("shirt")
			|| n.contains("pant") || n.contains("robe") || n.contains("cape")
			|| n.contains("coat") || n.contains("jacket") || n.contains("skirt")
			|| n.contains("trouser") || n.contains("kjole") || n.contains("cloak")) {
		specStrength = 0.05f; shininess = 10.0f;
	} else if (n.contains("face") || n.contains("skin") || n.contains("head")
			|| n.contains("hand") || n.contains("arm") || n.contains("leg")
			|| n.contains("body") || n.contains("neck") || n.contains("foot")) {
		specStrength = 0.12f; shininess = 16.0f; rim = 0.30f;
	}
}

OpenGLSActorRenderer::OpenGLSActorRenderer(OpenGLSDriver *gfx) :
		VisualActor(),
		_gfx(gfx),
		_faceVBO(0) {
	_shader = _gfx->createActorShaderInstance();
	_shadowShader = _gfx->createShadowShaderInstance();
	_shadowMapShader = _gfx->createShadowMapShaderInstance();
	_shadowRecvShader = _gfx->createShadowRecvShaderInstance();
	_shadowBgShader = _gfx->createShadowBgShaderInstance();
	_shadowRecvVBO = 0;
	_shadowBgVBO = 0;
	_shadowDominantIdx = -1;
}

OpenGLSActorRenderer::~OpenGLSActorRenderer() {
	clearVertices();

	delete _shader;
	delete _shadowShader;
	delete _shadowMapShader;
	delete _shadowRecvShader;
	delete _shadowBgShader;
	if (_shadowRecvVBO) {
		OpenGL::Shader::freeBuffer(_shadowRecvVBO);
		_shadowRecvVBO = 0;
	}
	if (_shadowBgVBO) {
		OpenGL::Shader::freeBuffer(_shadowBgVBO);
		_shadowBgVBO = 0;
	}
}

void OpenGLSActorRenderer::render(const Math::Vector3d &position, float direction, const LightEntryArray &lights) {
	if (_modelIsDirty) {
		// Update the OpenGL Buffer Objects if required
		clearVertices();
		uploadVertices();
		_modelIsDirty = false;
	}

	// TODO: Move updates outside of the rendering code
	_animHandler->animate(_time);
	_model->updateBoundingBox();

	_gfx->set3DMode();

	Math::Matrix4 model = getModelMatrix(position, direction);
	Math::Matrix4 view = StarkScene->getViewMatrix();
	Math::Matrix4 projection = StarkScene->getProjectionMatrix();

	Math::Matrix4 modelViewMatrix = view * model;
	modelViewMatrix.transpose(); // OpenGL expects matrices transposed

	Math::Matrix4 projectionMatrix = projection;
	projectionMatrix.transpose(); // OpenGL expects matrices transposed

	Math::Matrix4 normalMatrix = modelViewMatrix;
	normalMatrix.invertAffineOrthonormal();

	// Shadow mapping (Phase 1): render this actor from the light into the shadow
	// map before the main draw. Self-contained (binds/restores the FBO), so the
	// main shader setup below is unaffected. No-op when disabled.
	renderShadowMap(model, position, lights);

	_shader->enableVertexAttribute("position1", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 0);
	_shader->enableVertexAttribute("position2", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 12);
	_shader->enableVertexAttribute("bone1", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 24);
	_shader->enableVertexAttribute("bone2", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 28);
	_shader->enableVertexAttribute("boneWeight", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 32);
	_shader->enableVertexAttribute("normal", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 36);
	_shader->enableVertexAttribute("texcoord", _faceVBO, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 48);
	_shader->use(true);

	_shader->setUniform("modelViewMatrix", modelViewMatrix);
	_shader->setUniform("projectionMatrix", projectionMatrix);
	_shader->setUniform("normalMatrix", normalMatrix.getRotation());
	setBoneRotationArrayUniform(_shader, "boneRotation");
	setBonePositionArrayUniform(_shader, "bonePosition");
	setLightArrayUniform(lights);

	// Scene lighting tint, computed and time-smoothed by the owning
	// ModelItem so it survives renderer recreation on animation changes
	_shader->setUniform("ambientTint", _ambientTint);

	// Soft directional shaping derived from the baked background (from the item)
	_shader->setUniform("sceneLightDir", _sceneLightDir);
	_shader->setUniform1f("sceneLightStrength", _sceneLightStrength);

	_shader->setUniform("tex", 0);
	_shader->setUniform("normalTex", 1);
	_shader->setUniform("aoTex", 2);
	_shader->setUniform("debugShowNormals", ConfMan.getBool("debug_show_normals") ? 1 : 0);
	_shader->setUniform1f("enhancedLight",
			ConfMan.hasKey("enhanced_actor_light") && !ConfMan.getBool("enhanced_actor_light") ? 0.0f : 1.0f);

	// Distance from the camera to the character's origin, in the same eye-space
	// units as the background depth band. Fog is applied by this single stable
	// distance rather than per-fragment length(EyePosition): the animating
	// vertices wobble toward/away from the camera each frame, and against the
	// (steep) background-derived fog band that wobble made the character's color
	// pulsate. Its ground position only changes as it actually walks, smoothly.
	Math::Vector4d fogCharWorld(position.x(), position.y(), position.z(), 1.0f);
	Math::Vector4d fogCharEye = view * fogCharWorld;
	float fogCharDist = Math::Vector3d(fogCharEye.x(), fogCharEye.y(), fogCharEye.z()).getMagnitude();
	_shader->setUniform1f("fogCharDist", fogCharDist);

	// Atmospheric fog: only where the location has a depth-mapped background
	// (exteriors), using its depth range and horizon color
	float fogDensity = 0.0f;
	if (StarkSettings->getBoolSetting(Settings::kDepthFog) && StarkGlobal->getCurrent()) {
		float zMax = StarkScene->getBackgroundDepthMax();
		if (zMax > 0.0f) {
			Resources::Location *location = StarkGlobal->getCurrent()->getLocation();
			Gfx::Color fog = location->getHorizonColor();
			_shader->setUniform("fogColor", Math::Vector3d(fog.r / 255.0f, fog.g / 255.0f, fog.b / 255.0f));
			// Fog starts partway into the scene and reaches full at the far plane
			_shader->setUniform1f("fogStart", StarkScene->getBackgroundDepthMin()
					+ (zMax - StarkScene->getBackgroundDepthMin()) * 0.35f);
			_shader->setUniform1f("fogEnd", zMax);
			fogDensity = CLIP(ConfMan.getInt("fog_density"), 0, 100) / 100.0f;
		}
	}
	_shader->setUniform1f("fogDensity", fogDensity);

	bool normalMappingEnabled = StarkSettings->getBoolSetting(Settings::kNormalMapping);
	bool materialsEnabled = !ConfMan.hasKey("enable_materials") || ConfMan.getBool("enable_materials");
	bool aoEnabled = ConfMan.hasKey("enable_cavity_ao") && ConfMan.getBool("enable_cavity_ao");

	Common::Array<Face *> faces = _model->getFaces();
	Common::Array<Material *> mats = _model->getMaterials();

	for (Common::Array<Face *>::const_iterator face = faces.begin(); face != faces.end(); ++face) {
		// For each face draw its vertices from the VBO, indexed by the EBO
		const Material *material = mats[(*face)->materialId];
		const Gfx::Texture *tex = resolveTexture(material);

		glActiveTexture(GL_TEXTURE0);
		if (tex) {
			tex->bind();
		} else {
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		const Gfx::Texture *normalTex = normalMappingEnabled ? resolveNormalTexture(material) : nullptr;
		if (normalTex) {
			glActiveTexture(GL_TEXTURE1);
			normalTex->bind();
			glActiveTexture(GL_TEXTURE0);
		}

		const Gfx::Texture *aoTex = aoEnabled ? resolveAOTexture(material) : nullptr;
		if (aoTex) {
			glActiveTexture(GL_TEXTURE2);
			aoTex->bind();
			glActiveTexture(GL_TEXTURE0);
		}

		// Per-material specular/rim so different substances read distinctly
		float specStrength, shininess, rim;
		if (materialsEnabled) {
			classifyMaterial(material->name, material->texture, specStrength, shininess, rim);
		} else {
			specStrength = 0.18f; shininess = 18.0f; rim = 0.25f;
		}
		// Live global dial for overall specular intensity (percent, 100 = the
		// tuned defaults). Lets the highlight be softened further or removed
		// without a rebuild if it still twinkles on any given character.
		float specScale = CLIP(ConfMan.hasKey("specular_scale")
				? (int)ConfMan.getInt("specular_scale") : 100, 0, 300) / 100.0f;
		specStrength *= specScale;
		_shader->setUniform1f("specularStrength", specStrength);
		_shader->setUniform1f("specularShininess", shininess);
		_shader->setUniform1f("rimStrength", rim);

		_shader->setUniform("textured", tex != nullptr);
		_shader->setUniform("hasNormalMap", normalTex != nullptr);
		_shader->setUniform("hasAOMap", aoTex != nullptr);
		_shader->setUniform("color", Math::Vector3d(material->r, material->g, material->b));

		GLuint ebo = _faceEBO[*face];
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glDrawElements(GL_TRIANGLES, (*face)->vertexIndices.size(), GL_UNSIGNED_INT, 0);
	}

	_shader->unbind();

	// Some scenes (e.g. the chapter intro) disable shadows in their data via
	// castsShadow / shouldRenderShadows. force_shadows overrides both so the
	// character still casts one - at the risk of artifacts where the scene
	// wasn't staged with a floor at y=0.
	bool forceShadows = ConfMan.hasKey("force_shadows") && ConfMan.getBool("force_shadows");
	if ((_castsShadow || forceShadows) &&
	    (StarkScene->shouldRenderShadows() || forceShadows) &&
	    StarkSettings->getBoolSetting(Settings::kShadow)) {

		// Shadow mapping: cast the shadow by sampling the shadow map on a ground
		// quad, instead of the jittered silhouette projection. Skips the rest.
		if (ConfMan.getBool("enable_shadow_mapping") && _gfx->isShadowMapValid()) {
			renderShadowReceive(position);
			return;
		}

		glEnable(GL_BLEND);
		glEnable(GL_STENCIL_TEST);

		// The jittered passes all draw at the same depth. Don't write depth,
		// otherwise the first pass prevents the others from accumulating.
		glDepthMask(GL_FALSE);

		// The shadow lies on the floor plane, where depth-mapped backgrounds
		// write nearly the same depth values. Pull the shadow slightly towards
		// the camera so it isn't rejected by its own receiving surface, while
		// still being clipped by genuinely closer scene geometry (furniture).
		// Too much bias and the shadow also beats the furniture depth and paints
		// over it; too little and it z-fights the floor. shadow_depth_bias tunes
		// the units term so this can be balanced against a location's depth map.
		float depthBias = ConfMan.hasKey("shadow_depth_bias")
				? CLIP((int)ConfMan.getInt("shadow_depth_bias"), 0, 16) : 2;
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -depthBias);

		_shadowShader->enableVertexAttribute("position1", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 0);
		_shadowShader->enableVertexAttribute("position2", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 12);
		_shadowShader->enableVertexAttribute("bone1", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 24);
		_shadowShader->enableVertexAttribute("bone2", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 28);
		_shadowShader->enableVertexAttribute("boneWeight", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 32);
		_shadowShader->use(true);

		Math::Matrix4 mvp = projection * view * model;
		mvp.transpose();
		_shadowShader->setUniform("mvp", mvp);

		// Fade the far tip of the shadow. casterHeight normalizes the fade to the
		// model's height; shadow_fade (percent) controls how much the tip dissolves.
		float casterHeight = _model->getBoundingBox().getMax().y();
		float shadowFade = CLIP(ConfMan.hasKey("shadow_fade") ? (int)ConfMan.getInt("shadow_fade") : 70, 0, 100) / 100.0f;
		if (casterHeight <= 0.0f) {
			shadowFade = 0.0f; // bounding box not ready - keep the shadow uniform
		}
		_shadowShader->setUniform1f("casterHeight", casterHeight > 0.0f ? casterHeight : 1.0f);
		_shadowShader->setUniform1f("shadowFade", shadowFade);

		setBoneRotationArrayUniform(_shadowShader, "boneRotation");
		setBonePositionArrayUniform(_shadowShader, "bonePosition");

		Math::Matrix4 modelInverse = model;
		modelInverse.inverse();
		Math::Matrix3 worldToModelRot = modelInverse.getRotation();
		Math::Vector3d worldDirection = computeShadowLightDirection(lights, position);

		bool softShadows = StarkSettings->getBoolSetting(Settings::kSoftShadows);
		// Runtime-adjustable quality: fewer passes are much cheaper with the
		// high-poly enhanced meshes (each pass redraws the whole mesh)
		int passCount = softShadows
				? CLIP((int)ConfMan.getInt("shadow_passes"), 1, (int)kShadowPassCount)
				: 1;

		// Opacity of the fully covered shadow core
		static const float kCoreDarkness = 0.65f;

		// Sample offsets simulating an area light: center + two rings.
		// The radius shrinks with fewer passes so the samples stay densely
		// overlapped - a darker, crisper shadow - instead of a faint scatter.
		float jitterRadius = 0.03f * sqrtf(passCount / (float)kShadowPassCount);
		static const float kJitterX[kShadowPassCount] = {
			 0.0f,
			 0.5f,    0.1545f, -0.4045f, -0.4045f,  0.1545f,
			 0.951f,  0.588f,   0.0f,    -0.588f,  -0.951f,
			-0.951f, -0.588f,   0.0f,     0.588f,   0.951f
		};
		static const float kJitterY[kShadowPassCount] = {
			 0.0f,
			 0.0f,    0.4755f,  0.294f,  -0.294f,  -0.4755f,
			 0.309f,  0.809f,   1.0f,     0.809f,   0.309f,
			-0.309f, -0.809f,  -1.0f,    -0.809f,  -0.309f
		};

		// Uniform per-pass opacity that converges to kCoreDarkness where all
		// the passes overlap, independent of pass count. This keeps the shadow
		// equally dark at 4 or 16 passes (only the softness changes).
		if (softShadows) {
			float passAlpha = 1.0f - powf(1.0f - kCoreDarkness, 1.0f / passCount);
			_shadowShader->setUniform1f("shadowAlpha", passAlpha);
		} else {
			_shadowShader->setUniform1f("shadowAlpha", 0.5f);
		}

		for (int pass = 0; pass < passCount; pass++) {
			Math::Vector3d jitteredDirection = worldDirection;
			jitteredDirection.x() += kJitterX[pass] * jitterRadius;
			jitteredDirection.y() += kJitterY[pass] * jitterRadius;

			// Transform the direction to the model space and pass to the shader
			jitteredDirection = worldToModelRot * jitteredDirection;
			_shadowShader->setUniform("lightDirection", jitteredDirection);

			if (pass > 0) {
				// The stencil buffer prevents self-overlapping geometry from
				// darkening twice within a pass. Reset it so the passes accumulate.
				glClear(GL_STENCIL_BUFFER_BIT);
			}

			for (Common::Array<Face *>::const_iterator face = faces.begin(); face != faces.end(); ++face) {
				GLuint ebo = _faceEBO[*face];
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
				glDrawElements(GL_TRIANGLES, (*face)->vertexIndices.size(), GL_UNSIGNED_INT, 0);
			}
		}

		glDisable(GL_POLYGON_OFFSET_FILL);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glDisable(GL_STENCIL_TEST);

		_shadowShader->unbind();
	}
}

void OpenGLSActorRenderer::renderShadowMap(const Math::Matrix4 &model, const Math::Vector3d &position,
		const LightEntryArray &lights) {
	if (!ConfMan.getBool("enable_shadow_mapping")) {
		return;
	}

	// Select the strongest shadow-casting lights and render April into one shadow
	// map each; receivers blend them by weight so lamps cross-fade rather than the
	// dominant light hard-switching as she moves.
	int wanted = CLIP(ConfMan.hasKey("shadow_light_count")
			? (int)ConfMan.getInt("shadow_light_count") : 2, 1, (int)OpenGLSDriver::kMaxShadowLights);
	Math::Vector3d dirs[OpenGLSDriver::kMaxShadowLights];
	float weights[OpenGLSDriver::kMaxShadowLights];
	int count = computeShadowLights(lights, position, dirs, weights, wanted);
	if (count == 0) {
		return;
	}

	float reach = CLIP(ConfMan.hasKey("shadow_length_scale")
			? (int)ConfMan.getInt("shadow_length_scale") : 200, 50, 1000) / 100.0f;
	float shadowLen = 150.0f * reach;   // ~April height * reach, in world units
	Common::Array<Face *> faces = _model->getFaces();

	int rendered = 0;
	for (int li = 0; li < count; li++) {
		int size = _gfx->renderShadowMapBegin(li);
		if (size == 0) {
			break; // shadow mapping disabled or FBO unavailable
		}

		// World-space cast direction for this light (travels along L; z = -1 down).
		Math::Vector3d L = dirs[li];
		if (L.getMagnitude() < 0.001f) {
			L = Math::Vector3d(0.0f, 0.0f, -1.0f);
		}
		L.normalize();

		// Frame April AND the shadow she throws under this light, sliding the frame
		// toward where the shadow falls so the map's resolution is spent on it.
		float dist = 400.0f;
		Math::Vector3d center = position;
		center.z() += 90.0f;
		Math::Vector2d ground(L.x(), L.y());
		if (ground.getMagnitude() > 0.0001f) {
			ground.normalize();
			center.x() += ground.getX() * shadowLen * 0.5f;
			center.y() += ground.getY() * shadowLen * 0.5f;
		}
		Math::Vector3d eye = center - L * dist;
		Math::Vector3d up(0.0f, 0.0f, 1.0f);
		if (ABS(L.z()) > 0.99f) {
			up = Math::Vector3d(0.0f, 1.0f, 0.0f);
		}

		Math::Matrix4 lightView = Math::makeLookAtMatrix(eye, center, up);
		lightView.transpose();
		lightView.translate(-eye);

		float halfExtent = 120.0f + 0.6f * shadowLen;
		float nearZ = 1.0f;
		float farZ = dist + shadowLen + 250.0f;
		Math::Matrix4 lightProj;
		lightProj(0, 0) = 1.0f / halfExtent;
		lightProj(1, 1) = 1.0f / halfExtent;
		lightProj(2, 2) = -2.0f / (farZ - nearZ);
		lightProj(3, 2) = -(farZ + nearZ) / (farZ - nearZ);
		lightProj.transpose();

		Math::Matrix4 lightViewProj = lightProj * lightView;
		Math::Matrix4 lightMVP = lightViewProj * model;
		lightMVP.transpose();

		_shadowMapShader->enableVertexAttribute("position1", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 0);
		_shadowMapShader->enableVertexAttribute("position2", _faceVBO, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 12);
		_shadowMapShader->enableVertexAttribute("bone1", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 24);
		_shadowMapShader->enableVertexAttribute("bone2", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 28);
		_shadowMapShader->enableVertexAttribute("boneWeight", _faceVBO, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float), 32);
		_shadowMapShader->use(true);
		_shadowMapShader->setUniform("lightMVP", lightMVP);
		setBoneRotationArrayUniform(_shadowMapShader, "boneRotation");
		setBonePositionArrayUniform(_shadowMapShader, "bonePosition");

		for (Common::Array<Face *>::const_iterator face = faces.begin(); face != faces.end(); ++face) {
			GLuint ebo = _faceEBO[*face];
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glDrawElements(GL_TRIANGLES, (*face)->vertexIndices.size(), GL_UNSIGNED_INT, 0);
		}
		_shadowMapShader->unbind();

		// Store the world->light-clip matrix (pre final transpose) + weight.
		_gfx->renderShadowMapEnd(li, lightViewProj, weights[li]);
		rendered++;
	}
	_gfx->setShadowMapCount(rendered);

	// Debug: show the first shadow map in the screen corner to verify the caster.
	if (ConfMan.hasKey("shadow_map_debug") && ConfMan.getBool("shadow_map_debug")) {
		_gfx->debugDrawShadowMap();
	}
}

bool OpenGLSActorRenderer::renderShadowBackground(const Math::Vector3d &position) {
	if (!_gfx->isShadowMapValid() || !_gfx->hasWorldDepth()) {
		return false;
	}

	// Prefer reconstructing from the real depth buffer (includes depth-stamped
	// props) so shadows land on furniture correctly; fall back to the background
	// mask if this GL stack rejects the depth copy. Captured now, while the engine
	// framebuffer is bound and the viewport is the game region.
	GLuint sceneDepthTex = _gfx->captureViewportDepth();

	// Fullscreen NDC quad + UV.
	if (!_shadowBgVBO) {
		static const float quad[16] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			 1.0f,  1.0f,  1.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f
		};
		_shadowBgVBO = OpenGL::Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(quad), quad);
	}

	// Projection x/y scale (frustum diagonal, unchanged by the GL transpose) for
	// the eye-space reconstruction, and the inverse view (eye -> world).
	Math::Matrix4 projection = StarkScene->getProjectionMatrix();
	float projSX = projection(0, 0);
	float projSY = projection(1, 1);

	Math::Matrix4 invView = StarkScene->getViewMatrix();
	invView.inverse();
	invView.transpose();

	// Inverse projection (clip -> eye) for the real-depth reconstruction path.
	Math::Matrix4 invProj = StarkScene->getProjectionMatrix();
	invProj.inverse();
	invProj.transpose();

	Math::Matrix4 lightVP0 = _gfx->getShadowLightViewProj(0);
	lightVP0.transpose();
	Math::Matrix4 lightVP1 = _gfx->getShadowLightViewProj(1);
	lightVP1.transpose();
	int shadowCount = _gfx->getShadowMapCount();

	// First cut: no scene depth-test yet (validate the reconstruction first; the
	// character may briefly self-shadow until occlusion is added).
	int bgDebug = CLIP(ConfMan.hasKey("shadow_bg_debug") ? (int)ConfMan.getInt("shadow_bg_debug") : 0, 0, 4);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	if (bgDebug > 0) {
		glDisable(GL_BLEND);   // opaque debug fills the screen
	} else {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	_shadowBgShader->enableVertexAttribute("position", _shadowBgVBO, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	_shadowBgShader->enableVertexAttribute("texcoord", _shadowBgVBO, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 2 * sizeof(float));
	_shadowBgShader->use(true);
	_shadowBgShader->setUniform("shadowTex0", 0);
	_shadowBgShader->setUniform("shadowTex1", 3);
	_shadowBgShader->setUniform("bgDepthTex", 1);
	_shadowBgShader->setUniform1f("zMin", _gfx->getWorldDepthZMin());
	_shadowBgShader->setUniform1f("zMax", _gfx->getWorldDepthZMax());
	_shadowBgShader->setUniform("projScale", Math::Vector2d(projSX, projSY));
	_shadowBgShader->setUniform("invView", invView);
	_shadowBgShader->setUniform("lightVP0", lightVP0);
	_shadowBgShader->setUniform("lightVP1", lightVP1);
	_shadowBgShader->setUniform1f("weight0", _gfx->getShadowWeight(0));
	_shadowBgShader->setUniform1f("weight1", _gfx->getShadowWeight(1));
	_shadowBgShader->setUniform1f("shadowCount", (float)shadowCount);
	float alpha = CLIP(ConfMan.hasKey("shadow_map_alpha") ? (int)ConfMan.getInt("shadow_map_alpha") : 55, 0, 100) / 100.0f;
	_shadowBgShader->setUniform1f("shadowAlpha", alpha);
	float bias = CLIP(ConfMan.hasKey("shadow_map_bias") ? (int)ConfMan.getInt("shadow_map_bias") : 20, 0, 2000) / 100000.0f;
	_shadowBgShader->setUniform1f("shadowBias", bias);
	float soft = CLIP(ConfMan.hasKey("shadow_map_softness") ? (int)ConfMan.getInt("shadow_map_softness") : 2, 1, 40);
	_shadowBgShader->setUniform1f("shadowSoftness", soft);
	_shadowBgShader->setUniform1f("contactHarden", ConfMan.getBool("shadow_contact_harden") ? 1.0f : 0.0f);
	_shadowBgShader->setUniform("shadowTexel", Math::Vector2d(1.0f / 1024.0f, 1.0f / 1024.0f));
	_shadowBgShader->setUniform1f("bgDebug", (float)bgDebug);
	// Spatial falloff around April's feet: keeps the drape on nearby walls/furniture
	// and off far surfaces / under-floor areas that happen to fall in the frustum.
	_shadowBgShader->setUniform("actorWorld", position);
	float reach = CLIP(ConfMan.hasKey("shadow_length_scale") ? (int)ConfMan.getInt("shadow_length_scale") : 200, 50, 1000) / 100.0f;
	_shadowBgShader->setUniform1f("shadowReach", 150.0f * reach);
	// Real-depth reconstruction (props included) when the depth copy succeeded.
	_shadowBgShader->setUniform("sceneDepthTex", 2);
	_shadowBgShader->setUniform("invProj", invProj);
	_shadowBgShader->setUniform1f("useRealDepth", sceneDepthTex != 0 ? 1.0f : 0.0f);

	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, _gfx->getShadowMapTexture(1));
	if (sceneDepthTex != 0) {
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
	}
	glActiveTexture(GL_TEXTURE1);
	_gfx->bindWorldDepth();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _gfx->getShadowMapTexture(0));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (sceneDepthTex != 0) {
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0);
	_shadowBgShader->unbind();

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	return true;
}

void OpenGLSActorRenderer::renderShadowReceive(const Math::Vector3d &position) {
	if (!_gfx->isShadowMapValid()) {
		return;
	}

	// Prefer draping the shadow over the depth-mapped background (walls/furniture)
	// when a depth mask exists; fall back to the flat ground quad otherwise.
	if (!ConfMan.hasKey("shadow_wall") || ConfMan.getBool("shadow_wall")) {
		if (ConfMan.hasKey("shadow_bg_debug") && ConfMan.getInt("shadow_bg_debug") == 5) {
			Math::Matrix4 pj = StarkScene->getProjectionMatrix();
			Math::Matrix4 iv = StarkScene->getViewMatrix();
			iv.inverse();
			// invView translation = camera world position.
			float dbgReach = CLIP(ConfMan.hasKey("shadow_length_scale") ? (int)ConfMan.getInt("shadow_length_scale") : 200, 50, 1000) / 100.0f;
			warning("Stark shadow-bg: projScale=(%.4f,%.4f) actorPos=(%.1f,%.1f,%.1f) "
			        "camPos~(%.1f,%.1f,%.1f) zMin=%.1f zMax=%.1f reach=%.2f shadowLen=%.0f halfExtent=%.0f",
			        pj(0, 0), pj(1, 1), position.x(), position.y(), position.z(),
			        iv(0, 3), iv(1, 3), iv(2, 3),
			        _gfx->getWorldDepthZMin(), _gfx->getWorldDepthZMax(),
			        dbgReach, 150.0f * dbgReach, 120.0f + 0.6f * 150.0f * dbgReach);
			ConfMan.setInt("shadow_bg_debug", 0);
		}
		if (renderShadowBackground(position)) {
			return;
		}
	}

	// A ground quad on the floor plane under the actor (world is z-up; the floor
	// is at the actor's feet z). Sized to hold the shadow's reach (grows with
	// shadow_length_scale) so a long shadow isn't clipped by the quad edge. The
	// shadow map lookup decides where within it the shadow actually falls.
	float gReach = CLIP(ConfMan.hasKey("shadow_length_scale") ? (int)ConfMan.getInt("shadow_length_scale") : 200, 50, 1000) / 100.0f;
	const float S = 250.0f + 150.0f * gReach;
	float z = position.z();
	float quad[12] = {
		position.x() - S, position.y() - S, z,
		position.x() + S, position.y() - S, z,
		position.x() - S, position.y() + S, z,
		position.x() + S, position.y() + S, z
	};
	if (!_shadowRecvVBO) {
		_shadowRecvVBO = OpenGL::Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(quad), quad);
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, _shadowRecvVBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
	}

	Math::Matrix4 modelView = StarkScene->getViewMatrix();
	modelView.transpose();
	Math::Matrix4 proj = StarkScene->getProjectionMatrix();
	proj.transpose();
	Math::Matrix4 lightVP0 = _gfx->getShadowLightViewProj(0);
	lightVP0.transpose();
	Math::Matrix4 lightVP1 = _gfx->getShadowLightViewProj(1);
	lightVP1.transpose();
	int shadowCount = _gfx->getShadowMapCount();

	// Depth-test the floor quad against the scene so nearer geometry - furniture,
	// the character's own body - occludes the shadow instead of it painting over
	// everything. A polygon offset pushes the quad slightly toward the camera so
	// it isn't rejected by its own (depth-mapped) receiving floor, while genuinely
	// nearer things still win. Where the scene wrote no depth, the shadow just
	// draws (graceful fallback).
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	float ofs = CLIP(ConfMan.hasKey("shadow_map_depth_bias") ? (int)ConfMan.getInt("shadow_map_depth_bias") : 4, 0, 64);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -ofs);

	_shadowRecvShader->enableVertexAttribute("position", _shadowRecvVBO, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
	_shadowRecvShader->use(true);
	_shadowRecvShader->setUniform("modelViewMatrix", modelView);
	_shadowRecvShader->setUniform("projectionMatrix", proj);
	_shadowRecvShader->setUniform("lightVP0", lightVP0);
	_shadowRecvShader->setUniform("lightVP1", lightVP1);
	_shadowRecvShader->setUniform("shadowTex0", 0);
	_shadowRecvShader->setUniform("shadowTex1", 1);
	_shadowRecvShader->setUniform1f("weight0", _gfx->getShadowWeight(0));
	_shadowRecvShader->setUniform1f("weight1", _gfx->getShadowWeight(1));
	_shadowRecvShader->setUniform1f("shadowCount", (float)shadowCount);
	float alpha = CLIP(ConfMan.hasKey("shadow_map_alpha") ? (int)ConfMan.getInt("shadow_map_alpha") : 55, 0, 100) / 100.0f;
	_shadowRecvShader->setUniform1f("shadowAlpha", alpha);
	float bias = CLIP(ConfMan.hasKey("shadow_map_bias") ? (int)ConfMan.getInt("shadow_map_bias") : 20, 0, 2000) / 100000.0f;
	_shadowRecvShader->setUniform1f("shadowBias", bias);
	float softness = CLIP(ConfMan.hasKey("shadow_map_softness") ? (int)ConfMan.getInt("shadow_map_softness") : 3, 1, 40);
	_shadowRecvShader->setUniform1f("shadowSoftness", softness);
	_shadowRecvShader->setUniform1f("contactHarden", ConfMan.getBool("shadow_contact_harden") ? 1.0f : 0.0f);
	_shadowRecvShader->setUniform("shadowTexel", Math::Vector2d(1.0f / 1024.0f, 1.0f / 1024.0f));

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, _gfx->getShadowMapTexture(1));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _gfx->getShadowMapTexture(0));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	_shadowRecvShader->unbind();

	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void OpenGLSActorRenderer::clearVertices() {
	OpenGL::Shader::freeBuffer(_faceVBO); // Zero names are silently ignored
	_faceVBO = 0;

	for (FaceBufferMap::iterator it = _faceEBO.begin(); it != _faceEBO.end(); ++it) {
		OpenGL::Shader::freeBuffer(it->_value);
	}

	_faceEBO.clear();
}

void OpenGLSActorRenderer::uploadVertices() {
	_faceVBO = createModelVBO(_model);

	Common::Array<Face *> faces = _model->getFaces();
	for (Common::Array<Face *>::const_iterator face = faces.begin(); face != faces.end(); ++face) {
		_faceEBO[*face] = createFaceEBO(*face);
	}
}

GLuint OpenGLSActorRenderer::createModelVBO(const Model *model) {
	const Common::Array<VertNode *> &modelVertices = model->getVertices();

	float *vertices = new float[14 * modelVertices.size()];
	float *vertPtr = vertices;

	// Build a vertex array
	for (Common::Array<VertNode *>::const_iterator tri = modelVertices.begin(); tri != modelVertices.end(); ++tri) {
		*vertPtr++ = (*tri)->_pos1.x();
		*vertPtr++ = (*tri)->_pos1.y();
		*vertPtr++ = (*tri)->_pos1.z();

		*vertPtr++ = (*tri)->_pos2.x();
		*vertPtr++ = (*tri)->_pos2.y();
		*vertPtr++ = (*tri)->_pos2.z();

		*vertPtr++ = (*tri)->_bone1;
		*vertPtr++ = (*tri)->_bone2;

		*vertPtr++ = (*tri)->_boneWeight;

		*vertPtr++ = (*tri)->_normal.x();
		*vertPtr++ = (*tri)->_normal.y();
		*vertPtr++ = (*tri)->_normal.z();

		*vertPtr++ = -(*tri)->_texS;
		*vertPtr++ = (*tri)->_texT;
	}

	GLuint vbo = OpenGL::Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(float) * 14 * modelVertices.size(), vertices);
	delete[] vertices;

	return vbo;
}

GLuint OpenGLSActorRenderer::createFaceEBO(const Face *face) {
	return OpenGL::Shader::createBuffer(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32) * face->vertexIndices.size(), &face->vertexIndices[0]);
}

void OpenGLSActorRenderer::setBonePositionArrayUniform(OpenGL::Shader *shader, const char *uniform) {
	const Common::Array<BoneNode *> &bones = _model->getBones();

	GLint pos = shader->getUniformLocation(uniform);
	if (pos == -1) {
		error("No uniform named '%s'", uniform);
	}

	float *positions = new float[3 * bones.size()];
	float *positionsPtr = positions;

	for (uint i = 0; i < bones.size(); i++) {
		*positionsPtr++ = bones[i]->_animPos.x();
		*positionsPtr++ = bones[i]->_animPos.y();
		*positionsPtr++ = bones[i]->_animPos.z();
	}

	glUniform3fv(pos, bones.size(), positions);
	delete[] positions;
}

void OpenGLSActorRenderer::setBoneRotationArrayUniform(OpenGL::Shader *shader, const char *uniform) {
	const Common::Array<BoneNode *> &bones = _model->getBones();

	GLint rot = shader->getUniformLocation(uniform);
	if (rot == -1) {
		error("No uniform named '%s'", uniform);
	}

	float *rotations = new float[4 * bones.size()];
	float *rotationsPtr = rotations;

	for (uint i = 0; i < bones.size(); i++) {
		*rotationsPtr++ =  bones[i]->_animRot.x();
		*rotationsPtr++ =  bones[i]->_animRot.y();
		*rotationsPtr++ =  bones[i]->_animRot.z();
		*rotationsPtr++ =  bones[i]->_animRot.w();
	}

	glUniform4fv(rot, bones.size(), rotations);
	delete[] rotations;
}

void OpenGLSActorRenderer::setLightArrayUniform(const LightEntryArray &lights) {
	static const uint maxLights = 10;

	assert(lights.size() >= 1);
	assert(lights.size() <= maxLights);

	const LightEntry *ambient = lights[0];
	assert(ambient->type == LightEntry::kAmbient); // The first light must be the ambient light
	_shader->setUniform("ambientColor", ambient->color);

	Math::Matrix4 viewMatrix = StarkScene->getViewMatrix();
	Math::Matrix3 viewMatrixRot = viewMatrix.getRotation();

	for (uint i = 0; i < lights.size() - 1; i++) {
		const LightEntry *l = lights[i + 1];

		Math::Vector4d worldPosition;
		worldPosition.x() = l->position.x();
		worldPosition.y() = l->position.y();
		worldPosition.z() = l->position.z();
		worldPosition.w() = 1.0;

		Math::Vector4d eyePosition = viewMatrix * worldPosition;

		// The light type is stored in the w coordinate of the position to save an uniform slot
		eyePosition.w() = l->type;

		Math::Vector3d worldDirection = l->direction;
		Math::Vector3d eyeDirection = viewMatrixRot * worldDirection;
		eyeDirection.normalize();

		_shader->setUniform(Common::String::format("lights[%d].position", i).c_str(), eyePosition);
		_shader->setUniform(Common::String::format("lights[%d].direction", i).c_str(), eyeDirection);
		_shader->setUniform(Common::String::format("lights[%d].color", i).c_str(), l->color);

		Math::Vector4d params;
		params.x() = l->falloffNear;
		params.y() = l->falloffFar;
		params.z() = l->innerConeAngle.getCosine();
		params.w() = l->outerConeAngle.getCosine();

		_shader->setUniform(Common::String::format("lights[%d].params", i).c_str(), params);
	}

	for (uint i = lights.size() - 1; i < maxLights; i++) {
		// Make sure unused lights are disabled
		_shader->setUniform(Common::String::format("lights[%d].position", i).c_str(), Math::Vector4d());
	}
}

Math::Vector3d OpenGLSActorRenderer::computeShadowLightDirection(const LightEntryArray &lights,
                                            const Math::Vector3d &actorPosition) {
	// Follow the SINGLE strongest light, not the sum of all of them. Summing
	// opposing lamps cancels and reverses the horizontal direction as the
	// character crosses between them, which is the flip. The dominant light gives
	// a stable direction within its region.
	Math::Vector3d bestDir;
	float bestMag = 0.0f;
	int bestIdx = -1;
	Math::Vector3d stickyDir;
	float stickyMag = 0.0f;

	// The ambient light (index 0) is skipped intentionally.
	for (uint i = 1; i < lights.size(); ++i) {
		LightEntry *light = lights[i];

		// Only consider lights that could plausibly cast a ground shadow, i.e.
		// that come from ABOVE the actor. Scenes (Academy) include sideways/upward
		// "fill" directional lights that otherwise win the shadow and point it the
		// wrong way (back toward the window). World is z-up.
		bool overhead;
		if (light->type == LightEntry::kDirectional) {
			overhead = light->direction.z() < -0.05f;                       // travels downward
		} else {
			overhead = (light->position.z() - actorPosition.z()) > 0.0f;    // positioned above
		}
		if (!overhead) {
			continue;
		}

		bool contributes = false;
		Math::Vector3d lightDirection;
		switch (light->type) {
			case LightEntry::kPoint:
				contributes = getPointLightContribution(light, actorPosition, lightDirection);
				break;
			case LightEntry::kDirectional:
				contributes = getDirectionalLightContribution(light, lightDirection);
				break;
			case LightEntry::kSpot:
				contributes = getSpotLightContribution(light, actorPosition, lightDirection);
				break;
			case LightEntry::kAmbient:
			default:
				break;
		}

		if (!contributes) {
			continue;
		}
		float mag = lightDirection.getMagnitude();
		if (mag > bestMag) {
			bestMag = mag;
			bestDir = lightDirection;
			bestIdx = (int)i;
		}
		if ((int)i == _shadowDominantIdx) {
			stickyMag = mag;
			stickyDir = lightDirection;
		}
	}

	Math::Vector3d dir;
	if (bestIdx >= 0) {
		// Hysteresis: stay on the current light unless another is clearly stronger
		// (>1.4x), so the shadow doesn't flicker where two lamps are near-equal.
		if (_shadowDominantIdx >= 0 && stickyMag > 0.0f && bestMag < stickyMag * 1.4f) {
			dir = stickyDir;
		} else {
			dir = bestDir;
			_shadowDominantIdx = bestIdx;
		}

		// Set the shadow angle directly from shadow_length_scale, decoupled from
		// the game's tiny built-in cap and from the light's steepness. reach is the
		// horizontal:vertical ratio of the cast direction (z = -1), so a point at
		// height H throws its shadow ~H*reach along the floor. Bigger reach = a
		// lower, longer shadow that can actually reach walls/furniture. (The steep
		// overhead casters we pick would otherwise give a stub under the feet.)
		float reach = CLIP(ConfMan.hasKey("shadow_length_scale")
				? (int)ConfMan.getInt("shadow_length_scale") : 200, 50, 1000) / 100.0f;

		Math::Vector2d h(dir.x(), dir.y());
		if (h.getMagnitude() > 0.0001f) {
			h.normalize();
			h *= reach;
			dir.x() = h.getX();
			dir.y() = h.getY();
		} else {
			dir.x() = 0;
			dir.y() = 0;
		}
		dir.z() = -1;
	} else {
		_shadowDominantIdx = -1;
		dir = Math::Vector3d(0.0f, 0.0f, -1.0f);
	}

	// One-shot diagnostic (setInt shadow_debug_log 1): report the chosen light and
	// the type of every light, so we can see why a scene picks the wrong caster.
	// LightEntry types: point=1, directional=2, spot=4, ambient=other.
	if (ConfMan.hasKey("shadow_debug_log") && ConfMan.getInt("shadow_debug_log") > 0) {
		Common::String info;
		for (uint i = 0; i < lights.size(); ++i) {
			info += Common::String::format("[%u]type=%d pos=(%.0f,%.0f,%.0f) dir=(%.2f,%.2f,%.2f) ",
					i, (int)lights[i]->type,
					lights[i]->position.x(), lights[i]->position.y(), lights[i]->position.z(),
					lights[i]->direction.x(), lights[i]->direction.y(), lights[i]->direction.z());
		}
		warning("Stark shadow: chosenIdx=%d finalDir=(%.2f,%.2f,%.2f) actorPos=(%.0f,%.0f,%.0f) | %s",
				_shadowDominantIdx, dir.x(), dir.y(), dir.z(),
				actorPosition.x(), actorPosition.y(), actorPosition.z(), info.c_str());
		ConfMan.setInt("shadow_debug_log", 0);
	}

	return dir;
}

int OpenGLSActorRenderer::computeShadowLights(const LightEntryArray &lights,
		const Math::Vector3d &actorPosition, Math::Vector3d *outDirs, float *outWeights, int maxLights) {
	maxLights = CLIP(maxLights, 1, (int)OpenGLSDriver::kMaxShadowLights);

	// Collect the strongest 'maxLights' shadow-casting lights, kept in a small
	// descending-magnitude list. Same overhead + contribution filter as the single
	// dominant selection, just keeping more than one.
	float bestMag[OpenGLSDriver::kMaxShadowLights];
	Math::Vector3d bestDir[OpenGLSDriver::kMaxShadowLights];
	for (int k = 0; k < maxLights; k++) {
		bestMag[k] = 0.0f;
	}

	for (uint i = 1; i < lights.size(); ++i) {   // 0 = ambient, skip
		LightEntry *light = lights[i];

		bool overhead;
		if (light->type == LightEntry::kDirectional) {
			overhead = light->direction.z() < -0.05f;
		} else {
			overhead = (light->position.z() - actorPosition.z()) > 0.0f;
		}
		if (!overhead) {
			continue;
		}

		bool contributes = false;
		Math::Vector3d lightDirection;
		switch (light->type) {
			case LightEntry::kPoint:
				contributes = getPointLightContribution(light, actorPosition, lightDirection);
				break;
			case LightEntry::kDirectional:
				contributes = getDirectionalLightContribution(light, lightDirection);
				break;
			case LightEntry::kSpot:
				contributes = getSpotLightContribution(light, actorPosition, lightDirection);
				break;
			case LightEntry::kAmbient:
			default:
				break;
		}
		if (!contributes) {
			continue;
		}

		float mag = lightDirection.getMagnitude();
		for (int k = 0; k < maxLights; k++) {
			if (mag > bestMag[k]) {
				for (int j = maxLights - 1; j > k; j--) {
					bestMag[j] = bestMag[j - 1];
					bestDir[j] = bestDir[j - 1];
				}
				bestMag[k] = mag;
				bestDir[k] = lightDirection;
				break;
			}
		}
	}

	int found = 0;
	for (int k = 0; k < maxLights; k++) {
		if (bestMag[k] > 0.0f) {
			found = k + 1;
		}
	}
	if (found == 0) {
		// No overhead caster: a single straight-down shadow (matches the old
		// fallback) so the character still grounds visually.
		outDirs[0] = Math::Vector3d(0.0f, 0.0f, -1.0f);
		outWeights[0] = 1.0f;
		return 1;
	}

	float maxMag = bestMag[0];

	// Gate the secondary lights: a second shadow should only appear for a genuinely
	// comparable AND differently-aimed lamp. Otherwise a weak or near-opposite fill
	// light throws a stray shadow disconnected from the character (which reads as a
	// "misplaced" shadow). This keeps ordinary single-light rooms to one shadow.
	if (found > 1) {
		float minRel = CLIP(ConfMan.hasKey("shadow_second_light_min")
				? (int)ConfMan.getInt("shadow_second_light_min") : 50, 0, 100) / 100.0f;
		int kept = 1;
		for (int k = 1; k < found; k++) {
			float rel = maxMag > 0.0f ? bestMag[k] / maxMag : 0.0f;
			bool distinct = true;
			Math::Vector2d h0(bestDir[0].x(), bestDir[0].y());
			Math::Vector2d hk(bestDir[k].x(), bestDir[k].y());
			if (h0.getMagnitude() > 0.001f && hk.getMagnitude() > 0.001f) {
				h0.normalize();
				hk.normalize();
				distinct = (h0.getX() * hk.getX() + h0.getY() * hk.getY()) < 0.75f; // >~40 deg apart
			}
			if (rel >= minRel && distinct) {
				bestMag[kept] = bestMag[k];
				bestDir[kept] = bestDir[k];
				kept++;
			}
		}
		found = kept;
	}

	// Apply the cast angle (reach) to each and weight by strength relative to the
	// dominant light: dominant = 1.0, weaker lamps proportionally fainter, so a lone
	// light casts a full shadow and a rising second light fades in smoothly.
	float reach = CLIP(ConfMan.hasKey("shadow_length_scale")
			? (int)ConfMan.getInt("shadow_length_scale") : 200, 50, 1000) / 100.0f;
	for (int k = 0; k < found; k++) {
		Math::Vector3d dir = bestDir[k];
		Math::Vector2d h(dir.x(), dir.y());
		if (h.getMagnitude() > 0.0001f) {
			h.normalize();
			h *= reach;
			dir.x() = h.getX();
			dir.y() = h.getY();
		} else {
			dir.x() = 0.0f;
			dir.y() = 0.0f;
		}
		dir.z() = -1.0f;
		outDirs[k] = dir;
		outWeights[k] = maxMag > 0.0f ? bestMag[k] / maxMag : 1.0f;
	}
	return found;
}

bool OpenGLSActorRenderer::getPointLightContribution(LightEntry *light, const Math::Vector3d &actorPosition,
                                                     Math::Vector3d &direction, float weight) {
	float distance = light->position.getDistanceTo(actorPosition);

	if (distance > light->falloffFar) {
		return false;
	}

	float factor;
	if (distance > light->falloffNear) {
		if (light->falloffFar - light->falloffNear > 1) {
			factor = 1 - (distance - light->falloffNear) / (light->falloffFar - light->falloffNear);
		} else {
			factor = 0;
		}
	} else {
		factor = 1;
	}

	float brightness = (light->color.x() + light->color.y() + light->color.z()) / 3.0f;

	if (factor <= 0 || brightness <= 0) {
		return false;
	}

	direction = actorPosition - light->position;
	direction.normalize();
	direction *= factor * brightness * weight;

	return true;
}

bool OpenGLSActorRenderer::getDirectionalLightContribution(LightEntry *light, Math::Vector3d &direction) {
	float brightness = (light->color.x() + light->color.y() + light->color.z()) / 3.0f;

	if (brightness <= 0) {
		return false;
	}

	direction = light->direction;
	direction.normalize();
	direction *= brightness;

	return true;
}

bool OpenGLSActorRenderer::getSpotLightContribution(LightEntry *light, const Math::Vector3d &actorPosition,
                                                    Math::Vector3d &direction) {
	Math::Vector3d lightToActor = actorPosition - light->position;
	lightToActor.normalize();

	float cosAngle = MAX(0.0f, lightToActor.dotProduct(light->direction));
	float cone = (cosAngle - light->innerConeAngle.getCosine()) /
	             MAX(0.001f, light->outerConeAngle.getCosine() - light->innerConeAngle.getCosine());
	cone = CLIP(cone, 0.0f, 1.0f);

	if (cone <= 0) {
		return false;
	}

	return getPointLightContribution(light, actorPosition, direction, cone);
}

} // End of namespace Gfx
} // End of namespace Stark

#endif // defined(USE_OPENGL_SHADERS)
