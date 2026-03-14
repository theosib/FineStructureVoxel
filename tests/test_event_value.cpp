#include <gtest/gtest.h>

#include "finevox/script/event_value.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/sound_event.hpp"

#include <finescript/map_data.h>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// EventSymbols initialization
// ============================================================================

TEST(EventValueTest, SymbolsInitialized) {
    const auto& s = EventSymbols::instance();
    EXPECT_NE(s.type, 0u);
    EXPECT_NE(s.pos_x, 0u);
    EXPECT_NE(s.entity_id, 0u);
    // All fields should be distinct
    EXPECT_NE(s.type, s.pos_x);
    EXPECT_NE(s.pos_x, s.pos_y);
}

// ============================================================================
// Block event round-trips
// ============================================================================

TEST(EventValueTest, BlockPlaced_RoundTrip) {
    auto stoneId = BlockTypeId::fromName("finevox:stone");
    auto airId = AIR_BLOCK_TYPE;
    BlockCoord pos{10, 64, -5};

    auto val = makeBlockPlacedValue(pos, stoneId, airId);

    EXPECT_EQ(readEventType(val), EVT_BLOCK_PLACED);
    auto readPos = readBlockCoord(val);
    EXPECT_EQ(readPos.x, 10);
    EXPECT_EQ(readPos.y, 64);
    EXPECT_EQ(readPos.z, -5);

    const auto& s = EventSymbols::instance();
    auto bt = readBlockTypeId(val, s.block_type);
    EXPECT_EQ(bt.name(), "finevox:stone");
    auto pt = readBlockTypeId(val, s.previous_type);
    EXPECT_EQ(pt.name(), "finevox:air");
}

TEST(EventValueTest, BlockBroken_RoundTrip) {
    auto stoneId = BlockTypeId::fromName("finevox:stone");
    BlockCoord pos{-3, 100, 42};

    auto val = makeBlockBrokenValue(pos, stoneId);

    EXPECT_EQ(readEventType(val), EVT_BLOCK_BROKEN);
    auto readPos = readBlockCoord(val);
    EXPECT_EQ(readPos.x, -3);
    EXPECT_EQ(readPos.y, 100);
    EXPECT_EQ(readPos.z, 42);

    const auto& s = EventSymbols::instance();
    auto pt = readBlockTypeId(val, s.previous_type);
    EXPECT_EQ(pt.name(), "finevox:stone");
}

TEST(EventValueTest, BlockChanged_RoundTrip) {
    auto stoneId = BlockTypeId::fromName("finevox:stone");
    auto dirtId = BlockTypeId::fromName("finevox:dirt");
    BlockCoord pos{0, 0, 0};

    auto val = makeBlockChangedValue(pos, stoneId, dirtId);

    EXPECT_EQ(readEventType(val), EVT_BLOCK_CHANGED);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readBlockTypeId(val, s.previous_type).name(), "finevox:stone");
    EXPECT_EQ(readBlockTypeId(val, s.block_type).name(), "finevox:dirt");
}

// ============================================================================
// Interaction events
// ============================================================================

TEST(EventValueTest, PlayerUse_RoundTrip) {
    BlockCoord pos{5, 10, 15};
    auto val = makePlayerUseValue(pos, Face::NegX);

    EXPECT_EQ(readEventType(val), EVT_PLAYER_USE);
    EXPECT_EQ(readBlockCoord(val).x, 5);
    EXPECT_EQ(readFace(val), Face::NegX);
}

TEST(EventValueTest, PlayerHit_RoundTrip) {
    BlockCoord pos{1, 2, 3};
    auto val = makePlayerHitValue(pos, Face::PosZ);

    EXPECT_EQ(readEventType(val), EVT_PLAYER_HIT);
    EXPECT_EQ(readFace(val), Face::PosZ);
}

// ============================================================================
// Player state events
// ============================================================================

TEST(EventValueTest, PlayerPosition_RoundTrip) {
    EntityId id = 42;
    glm::dvec3 pos{100.5, 64.0, -200.25};
    glm::dvec3 vel{1.0, -0.5, 0.0};

    auto val = makePlayerPositionValue(id, pos, vel, true, 12345);

    EXPECT_EQ(readEventType(val), EVT_PLAYER_POSITION);
    EXPECT_EQ(readEntityId(val), 42u);

    const auto& s = EventSymbols::instance();
    auto readPos = readDVec3(val, s.pos_x, s.pos_y, s.pos_z);
    EXPECT_DOUBLE_EQ(readPos.x, 100.5);
    EXPECT_DOUBLE_EQ(readPos.y, 64.0);
    EXPECT_DOUBLE_EQ(readPos.z, -200.25);

    auto readVel = readDVec3(val, s.vel_x, s.vel_y, s.vel_z);
    EXPECT_DOUBLE_EQ(readVel.x, 1.0);
    EXPECT_DOUBLE_EQ(readVel.y, -0.5);
    EXPECT_DOUBLE_EQ(readVel.z, 0.0);

    EXPECT_TRUE(readBool(val, s.on_ground));
    EXPECT_EQ(readInt(val, s.input_sequence), 12345);
}

TEST(EventValueTest, PlayerLook_RoundTrip) {
    auto val = makePlayerLookValue(7, 90.0f, -45.0f);

    EXPECT_EQ(readEventType(val), EVT_PLAYER_LOOK);
    EXPECT_EQ(readEntityId(val), 7u);

    const auto& s = EventSymbols::instance();
    EXPECT_FLOAT_EQ(readFloat(val, s.yaw), 90.0f);
    EXPECT_FLOAT_EQ(readFloat(val, s.pitch), -45.0f);
}

TEST(EventValueTest, PlayerJump_RoundTrip) {
    auto val = makePlayerJumpValue(99);
    EXPECT_EQ(readEventType(val), EVT_PLAYER_JUMP);
    EXPECT_EQ(readEntityId(val), 99u);
}

TEST(EventValueTest, PlayerSprint_RoundTrip) {
    auto val = makePlayerSprintValue(5, true);
    EXPECT_EQ(readEventType(val), EVT_PLAYER_SPRINT);
    EXPECT_EQ(readEntityId(val), 5u);
    EXPECT_TRUE(readBool(val, EventSymbols::instance().starting));

    auto val2 = makePlayerSprintValue(5, false);
    EXPECT_FALSE(readBool(val2, EventSymbols::instance().starting));
}

TEST(EventValueTest, PlayerSneak_RoundTrip) {
    auto val = makePlayerSneakValue(8, false);
    EXPECT_EQ(readEventType(val), EVT_PLAYER_SNEAK);
    EXPECT_FALSE(readBool(val, EventSymbols::instance().starting));
}

// ============================================================================
// Fluid events
// ============================================================================

TEST(EventValueTest, FluidPlaced_RoundTrip) {
    auto waterId = FluidTypeId::fromName("finevox:water");
    BlockCoord pos{1, 2, 3};
    auto val = makeFluidPlacedValue(pos, waterId, 12);

    EXPECT_EQ(readEventType(val), EVT_FLUID_PLACED);
    EXPECT_EQ(readBlockCoord(val).x, 1);
    EXPECT_EQ(readFluidTypeId(val).name(), "finevox:water");
    EXPECT_EQ(readInt(val, EventSymbols::instance().fluid_level), 12);
}

TEST(EventValueTest, FluidRemoved_RoundTrip) {
    auto lavaId = FluidTypeId::fromName("finevox:lava");
    BlockCoord pos{4, 5, 6};
    auto val = makeFluidRemovedValue(pos, lavaId);

    EXPECT_EQ(readEventType(val), EVT_FLUID_REMOVED);
    EXPECT_EQ(readFluidTypeId(val).name(), "finevox:lava");
}

// ============================================================================
// Admin/system events
// ============================================================================

TEST(EventValueTest, SetWorldTime_RoundTrip) {
    auto val = makeSetWorldTimeValue(24000);
    EXPECT_EQ(readEventType(val), EVT_SET_WORLD_TIME);
    EXPECT_EQ(readInt(val, EventSymbols::instance().ticks), 24000);
}

TEST(EventValueTest, CraftItem_RoundTrip) {
    BlockCoord station{10, 20, 30};
    RecipeId recipe{StringInterner::global().intern("test_recipe")};
    auto val = makeCraftItemValue(station, recipe);

    EXPECT_EQ(readEventType(val), EVT_CRAFT_ITEM);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readInt(val, s.station_pos_x), 10);
    EXPECT_EQ(readInt(val, s.station_pos_y), 20);
    EXPECT_EQ(readInt(val, s.station_pos_z), 30);
}

// ============================================================================
// Sound events
// ============================================================================

TEST(EventValueTest, SoundEvent_RoundTrip) {
    auto soundSet = SoundSetId::fromName("stone");
    auto val = makeSoundEventValue(soundSet, "place", "effects", 1.0f, 2.0f, 3.0f, 0.8f, 1.2f, true);

    EXPECT_EQ(readEventType(val), EVT_PLAY_SOUND);
    const auto& s = EventSymbols::instance();
    EXPECT_FLOAT_EQ(readFloat(val, s.pos_x), 1.0f);
    EXPECT_FLOAT_EQ(readFloat(val, s.pos_y), 2.0f);
    EXPECT_FLOAT_EQ(readFloat(val, s.pos_z), 3.0f);
    EXPECT_FLOAT_EQ(readFloat(val, s.volume), 0.8f);
    EXPECT_TRUE(readBool(val, s.positional));
}

// ============================================================================
// Graphics events
// ============================================================================

TEST(EventValueTest, EntitySpawn_RoundTrip) {
    auto val = makeEntitySpawnValue(10, 3, glm::dvec3{100.0, 64.0, 200.0}, 90.0f, 0.0f);

    EXPECT_EQ(readEventType(val), EVT_ENTITY_SPAWN);
    EXPECT_EQ(readEntityId(val), 10u);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readInt(val, s.entity_type), 3);
    EXPECT_FLOAT_EQ(readFloat(val, s.yaw), 90.0f);
}

TEST(EventValueTest, EntityDespawn_RoundTrip) {
    auto val = makeEntityDespawnValue(55);
    EXPECT_EQ(readEventType(val), EVT_ENTITY_DESPAWN);
    EXPECT_EQ(readEntityId(val), 55u);
}

TEST(EventValueTest, PlayerCorrection_RoundTrip) {
    auto val = makePlayerCorrectionValue(1, glm::dvec3{10, 20, 30}, glm::dvec3{0, -1, 0},
                                          true, 999, "physics_divergence");

    EXPECT_EQ(readEventType(val), EVT_PLAYER_CORRECTION);
    EXPECT_EQ(readEntityId(val), 1u);
    const auto& s = EventSymbols::instance();
    EXPECT_TRUE(readBool(val, s.on_ground));
    EXPECT_EQ(readInt(val, s.input_sequence), 999);
}

TEST(EventValueTest, BlockCorrection_RoundTrip) {
    auto stone = BlockTypeId::fromName("finevox:stone");
    auto dirt = BlockTypeId::fromName("finevox:dirt");
    BlockCoord pos{5, 10, 15};

    auto val = makeBlockCorrectionValue(pos, stone, dirt);

    EXPECT_EQ(readEventType(val), EVT_BLOCK_CORRECTION);
    EXPECT_EQ(readBlockCoord(val).x, 5);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readBlockTypeId(val, s.correct_block_type).name(), "finevox:stone");
    EXPECT_EQ(readBlockTypeId(val, s.expected_block_type).name(), "finevox:dirt");
}

TEST(EventValueTest, EntityAnimation_RoundTrip) {
    auto val = makeEntityAnimationValue(7, 2, 0.5f);

    EXPECT_EQ(readEventType(val), EVT_ENTITY_ANIMATION);
    EXPECT_EQ(readEntityId(val), 7u);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readInt(val, s.animation_id), 2);
    EXPECT_FLOAT_EQ(readFloat(val, s.animation_time), 0.5f);
}

// ============================================================================
// Reader defaults
// ============================================================================

TEST(EventValueTest, ReadDefaults) {
    auto val = makePlayerJumpValue(1);
    const auto& s = EventSymbols::instance();

    // Fields not present should return defaults
    EXPECT_FLOAT_EQ(readFloat(val, s.volume, 0.5f), 0.5f);
    EXPECT_EQ(readInt(val, s.fluid_level, 99), 99);
    EXPECT_FALSE(readBool(val, s.positional, false));
}

// ============================================================================
// Extensibility — extra fields survive round-trip
// ============================================================================

TEST(EventValueTest, ExtensibleFields) {
    auto val = makeBlockPlacedValue(BlockCoord{0, 0, 0}, AIR_BLOCK_TYPE, AIR_BLOCK_TYPE);

    // Add a custom field (mod-defined)
    auto customKey = StringInterner::global().intern("custom_mod_data");
    val.asMap().set(customKey, finescript::Value::integer(42));

    // Read it back
    auto v = val.asMap().get(customKey);
    ASSERT_TRUE(v.isInt());
    EXPECT_EQ(v.asInt(), 42);

    // Standard fields still work
    EXPECT_EQ(readEventType(val), EVT_BLOCK_PLACED);
}
