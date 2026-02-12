#include <gtest/gtest.h>
#include "finevox/core/sound_event.hpp"
#include "finevox/core/sound_registry.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/config_parser.hpp"

using namespace finevox;

// ============================================================================
// SoundSetId
// ============================================================================

TEST(SoundSetIdTest, DefaultIsInvalid) {
    SoundSetId id;
    EXPECT_FALSE(id.isValid());
    EXPECT_EQ(id.id, 0u);
}

TEST(SoundSetIdTest, FromNameCreatesValid) {
    auto id = SoundSetId::fromName("stone");
    EXPECT_TRUE(id.isValid());
    EXPECT_NE(id.id, 0u);
}

TEST(SoundSetIdTest, FromNameRoundTrip) {
    auto id = SoundSetId::fromName("test_sound_roundtrip");
    EXPECT_EQ(id.name(), "test_sound_roundtrip");
}

TEST(SoundSetIdTest, SameNameSameId) {
    auto id1 = SoundSetId::fromName("test_same_sound");
    auto id2 = SoundSetId::fromName("test_same_sound");
    EXPECT_EQ(id1, id2);
}

TEST(SoundSetIdTest, DifferentNameDifferentId) {
    auto id1 = SoundSetId::fromName("test_sound_a");
    auto id2 = SoundSetId::fromName("test_sound_b");
    EXPECT_NE(id1, id2);
}

TEST(SoundSetIdTest, EmptyNameIsInvalid) {
    auto id = SoundSetId::fromName("");
    EXPECT_FALSE(id.isValid());
}

TEST(SoundSetIdTest, Hashable) {
    auto id = SoundSetId::fromName("test_hash_sound");
    std::hash<SoundSetId> hasher;
    // Just verify it compiles and doesn't crash
    [[maybe_unused]] auto h = hasher(id);
}

// ============================================================================
// SoundEvent Factory Methods
// ============================================================================

TEST(SoundEventTest, BlockPlaceFactory) {
    auto id = SoundSetId::fromName("stone_place_test");
    auto event = SoundEvent::blockPlace(id, BlockPos(10, 20, 30));

    EXPECT_EQ(event.soundSet, id);
    EXPECT_EQ(event.action, SoundAction::Place);
    EXPECT_EQ(event.category, SoundCategory::Effects);
    EXPECT_TRUE(event.positional);
    EXPECT_FLOAT_EQ(event.posX, 10.5f);
    EXPECT_FLOAT_EQ(event.posY, 20.5f);
    EXPECT_FLOAT_EQ(event.posZ, 30.5f);
}

TEST(SoundEventTest, BlockBreakFactory) {
    auto id = SoundSetId::fromName("stone_break_test");
    auto event = SoundEvent::blockBreak(id, BlockPos(-5, 64, 100));

    EXPECT_EQ(event.soundSet, id);
    EXPECT_EQ(event.action, SoundAction::Break);
    EXPECT_EQ(event.category, SoundCategory::Effects);
    EXPECT_FLOAT_EQ(event.posX, -4.5f);
    EXPECT_FLOAT_EQ(event.posY, 64.5f);
    EXPECT_FLOAT_EQ(event.posZ, 100.5f);
}

TEST(SoundEventTest, FootstepFactory) {
    auto id = SoundSetId::fromName("grass_step_test");
    auto event = SoundEvent::footstep(id, glm::vec3(1.0f, 2.0f, 3.0f));

    EXPECT_EQ(event.soundSet, id);
    EXPECT_EQ(event.action, SoundAction::Step);
    EXPECT_EQ(event.category, SoundCategory::Effects);
    EXPECT_FLOAT_EQ(event.volume, 0.5f);  // Footsteps are quieter
    EXPECT_FLOAT_EQ(event.posX, 1.0f);
}

TEST(SoundEventTest, FallFactory) {
    auto id = SoundSetId::fromName("stone_fall_test");

    // Short fall
    auto shortFall = SoundEvent::fall(id, glm::vec3(0, 0, 0), 2.0f);
    EXPECT_EQ(shortFall.action, SoundAction::Fall);
    EXPECT_FLOAT_EQ(shortFall.volume, 0.3f);  // Clamped to min

    // Medium fall
    auto medFall = SoundEvent::fall(id, glm::vec3(0, 0, 0), 5.0f);
    EXPECT_FLOAT_EQ(medFall.volume, 0.5f);

    // Long fall
    auto longFall = SoundEvent::fall(id, glm::vec3(0, 0, 0), 20.0f);
    EXPECT_FLOAT_EQ(longFall.volume, 1.0f);  // Clamped to max
}

TEST(SoundEventTest, MusicFactory) {
    auto id = SoundSetId::fromName("music_test_track");
    auto event = SoundEvent::music(id);

    EXPECT_EQ(event.soundSet, id);
    EXPECT_EQ(event.category, SoundCategory::Music);
    EXPECT_FALSE(event.positional);
}

TEST(SoundEventTest, AmbientFactory) {
    auto id = SoundSetId::fromName("ambient_test");
    auto event = SoundEvent::ambient(id, glm::vec3(10, 20, 30));

    EXPECT_EQ(event.soundSet, id);
    EXPECT_EQ(event.category, SoundCategory::Ambient);
    EXPECT_TRUE(event.positional);
}

TEST(SoundEventTest, PositionHelpers) {
    SoundEvent event;

    // Set from vec3
    event.setPosition(glm::vec3(1.5f, 2.5f, 3.5f));
    EXPECT_FLOAT_EQ(event.posX, 1.5f);
    EXPECT_FLOAT_EQ(event.posY, 2.5f);
    EXPECT_FLOAT_EQ(event.posZ, 3.5f);

    // Read back as vec3
    auto pos = event.position();
    EXPECT_FLOAT_EQ(pos.x, 1.5f);
    EXPECT_FLOAT_EQ(pos.y, 2.5f);
    EXPECT_FLOAT_EQ(pos.z, 3.5f);

    // Set from BlockPos (centers on block)
    event.setPosition(BlockPos(10, 20, 30));
    EXPECT_FLOAT_EQ(event.posX, 10.5f);
    EXPECT_FLOAT_EQ(event.posY, 20.5f);
    EXPECT_FLOAT_EQ(event.posZ, 30.5f);
}

// ============================================================================
// SoundEventQueue
// ============================================================================

TEST(SoundEventQueueTest, PushAndDrain) {
    SoundEventQueue queue;

    auto id = SoundSetId::fromName("queue_test_sound");
    queue.push(SoundEvent::blockPlace(id, BlockPos(0, 0, 0)));
    queue.push(SoundEvent::blockBreak(id, BlockPos(1, 1, 1)));

    auto events = queue.drainAll();
    EXPECT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].action, SoundAction::Place);
    EXPECT_EQ(events[1].action, SoundAction::Break);
}

TEST(SoundEventQueueTest, DrainEmptyReturnsEmpty) {
    SoundEventQueue queue;
    auto events = queue.drainAll();
    EXPECT_TRUE(events.empty());
}

TEST(SoundEventQueueTest, TryPopOrder) {
    SoundEventQueue queue;

    auto id = SoundSetId::fromName("queue_pop_test");
    queue.push(SoundEvent::blockPlace(id, BlockPos(0, 0, 0)));
    queue.push(SoundEvent::blockBreak(id, BlockPos(1, 1, 1)));

    auto first = queue.tryPop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->action, SoundAction::Place);

    auto second = queue.tryPop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->action, SoundAction::Break);

    auto third = queue.tryPop();
    EXPECT_FALSE(third.has_value());
}

// ============================================================================
// SoundSetDefinition
// ============================================================================

TEST(SoundSetDefinitionTest, HasAction) {
    SoundSetDefinition def;
    def.name = "test_def";

    // No actions yet
    EXPECT_FALSE(def.hasAction(SoundAction::Place));

    // Add a place action with variants
    SoundGroup group;
    group.variants.push_back({"sounds/test/place1.wav", 1.0f, 1.0f});
    def.actions[SoundAction::Place] = group;

    EXPECT_TRUE(def.hasAction(SoundAction::Place));
    EXPECT_FALSE(def.hasAction(SoundAction::Break));
}

TEST(SoundSetDefinitionTest, GetAction) {
    SoundSetDefinition def;
    def.name = "test_get";

    SoundGroup group;
    group.variants.push_back({"sounds/test/step1.wav", 1.0f, 1.0f});
    group.variants.push_back({"sounds/test/step2.wav", 0.9f, 1.1f});
    def.actions[SoundAction::Step] = group;

    auto* result = def.getAction(SoundAction::Step);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->size(), 2u);
    EXPECT_EQ(result->variants[0].path, "sounds/test/step1.wav");

    EXPECT_EQ(def.getAction(SoundAction::Dig), nullptr);
}

TEST(SoundSetDefinitionTest, EmptyGroupNotReported) {
    SoundSetDefinition def;
    def.name = "test_empty";

    // Add an empty group
    def.actions[SoundAction::Hit] = SoundGroup{};

    EXPECT_FALSE(def.hasAction(SoundAction::Hit));
    EXPECT_EQ(def.getAction(SoundAction::Hit), nullptr);
}

// ============================================================================
// SoundRegistry
// ============================================================================

class SoundRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        SoundRegistry::global().clear();
    }

    void TearDown() override {
        SoundRegistry::global().clear();
    }
};

TEST_F(SoundRegistryTest, RegisterAndLookupByName) {
    SoundSetDefinition def;
    SoundGroup placeGroup;
    placeGroup.variants.push_back({"sounds/stone/place1.wav", 1.0f, 1.0f});
    def.actions[SoundAction::Place] = placeGroup;

    EXPECT_TRUE(SoundRegistry::global().registerSoundSet("reg_test_stone", std::move(def)));
    EXPECT_EQ(SoundRegistry::global().size(), 1u);

    auto* result = SoundRegistry::global().getSoundSet("reg_test_stone");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->name, "reg_test_stone");
    EXPECT_TRUE(result->hasAction(SoundAction::Place));
}

TEST_F(SoundRegistryTest, RegisterAndLookupById) {
    SoundSetDefinition def;
    SoundGroup breakGroup;
    breakGroup.variants.push_back({"sounds/grass/break1.wav", 1.0f, 1.0f});
    def.actions[SoundAction::Break] = breakGroup;

    EXPECT_TRUE(SoundRegistry::global().registerSoundSet("reg_test_grass", std::move(def)));

    auto id = SoundRegistry::global().getSoundSetId("reg_test_grass");
    EXPECT_TRUE(id.isValid());

    auto* result = SoundRegistry::global().getSoundSet(id);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->name, "reg_test_grass");
}

TEST_F(SoundRegistryTest, DuplicateRegistrationFails) {
    SoundSetDefinition def1, def2;

    EXPECT_TRUE(SoundRegistry::global().registerSoundSet("reg_test_dup", std::move(def1)));
    EXPECT_FALSE(SoundRegistry::global().registerSoundSet("reg_test_dup", std::move(def2)));
    EXPECT_EQ(SoundRegistry::global().size(), 1u);
}

TEST_F(SoundRegistryTest, UnregisteredReturnsNull) {
    EXPECT_EQ(SoundRegistry::global().getSoundSet("nonexistent"), nullptr);

    auto id = SoundSetId::fromName("nonexistent_id_test");
    EXPECT_EQ(SoundRegistry::global().getSoundSet(id), nullptr);
}

TEST_F(SoundRegistryTest, GetIdForUnregisteredReturnsInvalid) {
    auto id = SoundRegistry::global().getSoundSetId("never_registered");
    EXPECT_FALSE(id.isValid());
}

TEST_F(SoundRegistryTest, ClearRemovesAll) {
    SoundSetDefinition def;
    SoundRegistry::global().registerSoundSet("reg_test_clear", std::move(def));
    EXPECT_EQ(SoundRegistry::global().size(), 1u);

    SoundRegistry::global().clear();
    EXPECT_EQ(SoundRegistry::global().size(), 0u);
    EXPECT_EQ(SoundRegistry::global().getSoundSet("reg_test_clear"), nullptr);
}

TEST_F(SoundRegistryTest, MultipleRegistrations) {
    SoundSetDefinition stone, grass, wood;

    SoundGroup g;
    g.variants.push_back({"dummy.wav", 1.0f, 1.0f});
    stone.actions[SoundAction::Place] = g;
    grass.actions[SoundAction::Step] = g;
    wood.actions[SoundAction::Break] = g;

    EXPECT_TRUE(SoundRegistry::global().registerSoundSet("reg_multi_stone", std::move(stone)));
    EXPECT_TRUE(SoundRegistry::global().registerSoundSet("reg_multi_grass", std::move(grass)));
    EXPECT_TRUE(SoundRegistry::global().registerSoundSet("reg_multi_wood", std::move(wood)));

    EXPECT_EQ(SoundRegistry::global().size(), 3u);

    auto* s = SoundRegistry::global().getSoundSet("reg_multi_stone");
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->hasAction(SoundAction::Place));
    EXPECT_FALSE(s->hasAction(SoundAction::Step));
}

// ============================================================================
// BlockType SoundSet Integration
// ============================================================================

TEST(BlockTypeSoundTest, DefaultSoundSetIsInvalid) {
    BlockType bt;
    EXPECT_FALSE(bt.soundSet().isValid());
}

TEST(BlockTypeSoundTest, SetAndGetSoundSet) {
    auto id = SoundSetId::fromName("bt_sound_test");
    BlockType bt;
    bt.setSoundSet(id);
    EXPECT_EQ(bt.soundSet(), id);
}

TEST(BlockTypeSoundTest, ChainingWorks) {
    auto id = SoundSetId::fromName("bt_chain_test");
    BlockType bt;
    bt.setHardness(2.0f).setSoundSet(id).setOpaque(true);
    EXPECT_EQ(bt.soundSet(), id);
    EXPECT_FLOAT_EQ(bt.hardness(), 2.0f);
    EXPECT_TRUE(bt.isOpaque());
}

// ============================================================================
// SoundLoader Config Parsing (using ConfigParser directly)
// ============================================================================

TEST(SoundLoaderTest, ParseSoundConfig) {
    ConfigParser parser;
    auto doc = parser.parseString(
        "place: sounds/stone/place1.wav\n"
        "place: sounds/stone/place2.wav\n"
        "place: sounds/stone/place3.wav\n"
        "break: sounds/stone/break1.wav\n"
        "step: sounds/stone/step1.wav\n"
        "step: sounds/stone/step2.wav\n"
        "volume: 0.8\n"
        "pitch-variance: 0.15\n"
    );

    // Verify we can read all place entries
    auto placeEntries = doc.getAll("place");
    EXPECT_EQ(placeEntries.size(), 3u);
    EXPECT_EQ(placeEntries[0]->value.asString(), "sounds/stone/place1.wav");
    EXPECT_EQ(placeEntries[1]->value.asString(), "sounds/stone/place2.wav");
    EXPECT_EQ(placeEntries[2]->value.asString(), "sounds/stone/place3.wav");

    auto breakEntries = doc.getAll("break");
    EXPECT_EQ(breakEntries.size(), 1u);

    auto stepEntries = doc.getAll("step");
    EXPECT_EQ(stepEntries.size(), 2u);

    EXPECT_NEAR(doc.getFloat("volume"), 0.8f, 0.01f);
    EXPECT_NEAR(doc.getFloat("pitch-variance"), 0.15f, 0.01f);
}

TEST(SoundLoaderTest, EmptyConfigHasNoActions) {
    ConfigParser parser;
    auto doc = parser.parseString("# Just a comment\n");

    EXPECT_TRUE(doc.getAll("place").empty());
    EXPECT_TRUE(doc.getAll("break").empty());
    EXPECT_TRUE(doc.getAll("step").empty());
}
