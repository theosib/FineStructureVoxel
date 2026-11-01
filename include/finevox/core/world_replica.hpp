#pragma once

/**
 * @file world_replica.hpp
 * @brief A faction's memory of the world, holding only what it has seen
 *
 * A client that never receives hidden terrain can't reveal it, by hack or by bug.
 * The authoritative World stays on the server side. WorldReplica keeps a second
 * World, and copies a subchunk across only while its faction can see some of that
 * column. Change it while nobody's looking and the copy stays stale, which is
 * exactly "remembered terrain": what you saw, not what's there.
 *
 * It lives on the server, so a player joining late or coming back gets the faction's
 * memory. Clients are sent subchunks from here, never from the real world.
 *
 * Copying is by subchunk and uses block and light versions, so unchanged terrain
 * costs an integer compare or two. Game thread.
 */

#include "finevox/core/faction_vision.hpp"
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/world.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <unordered_map>

namespace finevox {

class WorldReplica : public GameSubsystem {
public:
    WorldReplica(World& source, FactionVision& vision, uint16_t viewer);

    [[nodiscard]] std::string_view name() const override { return "WorldReplica"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::LateTick; }
    void tick(float dt) override;

    /// What the faction remembers. Clients are sent subchunks from here.
    [[nodiscard]] World& world() { return replica_; }

    [[nodiscard]] uint16_t viewer() const { return viewer_; }

    /// Change who this replica shows, for spectating or after a match
    void setViewer(uint16_t viewer) { viewer_ = viewer; }

    /// Copy every column the viewer currently sees
    void syncAll();

    /// Stop filtering: everything replicates, unseen or not
    void setRevealAll(bool reveal) { revealAll_ = reveal; }
    [[nodiscard]] bool revealAll() const { return revealAll_; }

    /// A client is about to change this column, or thinks so. Look again when the command has
    /// had time to run, not when the sweep comes round. Safe from any thread.
    void expectChange(BlockCoord pos);

    /// Subchunks that changed since last asked, for whoever's sending them on
    [[nodiscard]] std::vector<ChunkPos> takeChanges();

    /// Bring the light in every remembered subchunk up to date. Saves keep blocks and not light,
    /// and terrain that's out of sight would otherwise come back black.
    void refreshLight();

    [[nodiscard]] size_t columnsKnown() const { return versions_.size(); }

private:
    void syncColumn(ColumnPos pos);
    [[nodiscard]] bool columnVisible(ColumnPos pos) const;

    World& source_;
    FactionVision& vision_;
    uint16_t viewer_;
    World replica_;
    bool revealAll_ = false;

    /// Look at what's due
    void settleExpected();
    void noteChanged(ChunkPos pos);

    /// Copy from the server only once a tick has passed for it to see the command
    struct Expected {
        BlockCoord pos;
        uint64_t dueTick;
    };
    std::mutex expectedMutex_;
    std::vector<Expected> expected_;
    uint64_t tick_ = 0;

    /// Last copied versions per subchunk
    struct Copied {
        uint64_t blocks = 0;
        uint64_t light = 0;
        uint64_t fluid = 0;
    };
    std::unordered_map<uint64_t, Copied> versions_;

    std::vector<ChunkPos> changes_;
    std::unordered_set<uint64_t> changesNoted_;

    /// Columns walked per tick, so a big map never lands on one frame
    int32_t cursorX_ = 0;
    int32_t cursorZ_ = 0;
};

}  // namespace finevox
