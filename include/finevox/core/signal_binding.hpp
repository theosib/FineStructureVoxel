#pragma once

/**
 * @file signal_binding.hpp
 * @brief Blocks as a circuit: turns signal blocks into SignalSolver nodes, and levels back
 * into block states
 *
 * Blocks say what they are with `signal*` keys (SignalBlockRegistry). The binding keeps a
 * node for each signal block, and for each relay block next to one, links them by the rules
 * in docs/systems/signals.md, and steps the solver once a tick. It learns of block changes
 * from World (BlockObserver), so it needs nothing from whoever changed the block.
 *
 * Each tick, in order: the levels from the last step are shown (written as block states),
 * the block changes since then are taken in, and the solver steps. So a change shows one
 * tick after the step that made it, the same as if the solver ran on its own thread.
 *
 * A change between two states of the same shape (a level shown, a delay changed) changes
 * the node's level or spec and keeps its links. Anything else rebuilds the nodes in the
 * block's neighbourhood.
 *
 * Game thread.
 */

#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/signal_block.hpp"
#include "finevox/core/signal_solver.hpp"
#include "finevox/core/world.hpp"

#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace finevox {

class SignalBinding : public GameSubsystem {
public:
    SignalBinding(World& world, SignalLevel maxLevel);
    ~SignalBinding() override;

    [[nodiscard]] std::string_view name() const override { return "Signals"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::Tick; }
    [[nodiscard]] int32_t priority() const override { return 50; }
    void onAttach(GameSession& session, EventBus& eventBus) override;
    void onDetach() override;
    void tick(float dt) override;

    /// Start watching the world, and take in every signal block already in it. onAttach does
    /// this; call it directly when there's no session.
    void attach();
    /// Stop watching the world (onDetach does this). The world must still be there.
    void detach();
    /// One tick's work: show, take in changes, step
    void step();

    /// Opaque full cubes without a signal role carry signals between their neighbours
    /// (on by default)
    void setOpaqueRelays(bool relays) { opaqueRelays_ = relays; }

    /// The level of the block's own node (0 for a block that isn't in a circuit)
    [[nodiscard]] SignalLevel levelAt(BlockCoord pos) const;
    /// The level of a relay block's node for one class
    [[nodiscard]] SignalLevel relayLevelAt(BlockCoord pos, InternedId cls) const;
    /// Set a source block's level until its block changes (for sources driven by scripts)
    void setSourceLevel(BlockCoord pos, SignalLevel level);

    [[nodiscard]] SignalSolver& solver() { return solver_; }
    [[nodiscard]] const SignalSolver& solver() const { return solver_; }
    [[nodiscard]] size_t blockCount() const { return entries_.size(); }

private:
    struct Inbox : BlockObserver {
        std::mutex mutex;
        std::vector<BlockCoord> blocks;
        std::vector<ColumnPos> added, removed;
        void blockChanged(BlockCoord pos) override;
        void columnAdded(ColumnPos pos) override;
        void columnRemoved(ColumnPos pos) override;
    };

    struct Entry {
        BlockTypeId type;
        const SignalBlockSpec* spec = nullptr;  // null for a relay
        SignalNodeId node = NO_SIGNAL_NODE;
        std::vector<SignalNodeId> relays;  // by relay class index; NO_SIGNAL_NODE where none
        uint8_t joined = 0;   // conductor: bit per horizontal direction (n e s w)
        uint8_t joinedUp = 0; // conductor: joined one block up
        SignalFaces points = 0;
    };

    struct CoordLess {
        bool operator()(const BlockCoord& a, const BlockCoord& b) const {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        }
    };
    using CoordSet = std::set<BlockCoord, CoordLess>;

    void showResults();
    void takeChanges();
    void scanColumn(ColumnPos pos, CoordSet& changed);
    void dropColumn(ColumnPos pos);
    void consider(BlockCoord pos, CoordSet& refresh);
    void refresh(const CoordSet& positions);
    bool rebuildEntry(BlockCoord pos);
    void removeEntry(BlockCoord pos);
    void computeJoins(BlockCoord pos, Entry& e);
    void linkFrom(BlockCoord a, BlockCoord b);
    void linkClimb(BlockCoord a, BlockCoord b);
    void applyParams(Entry& e, const SignalBlockSpec* before);
    void showLevel(BlockCoord pos, const Entry& e);
    void showJoins(BlockCoord pos, const Entry& e);

    [[nodiscard]] const SignalBlockSpec* specOf(BlockTypeId type) const;
    [[nodiscard]] bool isRelayBlock(BlockTypeId type) const;
    [[nodiscard]] bool opaqueAt(BlockCoord pos) const;
    [[nodiscard]] SignalFaces effectiveOut(const Entry& e, SignalFaces faces) const;
    [[nodiscard]] Entry* entryAt(BlockCoord pos);
    [[nodiscard]] const Entry* entryAt(BlockCoord pos) const;

    /// Write one property of the block at pos, if it has it and it differs
    void setProperty(BlockCoord pos, const std::string& prop, const std::string& value);
    void writeBlock(BlockCoord pos, BlockTypeId type);

    World& world_;
    SignalSolver solver_;
    std::shared_ptr<Inbox> inbox_;
    bool attached_ = false;
    bool opaqueRelays_ = true;

    std::unordered_map<BlockCoord, Entry> entries_;
    std::unordered_map<SignalNodeId, BlockCoord> nodeAt_;  // blocks' own nodes
    std::vector<SignalChange> results_;                    // from the last step, to show
    CoordSet newlyMade_;                                   // show these whatever the results
    std::unordered_map<BlockCoord, BlockTypeId> ownWrites_;  // shown states on their way back

    // (type id, property, value) -> type id, and property values by family
    std::unordered_map<std::string, BlockTypeId> withCache_;
};

}  // namespace finevox
