#include <gtest/gtest.h>
#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_registry.hpp"
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
