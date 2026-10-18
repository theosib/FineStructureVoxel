#include <gtest/gtest.h>
#include "finevox/core/fluid_interaction.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_loader.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/fluid_simulator.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/string_interner.hpp"

using namespace finevox;

// ============================================================================
// Helpers
// ============================================================================

static FluidTypeId ensureFluid(const std::string& name) {
    FluidTypeId fid = FluidTypeId::fromName(name);
    if (!FluidRegistry::global().getType(fid)) {
        FluidType ft;
        ft.name = name;
        ft.id = fid;
        FluidRegistry::global().registerType(name, ft);
    }
    return fid;
}

// ============================================================================
// Registry Tests
// ============================================================================

TEST(FluidInteractions, SymmetricLookup) {
    FluidInteractionRegistry::global().clear();

    FluidTypeId a = ensureFluid("interact_a");
    FluidTypeId b = ensureFluid("interact_b");

    FluidInteraction interaction;
    interaction.fluidA = a;
    interaction.fluidB = b;
    interaction.resultBlock = BlockTypeId::fromName("test_result_block");
    interaction.consumeA = true;
    interaction.consumeB = true;

    EXPECT_TRUE(FluidInteractionRegistry::global().registerInteraction(interaction));

    // Forward lookup
    const auto* found1 = FluidInteractionRegistry::global().getInteraction(a, b);
    ASSERT_NE(found1, nullptr);
    EXPECT_EQ(found1->resultBlock, BlockTypeId::fromName("test_result_block"));

    // Reverse lookup (symmetric)
    const auto* found2 = FluidInteractionRegistry::global().getInteraction(b, a);
    ASSERT_NE(found2, nullptr);
    EXPECT_EQ(found2->resultBlock, BlockTypeId::fromName("test_result_block"));
}

TEST(FluidInteractions, NoInteractionReturnsNull) {
    FluidInteractionRegistry::global().clear();

    FluidTypeId a = ensureFluid("no_interact_a");
    FluidTypeId b = ensureFluid("no_interact_b");

    EXPECT_EQ(FluidInteractionRegistry::global().getInteraction(a, b), nullptr);
    EXPECT_FALSE(FluidInteractionRegistry::global().hasInteraction(a, b));
}

TEST(FluidInteractions, DuplicateRegistrationFails) {
    FluidInteractionRegistry::global().clear();

    FluidTypeId a = ensureFluid("dup_a");
    FluidTypeId b = ensureFluid("dup_b");

    FluidInteraction interaction;
    interaction.fluidA = a;
    interaction.fluidB = b;
    interaction.resultBlock = BlockTypeId::fromName("dup_result");

    EXPECT_TRUE(FluidInteractionRegistry::global().registerInteraction(interaction));
    EXPECT_FALSE(FluidInteractionRegistry::global().registerInteraction(interaction));
}

TEST(FluidInteractions, BothFluidsConsumed) {
    FluidInteractionRegistry::global().clear();

    FluidTypeId a = ensureFluid("consume_a");
    FluidTypeId b = ensureFluid("consume_b");

    FluidInteraction interaction;
    interaction.fluidA = a;
    interaction.fluidB = b;
    interaction.resultBlock = BlockTypeId::fromName("consume_result");
    interaction.consumeA = true;
    interaction.consumeB = true;

    EXPECT_TRUE(FluidInteractionRegistry::global().registerInteraction(interaction));

    const auto* found = FluidInteractionRegistry::global().getInteraction(a, b);
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->consumeA);
    EXPECT_TRUE(found->consumeB);
}

// ============================================================================
// Config Loading Tests
// ============================================================================

TEST(FluidInteractions, LoadInteractionsFromConfig) {
    FluidInteractionRegistry::global().clear();

    FluidTypeId alpha = ensureFluid("cfg_alpha");
    FluidTypeId beta = ensureFluid("cfg_beta");

    std::string content = R"(
name: cfg_alpha
density: 1000.0
interaction:cfg_beta: some_rock
)";

    size_t count = FluidLoader::loadInteractions(content, "cfg_alpha");
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(FluidInteractionRegistry::global().hasInteraction(alpha, beta));

    const auto* found = FluidInteractionRegistry::global().getInteraction(alpha, beta);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->resultBlock, BlockTypeId::fromName("some_rock"));
}

TEST(FluidInteractions, LoadInteractionsSkipsDuplicate) {
    FluidInteractionRegistry::global().clear();

    ensureFluid("skip_a");
    ensureFluid("skip_b");

    std::string content = R"(
name: skip_a
interaction:skip_b: rock1
)";

    size_t count1 = FluidLoader::loadInteractions(content, "skip_a");
    EXPECT_EQ(count1, 1);

    // Loading from the other side should skip (already registered)
    std::string content2 = R"(
name: skip_b
interaction:skip_a: rock2
)";
    size_t count2 = FluidLoader::loadInteractions(content2, "skip_b");
    EXPECT_EQ(count2, 0);
}

TEST(FluidInteractions, SolidifyingLavaEnqueuesLightingUpdate) {
    FluidInteractionRegistry::global().clear();

    FluidType waterFt;
    waterFt.name = "r0_interact_water";
    waterFt.flowSpeed = 1;
    FluidRegistry::global().registerType("r0_interact_water", waterFt);
    FluidTypeId waterId = FluidTypeId::fromName("r0_interact_water");

    FluidType lavaFt;
    lavaFt.name = "r0_interact_lava";
    lavaFt.lightEmission = 15;
    lavaFt.flowSpeed = 1;
    FluidRegistry::global().registerType("r0_interact_lava", lavaFt);
    FluidTypeId lavaId = FluidTypeId::fromName("r0_interact_lava");

    BlockType cobbleBt;
    cobbleBt.setShape(CollisionShape::FULL_BLOCK).setOpaque(true).setLightAttenuation(15).setLightEmission(0);
    BlockTypeId cobbleId = BlockTypeId::fromName("r0_interact:cobble");
    BlockRegistry::global().registerType(cobbleId, cobbleBt);

    FluidInteraction interaction;
    interaction.fluidA = waterId;
    interaction.fluidB = lavaId;
    interaction.resultBlock = cobbleId;
    interaction.consumeA = false;
    interaction.consumeB = true;
    FluidInteractionRegistry::global().registerInteraction(interaction);

    World world;
    LightEngine lightEngine(world);
    world.setLightEngine(&lightEngine);

    FluidSimulator sim(world);
    sim.setLightEngine(&lightEngine);

    // Place lava at (5, 5, 5) on a solid block at (5, 4, 5), and water above at (5, 6, 5)
    world.setBlock({5, 4, 5}, cobbleId);
    world.setFluid({5, 5, 5}, lavaId, FLUID_SOURCE_LEVEL);
    world.setFluid({5, 6, 5}, waterId, FLUID_SOURCE_LEVEL);
    sim.notifyFluidChanged({5, 6, 5});

    // Simulate tick: water at (5, 6, 5) flows down into lava at (5, 5, 5) and solidifies it
    sim.simulateTick();

    EXPECT_EQ(world.getBlock({5, 5, 5}), cobbleId);
    EXPECT_FALSE(world.hasFluid({5, 5, 5}));

    // Verify a LightingUpdate was enqueued for (5, 5, 5) with oldFluid = lavaId and newType = cobbleId
    auto updates = lightEngine.queue().tryDequeueBatch(16);
    bool foundInteractionUpdate = false;
    for (const auto& u : updates) {
        if (u.pos == BlockCoord{5, 5, 5} && u.newType == cobbleId && u.oldFluid == lavaId) {
            foundInteractionUpdate = true;
            EXPECT_TRUE(u.triggerMeshRebuild);
        }
    }
    EXPECT_TRUE(foundInteractionUpdate);
}

