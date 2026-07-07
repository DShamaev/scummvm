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
}

OpenGLSActorRenderer::~OpenGLSActorRenderer() {
	clearVertices();

	delete _shader;
	delete _shadowShader;
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
	Math::Vector3d sumDirection;
	bool hasLight = false;

	// Compute the contribution from each lights
	// The ambient light is skipped intentionally
	for (uint i = 1; i < lights.size(); ++i) {
		LightEntry *light = lights[i];
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

		if (contributes) {
			sumDirection += lightDirection;
			hasLight = true;
		}
	}

	if (hasLight) {
		// Clip the horizontal length. The game data caps this very short
		// (~0.075 world units), which pins the shadow right under the feet even
		// when a lamp is off to the side. shadow_length_scale (percent) raises
		// the cap so the shadow can stretch out in the lamp-cast direction,
		// up to the light geometry's own magnitude.
		int scalePercent = ConfMan.hasKey("shadow_length_scale")
				? CLIP((int)ConfMan.getInt("shadow_length_scale"), 100, 2000) : 600;
		float maxLen = StarkScene->getMaxShadowLength() * (scalePercent / 100.0f);

		Math::Vector2d horizontalProjection(sumDirection.x(), sumDirection.y());
		float shadowLength = MIN(horizontalProjection.getMagnitude(), maxLen);

		horizontalProjection.normalize();
		horizontalProjection *= shadowLength;

		sumDirection.x() = horizontalProjection.getX();
		sumDirection.y() = horizontalProjection.getY();
		sumDirection.z() = -1;
	} else {
		// Cast from above by default
		sumDirection.x() = 0;
		sumDirection.y() = 0;
		sumDirection.z() = -1;
	}

	return sumDirection;
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
