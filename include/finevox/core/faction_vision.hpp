#pragma once

/**
 * @file faction_vision.hpp
 * @brief Per-faction fog of war with real line of sight
 *
 * The map is divided into square vision cells. Each cell is, per faction, unknown
 * (never seen), explored (seen once), or visible (seen now). Sight comes from a
 * faction's entities and from fixed sources such as towers, tested against the
 * actual blocks: a hill or a wall blocks view, and height sees further.
 *
 * This is server-side truth. Clients and AI players are told what a faction can
 * see, never the reverse, so neither can peek. Game thread only, except where
 * noted.
 *
 * Sight is a horizon sweep: rays march out from the eye over a per-column height
 * grid, tracking the steepest angle so far. Anything below that horizon is hidden.
 * That is a few thousand array reads per unit rather than a block-by-block ray to
 * every cell, which is what keeps it affordable with armies on the map. The grid
 * refreshes a few chunks per tick, only where blocks actually changed.
 */

#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/position.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class World;
class EntityManager;

enum class VisionState : uint8_t {
    Unknown = 0,
    Explored = 1,
    Visible = 2,
};

struct VisionBounds {
    int32_t minX = 0;
    int32_t minZ = 0;
    int32_t maxX = 0;   ///< Exclusive
    int32_t maxZ = 0;   ///< Exclusive
};

/// What of a FactionVision is worth keeping. Current sight comes back by itself.
struct FactionVisionState {
    struct Lookout {
        int64_t handle;
        uint16_t faction;
        glm::dvec3 eye;
        float radius;
    };
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> factions;  ///< Each faction's cells
    std::vector<Lookout> lookouts;
    int64_t nextHandle = 1;
};

class FactionVision : public GameSubsystem {
public:
    static constexpr int32_t CELL_SIZE = 4;

    FactionVision(World& world, EntityManager& entities, VisionBounds bounds);

    [[nodiscard]] std::string_view name() const override { return "FactionVision"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PostTick; }
    [[nodiscard]] int32_t priority() const override { return 50; }
    void tick(float dt) override;

    /// Track a faction. Harmless if already tracked.
    void addFaction(uint16_t faction);

    /// A fixed lookout, such as a tower. Returns a handle for removeSource.
    int64_t addSource(uint16_t faction, const glm::dvec3& eye, float radius);
    void removeSource(int64_t handle);

    /// Reveal an area without line of sight (scripted events, map setup)
    void reveal(uint16_t faction, const glm::dvec3& center, float radius);

    /// Recompute a faction's sight now instead of waiting for its turn. Exact: unlike
    /// the periodic update, cells out of sight lapse at once.
    void refresh(uint16_t faction);

    [[nodiscard]] FactionVisionState exportState() const;
    void importState(const FactionVisionState& state);

    /// Default sight radius for entities with no `vision` property
    void setDefaultRadius(float radius) { defaultRadius_ = radius; }

    [[nodiscard]] VisionState stateAt(uint16_t faction, int32_t x, int32_t z) const;
    [[nodiscard]] bool isVisible(uint16_t faction, int32_t x, int32_t z) const {
        return stateAt(faction, x, z) == VisionState::Visible;
    }

    /// Cells as one byte each, row-major in Z then X. Thread-safe copy for a renderer.
    [[nodiscard]] std::vector<uint8_t> snapshot(uint16_t faction) const;

    [[nodiscard]] int32_t cellsX() const { return cellsX_; }
    [[nodiscard]] int32_t cellsZ() const { return cellsZ_; }
    [[nodiscard]] const VisionBounds& bounds() const { return bounds_; }

    /// Count of cells at or above a state (for tests and map stats)
    [[nodiscard]] size_t countCells(uint16_t faction, VisionState atLeast) const;

    /// True if a straight sight line between two points is clear of solid blocks
    [[nodiscard]] bool hasLineOfSight(const glm::dvec3& from, const glm::dvec3& to) const;

private:
    struct Source {
        uint16_t faction;
        glm::dvec3 eye;
        float radius;
    };

    /// Top of what blocks sight in a column, and of see-through growth above it
    struct ColumnHeight {
        int16_t solid = -1;
        int16_t foliage = -1;
    };

    [[nodiscard]] int32_t cellIndex(int32_t x, int32_t z) const;
    [[nodiscard]] int32_t columnIndex(int32_t x, int32_t z) const;
    void updateFaction(uint16_t faction, std::vector<uint8_t>& cells, bool linger);
    void castFrom(const glm::dvec3& eye, float radius, std::vector<uint8_t>& cells);

    void ensureHeights();
    void refreshChunkHeights(int32_t chunkX, int32_t chunkZ);
    void refreshSomeHeights();

    World& world_;
    EntityManager& entities_;
    VisionBounds bounds_;
    int32_t cellsX_ = 0;
    int32_t cellsZ_ = 0;
    float defaultRadius_ = 24.0f;

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint16_t, std::vector<uint8_t>> factions_;
    /// Updates each visible cell has left before it lapses to Explored
    std::unordered_map<uint16_t, std::vector<uint8_t>> linger_;
    std::unordered_map<int64_t, Source> sources_;
    std::vector<uint16_t> order_;
    std::vector<ColumnHeight> heights_;
    std::vector<uint64_t> chunkVersions_;   ///< Sum of block versions per chunk column
    int32_t chunksX_ = 0;
    int32_t chunksZ_ = 0;
    int32_t heightCursor_ = 0;
    int64_t nextSource_ = 1;
    size_t turn_ = 0;
    uint32_t ticks_ = 0;
};

}  // namespace finevox
