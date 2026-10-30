#pragma once

/**
 * @file world_replica.hpp
 * @brief A viewer's copy of the world, holding only what that viewer has seen
 *
 * A client that never receives hidden terrain can't reveal it, by hack or by bug.
 * The authoritative World stays on the server side. WorldReplica keeps a second
 * World, and copies a subchunk across only while its faction can see some of that
 * column. Change it while nobody's looking and the copy stays stale, which is
 * exactly "remembered terrain": what you saw, not what's there.
 *
 * Copying is by subchunk and uses block versions, so unchanged terrain costs one
 * integer compare. Game thread.
 */

#include "finevox/core/faction_vision.hpp"
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/world.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace finevox {

class WorldReplica : public GameSubsystem {
public:
    WorldReplica(World& source, FactionVision& vision, uint16_t viewer);

    [[nodiscard]] std::string_view name() const override { return "WorldReplica"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::LateTick; }
    void tick(float dt) override;

    /// The viewer's world. Hand this to the renderer.
    [[nodiscard]] World& world() { return replica_; }

    /// Change who this replica shows, for spectating or after a match
    void setViewer(uint16_t viewer) { viewer_ = viewer; }

    /// Copy every column the viewer currently sees
    void syncAll();

    /// Stop filtering: everything replicates, unseen or not
    void setRevealAll(bool reveal) { revealAll_ = reveal; }

    [[nodiscard]] size_t columnsKnown() const { return versions_.size(); }

private:
    void syncColumn(ColumnPos pos);
    [[nodiscard]] bool columnVisible(ColumnPos pos) const;

    World& source_;
    FactionVision& vision_;
    uint16_t viewer_;
    World replica_;
    bool revealAll_ = false;

    /// Last copied block version per subchunk
    std::unordered_map<uint64_t, uint64_t> versions_;
    /// Columns walked per tick, so a big map never lands on one frame
    int32_t cursorX_ = 0;
    int32_t cursorZ_ = 0;
};

}  // namespace finevox
