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
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class AnimationClip;

namespace render {

class EntityRenderer {
public:
    using PlayerCorrectionCallback = std::function<void(
        EntityId id, glm::dvec3 pos, glm::dvec3 vel,
        bool onGround, uint64_t inputSequence, std::string_view reason)>;

    EntityRenderer() = default;

    /// Process entity events from the game thread
    void processEvents(GraphicsEventQueue& queue);

    /// Advance animations, update interpolation state
    void update(float dt, const glm::dvec3& cameraPos);

    /// Set callback invoked when EVT_PLAYER_CORRECTION is received
    void setPlayerCorrectionCallback(PlayerCorrectionCallback callback) {
        playerCorrectionCallback_ = std::move(callback);
    }

    /// Register an AnimationClip for an entity type and numeric animation slot
    void registerAnimationClip(EntityTypeId typeId, uint8_t slotId, const AnimationClip* clip);

    /// Register an AnimationClip for an entity type and named animation state (resolved via EntityTypeRegistry)
    void registerAnimationClip(EntityTypeId typeId, std::string_view stateName, const AnimationClip* clip);

    /// Look up a registered AnimationClip for an entity type and slot
    [[nodiscard]] const AnimationClip* findAnimationClip(EntityTypeId typeId, uint8_t slotId) const;

    /// Get all entities for external rendering integration
    [[nodiscard]] const std::unordered_map<EntityId, EntityRenderState>& entities() const {
        return entities_;
    }

    /// Look up a specific entity's render state (or nullptr if not found)
    [[nodiscard]] const EntityRenderState* getEntity(EntityId id) const {
        auto it = entities_.find(id);
        return it != entities_.end() ? &it->second : nullptr;
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
    std::unordered_map<uint64_t, const AnimationClip*> animationClips_;
    PlayerCorrectionCallback playerCorrectionCallback_;

    static uint64_t makeClipKey(EntityTypeId typeId, uint8_t slotId) {
        return (static_cast<uint64_t>(typeId.id) << 8) | static_cast<uint64_t>(slotId);
    }

    void handleSnapshot(const EntitySnapshot& snap);
    void handleEvent(const finescript::Value& event);
};

}  // namespace render
}  // namespace finevox
