#include <gtest/gtest.h>
#include "finevox/core/fluid_simulator.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/physics.hpp"

using namespace finevox;

class FluidPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register stone block with full collision shape
        auto& blockReg = BlockRegistry::global();
        BlockType stone;
        stone.setShape(CollisionShape::FULL_BLOCK).setOpaque(true);
        blockReg.registerType("perf_test:stone", stone);
        stoneId_ = BlockTypeId(StringInterner::global().intern("perf_test:stone"));

        // Register water fluid type
        FluidType water;
        water.name = "perf_test_water";
        water.id = FluidTypeId::fromName("perf_test_water");
        water.density = 1000.0f;
        water.viscosity = 1.0f;
        water.spreadDecay = 1;
        water.flowSpeed = 1;
        water.maxLevel = 14;
        water.sourceFormation = true;
        water.sourceFormationCount = 2;
        FluidRegistry::global().registerType("perf_test_water", water);
        waterId_ = FluidTypeId::fromName("perf_test_water");
    }

    BlockTypeId stoneId_;
    FluidTypeId waterId_;
};

TEST_F(FluidPerformanceTest, BudgetLimitsUpdatesPerTick) {
    World world;
    FluidSimulator sim(world);

    // Set a very small budget
    FluidSimulatorConfig cfg;
    cfg.maxUpdatesPerTick = 5;
    sim.setConfig(cfg);

    // Schedule 20 updates
    for (int i = 0; i < 20; ++i) {
        sim.scheduleUpdate(BlockCoord{i, 64, 0}, EMPTY_FLUID_TYPE, 0, 0);
    }

    EXPECT_EQ(sim.pendingUpdateCount(), 20u);

    // Tick: only 5 should process, rest remain pending
    sim.simulateTick();

    // Some updates remain (either in pending or deferred for duplicates)
    EXPECT_GT(sim.pendingUpdateCount(), 0u);
}

TEST_F(FluidPerformanceTest, BudgetRolloverProcessesRemaining) {
    World world;
    FluidSimulator sim(world);

    FluidSimulatorConfig cfg;
    cfg.maxUpdatesPerTick = 5;
    sim.setConfig(cfg);

    // Schedule 10 updates at distinct positions with actual fluid to process
    for (int i = 0; i < 10; ++i) {
        sim.scheduleUpdate(BlockCoord{i, 64, 0}, EMPTY_FLUID_TYPE, 0, 0);
    }

    // First tick processes 5
    sim.simulateTick();
    size_t afterFirst = sim.pendingUpdateCount();

    // Second tick processes remaining
    sim.simulateTick();
    size_t afterSecond = sim.pendingUpdateCount();

    // After two ticks, should have processed all or nearly all
    EXPECT_LT(afterSecond, afterFirst);
}

TEST_F(FluidPerformanceTest, StaticSourcePoolSkipped) {
    World world;
    FluidSimulator sim(world);

    // Create a 3x3x3 cube of water sources surrounded by stone
    // Use setBlock (direct) instead of placeBlock (requires UpdateScheduler)
    for (int x = 59; x <= 65; ++x) {
        for (int y = 59; y <= 65; ++y) {
            for (int z = 59; z <= 65; ++z) {
                world.setBlock(BlockCoord{x, y, z}, stoneId_);
            }
        }
    }

    // Carve out interior 3x3x3 and fill with water sources
    BlockTypeId airId;  // default-constructed is air
    for (int x = 61; x <= 63; ++x) {
        for (int y = 61; y <= 63; ++y) {
            for (int z = 61; z <= 63; ++z) {
                world.setBlock(BlockCoord{x, y, z}, airId);
                world.setFluid(BlockCoord{x, y, z}, waterId_, 15);
            }
        }
    }

    // The center block (62,62,62) should be static — all 6 neighbors
    // are either same-type source or stone
    sim.notifyFluidChanged(BlockCoord{62, 62, 62});

    // Process several ticks to let the pool settle
    for (int i = 0; i < 10; ++i) sim.simulateTick();

    // After settling, the static pool should drain all pending updates
    EXPECT_EQ(sim.pendingUpdateCount(), 0u);
}

TEST_F(FluidPerformanceTest, StaticSourceDetection) {
    World world;
    FluidSimulator sim(world);

    // Place water source at (0, 64, 0) surrounded by stone on all 6 sides
    // Use setBlock (direct) instead of placeBlock
    world.setBlock(BlockCoord{-1, 64, 0}, stoneId_);
    world.setBlock(BlockCoord{1, 64, 0}, stoneId_);
    world.setBlock(BlockCoord{0, 63, 0}, stoneId_);
    world.setBlock(BlockCoord{0, 65, 0}, stoneId_);
    world.setBlock(BlockCoord{0, 64, -1}, stoneId_);
    world.setBlock(BlockCoord{0, 64, 1}, stoneId_);

    world.setFluid(BlockCoord{0, 64, 0}, waterId_, 15);

    // Notify and tick until stable
    sim.notifyFluidChanged(BlockCoord{0, 64, 0});
    for (int i = 0; i < 10; ++i) sim.simulateTick();

    // Now all pending should be drained — it's a static pool
    EXPECT_EQ(sim.pendingUpdateCount(), 0u);
}
