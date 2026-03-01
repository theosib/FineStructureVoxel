#include <gtest/gtest.h>
#include "finevox/core/sound_event.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/entity.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_registry.hpp"

using namespace finevox;

// ============================================================================
// SoundEvent factory tests
// ============================================================================

TEST(FluidSound, SplashFactory) {
    SoundSetId set = SoundSetId::fromName("splash_test");
    auto event = SoundEvent::fluidSplash(set, glm::vec3(1.0f, 2.0f, 3.0f));

    EXPECT_EQ(event.soundSet, set);
    EXPECT_EQ(event.action, SoundAction::Splash);
    EXPECT_EQ(event.category, SoundCategory::Effects);
    EXPECT_FLOAT_EQ(event.posX, 1.0f);
    EXPECT_FLOAT_EQ(event.posY, 2.0f);
    EXPECT_FLOAT_EQ(event.posZ, 3.0f);
}

TEST(FluidSound, SwimFactory) {
    SoundSetId set = SoundSetId::fromName("swim_test");
    auto event = SoundEvent::fluidSwim(set, glm::vec3(4.0f, 5.0f, 6.0f));

    EXPECT_EQ(event.soundSet, set);
    EXPECT_EQ(event.action, SoundAction::Swim);
    EXPECT_EQ(event.category, SoundCategory::Effects);
    EXPECT_FLOAT_EQ(event.volume, 0.4f);
}

// ============================================================================
// Entity splash sound integration tests
// ============================================================================

class FluidSplashTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register test fluid type with sound
        FluidType water;
        water.name = "splash_water";
        water.id = FluidTypeId::fromName("splash_water");
        water.density = 1000.0f;
        water.soundSet = SoundSetId::fromName("water_splash_set");
        FluidRegistry::global().registerType("splash_water", water);
        waterId_ = FluidTypeId::fromName("splash_water");

        GameSessionConfig config;
        config.enableFluidSimulation = true;
        session_ = GameSession::createLocal(config);

        // Create columns and place water
        (void)session_->world().getOrCreateColumn({0, 0});
    }

    FluidTypeId waterId_;
    std::unique_ptr<GameSession> session_;
};

TEST_F(FluidSplashTest, EntityEnteringWaterPushesSplash) {
    // Place water at y=63
    session_->world().setFluid({0, 63, 0}, waterId_, 15);

    // Spawn entity above water
    auto entity = std::make_unique<Entity>(INVALID_ENTITY_ID, EntityType::Player);
    entity->setPosition(glm::vec3(0.5f, 65.0f, 0.5f));
    entity->setHalfExtents(glm::vec3(0.3f, 0.9f, 0.3f));
    entity->setHasGravity(true);
    EntityId eid = session_->entities().spawnEntity(std::move(entity));

    // Drain any existing sounds
    session_->soundEvents().drainAll();

    // The entity is above water — no splash yet
    // Manually set the entity position into the water
    Entity* e = session_->entities().getEntity(eid);
    ASSERT_NE(e, nullptr);
    e->setPosition(glm::vec3(0.5f, 63.0f, 0.5f));

    // Tick to process physics (will detect fluid contact)
    session_->tick(1.0f / 30.0f);

    // Check for splash sound
    auto sounds = session_->soundEvents().drainAll();
    bool hasSplash = false;
    for (const auto& s : sounds) {
        if (s.action == SoundAction::Splash &&
            s.soundSet == SoundSetId::fromName("water_splash_set")) {
            hasSplash = true;
            break;
        }
    }
    EXPECT_TRUE(hasSplash);
}

TEST_F(FluidSplashTest, PlaceFluidPushesPlaceSound) {
    // This was already tested in test_fluid_actions.cpp but verify the
    // SoundAction is Place (not Splash)
    session_->actions().placeFluid({0, 64, 0}, waterId_);
    auto sounds = session_->soundEvents().drainAll();
    ASSERT_FALSE(sounds.empty());
    EXPECT_EQ(sounds[0].action, SoundAction::Place);
}
