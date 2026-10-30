#include <gtest/gtest.h>
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/faction_vision.hpp"
#include "finevox/core/world_replica.hpp"

using namespace finevox;

namespace {

constexpr int GROUND_Y = 20;
constexpr uint16_t VIEWER = 1;

class WorldReplicaTest : public ::testing::Test {
protected:
    void SetUp() override {
        stone = BlockTypeId::fromName("replica_stone");
        marker = BlockTypeId::fromName("replica_marker");
        BlockType type;
        type.setCollisionShape(CollisionShape::FULL_BLOCK);
        BlockRegistry::global().registerOrUpdateType(stone, type);
        BlockRegistry::global().registerOrUpdateType(marker, type);

        for (int x = 0; x < 128; ++x) {
            for (int z = 0; z < 32; ++z) world.setBlock(x, GROUND_Y - 1, z, stone);
        }
        entities = std::make_unique<EntityManager>(world, graphics);
        vision = std::make_unique<FactionVision>(world, *entities, VisionBounds{0, 0, 128, 32});
        vision->addFaction(VIEWER);
        replica = std::make_unique<WorldReplica>(world, *vision, VIEWER);
    }

    /// A lookout at x, moved each call by adding and removing a source
    void lookFrom(int x) {
        if (source != 0) vision->removeSource(source);
        source = vision->addSource(VIEWER, glm::dvec3(x + 0.5, GROUND_Y + 2.0, 16.5), 20.0f);
        vision->refresh(VIEWER);
        replica->syncAll();
    }

    World world;
    GraphicsEventQueue graphics;
    std::unique_ptr<EntityManager> entities;
    std::unique_ptr<FactionVision> vision;
    std::unique_ptr<WorldReplica> replica;
    int64_t source = 0;
    BlockTypeId stone;
    BlockTypeId marker;
};

}  // namespace

TEST_F(WorldReplicaTest, StartsEmpty) {
    EXPECT_EQ(replica->world().getBlock(20, GROUND_Y - 1, 16), BlockTypeId());
}

TEST_F(WorldReplicaTest, SeenTerrainArrives) {
    lookFrom(20);
    EXPECT_EQ(replica->world().getBlock(20, GROUND_Y - 1, 16), stone);
}

TEST_F(WorldReplicaTest, UnseenTerrainNeverArrives) {
    lookFrom(20);
    EXPECT_EQ(replica->world().getBlock(100, GROUND_Y - 1, 16), BlockTypeId());
    EXPECT_EQ(replica->columnsKnown() > 0, true);
}

TEST_F(WorldReplicaTest, RememberedTerrainStaysStaleUntilSeenAgain) {
    lookFrom(20);
    lookFrom(100);  // Walk away

    // An enemy quarries a moat where nobody's looking
    world.setBlock(20, GROUND_Y - 1, 16, BlockTypeId());
    replica->syncAll();
    EXPECT_EQ(replica->world().getBlock(20, GROUND_Y - 1, 16), stone) << "memory, not reality";

    lookFrom(20);
    EXPECT_EQ(replica->world().getBlock(20, GROUND_Y - 1, 16), BlockTypeId())
        << "a scout returning sees the truth";
}

TEST_F(WorldReplicaTest, ChangesInViewReplicate) {
    lookFrom(20);
    world.setBlock(21, GROUND_Y, 16, marker);
    replica->syncAll();
    EXPECT_EQ(replica->world().getBlock(21, GROUND_Y, 16), marker);
}

TEST_F(WorldReplicaTest, RevealAllShowsEverything) {
    replica->setRevealAll(true);
    replica->syncAll();
    EXPECT_EQ(replica->world().getBlock(100, GROUND_Y - 1, 16), stone);
}

TEST_F(WorldReplicaTest, TickingEventuallyCoversTheMap) {
    vision->addSource(VIEWER, glm::dvec3(100.5, GROUND_Y + 2.0, 16.5), 20.0f);
    vision->refresh(VIEWER);
    for (int i = 0; i < 40; ++i) replica->tick(1.0f / 30.0f);
    EXPECT_EQ(replica->world().getBlock(100, GROUND_Y - 1, 16), stone);
}
