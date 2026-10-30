#pragma once

/**
 * @file scene_geometry.hpp
 * @brief CPU-side geometry for entities and world markers
 *
 * No graphics dependency, so posing and marker shapes can be tested without a
 * device. All positions are view-relative: subtract the camera in double
 * precision first, then store as float.
 */

#include "finevox/core/entity_model.hpp"
#include "finevox/core/entity_render_state.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace finevox::render {

struct SceneVertex {
    glm::vec3 position;
    glm::vec3 normal;   ///< Zero = unlit
    glm::vec4 color;
};

/// Axis-aligned box. `lit` false gives a flat, unlit marker.
void appendBox(std::vector<SceneVertex>& out, const glm::vec3& center, const glm::vec3& halfExtents,
               const glm::vec4& color, bool lit = true);

/// Flat ring on the ground, facing up
void appendRing(std::vector<SceneVertex>& out, const glm::vec3& center, float radius, float width,
                const glm::vec4& color, int segments = 28);

/// Camera-facing quad, `center` at its middle
void appendBillboard(std::vector<SceneVertex>& out, const glm::vec3& center, const glm::vec3& right,
                     const glm::vec3& up, float halfWidth, float halfHeight, const glm::vec4& color);

/// Pose an entity model at its interpolated position and append its triangles.
/// `scratch*` keep allocations across calls.
struct EntityPoseScratch {
    std::vector<glm::mat4> local;
    std::vector<glm::mat4> world;
    std::vector<EntityVertex> vertices;
    std::vector<uint32_t> indices;
};

void appendEntity(std::vector<SceneVertex>& out, const EntityModel& model,
                  const EntityRenderState& state, const glm::dvec3& cameraPos,
                  const glm::vec3& teamColor, float light, float tickRate,
                  EntityPoseScratch& scratch);

}  // namespace finevox::render
