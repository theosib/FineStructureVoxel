#pragma once

/**
 * @file signal_solver.hpp
 * @brief Deterministic solver for networks of signal-carrying blocks
 *
 * A circuit is a graph of nodes, each holding a level from 0 to the solver's maximum:
 *
 * - **Sources** take their level from outside (a switch, a sensor, a script).
 * - **Conductors** are instant: a conductor's level is the strongest of what reaches it,
 *   each link losing its own amount on the way. Conductors joined by links settle
 *   together, as one region.
 * - **Components** compute a level from what they read (invert, compare, latch...) and
 *   output it at least one step later.
 *
 * Because every component has a delay of at least one step, a step is synchronous logic:
 * the conductors settle from the components' outputs as they stand, then every component
 * computes its next output from the settled levels. Nothing depends on the order nodes are
 * visited, and no loop of instant connections can form. A loop through a component is a
 * register and oscillates the same way every run.
 *
 * Edits (adding, removing, linking) and source levels take effect at the next step(). The
 * solver is single-threaded and owns all of its state: it can be stepped inline on the game
 * thread, or a wrapper can hand it batches on another thread, with the same results.
 *
 * See docs/systems/signals.md.
 */

#include <cstdint>
#include <map>
#include <vector>

namespace finevox {

using SignalLevel = uint8_t;
using SignalNodeId = uint32_t;
inline constexpr SignalNodeId NO_SIGNAL_NODE = ~SignalNodeId{0};

/// What a node does with its inputs
enum class SignalOp : uint8_t {
    Source,     ///< Level set from outside with setSource()
    Conductor,  ///< Instant: strongest incoming level less each link's loss
    // Components (delay >= 1). A and B below are the strongest level on each port.
    Buffer,     ///< A
    Invert,     ///< maximum if A is 0, else 0
    Compare,    ///< A if A >= B, else 0
    Subtract,   ///< A - B, not below 0
    Max,        ///< the strongest level on port A
    Min,        ///< the weakest level on port A (0 with nothing connected)
    Parity,     ///< maximum if an odd number of port A's inputs are on, else 0
    Latch,      ///< on (maximum) once A is on, off once B is on; B wins
    Toggle,     ///< flips between 0 and maximum each time A turns on
};

/// Whether an op is a component (has a delay and reads ports)
[[nodiscard]] constexpr bool isSignalComponent(SignalOp op) {
    return op != SignalOp::Source && op != SignalOp::Conductor;
}

/// How a component behaves
struct SignalComponentSpec {
    SignalOp op = SignalOp::Buffer;
    /// Steps from reading inputs to outputting the result. Raised to 1 if lower.
    uint16_t delay = 1;
    /// Once its output turns on, it stays on for at least this many steps
    uint16_t minPulse = 0;
    /// A result above 0 is output at the solver's maximum
    bool fullOutput = false;
};

/// Inputs a component reads. Each port takes any number of nodes, of any kind.
enum class SignalPort : uint8_t {
    A,     ///< main input
    B,     ///< second input (Compare, Subtract, Latch's reset)
    Hold,  ///< while on, the output stays as it is and the component computes nothing
};

/// A node whose level a step changed
struct SignalChange {
    SignalNodeId node;
    SignalLevel level;
};

/// What a component has in flight, for saving. Times count steps from the one about to run.
struct SignalComponentState {
    SignalLevel level = 0;       ///< its output
    SignalLevel lineLevel = 0;   ///< what its delay line puts out
    SignalLevel lastTarget = 0;  ///< the level most recently sent on its way
    SignalLevel prevA = 0;
    bool state = false;          ///< latch or toggle
    uint32_t holdFor = 0;        ///< minPulse: steps it stays on whatever the line says
    std::vector<std::pair<uint32_t, SignalLevel>> pending;  ///< (steps from now, level)

    /// Nothing a component's inputs wouldn't give it again
    [[nodiscard]] bool settled() const {
        return pending.empty() && !state && holdFor == 0 && lineLevel == level && lastTarget == level;
    }
};

/// Work a step did, for tests and profiling
struct SignalStepStats {
    uint32_t regionsSettled = 0;
    uint32_t conductorsSettled = 0;
    uint32_t componentsEvaluated = 0;
    uint32_t componentsFired = 0;
};

class SignalSolver {
public:
    /// maxLevel is the strongest signal; it's the game's choice, there is no default
    explicit SignalSolver(SignalLevel maxLevel);

    [[nodiscard]] SignalLevel maxLevel() const { return maxLevel_; }

    // ---- Building the graph. Ids are reused after removal. ----

    SignalNodeId addSource(SignalLevel level = 0);
    SignalNodeId addConductor();
    SignalNodeId addComponent(const SignalComponentSpec& spec);

    /// Remove a node and every link and port connection it has
    void removeNode(SignalNodeId id);

    /// Drop every link and port connection a node has, both ways, and keep the node with its
    /// state (what's on its way out of a component, a latch's state)
    void isolate(SignalNodeId id);

    /// A component's state, to save; and back, after it's been made again (no change is
    /// reported for the level it comes back with)
    [[nodiscard]] SignalComponentState componentState(SignalNodeId component) const;
    void setComponentState(SignalNodeId component, const SignalComponentState& state);

    /// Start a source or component at a level, as if it had been there all along: no change
    /// is reported for it. For bringing back a circuit whose levels the world still shows.
    void presetLevel(SignalNodeId id, SignalLevel level);

    /// Feed a conductor from any node, losing `loss` on the way. Linking the same pair again
    /// replaces the loss.
    void link(SignalNodeId from, SignalNodeId toConductor, SignalLevel loss);
    void unlink(SignalNodeId from, SignalNodeId toConductor);

    /// Have a component read a node on one of its ports
    void connect(SignalNodeId from, SignalNodeId component, SignalPort port);
    void disconnect(SignalNodeId from, SignalNodeId component, SignalPort port);

    /// Change a component's behaviour (a new delay, a different op). Changes already on their
    /// way out keep their timing.
    void setSpec(SignalNodeId component, const SignalComponentSpec& spec);

    /// Set a source's level for the next step
    void setSource(SignalNodeId source, SignalLevel level);

    // ---- Running ----

    /**
     * Advance one step: apply edits and source levels, output the components' changes that
     * are due, settle the conductors, and let the components read the result.
     * @return every node whose level changed, in id order. Valid until the next step().
     */
    const std::vector<SignalChange>& step();

    [[nodiscard]] SignalLevel level(SignalNodeId id) const;
    /// The strongest level on one of a component's ports
    [[nodiscard]] SignalLevel portLevel(SignalNodeId component, SignalPort port) const;
    /// The components reading a node (one entry per port connection)
    [[nodiscard]] const std::vector<SignalNodeId>& readersOf(SignalNodeId id) const;
    [[nodiscard]] bool exists(SignalNodeId id) const;
    [[nodiscard]] SignalOp op(SignalNodeId id) const;
    /// Steps taken so far
    [[nodiscard]] uint64_t stepCount() const { return step_; }
    [[nodiscard]] size_t nodeCount() const { return nodes_.size() - freeIds_.size(); }
    /// What the last step() did
    [[nodiscard]] const SignalStepStats& lastStats() const { return stats_; }

private:
    struct Link {
        SignalNodeId node;
        SignalLevel loss;
    };
    struct Pending {
        uint64_t due;
        SignalLevel level;
    };
    struct Node {
        bool alive = false;
        SignalOp op = SignalOp::Source;
        SignalLevel level = 0;
        uint32_t generation = 0;  // bumped on removal, so stale wake-ups are ignored

        std::vector<Link> in;    // conductor: what feeds it
        std::vector<Link> out;   // conductors this node feeds
        std::vector<SignalNodeId> readers;  // components reading this node, once per port use

        // Conductor
        uint32_t region = 0;

        // Component
        SignalComponentSpec spec;
        std::vector<SignalNodeId> ports[3];
        std::vector<Pending> pending;  // levels on their way out, in due order
        SignalLevel lastTarget = 0;    // the level most recently sent on its way
        SignalLevel lineLevel = 0;     // what the delay line is putting out
        uint64_t holdUntil = 0;        // minPulse: stays on until this step
        SignalLevel prevA = 0;         // Toggle: A when last evaluated
        bool state = false;            // Latch and Toggle
    };
    struct Region {
        bool alive = false;
        std::vector<SignalNodeId> members;
    };
    struct SourceSet {
        SignalNodeId node;
        uint32_t generation;
        SignalLevel level;
    };

    SignalNodeId allocate(SignalOp op);
    void dropConnections(SignalNodeId id);
    Node& node(SignalNodeId id);
    const Node& node(SignalNodeId id) const;

    void markAffected(SignalNodeId conductor);   // region shape may have changed
    void markRegionDirty(SignalNodeId conductor);
    void markEvaluate(SignalNodeId component);
    void noteLevelChange(SignalNodeId id, SignalLevel oldLevel);
    void wake(SignalNodeId id, uint64_t due);

    void refloodRegions();
    void settleRegion(uint32_t region);
    void fireComponent(SignalNodeId id);
    void evaluateComponent(SignalNodeId id);
    [[nodiscard]] SignalLevel portMax(const Node& n, SignalPort port) const;
    [[nodiscard]] bool held(const Node& n) const;
    void setOutput(SignalNodeId id, SignalLevel level);

    SignalLevel maxLevel_;
    uint64_t step_ = 0;

    std::vector<Node> nodes_;
    std::vector<SignalNodeId> freeIds_;
    std::vector<Region> regions_;
    std::vector<uint32_t> freeRegions_;

    // Work for the next step. Marks are step-stamped, so they never need clearing.
    std::vector<SignalNodeId> affected_;
    std::vector<uint32_t> dirtyRegions_;
    std::vector<SignalNodeId> toEvaluate_;
    std::vector<uint64_t> affectedMark_, floodMark_, evaluateMark_, changedMark_;
    std::vector<uint64_t> regionDirtyMark_;
    std::vector<SignalLevel> levelAtStart_;
    std::vector<SourceSet> sourceSets_;
    std::map<uint64_t, std::vector<std::pair<SignalNodeId, uint32_t>>> wakeups_;

    std::vector<SignalChange> changes_;
    std::vector<SignalNodeId> changedNodes_;
    SignalStepStats stats_;

    // Reused by settleRegion
    std::vector<std::vector<SignalNodeId>> buckets_;
    std::vector<SignalLevel> settled_;
};

}  // namespace finevox
