#include <gtest/gtest.h>
#include "finevox/core/block_properties.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/signal_binding.hpp"
#include "finevox/core/world.hpp"

#include <functional>
#include <random>

using namespace finevox;

namespace {

constexpr SignalLevel MAX = 15;

/// Register a block the way content loading does: every state its own type, each with the
/// spec its keys give, faces turned by its own facing
void defineBlock(const std::string& family, const std::vector<BlockProperty>& props, SignalKeys keys,
                 bool opaque, const std::function<void(const std::vector<std::string>&, SignalKeys&)>& perState = {}) {
    const std::string name = "sigt:" + family;
    if (!props.empty()) BlockPropertyRegistry::global().addFamily(family, props);
    std::vector<size_t> at(props.size(), 0);
    while (true) {
        std::vector<std::string> values;
        for (size_t i = 0; i < props.size(); ++i) values.push_back(props[i].values[at[i]]);
        std::string state = props.empty() ? name : BlockPropertyRegistry::global().stateName(name, values);
        BlockType bt;
        bt.setOpaque(opaque);
        if (!opaque) bt.setTransparent(true);
        BlockTypeId id = BlockTypeId::fromName(state);
        BlockRegistry::global().registerOrUpdateType(id, bt);
        SignalKeys k = keys;
        if (perState) perState(values, k);
        auto property = [&](std::string_view p) -> std::optional<std::string> {
            for (size_t i = 0; i < props.size(); ++i) {
                if (props[i].name == p) return values[i];
            }
            return std::nullopt;
        };
        std::string error;
        if (k.count("signal")) {
            auto spec = parseSignalSpec(k, property, &error);
            EXPECT_TRUE(error.empty()) << state << ": " << error;
            if (spec) SignalBlockRegistry::global().set(id, *spec);
        }
        size_t i = props.size();
        while (i > 0 && ++at[i - 1] == props[i - 1].values.size()) at[--i] = 0;
        if (i == 0) break;
    }
}

std::vector<std::string> range(int lo, int hi) {
    std::vector<std::string> v;
    for (int i = lo; i <= hi; ++i) v.push_back(std::to_string(i));
    return v;
}

const std::vector<std::string> JOIN = {"none", "side", "up"};
const std::vector<std::string> BOOL = {"false", "true"};
const std::vector<std::string> FACING = {"north", "east", "south", "west"};

void defineAll() {
    static bool done = false;
    if (done) return;
    done = true;
    defineBlock("stone", {}, {}, true);
    defineBlock("glass", {}, {}, false);
    defineBlock("wire",
                {{"power", range(0, 15)}, {"north", JOIN}, {"east", JOIN}, {"south", JOIN}, {"west", JOIN}},
                {{"signal", "conductor"}, {"signal-group", "wire"}, {"signal-climb", "true"},
                 {"signal-out", "down points"}, {"signal-power:weak", "down points"},
                 {"signal-reads", "strong"}, {"signal-show", "power"},
                 {"signal-joins", "north east south west"}, {"signal-join-values", "none side up"}},
                false);
    defineBlock("lever", {{"powered", BOOL}},
                {{"signal", "source"}, {"signal-out", "all"}, {"signal-power:strong", "down"}}, false,
                [](const std::vector<std::string>& v, SignalKeys& k) { k["signal-level"] = v[0] == "true" ? "15" : "0"; });
    defineBlock("lamp", {{"lit", BOOL}},
                {{"signal", "sink"}, {"signal-in", "all"}, {"signal-show", "lit"}}, true);
    defineBlock("inverter", {{"lit", BOOL}},
                {{"signal", "component"}, {"signal-op", "invert"}, {"signal-delay", "2"},
                 {"signal-in", "down"}, {"signal-out", "sides up"}, {"signal-power:strong", "up"},
                 {"signal-show", "lit"}},
                false);
    defineBlock("delay", {{"facing", FACING}, {"ticks", range(1, 4)}, {"powered", BOOL}},
                {{"signal", "component"}, {"signal-op", "buffer"}, {"signal-full", "true"},
                 {"signal-in", "front"}, {"signal-out", "back"}, {"signal-power:strong", "back"},
                 {"signal-hold", "left right"}, {"signal-hold-from", "component"}, {"signal-show", "powered"}},
                false, [](const std::vector<std::string>& v, SignalKeys& k) { k["signal-delay"] = v[1]; });
}

BlockTypeId block(const std::string& name) { return BlockTypeId::fromName("sigt:" + name); }

/// A state's name, from its family and the properties that differ from the default
std::string state(const std::string& family, std::initializer_list<std::pair<std::string, std::string>> props) {
    std::string name = "sigt:" + family;
    for (const auto& [p, v] : props) name = BlockPropertyRegistry::global().withProperty(name, p, v).value_or(name);
    return name.substr(5);
}

std::string prop(World& w, BlockCoord p, const std::string& name) {
    return BlockPropertyRegistry::global().property(w.getBlock(p).name(), name).value_or("?");
}

struct Rig {
    World world;
    SignalBinding signals{world, MAX};
    Rig() {
        defineAll();
        for (int x = -4; x <= 24; ++x) {
            for (int z = -4; z <= 8; ++z) world.setBlock(BlockCoord(x, 0, z), block("stone"));
        }
        signals.attach();
    }
    void set(int x, int y, int z, const std::string& name) {
        world.setBlock(BlockCoord(x, y, z), name == "air" ? AIR_BLOCK_TYPE : block(name));
    }
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) signals.step();
    }
    SignalLevel at(int x, int y, int z) { return signals.levelAt(BlockCoord(x, y, z)); }
};

}  // namespace

TEST(SignalBinding, ParsesFacesRelativeToFacing) {
    SignalKeys keys{{"signal", "component"}, {"signal-in", "front"}, {"signal-out", "back"},
                    {"signal-side", "left right"}};
    auto east = parseSignalSpec(keys, [](std::string_view p) -> std::optional<std::string> {
        return p == "facing" ? std::optional<std::string>("east") : std::nullopt;
    });
    ASSERT_TRUE(east);
    EXPECT_EQ(east->in.faces, signalFace(Face::PosX));
    EXPECT_EQ(east->out, signalFace(Face::NegX));
    EXPECT_EQ(east->side.faces, signalFace(Face::PosZ) | signalFace(Face::NegZ));

    std::string error;
    auto bad = parseSignalSpec({{"signal", "component"}, {"signal-in", "sideways"}}, {}, &error);
    EXPECT_FALSE(error.empty());
}

TEST(SignalBinding, ALeverPowersAWireThatFades) {
    Rig r;
    r.set(0, 1, 0, "lever[powered=true]");
    for (int x = 1; x <= 18; ++x) r.set(x, 1, 0, "wire");
    r.run(2);
    EXPECT_EQ(r.at(1, 1, 0), 15);
    EXPECT_EQ(r.at(5, 1, 0), 11);
    EXPECT_EQ(r.at(15, 1, 0), 1);
    EXPECT_EQ(r.at(16, 1, 0), 0);
    EXPECT_EQ(prop(r.world, BlockCoord(5, 1, 0), "power"), "11") << "the level is shown";
    EXPECT_EQ(prop(r.world, BlockCoord(5, 1, 0), "east"), "side");
    EXPECT_EQ(prop(r.world, BlockCoord(5, 1, 0), "north"), "none");

    r.set(0, 1, 0, "lever");  // off
    r.run(2);
    EXPECT_EQ(r.at(1, 1, 0), 0);
    EXPECT_EQ(prop(r.world, BlockCoord(5, 1, 0), "power"), "0");
}

TEST(SignalBinding, BreakingAWireCutsWhatsPastIt) {
    Rig r;
    r.set(0, 1, 0, "lever[powered=true]");
    for (int x = 1; x <= 6; ++x) r.set(x, 1, 0, "wire");
    r.run(2);
    ASSERT_EQ(r.at(6, 1, 0), 10);
    r.set(3, 1, 0, "air");
    r.run(2);
    EXPECT_EQ(r.at(2, 1, 0), 14);
    EXPECT_EQ(r.at(4, 1, 0), 0);
}

TEST(SignalBinding, AWireLightsALampItPointsInto) {
    Rig r;
    r.set(0, 1, 0, "lever[powered=true]");
    for (int x = 1; x <= 3; ++x) r.set(x, 1, 0, "wire");
    r.set(4, 1, 0, "lamp");
    r.set(2, 1, 1, "lamp");  // beside the line, not pointed at
    r.run(3);
    EXPECT_EQ(prop(r.world, BlockCoord(4, 1, 0), "lit"), "true");
    EXPECT_EQ(prop(r.world, BlockCoord(2, 1, 1), "lit"), "false");
}

TEST(SignalBinding, AWirePowersTheBlockUnderItWeakly) {
    Rig r;
    // Wire on a raised block; a lamp beside that block lights, a wire beside it doesn't
    r.set(5, 1, 0, "stone");
    r.set(5, 2, 0, "wire");
    r.set(4, 2, 0, "lever[powered=true]");
    r.set(6, 1, 0, "lamp");
    r.set(5, 1, 1, "wire");
    r.set(5, 2, 1, "stone");  // or the side wire would climb up to the raised one
    r.run(3);
    EXPECT_EQ(r.at(5, 2, 0), 15);
    EXPECT_EQ(prop(r.world, BlockCoord(6, 1, 0), "lit"), "true");
    EXPECT_EQ(r.at(5, 1, 1), 0) << "a weakly powered block doesn't feed a wire";
}

TEST(SignalBinding, AnInverterOnAPoweredBlockIsOff) {
    Rig r;
    r.set(3, 1, 0, "stone");
    r.set(3, 2, 0, "inverter");
    r.set(4, 2, 0, "wire");
    r.set(2, 1, 0, "lever");  // beside the block, powering into it strongly... only down here
    r.run(4);
    EXPECT_EQ(r.at(3, 2, 0), 15) << "nothing powers its block: on";
    EXPECT_EQ(r.at(4, 2, 0), 15) << "and it powers the wire beside it";
    EXPECT_EQ(prop(r.world, BlockCoord(3, 2, 0), "lit"), "true");

    // A lever on top of the block below the inverter's block can't be placed here, so power the
    // block from a delay pointing into it instead
    r.set(1, 1, 0, "lever[powered=true]");
    r.set(2, 1, 0, state("delay", {{"facing", "west"}}));  // reads west, outputs east into the block
    r.run(6);
    EXPECT_EQ(r.at(3, 2, 0), 0) << "its block is powered: off";
    EXPECT_EQ(r.at(4, 2, 0), 0);
}

TEST(SignalBinding, WiresClimbUnlessCut) {
    Rig r;
    r.set(0, 1, 0, "lever[powered=true]");
    r.set(1, 1, 0, "wire");
    r.set(2, 1, 0, "stone");
    r.set(2, 2, 0, "wire");  // one up, on the block
    r.run(2);
    EXPECT_EQ(r.at(2, 2, 0), 14);
    EXPECT_EQ(prop(r.world, BlockCoord(1, 1, 0), "east"), "up");
    r.set(1, 2, 0, "stone");  // over the lower wire: cuts the climb
    r.run(2);
    EXPECT_EQ(r.at(2, 2, 0), 0);
    EXPECT_EQ(prop(r.world, BlockCoord(1, 1, 0), "east"), "none");
}

TEST(SignalBinding, ADelayWaitsItsTicksAndChangingThemKeepsItsNode) {
    Rig r;
    r.set(0, 1, 0, "lever");
    r.set(1, 1, 0, state("delay", {{"facing", "west"}, {"ticks", "3"}}));
    r.set(2, 1, 0, "wire");
    r.run(2);
    SignalNodeId before = r.signals.solver().nodeCount();
    r.set(0, 1, 0, "lever[powered=true]");
    r.run(1);  // takes in the lever, steps: the delay reads it
    int ticks = 0;
    while (r.at(2, 1, 0) == 0 && ticks < 10) {
        r.run(1);
        ++ticks;
    }
    EXPECT_EQ(ticks, 3);
    EXPECT_EQ(r.at(2, 1, 0), 15) << "full output";
    r.run(1);  // shown a tick after the step
    EXPECT_EQ(prop(r.world, BlockCoord(1, 1, 0), "powered"), "true");

    r.set(1, 1, 0, state("delay", {{"facing", "west"}, {"ticks", "1"}, {"powered", "true"}}));
    r.run(2);
    EXPECT_EQ(r.signals.solver().nodeCount(), before) << "a new delay is a new spec, not a new node";
}

TEST(SignalBinding, ACircuitInTheWorldBeforeAttachingIsFound) {
    defineAll();
    World world;
    for (int x = 0; x <= 4; ++x) world.setBlock(BlockCoord(x, 0, 0), block("stone"));
    world.setBlock(BlockCoord(0, 1, 0), block("lever[powered=true]"));
    world.setBlock(BlockCoord(1, 1, 0), block("wire"));
    world.setBlock(BlockCoord(2, 1, 0), block("wire"));
    SignalBinding signals(world, MAX);
    signals.attach();
    signals.step();
    signals.step();
    EXPECT_EQ(signals.levelAt(BlockCoord(2, 1, 0)), 14);
}

TEST(SignalBinding, ALoadedDelayShownOnStartsOn) {
    defineAll();
    World world;
    for (int x = 0; x <= 4; ++x) world.setBlock(BlockCoord(x, 0, 0), block("stone"));
    world.setBlock(BlockCoord(0, 1, 0), block("lever[powered=true]"));
    world.setBlock(BlockCoord(1, 1, 0), block(state("delay", {{"facing", "west"}, {"ticks", "4"}, {"powered", "true"}})));
    world.setBlock(BlockCoord(2, 1, 0), block("wire"));
    SignalBinding signals(world, MAX);
    signals.attach();
    signals.step();
    EXPECT_EQ(signals.levelAt(BlockCoord(2, 1, 0)), 15) << "no wait for a delay already on";
}


TEST(SignalBinding, TwoJoinedSinksActAsOne) {
    static bool defined = false;
    if (!defined) {
        defined = true;
        defineAll();
        defineBlock("gate", {{"half", {"lower", "upper"}}, {"open", BOOL}},
                    {{"signal", "sink"}, {"signal-group", "gate"}, {"signal-in", "all"},
                     {"signal-show", "open"}, {"signal-show-edges", "true"}},
                    false, [](const std::vector<std::string>& v, SignalKeys& k) {
                        k["signal-join-faces"] = v[0] == "lower" ? "up" : "down";
                    });
    }
    Rig r;
    r.set(3, 1, 0, "gate");
    r.set(3, 2, 0, state("gate", {{"half", "upper"}, {"open", "true"}}));  // opened by hand
    r.run(2);
    EXPECT_EQ(prop(r.world, BlockCoord(3, 2, 0), "open"), "true") << "found open, stays open";
    r.set(2, 2, 0, "lever[powered=true]");  // beside the upper half only
    r.run(2);
    EXPECT_EQ(r.at(3, 1, 0), 15) << "the lower half takes the upper's level";
    EXPECT_EQ(prop(r.world, BlockCoord(3, 1, 0), "open"), "true");
    r.set(2, 2, 0, "lever");
    r.run(2);
    EXPECT_EQ(prop(r.world, BlockCoord(3, 1, 0), "open"), "false");
    EXPECT_EQ(prop(r.world, BlockCoord(3, 2, 0), "open"), "false");
}

// ============================================================================
// Incremental against from-scratch
// ============================================================================

namespace {

constexpr int RX = 7, RY = 4, RZ = 7;

void copyRegion(World& from, World& to) {
    for (int x = -1; x <= RX; ++x) {
        for (int y = 0; y <= RY; ++y) {
            for (int z = -1; z <= RZ; ++z) to.setBlock(BlockCoord(x, y, z), from.getBlock(BlockCoord(x, y, z)));
        }
    }
}

}  // namespace

class SignalBindingRandom : public ::testing::TestWithParam<uint32_t> {};

// Without components a circuit's levels depend only on its blocks, so a binding that has followed
// every edit must agree with one built afresh from the same blocks: levels, relays, and what's shown.
TEST_P(SignalBindingRandom, IncrementalMatchesFresh) {
    defineAll();
    std::mt19937 rng(GetParam());
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<uint32_t>(n)); };
    World world;
    for (int x = -1; x <= RX; ++x) {
        for (int z = -1; z <= RZ; ++z) world.setBlock(BlockCoord(x, 0, z), block("stone"));
    }
    SignalBinding signals(world, MAX);
    signals.attach();
    const InternedId strong = StringInterner::global().intern("strong");
    const InternedId weak = StringInterner::global().intern("weak");

    for (int round = 0; round < 60; ++round) {
        int edits = 1 + pick(4);
        for (int e = 0; e < edits; ++e) {
            BlockCoord p(pick(RX), 1 + pick(RY - 1), pick(RZ));
            int what = pick(20);
            std::string name;
            if (what < 9) name = "wire";
            else if (what < 12) name = "stone";
            else if (what < 13) name = "glass";
            else if (what < 15) name = pick(2) ? "lever[powered=true]" : "lever";
            else if (what < 16) name = "lamp";
            if (name.empty()) world.setBlock(p, AIR_BLOCK_TYPE);
            else world.setBlock(p, block(name));
        }
        signals.step();
        signals.step();

        World fresh;
        copyRegion(world, fresh);
        SignalBinding scratch(fresh, MAX);
        scratch.attach();
        scratch.step();
        scratch.step();
        for (int x = 0; x < RX; ++x) {
            for (int y = 1; y < RY; ++y) {
                for (int z = 0; z < RZ; ++z) {
                    BlockCoord p(x, y, z);
                    ASSERT_EQ(signals.levelAt(p), scratch.levelAt(p))
                        << "seed " << GetParam() << " round " << round << " at " << x << "," << y << "," << z
                        << " " << world.getBlock(p).name();
                    ASSERT_EQ(signals.relayLevelAt(p, strong), scratch.relayLevelAt(p, strong));
                    ASSERT_EQ(signals.relayLevelAt(p, weak), scratch.relayLevelAt(p, weak));
                }
            }
        }
        // And what they show, once both have shown their last step
        signals.step();
        scratch.step();
        for (int x = 0; x < RX; ++x) {
            for (int y = 1; y < RY; ++y) {
                for (int z = 0; z < RZ; ++z) {
                    BlockCoord p(x, y, z);
                    ASSERT_EQ(world.getBlock(p), fresh.getBlock(p))
                        << "seed " << GetParam() << " round " << round << " at " << x << "," << y << "," << z
                        << ": " << world.getBlock(p).name() << " vs " << fresh.getBlock(p).name();
                }
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Seeds, SignalBindingRandom, ::testing::Range(1u, 21u));
