#pragma once

/**
 * @file entity_renderer.hpp
 * @brief Graphics-thread entity renderer
 *
 * Processes entity events from the game thread (via GraphicsEventQueue),
 * maintains per-entity render state with interpolation and animation,
 * and generates vertex data for rendering.
 */

#include "finevox/core/entity_render_state.hpp"
#include "finevox/core/entity_mesh.hpp"
#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include <unordered_map>
#include <vector>

namespace finevox {

namespace render {

class EntityRenderer {
public:
    EntityRenderer() = default;

    /// Process entity events from the game thread
    void processEvents(GraphicsEventQueue& queue);

    /// Advance animations, update interpolation state
    void update(float dt, const glm::dvec3& cameraPos);

    /// Get all entities for external rendering integration
    [[nodiscard]] const std::unordered_map<EntityId, EntityRenderState>& entities() const {
        return entities_;
    }

    /// Build vertex data for a specific entity given its mesh and skeleton poses
    void buildEntityVertices(EntityId id,
                             const EntityMesh& mesh,
                             const std::vector<glm::mat4>& worldPoses,
                             std::vector<EntityVertex>& vertices,
                             std::vector<uint32_t>& indices) const;

    // Stats
    [[nodiscard]] size_t entityCount() const { return entities_.size(); }
    [[nodiscard]] size_t visibleCount() const;

    /// Clear all entities
    void clear();

private:
    std::unordered_map<EntityId, EntityRenderState> entities_;

    void handleSnapshot(const EntitySnapshot& snap);
    void handleEvent(const finescript::Value& event);
};

}  // namespace render
}  // namespace finevox
