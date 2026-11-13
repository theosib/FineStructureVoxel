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
#include <cstdint>
#include <vector>

namespace finevox::render {

struct SceneVertex {
    glm::vec3 position;
    glm::vec3 normal;   ///< Zero = unlit
    glm::vec4 color;
    glm::vec2 uv{-1.0f, -1.0f};  ///< In the entity atlas, multiplying color; negative = none
};

/// Axis-aligned box. `lit` false gives a flat, unlit marker. `faces` has a bit per side, in
/// the order of finevox::Face, so stacked boxes can leave out the sides they share.
void appendBox(std::vector<SceneVertex>& out, const glm::vec3& center, const glm::vec3& halfExtents,
               const glm::vec4& color, bool lit = true, uint8_t faces = 0x3F);

/// A layer of cloud from a picture: each pixel that isn't clear is a cell of cloud, the picture
/// repeating across the sky
struct CloudMask {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> solid;  ///< width*height, row by row
    [[nodiscard]] bool at(int64_t x, int64_t z) const {
        if (width <= 0 || height <= 0) return false;
        int64_t i = ((x % width) + width) % width;
        int64_t j = ((z % height) + height) % height;
        return solid[static_cast<size_t>(j * width + i)] != 0;
    }
};

/// The cells of cloud within `radius` cells of the camera, flat at `height`, `cell` blocks across,
/// drifted by `drift` blocks along +X; a row's neighbouring cells merge into one quad
void appendCloudLayer(std::vector<SceneVertex>& out, const CloudMask& mask, const glm::dvec3& cameraPos,
                      double height, double cell, double drift, int radius, const glm::vec4& color);

/// Flat ring on the ground, facing up
void appendRing(std::vector<SceneVertex>& out, const glm::vec3& center, float radius, float width,
                const glm::vec4& color, int segments = 28);

/// Camera-facing quad, `center` at its middle
void appendBillboard(std::vector<SceneVertex>& out, const glm::vec3& center, const glm::vec3& right,
                     const glm::vec3& up, float halfWidth, float halfHeight, const glm::vec4& color);

/// The stages of dying, as fractions of EntityRenderState::dying: the body falls, lies still
/// a moment while it pales, then shrinks away as it turns to its burst of particles.
inline constexpr float DEATH_FALL_END = 0.4f;
inline constexpr float DEATH_REST = 0.25f;

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
