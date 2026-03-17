#include <gtest/gtest.h>
#include "finevox/core/game_session.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/world.hpp"
#include "finevox/script/event_value.hpp"

#include <finescript/value.h>
#include <finescript/map_data.h>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Helper: create a test mob directly through EntityManager
// ============================================================================

static EntityId spawnTestMob(EntityManager& em, Vec3 pos, float health = 20.0f) {
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId{});
    mob->setMaxHealth(health);
    mob->setHealth(health);
    mob->setPosition(pos);
    mob->setHalfExtents(Vec3(0.35f, 0.9f, 0.35f));
    return em.spawnEntity(std::move(mob));
}

// ============================================================================
// attackEntity action
// ============================================================================

TEST(CombatBridgeTest, AttackEntityViaDamage) {
    auto session = GameSession::createLocal();
    auto& em = session->entities();

    EntityId mobId = spawnTestMob(em, Vec3(5, 10, 5), 20.0f);
    MobEntity* mob = em.getMob(mobId);
    ASSERT_NE(mob, nullptr);
    EXPECT_FLOAT_EQ(mob->health(), 20.0f);

    // Build damage info map
    auto damageInfo = finescript::Value::map();
    auto& si = StringInterner::global();
    damageInfo.asMap().set(si.intern("amount"), finescript::Value::number(5.0));
    damageInfo.asMap().set(si.intern("type"), finescript::Value::string("slash"));

    // Attack via actions (game thread command queue)
    session->actions().attackEntity(INVALID_ENTITY_ID, mobId, std::move(damageInfo));
    session->tick(0.033f);

    EXPECT_FLOAT_EQ(mob->health(), 15.0f);
}

TEST(CombatBridgeTest, AttackEntityZeroDamageNoEffect) {
    auto session = GameSession::createLocal();
    auto& em = session->entities();

    EntityId mobId = spawnTestMob(em, Vec3(0, 10, 0), 10.0f);
    MobEntity* mob = em.getMob(mobId);
    ASSERT_NE(mob, nullptr);

    auto damageInfo = finescript::Value::map();
    auto& si = StringInterner::global();
    damageInfo.asMap().set(si.intern("amount"), finescript::Value::number(0.0));

    session->actions().attackEntity(INVALID_ENTITY_ID, mobId, std::move(damageInfo));
    session->tick(0.033f);

    EXPECT_FLOAT_EQ(mob->health(), 10.0f);
}

TEST(CombatBridgeTest, AttackEntitySetsLastAttacker) {
    auto session = GameSession::createLocal();
    auto& em = session->entities();

    EntityId attackerId = spawnTestMob(em, Vec3(0, 10, 0));
    EntityId targetId = spawnTestMob(em, Vec3(2, 10, 0));
    MobEntity* target = em.getMob(targetId);
    ASSERT_NE(target, nullptr);

    auto damageInfo = finescript::Value::map();
    auto& si = StringInterner::global();
    damageInfo.asMap().set(si.intern("amount"), finescript::Value::number(3.0));

    session->actions().attackEntity(attackerId, targetId, std::move(damageInfo));
    session->tick(0.033f);

    EXPECT_EQ(target->lastAttacker(), attackerId);
    EXPECT_FLOAT_EQ(target->health(), 17.0f);
}

TEST(CombatBridgeTest, AttackNonexistentEntityNoOp) {
    auto session = GameSession::createLocal();

    auto damageInfo = finescript::Value::map();
    auto& si = StringInterner::global();
    damageInfo.asMap().set(si.intern("amount"), finescript::Value::number(10.0));

    // Should not crash even with invalid target
    session->actions().attackEntity(INVALID_ENTITY_ID, 9999, std::move(damageInfo));
    session->tick(0.033f);
}

TEST(CombatBridgeTest, AttackEntityKillsTriggersDeath) {
    auto session = GameSession::createLocal();
    auto& em = session->entities();

    EntityId mobId = spawnTestMob(em, Vec3(5, 10, 5), 5.0f);
    MobEntity* mob = em.getMob(mobId);
    ASSERT_NE(mob, nullptr);

    auto damageInfo = finescript::Value::map();
    auto& si = StringInterner::global();
    damageInfo.asMap().set(si.intern("amount"), finescript::Value::number(10.0));

    session->actions().attackEntity(INVALID_ENTITY_ID, mobId, std::move(damageInfo));
    session->tick(0.033f);

    // After tick, entity should be dead and removed
    EXPECT_EQ(em.getEntity(mobId), nullptr);
}

// ============================================================================
// getMob helper
// ============================================================================

TEST(CombatBridgeTest, GetMobReturnsMobEntity) {
    auto session = GameSession::createLocal();
    auto& em = session->entities();

    EntityId mobId = spawnTestMob(em, Vec3(0, 10, 0));
    EXPECT_NE(em.getMob(mobId), nullptr);

    // Non-mob entity (plain Entity spawned via type)
    EntityId plainId = em.spawnEntity(EntityType::Arrow, Vec3(0, 15, 0));
    EXPECT_EQ(em.getMob(plainId), nullptr);  // Arrow is not a MobEntity
}

TEST(CombatBridgeTest, GetMobInvalidId) {
    auto session = GameSession::createLocal();
    EXPECT_EQ(session->entities().getMob(9999), nullptr);
}

// ============================================================================
// Event Value builders
// ============================================================================

TEST(CombatBridgeTest, MakeAttackEntityValue) {
    auto& si = StringInterner::global();
    auto damageInfo = finescript::Value::map();
    damageInfo.asMap().set(si.intern("amount"), finescript::Value::number(7.0));
    damageInfo.asMap().set(si.intern("type"), finescript::Value::string("pierce"));

    auto val = makeAttackEntityValue(42, 99, std::move(damageInfo));

    ASSERT_TRUE(val.isMap());
    auto typeStr = readEventType(val);
    EXPECT_EQ(typeStr, EVT_ATTACK_ENTITY);

    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readInt(val, s.attacker_id), 42);
    EXPECT_EQ(readInt(val, s.target_id), 99);

    // Check nested damage info
    auto info = val.asMap().get(s.damage_info);
    ASSERT_TRUE(info.isMap());
    auto amt = info.asMap().get(si.intern("amount"));
    EXPECT_TRUE(amt.isFloat());
    EXPECT_DOUBLE_EQ(amt.asFloat(), 7.0);
}

// ============================================================================
// EntitySpatialIndex cone queries (unit test for the logic)
// ============================================================================

TEST(CombatBridgeTest, ConeQueryBasic) {
    // Test the cone math manually (not through scripts)
    // Entities at (5,10,5), (5,10,8), (10,10,5) — cone from origin (5,10,5) dir=(0,0,1)
    auto session = GameSession::createLocal();
    auto& em = session->entities();

    // Place entity at origin, target straight ahead, and target to the side
    EntityId origin = spawnTestMob(em, Vec3(5, 10, 5));
    EntityId ahead = spawnTestMob(em, Vec3(5, 10, 8));   // 3 blocks ahead in Z
    EntityId side = spawnTestMob(em, Vec3(10, 10, 5));    // 5 blocks to the side in X
    EntityId behind = spawnTestMob(em, Vec3(5, 10, 2));   // 3 blocks behind

    // Query all entities in cone from (5,10,5) dir=(0,0,1) 45deg half-angle 10 range
    Vec3 coneOrigin(5, 10, 5);
    Vec3 coneDir(0, 0, 1);
    float halfAngleDeg = 45.0f;
    float range = 10.0f;
    float cosHalfAngle = std::cos(halfAngleDeg * 3.14159265f / 180.0f);

    auto candidates = em.spatialIndex().queryRadius(coneOrigin, range);

    std::vector<EntityId> inCone;
    for (EntityId id : candidates) {
        if (id == origin) continue;  // Skip self
        Entity* e = em.getEntity(id);
        if (!e) continue;
        Vec3 toEntity = e->position() - coneOrigin;
        float dist = glm::length(toEntity);
        if (dist < 0.01f) continue;
        float dot = glm::dot(toEntity / dist, coneDir);
        if (dot >= cosHalfAngle) {
            inCone.push_back(id);
        }
    }

    // 'ahead' should be in cone (directly in front)
    EXPECT_NE(std::find(inCone.begin(), inCone.end(), ahead), inCone.end());
    // 'side' should NOT be in cone (90 degrees off)
    EXPECT_EQ(std::find(inCone.begin(), inCone.end(), side), inCone.end());
    // 'behind' should NOT be in cone (behind)
    EXPECT_EQ(std::find(inCone.begin(), inCone.end(), behind), inCone.end());
}
