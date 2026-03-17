#include <gtest/gtest.h>
#include "finevox/script/player_stats_bridge.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/script/finevox_interner.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <finescript/value.h>

using namespace finevox;
using namespace finevox::script;

class PlayerStatsBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        session_ = GameSession::createLocal();
        playerId_ = session_->entities().spawnPlayer(Vec3(0, 64, 0));
        session_->entities().setLocalPlayerId(playerId_);

        engine_.setInterner(&interner_);
        bridge_ = std::make_unique<PlayerStatsBridge>(session_->entities());
        bridge_->registerNativeFunctions(engine_);
    }

    finescript::Value eval(std::string_view expr) {
        finescript::ExecutionContext ctx(engine_);
        auto result = engine_.executeCommand(expr, ctx);
        EXPECT_TRUE(result.success) << result.error;
        return result.returnValue;
    }

    std::unique_ptr<GameSession> session_;
    EntityId playerId_ = INVALID_ENTITY_ID;
    FineVoxInterner interner_;
    finescript::ScriptEngine engine_;
    std::unique_ptr<PlayerStatsBridge> bridge_;
};

TEST_F(PlayerStatsBridgeTest, PlayerHealth) {
    auto result = eval("{player_health}");
    ASSERT_TRUE(result.isFloat());
    EXPECT_DOUBLE_EQ(result.asFloat(), 20.0);
}

TEST_F(PlayerStatsBridgeTest, PlayerMaxHealth) {
    auto result = eval("{player_max_health}");
    ASSERT_TRUE(result.isFloat());
    EXPECT_DOUBLE_EQ(result.asFloat(), 20.0);
}

TEST_F(PlayerStatsBridgeTest, PlayerHealthAfterDamage) {
    auto* mob = session_->entities().getMob(playerId_);
    ASSERT_NE(mob, nullptr);
    mob->damage(5.0f);

    auto result = eval("{player_health}");
    ASSERT_TRUE(result.isFloat());
    EXPECT_DOUBLE_EQ(result.asFloat(), 15.0);
}

TEST_F(PlayerStatsBridgeTest, PlayerGetStatDefault) {
    auto result = eval("{player_get_stat \"hunger\"}");
    ASSERT_TRUE(result.isFloat());
    EXPECT_DOUBLE_EQ(result.asFloat(), 0.0);
}

TEST_F(PlayerStatsBridgeTest, PlayerSetAndGetStat) {
    eval("{player_set_stat \"hunger\" 18.5}");
    auto result = eval("{player_get_stat \"hunger\"}");
    ASSERT_TRUE(result.isFloat());
    EXPECT_NEAR(result.asFloat(), 18.5, 0.01);
}

TEST_F(PlayerStatsBridgeTest, PlayerIsAlive) {
    auto result = eval("{player_is_alive}");
    ASSERT_TRUE(result.isBool());
    EXPECT_TRUE(result.asBool());
}

TEST_F(PlayerStatsBridgeTest, PlayerIsAliveAfterDeath) {
    auto* mob = session_->entities().getMob(playerId_);
    ASSERT_NE(mob, nullptr);
    mob->damage(100.0f);

    auto result = eval("{player_is_alive}");
    ASSERT_TRUE(result.isBool());
    EXPECT_FALSE(result.asBool());
}

TEST_F(PlayerStatsBridgeTest, PlayerPosition) {
    auto result = eval("{player_position}");
    ASSERT_TRUE(result.isArray());
    const auto& arr = result.asArray();
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_NEAR(arr[0].asFloat(), 0.0, 0.1);
    EXPECT_NEAR(arr[1].asFloat(), 64.0, 0.1);
    EXPECT_NEAR(arr[2].asFloat(), 0.0, 0.1);
}

TEST_F(PlayerStatsBridgeTest, NoPlayerReturnsDefaults) {
    auto session2 = GameSession::createLocal();
    finescript::ScriptEngine engine2;
    FineVoxInterner interner2;
    engine2.setInterner(&interner2);
    PlayerStatsBridge bridge2(session2->entities());
    bridge2.registerNativeFunctions(engine2);

    finescript::ExecutionContext ctx(engine2);
    auto r = engine2.executeCommand("{player_health}", ctx);
    EXPECT_DOUBLE_EQ(r.returnValue.asFloat(), 0.0);

    r = engine2.executeCommand("{player_is_alive}", ctx);
    EXPECT_FALSE(r.returnValue.asBool());
}
