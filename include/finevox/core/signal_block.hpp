#pragma once

/**
 * @file signal_block.hpp
 * @brief What a block is to the signal solver, as its .model declares it
 *
 * A block state's `signal*` keys say what it is in a circuit: a source, a conductor, a
 * component or a sink, which of its faces read and drive, and which properties show its
 * level. Each state is its own block type, so each has its own spec, with faces relative to
 * the block's facing already turned into world directions.
 *
 * See docs/systems/signals.md for the keys.
 */

#include "finevox/core/position.hpp"
#include "finevox/core/signal_solver.hpp"
#include "finevox/core/string_interner.hpp"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace finevox {

enum class SignalRole : uint8_t {
    None,
    Source,     ///< puts out its level (from its state, or a script)
    Conductor,  ///< joins others of its group; instant
    Component,  ///< computes from its ports, outputs a step or more later
    Sink,       ///< takes the strongest level it reads, and shows it
};

/// A set of faces: a bit per Face, plus SIGNAL_POINTS
using SignalFaces = uint8_t;
inline constexpr SignalFaces SIGNAL_POINTS = 1u << 6;  ///< conductor: the way its joins point
inline constexpr SignalFaces SIGNAL_ALL_FACES = 0x3F;

[[nodiscard]] constexpr SignalFaces signalFace(Face f) { return static_cast<SignalFaces>(1u << static_cast<uint8_t>(f)); }
[[nodiscard]] constexpr bool hasSignalFace(SignalFaces faces, Face f) { return (faces & signalFace(f)) != 0; }

/// Which kinds of neighbour a port reads
using SignalFrom = uint8_t;
inline constexpr SignalFrom SIGNAL_FROM_CONDUCTOR = 1;
inline constexpr SignalFrom SIGNAL_FROM_SOURCE = 2;
inline constexpr SignalFrom SIGNAL_FROM_COMPONENT = 4;
inline constexpr SignalFrom SIGNAL_FROM_RELAY = 8;
inline constexpr SignalFrom SIGNAL_FROM_ALL = 15;

struct SignalPortSpec {
    SignalFaces faces = 0;
    SignalFrom from = SIGNAL_FROM_ALL;
    bool operator==(const SignalPortSpec&) const = default;
};

struct SignalBlockSpec {
    SignalRole role = SignalRole::None;

    // Conductor
    InternedId group = 0;
    SignalLevel loss = 1;
    bool climb = false;        ///< also joins its group one block up or down beside it

    // Source
    SignalLevel level = 0;

    // Component (op, delay...) and the ports. A sink reads `in`.
    SignalComponentSpec component;
    SignalPortSpec in, side, hold;

    /// Faces it drives: conductors, components' ports and sinks there read it
    SignalFaces out = 0;
    /// Faces whose relay block it powers, by relay class
    std::vector<std::pair<InternedId, SignalFaces>> power;
    /// Relay classes it reads from relay blocks; empty means all of them
    std::vector<InternedId> reads;

    // Showing
    std::string show;                      ///< property that shows its level
    std::array<std::string, 4> joins;      ///< properties showing its joins, north east south west
    std::array<std::string, 3> joinValues; ///< not joined, joined, joined one block up

    [[nodiscard]] bool readsClass(InternedId cls) const;
    [[nodiscard]] SignalFaces powers(InternedId cls) const;

    /// Same place in a circuit: changing between two such states needs no new links, only a
    /// new level or component spec
    [[nodiscard]] bool sameShape(const SignalBlockSpec& o) const;
};

/// One state's `signal*` keys: "key" or "key:suffix" to its value (lists space-separated)
using SignalKeys = std::map<std::string, std::string>;

/**
 * Build a spec from a state's keys. `property` gives the state's property values, for faces
 * relative to its facing (`front back left right`, `signal-facing` naming the property,
 * `facing` by default). Returns nothing for a state with no `signal` key, and sets `error`
 * for one it can't read.
 */
[[nodiscard]] std::optional<SignalBlockSpec> parseSignalSpec(
    const SignalKeys& keys,
    const std::function<std::optional<std::string>(std::string_view)>& property,
    std::string* error = nullptr);

/// Signal specs by block type. Written while content loads, read on the game thread.
class SignalBlockRegistry {
public:
    static SignalBlockRegistry& global();

    void set(BlockTypeId type, SignalBlockSpec spec);
    [[nodiscard]] const SignalBlockSpec* get(BlockTypeId type) const;
    /// Relay classes any block powers or reads, in a fixed order
    [[nodiscard]] const std::vector<InternedId>& relayClasses() const { return relayClasses_; }
    [[nodiscard]] size_t size() const { return specs_.size(); }
    void clear();

private:
    std::unordered_map<InternedId, SignalBlockSpec> specs_;
    std::vector<InternedId> relayClasses_;
};

}  // namespace finevox
