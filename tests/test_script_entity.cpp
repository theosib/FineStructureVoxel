#include <gtest/gtest.h>

#include "finevox/script/script_entity_handler.hpp"
#include "finevox/script/entity_context_proxy.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>

#include <filesystem>
#include <fstream>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Test fixture
// ============================================================================

class ScriptEntityTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = std::filesystem::temp_directory_path() / "finevox_script_entity_test";
        std::filesystem::create_directories(tempDir_);

        // Register a test entity type
        EntityTypeDef def;
        def.name = "test_mob";
        def.maxHealth = 20.0f;
        def.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
        def.maxSpeed = 4.0f;
        def.jumpStrength = 0.5f;
        EntityTypeRegistry::global().registerType("test_mob", std::move(def));
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir_);
    }

    std::string writeScript(const std::string& name, const std::string& content) {
        auto path = tempDir_ / name;
        std::ofstream out(path);
        out << content;
        out.close();
        return path.string();
    }

    std::filesystem::path tempDir_;
};

// ============================================================================
// EntityContextProxy Tests
// ============================================================================

TEST_F(ScriptEntityTest, EntityContextProxyReadPosition) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    mob.setPosition(Vec3(10.0f, 20.0f, 30.0f));

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    auto x = proxy.get(f.x);
    auto y = proxy.get(f.y);
    auto z = proxy.get(f.z);

    EXPECT_NEAR(x.asFloat(), 10.0, 0.01);
    EXPECT_NEAR(y.asFloat(), 20.0, 0.01);
    EXPECT_NEAR(z.asFloat(), 30.0, 0.01);
}

TEST_F(ScriptEntityTest, EntityContextProxyReadVelocity) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    mob.setVelocity(Vec3(1.0f, -2.0f, 3.0f));

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    EXPECT_NEAR(proxy.get(f.vx).asFloat(), 1.0, 0.01);
    EXPECT_NEAR(proxy.get(f.vy).asFloat(), -2.0, 0.01);
    EXPECT_NEAR(proxy.get(f.vz).asFloat(), 3.0, 0.01);
}

TEST_F(ScriptEntityTest, EntityContextProxyReadHealth) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    mob.setHealth(15.0f);

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    EXPECT_NEAR(proxy.get(f.health).asFloat(), 15.0, 0.01);
    EXPECT_NEAR(proxy.get(f.max_health).asFloat(), 20.0, 0.01);
}

TEST_F(ScriptEntityTest, EntityContextProxySetHealth) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    proxy.set(f.health, finescript::Value::number(5.0));
    EXPECT_NEAR(mob.health(), 5.0f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityContextProxyReadLookDirection) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    mob.setYaw(45.0f);
    mob.setPitch(-10.0f);

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    EXPECT_NEAR(proxy.get(f.yaw).asFloat(), 45.0, 0.01);
    EXPECT_NEAR(proxy.get(f.pitch).asFloat(), -10.0, 0.01);
}

TEST_F(ScriptEntityTest, EntityContextProxyReadIdentity) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(42, typeId);

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    EXPECT_EQ(proxy.get(f.id).asInt(), 42);
    EXPECT_EQ(proxy.get(f.type).asSymbol(), typeId.id);
}

TEST_F(ScriptEntityTest, EntityContextProxyOnGround) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    mob.setOnGround(true);

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    EXPECT_TRUE(proxy.get(f.on_ground).asBool());
}

TEST_F(ScriptEntityTest, EntityContextProxySpeed) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);

    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    // Default speed
    EXPECT_NEAR(proxy.get(f.speed).asFloat(), 1.0, 0.01);

    // Set via proxy
    proxy.set(f.speed, finescript::Value::number(0.5));
    EXPECT_NEAR(mob.speedMultiplier(), 0.5f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityContextProxyHasAndKeys) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    EXPECT_TRUE(proxy.has(f.x));
    EXPECT_TRUE(proxy.has(f.health));
    EXPECT_TRUE(proxy.has(f.id));
    EXPECT_FALSE(proxy.has(999999));  // Unknown key

    auto keys = proxy.keys();
    EXPECT_EQ(keys.size(), 14u);  // 14 fields total
}

TEST_F(ScriptEntityTest, EntityContextProxyRemoveReturnsFalse) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    EntityContextProxy proxy(mob);
    const auto& f = EntityContextFields::instance();

    // Entity fields cannot be removed
    EXPECT_FALSE(proxy.remove(f.x));
}

TEST_F(ScriptEntityTest, MakeEntityProxy) {
    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(1, typeId);
    mob.setPosition(Vec3(5.0f, 10.0f, 15.0f));

    auto proxy = makeEntityProxy(mob);
    ASSERT_NE(proxy, nullptr);

    const auto& f = EntityContextFields::instance();
    EXPECT_NEAR(proxy->get(f.x).asFloat(), 5.0, 0.01);
}

// ============================================================================
// ScriptEntityHandler Tests
// ============================================================================

TEST_F(ScriptEntityTest, LoadEntityScriptWithHandlers) {
    World world;
    GameScriptEngine gse(world);

    // finescript: entity.health is proxy map field access (sets health on mob)
    auto path = writeScript("test_entity.fsc", R"(
on :spawn do
    set entity.health 10
end

on :tick do
    set entity.speed 0.5
end
)");

    auto* handler = gse.loadEntityScript(path, "test_mob");
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->hasHandlers());
    EXPECT_EQ(handler->name(), "test_mob");
}

TEST_F(ScriptEntityTest, LoadEntityScriptNoHandlers) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("no_handlers.fsc", R"(
set x 42
)");

    auto* handler = gse.loadEntityScript(path, "empty_entity");
    EXPECT_EQ(handler, nullptr);
}

TEST_F(ScriptEntityTest, GetEntityHandler) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("test_entity2.fsc", R"(
on :spawn do
    set entity.health 10
end
)");

    gse.loadEntityScript(path, "mob_a");

    EXPECT_NE(gse.getEntityHandler("mob_a"), nullptr);
    EXPECT_EQ(gse.getEntityHandler("mob_b"), nullptr);
}

TEST_F(ScriptEntityTest, EntityHandlerOnSpawn) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("spawn_handler.fsc", R"(
on :spawn do
    set entity.health 7
end
)");

    auto* handler = gse.loadEntityScript(path, "spawn_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(10, typeId);

    handler->onSpawn(mob);

    // Verify through mob side effect
    EXPECT_NEAR(mob.health(), 7.0f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerOnTick) {
    World world;
    GameScriptEngine gse(world);

    // Use entity proxy to set speed based on dt
    auto path = writeScript("tick_handler.fsc", R"(
on :tick do
    set entity.speed 0.25
end
)");

    auto* handler = gse.loadEntityScript(path, "tick_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(11, typeId);

    handler->onTick(mob, 0.05f);

    EXPECT_NEAR(mob.speedMultiplier(), 0.25f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerOnDamage) {
    World world;
    GameScriptEngine gse(world);

    // On damage: set health to a fixed value to prove the handler ran
    auto path = writeScript("damage_handler.fsc", R"(
on :damage do
    set entity.health 3
end
)");

    auto* handler = gse.loadEntityScript(path, "damage_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(12, typeId);

    handler->onDamage(mob, 5.0f, 99);

    EXPECT_NEAR(mob.health(), 3.0f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerOnDeath) {
    World world;
    GameScriptEngine gse(world);

    // On death: set speed to 0
    auto path = writeScript("death_handler.fsc", R"(
on :death do
    set entity.speed 0
end
)");

    auto* handler = gse.loadEntityScript(path, "death_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(13, typeId);

    handler->onDeath(mob, 77);

    EXPECT_NEAR(mob.speedMultiplier(), 0.0f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerOnInteract) {
    World world;
    GameScriptEngine gse(world);

    // On interact: set health to a specific value (within max_health=20)
    auto path = writeScript("use_handler.fsc", R"(
on :interact do
    set entity.health 15
end
)");

    auto* handler = gse.loadEntityScript(path, "use_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(14, typeId);
    mob.setHealth(5.0f);

    handler->onInteract(mob, 55);

    EXPECT_NEAR(mob.health(), 15.0f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerOnStrike) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("hit_handler.fsc", R"(
on :strike do
    set entity.health 1
end
)");

    auto* handler = gse.loadEntityScript(path, "hit_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(15, typeId);

    handler->onStrike(mob, 33);

    EXPECT_NEAR(mob.health(), 1.0f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerMissingEventIsNoop) {
    World world;
    GameScriptEngine gse(world);

    // Only registers :spawn handler
    auto path = writeScript("partial_handler.fsc", R"(
on :spawn do
    set entity.health 5
end
)");

    auto* handler = gse.loadEntityScript(path, "partial_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(16, typeId);

    // These should be no-ops (no handler registered)
    handler->onTick(mob, 0.05f);
    handler->onDamage(mob, 5.0f, 0);
    handler->onDeath(mob, 0);
    handler->onInteract(mob, 0);
    handler->onStrike(mob, 0);

    // Mob should be unaffected (health stays at default)
    EXPECT_NEAR(mob.health(), 20.0f, 0.01f);
}

// ============================================================================
// mob_* Native Function Tests (called from script)
// ============================================================================

TEST_F(ScriptEntityTest, MobNativeSetHealth) {
    World world;
    GameScriptEngine gse(world);

    // finescript prefix syntax: verb arg arg ...
    auto path = writeScript("mob_set_health.fsc", R"(
on :tick do
    mob_set_health 5
end
)");

    auto* handler = gse.loadEntityScript(path, "set_health_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(21, typeId);

    handler->onTick(mob, 0.05f);
    EXPECT_NEAR(mob.health(), 5.0f, 0.01f);
}

TEST_F(ScriptEntityTest, MobNativeMoveTo) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("mob_move.fsc", R"(
on :tick do
    mob_move_to 10 20 30
end
)");

    auto* handler = gse.loadEntityScript(path, "move_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(23, typeId);

    handler->onTick(mob, 0.05f);

    EXPECT_TRUE(mob.hasMoveTarget());
    auto target = mob.moveTarget();
    EXPECT_NEAR(target.x, 10.0, 0.01);
    EXPECT_NEAR(target.y, 20.0, 0.01);
    EXPECT_NEAR(target.z, 30.0, 0.01);
}

TEST_F(ScriptEntityTest, MobNativeJump) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("mob_jump.fsc", R"(
on :tick do
    mob_jump
end
)");

    auto* handler = gse.loadEntityScript(path, "jump_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(24, typeId);
    mob.setOnGround(true);

    handler->onTick(mob, 0.05f);

    // After jump, velocity Y should be positive
    auto vel = mob.velocity();
    EXPECT_GT(vel.y, 0.0f);
}

TEST_F(ScriptEntityTest, MobNativeDamage) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("mob_damage.fsc", R"(
on :tick do
    mob_damage 3
end
)");

    auto* handler = gse.loadEntityScript(path, "damage_native_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(25, typeId);
    float startHealth = mob.health();

    handler->onTick(mob, 0.05f);
    EXPECT_NEAR(mob.health(), startHealth - 3.0f, 0.01f);
}

TEST_F(ScriptEntityTest, MobNativeHeal) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("mob_heal.fsc", R"(
on :tick do
    mob_heal 5
end
)");

    auto* handler = gse.loadEntityScript(path, "heal_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(26, typeId);
    mob.setHealth(10.0f);

    handler->onTick(mob, 0.05f);
    EXPECT_NEAR(mob.health(), 15.0f, 0.01f);
}

TEST_F(ScriptEntityTest, MobNativeSetSpeed) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("mob_speed.fsc", R"(
on :tick do
    mob_set_speed 0.5
end
)");

    auto* handler = gse.loadEntityScript(path, "speed_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(28, typeId);

    handler->onTick(mob, 0.05f);
    EXPECT_NEAR(mob.speedMultiplier(), 0.5f, 0.01f);
}

TEST_F(ScriptEntityTest, EntityHandlerEntityProxyFields) {
    World world;
    GameScriptEngine gse(world);

    // Read entity fields via proxy map and set health to prove access works
    auto path = writeScript("entity_proxy_access.fsc", R"(
on :spawn do
    set entity.health (entity.x + entity.y)
end
)");

    auto* handler = gse.loadEntityScript(path, "proxy_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(31, typeId);
    mob.setPosition(Vec3(3.0f, 4.0f, 9.0f));

    handler->onSpawn(mob);

    // health should be set to x + y = 3 + 4 = 7
    EXPECT_NEAR(mob.health(), 7.0f, 0.01f);
}

TEST_F(ScriptEntityTest, SetEntityManager) {
    World world;
    GameScriptEngine gse(world);

    // Should not crash even if no entity manager set
    EXPECT_NO_THROW(gse.setEntityManager(nullptr));
}

// ============================================================================
// Integration: entity proxy read back via mob_health native function
// ============================================================================

TEST_F(ScriptEntityTest, MobNativeHealthReadsCurrentMob) {
    World world;
    GameScriptEngine gse(world);

    // Use mob_health native to read, then set entity.health to confirm round-trip
    auto path = writeScript("health_roundtrip.fsc", R"(
on :tick do
    set h {mob_health}
    set entity.health (h - 1)
end
)");

    auto* handler = gse.loadEntityScript(path, "roundtrip_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(40, typeId);
    mob.setHealth(10.0f);

    handler->onTick(mob, 0.05f);

    // health should be 10 - 1 = 9
    EXPECT_NEAR(mob.health(), 9.0f, 0.01f);
}

TEST_F(ScriptEntityTest, MobNativePositionReturnsArray) {
    World world;
    GameScriptEngine gse(world);

    // mob_position returns [x, y, z] array; use pos[0] to access first element
    auto path = writeScript("mob_pos_test.fsc", R"(
on :tick do
    set pos {mob_position}
    set entity.health pos[0]
end
)");

    auto* handler = gse.loadEntityScript(path, "pos_read_mob");
    ASSERT_NE(handler, nullptr);

    auto typeId = EntityTypeId::fromName("test_mob");
    MobEntity mob(41, typeId);
    mob.setPosition(Vec3(15.0f, 20.0f, 25.0f));

    handler->onTick(mob, 0.05f);

    // health should be set to x coordinate = 15
    EXPECT_NEAR(mob.health(), 15.0f, 0.01f);
}
