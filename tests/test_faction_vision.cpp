#include <gtest/gtest.h>
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/faction_vision.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"

using namespace finevox;

namespace {

constexpr int GROUND_Y = 10;
constexpr uint16_t BLUE = 1;
constexpr uint16_t RED = 2;

class FactionVisionTest : public ::testing::Test {
protected:
    void SetUp() override {
        stone = BlockTypeId::fromName("vision_stone");
        leaves = BlockTypeId::fromName("vision_leaves");
        BlockType stoneType;
        stoneType.setCollisionShape(CollisionShape::FULL_BLOCK);
        BlockRegistry::global().registerOrUpdateType(stone, stoneType);
        BlockType leafType;
        leafType.setOpaque(false);
        leafType.setNoCollision();
        BlockRegistry::global().registerOrUpdateType(leaves, leafType);

        EntityTypeRegistry::global().clear();
        EntityTypeDef def;
        def.name = "vision_scout";
        def.aiType = AIType::None;
        EntityTypeRegistry::global().registerType("vision_scout", std::move(def));

        for (int x = 0; x < 128; ++x) {
            for (int z = 0; z < 64; ++z) world.setBlock(x, GROUND_Y - 1, z, stone);
        }
        entities = std::make_unique<EntityManager>(world, graphics);
        vision = std::make_unique<FactionVision>(world, *entities, VisionBounds{0, 0, 128, 64});
        vision->addFaction(BLUE);
        vision->addFaction(RED);
    }

    EntityId spawn(uint16_t faction, int x, int z, int y = GROUND_Y) {
        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId::fromName("vision_scout"));
        mob->setPosition(Vec3(x + 0.5f, float(y), z + 0.5f));
        mob->setFaction(faction);
        return entities->spawnEntity(std::move(mob));
    }

    void wall(int x, int height, BlockTypeId block) {
        for (int z = 0; z < 64; ++z) {
            for (int y = 0; y < height; ++y) world.setBlock(x, GROUND_Y + y, z, block);
        }
    }

    World world;
    GraphicsEventQueue graphics;
    std::unique_ptr<EntityManager> entities;
    std::unique_ptr<FactionVision> vision;
    BlockTypeId stone;
    BlockTypeId leaves;
};

}  // namespace

TEST_F(FactionVisionTest, EverythingStartsUnknown) {
    EXPECT_EQ(vision->stateAt(BLUE, 40, 30), VisionState::Unknown);
    EXPECT_EQ(vision->countCells(BLUE, VisionState::Explored), 0u);
}

TEST_F(FactionVisionTest, UnitsSeeAroundThemselves) {
    spawn(BLUE, 40, 30);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 44, 30), VisionState::Visible);
    EXPECT_EQ(vision->stateAt(BLUE, 40, 44), VisionState::Visible);
    EXPECT_EQ(vision->stateAt(BLUE, 100, 30), VisionState::Unknown) << "beyond sight range";
}

TEST_F(FactionVisionTest, FactionsDoNotShareSight) {
    spawn(BLUE, 40, 30);
    vision->refresh(BLUE);
    vision->refresh(RED);
    EXPECT_EQ(vision->stateAt(RED, 44, 30), VisionState::Unknown);
}

TEST_F(FactionVisionTest, ExploredStaysAfterTheScoutLeaves) {
    EntityId id = spawn(BLUE, 20, 30);
    vision->refresh(BLUE);
    ASSERT_EQ(vision->stateAt(BLUE, 24, 30), VisionState::Visible);

    entities->getMob(id)->setPosition(Vec3(110.5f, GROUND_Y, 30.5f));
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 24, 30), VisionState::Explored);
    EXPECT_EQ(vision->stateAt(BLUE, 112, 30), VisionState::Visible);
}

TEST_F(FactionVisionTest, WallsBlockSight) {
    wall(50, 4, stone);
    spawn(BLUE, 44, 30);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 46, 30), VisionState::Visible);
    EXPECT_EQ(vision->stateAt(BLUE, 56, 30), VisionState::Unknown) << "behind the wall";
}

TEST_F(FactionVisionTest, ALowWallDoesNotBlockSight) {
    wall(50, 1, stone);
    spawn(BLUE, 44, 30);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 56, 30), VisionState::Visible);
}

TEST_F(FactionVisionTest, HeightSeesOverWallsAndFurther) {
    wall(50, 4, stone);
    // A tower behind the same wall
    for (int y = 0; y < 14; ++y) world.setBlock(44, GROUND_Y + y, 30, stone);
    spawn(BLUE, 44, 30, GROUND_Y + 14);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 56, 30), VisionState::Visible) << "sees over";
    EXPECT_EQ(vision->stateAt(BLUE, 70, 30), VisionState::Visible) << "past ground-level range";
}

TEST_F(FactionVisionTest, ThinFoliageThinsSightAndDenseFoliageBlocksIt) {
    wall(50, 3, leaves);
    spawn(BLUE, 44, 30);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 56, 30), VisionState::Visible) << "one layer of leaves";

    for (int x = 51; x <= 53; ++x) wall(x, 3, leaves);
    vision->refresh(BLUE);
    EXPECT_NE(vision->stateAt(BLUE, 60, 30), VisionState::Visible) << "a thicket";
}

TEST_F(FactionVisionTest, FixedSourcesSeeWithoutAnyUnit) {
    int64_t tower = vision->addSource(BLUE, glm::dvec3(60.5, GROUND_Y + 6.0, 30.5), 20.0f);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 64, 30), VisionState::Visible);

    vision->removeSource(tower);
    vision->refresh(BLUE);
    EXPECT_EQ(vision->stateAt(BLUE, 64, 30), VisionState::Explored);
}

TEST_F(FactionVisionTest, RevealMarksExploredWithoutLineOfSight) {
    vision->reveal(BLUE, glm::dvec3(60.0, GROUND_Y, 30.0), 10.0f);
    EXPECT_EQ(vision->stateAt(BLUE, 60, 30), VisionState::Explored);
}

TEST_F(FactionVisionTest, PositionsOutsideTheMapAreUnknown) {
    EXPECT_EQ(vision->stateAt(BLUE, -5, 30), VisionState::Unknown);
    EXPECT_EQ(vision->stateAt(BLUE, 500, 30), VisionState::Unknown);
}

TEST_F(FactionVisionTest, SnapshotMatchesQueries) {
    spawn(BLUE, 40, 30);
    vision->refresh(BLUE);
    auto cells = vision->snapshot(BLUE);
    ASSERT_EQ(cells.size(), size_t(vision->cellsX()) * vision->cellsZ());
    size_t index = size_t(30 / FactionVision::CELL_SIZE) * vision->cellsX() + 44 / FactionVision::CELL_SIZE;
    EXPECT_EQ(cells[index], static_cast<uint8_t>(VisionState::Visible));
}
