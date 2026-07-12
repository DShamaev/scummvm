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

#ifndef STARK_GFX_OPENGL_S_ACTOR_H
#define STARK_GFX_OPENGL_S_ACTOR_H

#include "engines/stark/gfx/renderentry.h"
#include "engines/stark/visual/actor.h"

#include "common/hashmap.h"
#include "common/hash-ptr.h"

#include "graphics/opengl/system_headers.h"

#if defined(USE_OPENGL_SHADERS)

namespace OpenGL {
	class Shader;
}

namespace Stark {
namespace Gfx {

class OpenGLSDriver;

class OpenGLSActorRenderer : public VisualActor {
public:
	OpenGLSActorRenderer(OpenGLSDriver *gfx);
	virtual ~OpenGLSActorRenderer();

	void render(const Math::Vector3d &position, float direction, const LightEntryArray &lights) override;

protected:
	typedef Common::HashMap<Face *, GLuint> FaceBufferMap;

	OpenGLSDriver *_gfx;
	OpenGL::Shader *_shader, *_shadowShader, *_shadowMapShader, *_shadowRecvShader, *_shadowBgShader;

	GLuint _faceVBO;
	GLuint _shadowRecvVBO;   // dynamic world-space ground quad for the shadow receive
	GLuint _shadowBgVBO;     // static fullscreen quad for the screen-space background shadow
	FaceBufferMap _faceEBO;

	// Shadow mapping: render this actor from the light into the driver's shadow
	// map (depth-encoded-in-colour) before the main draw, so receivers can sample
	// it. Builds an orthographic light view-projection framing the actor.
	void renderShadowMap(const Math::Matrix4 &model, const Math::Vector3d &position,
			const LightEntryArray &lights);
	// Receive the shadow map on a ground quad under the actor (replaces the
	// jittered projection when shadow mapping is on).
	void renderShadowReceive(const Math::Vector3d &position);
	// Screen-space receive: drape the shadow over the depth-mapped background
	// (floor, walls, furniture) by reconstructing each pixel's world position
	// from the background depth mask. Falls back to the ground quad when there's
	// no depth mask. Returns false if it couldn't run.
	bool renderShadowBackground();

	void clearVertices();
	void uploadVertices();
	GLuint createModelVBO(const Model *model);
	GLuint createFaceEBO(const Face *face);
	void setBonePositionArrayUniform(OpenGL::Shader *shader, const char *uniform);
	void setBoneRotationArrayUniform(OpenGL::Shader *shader, const char *uniform);
	void setLightArrayUniform(const LightEntryArray &lights);

	/** Number of jittered passes used to soften the shadow penumbra */
	static const int kShadowPassCount = 16;

	Math::Vector3d computeShadowLightDirection(const LightEntryArray &lights, const Math::Vector3d &actorPosition);

	// The light index the shadow currently follows. Using the single dominant
	// light (not the sum of all) stops the shadow flipping when the character
	// crosses between opposing lamps; hysteresis on this index stops it flickering
	// when two lights are near-equal.
	int _shadowDominantIdx;

	bool getPointLightContribution(LightEntry *light, const Math::Vector3d &actorPosition,
			Math::Vector3d &direction, float weight = 1.0f);
	bool getDirectionalLightContribution(LightEntry *light, Math::Vector3d &direction);
	bool getSpotLightContribution(LightEntry *light, const Math::Vector3d &actorPosition, Math::Vector3d &direction);
};

} // End of namespace Gfx
} // End of namespace Stark

#endif // defined(USE_OPENGL_SHADERS)

#endif // STARK_GFX_OPENGL_S_ACTOR_H
