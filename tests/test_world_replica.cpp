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

TEST_F(WorldReplicaTest, EmptiedSubchunkClearsInTheCopy) {
    lookFrom(16);
    // Place a single block in subchunk (1, 4, 1) and sync
    world.setBlock(16, 64, 16, marker);
    replica->syncAll();
    (void)replica->takeChanges();
    EXPECT_EQ(replica->world().getBlock(16, 64, 16), marker);

    // Breaking the last block removes the subchunk from source via takeOut(chunkY)
    world.setBlock(16, 64, 16, AIR_BLOCK_TYPE);
    ASSERT_EQ(world.getSubChunk(ChunkPos(1, 4, 1)), nullptr);

    replica->syncAll();
    EXPECT_TRUE(replica->world().getBlock(16, 64, 16).isAir());
    auto changed = replica->takeChanges();
    EXPECT_NE(std::find(changed.begin(), changed.end(), ChunkPos(1, 4, 1)), changed.end());
}

TEST_F(WorldReplicaTest, TickingEventuallyCoversTheMap) {
    vision->addSource(VIEWER, glm::dvec3(100.5, GROUND_Y + 2.0, 16.5), 20.0f);
    vision->refresh(VIEWER);
    for (int i = 0; i < 40; ++i) replica->tick(1.0f / 30.0f);
    EXPECT_EQ(replica->world().getBlock(100, GROUND_Y - 1, 16), stone);
}

namespace {

/// A map far bigger than a tick's sweep, so only the block-change path can bring a change in fast
struct BigMap {
    BigMap() {
        stone = BlockTypeId::fromName("replica_stone");
        marker = BlockTypeId::fromName("replica_marker");
        BlockType type;
        type.setCollisionShape(CollisionShape::FULL_BLOCK);
        BlockRegistry::global().registerOrUpdateType(stone, type);
        BlockRegistry::global().registerOrUpdateType(marker, type);
        world = std::make_unique<World>();
        for (int x = SPOT - 8; x < SPOT + 8; ++x) {
            for (int z = SPOT - 8; z < SPOT + 8; ++z) world->setBlock(x, GROUND_Y - 1, z, stone);
        }
        entities = std::make_unique<EntityManager>(*world, graphics);
        vision = std::make_unique<FactionVision>(*world, *entities, VisionBounds{0, 0, 4096, 4096});
        vision->addFaction(VIEWER);
        replica = std::make_unique<WorldReplica>(*world, *vision, VIEWER);
    }
    void look() {
        vision->addSource(VIEWER, glm::dvec3(SPOT + 0.5, GROUND_Y + 2.0, SPOT + 0.5), 20.0f);
        vision->refresh(VIEWER);
        replica->syncAll();
        (void)replica->takeChanges();
    }

    static constexpr int SPOT = 3000;  // Far from where the sweep starts
    GraphicsEventQueue graphics;
    std::unique_ptr<World> world;
    std::unique_ptr<EntityManager> entities;
    std::unique_ptr<FactionVision> vision;
    std::unique_ptr<WorldReplica> replica;
    BlockTypeId stone;
    BlockTypeId marker;
};

}  // namespace

TEST(WorldReplicaChanges, ABlockChangeInViewArrivesOnTheNextTick) {
    BigMap map;
    map.look();
    ASSERT_EQ(map.replica->world().getBlock(BigMap::SPOT, GROUND_Y - 1, BigMap::SPOT), map.stone);

    // A chain of changes, one a tick, as a circuit makes them: each is there a tick later
    for (int i = 0; i < 5; ++i) {
        BlockCoord cell(BigMap::SPOT + i, GROUND_Y, BigMap::SPOT);
        map.world->setBlock(cell, map.marker);
        map.replica->tick(1.0f / 30.0f);
        EXPECT_EQ(map.replica->world().getBlock(cell), map.marker) << "change " << i;
        EXPECT_FALSE(map.replica->takeChanges().empty()) << "change " << i << " goes out";
    }
}

TEST(WorldReplicaChanges, ABlockChangeOutOfSightStaysRemembered) {
    BigMap map;
    map.look();
    BlockCoord far(BigMap::SPOT + 600, GROUND_Y, BigMap::SPOT);
    map.world->setBlock(far, map.marker);
    for (int i = 0; i < 10; ++i) map.replica->tick(1.0f / 30.0f);
    EXPECT_TRUE(map.replica->world().getBlock(far).isAir());
}

TEST(WorldReplicaChanges, AReplicaCanGoBeforeOrAfterItsWorld) {
    {
        BigMap map;
        map.look();
        map.replica.reset();
        map.world->setBlock(BigMap::SPOT, GROUND_Y, BigMap::SPOT, map.marker);  // Into a closed inbox
    }
    {
        BigMap map;
        map.look();
        map.entities.reset();
        map.vision.reset();
        map.world.reset();
        map.replica.reset();  // Mustn't reach for the world
    }
}

TEST(WorldReplicaChanges, LightABlockChangeSpreadsIntoTheNextColumnArrivesSoon) {
    BigMap map;
    map.world->setBlock(BigMap::SPOT + 8, GROUND_Y - 1, BigMap::SPOT, map.stone);  // Ground next door too
    map.look();
    for (int i = 0; i < 20; ++i) map.replica->tick(1.0f / 30.0f);  // Nothing left to look at again
    // A light put down at the east edge of its column: the lighting thread spreads it into the
    // next column a little later, where no block changed
    BlockCoord lamp(BigMap::SPOT + 7, GROUND_Y, BigMap::SPOT);  // 3007: the last cell of its column
    ASSERT_EQ((lamp.x + 1) >> 4, (lamp.x >> 4) + 1);
    map.world->setBlock(lamp, map.marker);
    map.replica->tick(1.0f / 30.0f);

    ChunkPos next((lamp.x + 1) >> 4, GROUND_Y >> 4, lamp.z >> 4);
    SubChunk* source = map.world->getSubChunk(next);
    ASSERT_NE(source, nullptr);
    source->setBlockLight(0, GROUND_Y & 15, lamp.z & 15, 9);

    for (int i = 0; i < 16; ++i) map.replica->tick(1.0f / 30.0f);
    const SubChunk* copy = map.replica->world().getSubChunk(next);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->getBlockLight(0, GROUND_Y & 15, lamp.z & 15), 9) << "not left for the sweep";
}
