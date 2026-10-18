#include <gtest/gtest.h>
#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Test fixture
// ============================================================================

class ScriptFluidTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register test fluid types
        FluidType water;
        water.name = "script_water";
        water.id = FluidTypeId::fromName("script_water");
        water.density = 1000.0f;
        FluidRegistry::global().registerType("script_water", water);
        waterId_ = FluidTypeId::fromName("script_water");

        world_ = std::make_unique<World>();
        gse_ = std::make_unique<GameScriptEngine>(*world_);
        ctx_ = std::make_unique<finescript::ExecutionContext>(gse_->engine());

        // Set up user data with world pointer
        userData_.world = world_.get();
        ctx_->setUserData(&userData_);

        // Ensure a column exists at origin
        (void)world_->getOrCreateColumn({0, 0});
    }

    finescript::FullScriptResult exec(const std::string& cmd) {
        return gse_->engine().executeCommand(cmd, *ctx_);
    }

    FluidTypeId waterId_;
    std::unique_ptr<World> world_;
    std::unique_ptr<GameScriptEngine> gse_;
    std::unique_ptr<finescript::ExecutionContext> ctx_;
    ScriptUserData userData_;
};

// ============================================================================
// fluid_at tests
// ============================================================================

TEST_F(ScriptFluidTest, FluidAtReturnsNilWhenEmpty) {
    auto result = exec("fluid_at 0 64 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isNil());
}

TEST_F(ScriptFluidTest, FluidAtReturnsTypeName) {
    world_->setFluid({0, 64, 0}, waterId_, 15);

    auto result = exec("fluid_at 0 64 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isSymbol());
    EXPECT_EQ(result.returnValue.asSymbol(), waterId_.id);
}

// ============================================================================
// fluid_level tests
// ============================================================================

TEST_F(ScriptFluidTest, FluidLevelReturnsZeroWhenEmpty) {
    auto result = exec("fluid_level 0 64 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isInt());
    EXPECT_EQ(result.returnValue.asInt(), 0);
}

TEST_F(ScriptFluidTest, FluidLevelReturnsCorrectLevel) {
    world_->setFluid({0, 64, 0}, waterId_, 10);

    auto result = exec("fluid_level 0 64 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isInt());
    EXPECT_EQ(result.returnValue.asInt(), 10);
}

// ============================================================================
// fluid_place tests
// ============================================================================

TEST_F(ScriptFluidTest, FluidPlaceCreatesFluidInWorld) {
    auto result = exec("fluid_place 0 64 0 :script_water 15");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isBool());
    EXPECT_TRUE(result.returnValue.asBool());

    EXPECT_EQ(world_->getFluid({0, 64, 0}), waterId_);
    EXPECT_EQ(world_->getFluidLevel({0, 64, 0}), 15);
}

// ============================================================================
// fluid_remove tests
// ============================================================================

TEST_F(ScriptFluidTest, FluidRemoveClearsFluid) {
    world_->setFluid({0, 64, 0}, waterId_, 15);

    auto result = exec("fluid_remove 0 64 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isBool());
    EXPECT_TRUE(result.returnValue.asBool());

    EXPECT_TRUE(world_->getFluid({0, 64, 0}).isEmpty());
}

TEST_F(ScriptFluidTest, FluidRemoveOnEmptyReturnsFalse) {
    auto result = exec("fluid_remove 0 64 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.returnValue.isBool());
    EXPECT_FALSE(result.returnValue.asBool());
}

TEST_F(ScriptFluidTest, ScriptBlockAndFluidMutationsNotifyLightFluidAndRemesh) {
    LightEngine lightEngine(*world_);
    FluidTickManager fluidTicks(*world_);
    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);

    world_->setLightEngine(&lightEngine);
    world_->setFluidTickManager(&fluidTicks);
    world_->setMeshRebuildQueue(&meshQueue);

    // 1. world.set_block notifies LightEngine, FluidTickManager, and MeshRebuildQueue
    auto r1 = exec("world.set_block 1 64 1 :stone");
    EXPECT_TRUE(r1.success);
    EXPECT_FALSE(lightEngine.queue().empty());
    EXPECT_TRUE(fluidTicks.simulator().hasPendingUpdate({1, 64, 1}));
    EXPECT_FALSE(meshQueue.empty());

    // Clear queues before fluid mutation check
    (void)lightEngine.queue().dequeueBatch(64);
    fluidTicks.simulator().clearPendingUpdates();
    meshQueue.drainAll();

    // 2. fluid_place notifies LightEngine, FluidTickManager, and MeshRebuildQueue
    auto r2 = exec("fluid_place 2 64 2 :script_water 15");
    EXPECT_TRUE(r2.success);
    EXPECT_TRUE(r2.returnValue.asBool());
    EXPECT_FALSE(lightEngine.queue().empty());
    EXPECT_TRUE(fluidTicks.simulator().hasPendingUpdate({2, 64, 2}));
    EXPECT_FALSE(meshQueue.empty());

    (void)lightEngine.queue().dequeueBatch(64);
    fluidTicks.simulator().clearPendingUpdates();
    meshQueue.drainAll();

    // 3. fluid_set_level notifies LightEngine, FluidTickManager, and MeshRebuildQueue
    auto r3 = exec("fluid_set_level 2 64 2 8");
    EXPECT_TRUE(r3.success);
    EXPECT_TRUE(r3.returnValue.asBool());
    EXPECT_EQ(world_->getFluidLevel({2, 64, 2}), 8);
    EXPECT_FALSE(lightEngine.queue().empty());
    EXPECT_TRUE(fluidTicks.simulator().hasPendingUpdate({2, 64, 2}));
    EXPECT_FALSE(meshQueue.empty());

    (void)lightEngine.queue().dequeueBatch(64);
    fluidTicks.simulator().clearPendingUpdates();
    meshQueue.drainAll();

    // 4. fluid_remove notifies LightEngine, FluidTickManager, and MeshRebuildQueue
    auto r4 = exec("fluid_remove 2 64 2");
    EXPECT_TRUE(r4.success);
    EXPECT_TRUE(r4.returnValue.asBool());
    EXPECT_FALSE(lightEngine.queue().empty());
    EXPECT_TRUE(fluidTicks.simulator().hasPendingUpdate({2, 64, 2}));
    EXPECT_FALSE(meshQueue.empty());

    world_->setLightEngine(nullptr);
    world_->setFluidTickManager(nullptr);
    world_->setMeshRebuildQueue(nullptr);
}

