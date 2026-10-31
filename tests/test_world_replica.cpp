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

TEST_F(WorldReplicaTest, APredictionShowsAtOnceAndStaysIfTheServerAgrees) {
    lookFrom(16);
    BlockCoord spot(16, GROUND_Y, 16);

    replica->predictBlock(spot, marker);
    EXPECT_EQ(replica->world().getBlock(spot), marker);

    world.setBlock(spot, marker);
    for (int i = 0; i < 10; ++i) replica->tick(0.033f);
    EXPECT_EQ(replica->world().getBlock(spot), marker);
}

TEST_F(WorldReplicaTest, TheServerCorrectsAPredictionItDidntAllow) {
    lookFrom(16);
    BlockCoord spot(16, GROUND_Y, 16);

    replica->predictBlock(spot, marker);
    for (int i = 0; i < 2; ++i) replica->tick(0.033f);
    EXPECT_EQ(replica->world().getBlock(spot), marker);  // Time to act on the command first

    // The server's version of this block never changed, so only the prediction can undo it
    for (int i = 0; i < 2; ++i) replica->tick(0.033f);
    EXPECT_TRUE(replica->world().getBlock(spot).isAir());
}

TEST_F(WorldReplicaTest, AnotherChangeNearbyDoesntUndoAPrediction) {
    lookFrom(16);
    BlockCoord spot(16, GROUND_Y, 16);
    BlockCoord other(17, GROUND_Y, 16);

    replica->predictBlock(spot, marker);
    world.setBlock(other, stone);  // A Hand working next door, in the same subchunk
    replica->syncAll();

    EXPECT_EQ(replica->world().getBlock(other), stone);
    EXPECT_EQ(replica->world().getBlock(spot), marker);
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

TEST_F(WorldReplicaTest, TickingEventuallyCoversTheMap) {
    vision->addSource(VIEWER, glm::dvec3(100.5, GROUND_Y + 2.0, 16.5), 20.0f);
    vision->refresh(VIEWER);
    for (int i = 0; i < 40; ++i) replica->tick(1.0f / 30.0f);
    EXPECT_EQ(replica->world().getBlock(100, GROUND_Y - 1, 16), stone);
}
