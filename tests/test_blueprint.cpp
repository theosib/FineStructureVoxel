#include <gtest/gtest.h>
#include "finevox/worldgen/blueprint.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>

using namespace finevox;
using namespace finevox::worldgen;

namespace {

const char* HUT = R"(# A hut
p = planks
t = thatch
---
ppp
p p
ppp
---
ttt
t_t
ttt
)";

BlockTypeId planks() { return BlockTypeId::fromName("planks"); }
BlockTypeId thatch() { return BlockTypeId::fromName("thatch"); }

}  // namespace

TEST(BlueprintParse, ReadsLayersBottomUp) {
    std::string error;
    auto schematic = parseBlueprint(HUT, &error);
    ASSERT_TRUE(schematic.has_value()) << error;
    EXPECT_EQ(schematic->size(), glm::ivec3(3, 2, 3));
    EXPECT_EQ(schematic->at(0, 0, 0).typeName, "planks");
    EXPECT_EQ(schematic->at(0, 1, 0).typeName, "thatch");
    EXPECT_TRUE(schematic->at(1, 0, 1).isAir()) << "space keeps the world as it is";
}

TEST(BlueprintParse, RejectsUnknownSymbols) {
    std::string error;
    EXPECT_FALSE(parseBlueprint("p = planks\n---\npq\n", &error).has_value());
    EXPECT_NE(error.find('q'), std::string::npos);
}

TEST(BlueprintParse, RejectsFilesWithNoLayers) {
    EXPECT_FALSE(parseBlueprint("p = planks\n").has_value());
}

class BlueprintSiteTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto parsed = parseBlueprint(HUT);
        ASSERT_TRUE(parsed.has_value());
        schematic = std::make_unique<Schematic>(std::move(*parsed));
    }

    std::unique_ptr<Schematic> schematic;
    World world;
    const BlockCoord origin{10, 20, 30};
};

TEST_F(BlueprintSiteTest, ProgressCountsCellsAlreadyInTheWorld) {
    BlueprintSite site(*schematic, origin, 0);
    EXPECT_EQ(site.progress(world).total, site.cells().size());
    // The clear cell in the roof already matches an empty world
    EXPECT_EQ(site.progress(world).done, 1u);

    world.setBlock(origin, planks());
    EXPECT_EQ(site.progress(world).done, 2u);
}

TEST_F(BlueprintSiteTest, BuildsBottomUpAndNeverHandsOutACellTwice) {
    BlueprintSite site(*schematic, origin, 0);

    std::vector<BlockCoord> first;
    for (EntityId agent = 1; agent <= 8; ++agent) {
        auto cell = site.claimNext(world, glm::dvec3(10.0, 20.0, 30.0), agent);
        ASSERT_TRUE(cell.has_value());
        EXPECT_EQ(cell->pos.y, origin.y) << "must finish the floor before the roof";
        for (const auto& seen : first) EXPECT_NE(seen, cell->pos);
        first.push_back(cell->pos);
    }
    EXPECT_FALSE(site.claimNext(world, glm::dvec3(0.0), 9).has_value())
        << "all eight floor cells are taken and the roof must wait";

    for (const auto& pos : first) world.setBlock(pos, planks());
    auto roof = site.claimNext(world, glm::dvec3(0.0), 9);
    ASSERT_TRUE(roof.has_value());
    EXPECT_EQ(roof->pos.y, origin.y + 1);
}

TEST_F(BlueprintSiteTest, ReleasedClaimsGoBackInThePool) {
    BlueprintSite site(*schematic, origin, 0);
    for (EntityId agent = 1; agent <= 8; ++agent) {
        ASSERT_TRUE(site.claimNext(world, glm::dvec3(0.0), agent).has_value());
    }
    site.releaseAgent(3);
    EXPECT_TRUE(site.claimNext(world, glm::dvec3(0.0), 9).has_value());
}

TEST_F(BlueprintSiteTest, ADestroyedBlockBecomesWorkAgain) {
    BlueprintSite site(*schematic, origin, 0);
    for (const auto& cell : site.cells()) world.setBlock(cell.pos, cell.block);
    EXPECT_TRUE(site.progress(world).complete());

    world.setBlock(origin, BlockTypeId());
    EXPECT_FALSE(site.progress(world).complete());
    auto cell = site.claimNext(world, glm::dvec3(0.0), 1);
    ASSERT_TRUE(cell.has_value());
    EXPECT_EQ(cell->pos, origin);
}

TEST_F(BlueprintSiteTest, AClearCellRemovesWhateverIsThere) {
    BlueprintSite site(*schematic, origin, 0);
    BlockCoord chimney(origin.x + 1, origin.y + 1, origin.z + 1);
    world.setBlock(chimney, thatch());
    auto remaining = site.remaining(world);
    auto it = std::find_if(remaining.begin(), remaining.end(),
                           [&](const SiteCell& c) { return c.pos == chimney; });
    ASSERT_NE(it, remaining.end());
    EXPECT_TRUE(it->block.isAir());
}

TEST_F(BlueprintSiteTest, RotationTurnsTheFootprintAndKeepsTheOrigin) {
    auto l = parseBlueprint("p = planks\n---\nppp\np\n");
    ASSERT_TRUE(l.has_value());
    BlueprintSite turned(*l, origin, 1);
    int32_t maxX = origin.x, maxZ = origin.z;
    for (const auto& cell : turned.cells()) {
        EXPECT_GE(cell.pos.x, origin.x);
        EXPECT_GE(cell.pos.z, origin.z);
        maxX = std::max(maxX, cell.pos.x);
        maxZ = std::max(maxZ, cell.pos.z);
    }
    EXPECT_EQ(maxX - origin.x, 1) << "3x2 becomes 2x3";
    EXPECT_EQ(maxZ - origin.z, 2);
}

TEST_F(BlueprintSiteTest, CellsCanBeAddedByHand) {
    BlueprintSite site;
    site.addCell({1, 2, 3}, planks());
    site.addCell({1, 3, 3}, planks());
    EXPECT_EQ(site.progress(world).total, 2u);
    auto cell = site.claimNext(world, glm::dvec3(0.0), 1);
    ASSERT_TRUE(cell.has_value());
    EXPECT_EQ(cell->pos, BlockCoord(1, 2, 3));
}

TEST_F(BlueprintSiteTest, APositionKeepsItsFirstTarget) {
    BlueprintSite site;
    site.addCell({1, 2, 3}, planks());
    site.addCell({1, 2, 3}, BlockTypeId());  // A foundation clear over the same spot
    ASSERT_EQ(site.cells().size(), 1u);

    world.setBlock({1, 2, 3}, planks());
    EXPECT_TRUE(site.progress(world).complete()) << "workers must not clear what they just built";
}

TEST(BlueprintLibraryTest, LooksUpByName) {
    BlueprintLibrary library;
    auto schematic = parseBlueprint(HUT);
    ASSERT_TRUE(schematic.has_value());
    library.add("hut", std::move(*schematic));
    EXPECT_NE(library.find("hut"), nullptr);
    EXPECT_EQ(library.find("palace"), nullptr);
    EXPECT_EQ(library.names(), std::vector<std::string>{"hut"});
}
