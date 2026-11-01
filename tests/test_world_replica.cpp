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

TEST_F(WorldReplicaTest, AnExpectedChangeArrivesWithoutWaitingForTheSweep) {
    lookFrom(16);
    BlockCoord spot(16, GROUND_Y, 16);
    BlockCoord trunk(19, GROUND_Y + 3, 18);  // The rest of what the action did, in this column

    replica->expectChange(spot);
    world.setBlock(trunk, stone);
    EXPECT_TRUE(replica->world().getBlock(trunk).isAir());

    for (int i = 0; i < 4; ++i) replica->tick(0.033f);
    EXPECT_EQ(replica->world().getBlock(trunk), stone);
}

TEST_F(WorldReplicaTest, AnExpectedChangeOutOfSightStaysHidden) {
    lookFrom(16);
    BlockCoord far(100, GROUND_Y, 16);
    world.setBlock(far, marker);

    // A client can't learn about a column by claiming it's about to change
    replica->expectChange(far);
    for (int i = 0; i < 4; ++i) replica->tick(0.033f);
    EXPECT_TRUE(replica->world().getBlock(far).isAir());
}

TEST_F(WorldReplicaTest, LightComesAcrossWithTheBlocks) {
    world.getOrCreateColumn(ColumnPos(1, 1)).getOrCreateSubChunk(1);
    SubChunk* source = world.getSubChunk(ChunkPos(1, 1, 1));
    ASSERT_NE(source, nullptr);
    source->setSkyLight(3, 3, 3, 9);

    lookFrom(16);
    const SubChunk* copy = replica->world().getSubChunk(ChunkPos(1, 1, 1));
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->getSkyLight(3, 3, 3), 9);
}

TEST_F(WorldReplicaTest, ALightChangeAloneCounts) {
    lookFrom(16);
    (void)replica->takeChanges();

    SubChunk* source = world.getSubChunk(ChunkPos(1, 1, 1));
    ASSERT_NE(source, nullptr);
    source->setSkyLight(3, 3, 3, 4);
    replica->syncAll();

    EXPECT_EQ(replica->world().getSubChunk(ChunkPos(1, 1, 1))->getSkyLight(3, 3, 3), 4);
    auto changed = replica->takeChanges();
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], ChunkPos(1, 1, 1));
}

TEST_F(WorldReplicaTest, ChangesAreReportedOnce) {
    lookFrom(16);
    EXPECT_FALSE(replica->takeChanges().empty());
    EXPECT_TRUE(replica->takeChanges().empty());

    replica->syncAll();
    EXPECT_TRUE(replica->takeChanges().empty()) << "nothing changed";

    world.setBlock(17, GROUND_Y, 16, marker);
    world.setBlock(18, GROUND_Y, 16, marker);
    replica->syncAll();
    EXPECT_EQ(replica->takeChanges().size(), 1u) << "two blocks in one subchunk";
}

TEST_F(WorldReplicaTest, RefreshLightReachesTerrainOutOfSight) {
    lookFrom(16);
    lookFrom(100);
    (void)replica->takeChanges();

    SubChunk* source = world.getSubChunk(ChunkPos(1, 1, 1));
    source->setSkyLight(3, 3, 3, 2);
    replica->refreshLight();
    EXPECT_EQ(replica->world().getSubChunk(ChunkPos(1, 1, 1))->getSkyLight(3, 3, 3), 2);
}

TEST_F(WorldReplicaTest, ClearedFluidClearsInTheCopy) {
    lookFrom(16);
    auto water = FluidTypeId::fromName("replica_water");
    SubChunk* source = world.getSubChunk(ChunkPos(1, 1, 1));
    ASSERT_TRUE(source->setFluid(2, 2, 2, water, 8));
    replica->syncAll();
    EXPECT_EQ(replica->world().getSubChunk(ChunkPos(1, 1, 1))->getFluid(2, 2, 2), water);

    source->removeFluid(2, 2, 2);
    replica->syncAll();
    EXPECT_TRUE(replica->world().getSubChunk(ChunkPos(1, 1, 1))->getFluid(2, 2, 2).isEmpty());
}

TEST_F(WorldReplicaTest, TickingEventuallyCoversTheMap) {
    vision->addSource(VIEWER, glm::dvec3(100.5, GROUND_Y + 2.0, 16.5), 20.0f);
    vision->refresh(VIEWER);
    for (int i = 0; i < 40; ++i) replica->tick(1.0f / 30.0f);
    EXPECT_EQ(replica->world().getBlock(100, GROUND_Y - 1, 16), stone);
}
