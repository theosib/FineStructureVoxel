/**
 * @file test_block_mover.cpp
 * @brief Which blocks a mover takes along, and moving them as one change
 */

#include <gtest/gtest.h>

#include "finevox/core/block_mover.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>

using namespace finevox;

namespace {

BlockTypeId id(const char* name) { return BlockTypeId::fromName(std::string("mover_test:") + name); }

void defineAll() {
    auto define = [](const char* name, MoveRule rule, const char* grip = nullptr) {
        BlockType bt;
        bt.setMoveRule(rule);
        if (grip) bt.setGripGroup(StringInterner::global().intern(grip));
        BlockRegistry::global().registerOrUpdateType(id(name), bt);
    };
    define("stone", MoveRule::Moves);
    define("wall", MoveRule::Fixed);
    define("flower", MoveRule::Breaks);
    define("tile", MoveRule::NoDrag);
    define("glue", MoveRule::Moves, "glue");
    define("gum", MoveRule::Moves, "gum");
}

class BlockMover : public ::testing::Test {
protected:
    void SetUp() override { defineAll(); }

    void put(int x, int y, int z, const char* name) { world.setBlock(BlockCoord(x, y, z), id(name)); }
    BlockTypeId at(int x, int y, int z) const { return world.getBlock(BlockCoord(x, y, z)); }

    std::optional<MovePlan> plan(BlockCoord start, Face dir, bool pull = false, int limit = 64,
                                 std::optional<BlockCoord> anchor = std::nullopt) {
        MoveRequest r;
        r.start = start;
        r.dir = dir;
        r.pull = pull;
        r.limit = limit;
        r.anchor = anchor;
        return planMove(world, r);
    }

    bool move(BlockCoord start, Face dir, bool pull = false, int limit = 64,
              std::optional<BlockCoord> anchor = std::nullopt) {
        auto p = plan(start, dir, pull, limit, anchor);
        if (!p) return false;
        applyMove(world, *p, dir);
        return true;
    }

    World world;
};

bool has(const std::vector<BlockCoord>& cells, BlockCoord c) {
    return std::find(cells.begin(), cells.end(), c) != cells.end();
}

}  // namespace

TEST_F(BlockMover, PushesALineUpToTheFirstGap) {
    put(0, 5, 0, "stone");
    put(1, 5, 0, "stone");
    put(3, 5, 0, "stone");  // past the gap: stays
    std::vector<BlockCoord> changed;
    auto p = plan(BlockCoord(0, 5, 0), Face::PosX);
    ASSERT_TRUE(p);
    EXPECT_EQ(p->moves.size(), 2u);
    applyMove(world, *p, Face::PosX, &changed);
    EXPECT_TRUE(at(0, 5, 0).isAir());
    EXPECT_EQ(at(1, 5, 0), id("stone"));
    EXPECT_EQ(at(2, 5, 0), id("stone"));
    EXPECT_EQ(at(3, 5, 0), id("stone"));
    EXPECT_EQ(changed.size(), 3u) << "each changed cell once";
}

TEST_F(BlockMover, AFixedBlockPushedStopsTheWholeMove) {
    put(0, 5, 0, "stone");
    put(1, 5, 0, "wall");
    EXPECT_FALSE(move(BlockCoord(0, 5, 0), Face::PosX));
    EXPECT_EQ(at(0, 5, 0), id("stone"));
    EXPECT_FALSE(plan(BlockCoord(1, 5, 0), Face::PosX)) << "nor can the fixed block itself be pushed";
}

TEST_F(BlockMover, APushBreaksWhatBreaks) {
    put(0, 5, 0, "stone");
    put(1, 5, 0, "flower");
    auto p = plan(BlockCoord(0, 5, 0), Face::PosX);
    ASSERT_TRUE(p);
    EXPECT_EQ(p->moves.size(), 1u);
    ASSERT_EQ(p->breaks.size(), 1u);
    EXPECT_EQ(p->breaks[0], BlockCoord(1, 5, 0));
    applyMove(world, *p, Face::PosX);
    EXPECT_EQ(at(1, 5, 0), id("stone"));
    EXPECT_TRUE(at(2, 5, 0).isAir());

    put(5, 5, 0, "flower");
    p = plan(BlockCoord(5, 5, 0), Face::PosX);
    ASSERT_TRUE(p);
    EXPECT_TRUE(p->moves.empty());
    applyMove(world, *p, Face::PosX);
    EXPECT_TRUE(at(5, 5, 0).isAir()) << "the first block breaks when it's the one pushed";
}

TEST_F(BlockMover, AGrippingBlockDragsItsNeighboursOnEverySide) {
    put(0, 5, 0, "glue");
    put(0, 6, 0, "stone");   // above
    put(0, 4, 0, "stone");   // below
    put(0, 5, -1, "stone");  // beside
    put(0, 5, 1, "stone");   // beside
    put(-1, 5, 0, "stone");  // behind
    put(1, 6, 0, "stone");   // ahead of the one above: pushed by it
    ASSERT_TRUE(move(BlockCoord(0, 5, 0), Face::PosX));
    EXPECT_EQ(at(1, 5, 0), id("glue"));
    EXPECT_EQ(at(1, 6, 0), id("stone"));
    EXPECT_EQ(at(2, 6, 0), id("stone"));
    EXPECT_EQ(at(1, 4, 0), id("stone"));
    EXPECT_EQ(at(1, 5, -1), id("stone"));
    EXPECT_EQ(at(1, 5, 1), id("stone"));
    EXPECT_EQ(at(0, 5, 0), id("stone")) << "the one behind follows into the glue's cell";
    EXPECT_TRUE(at(-1, 5, 0).isAir());
    EXPECT_TRUE(at(0, 6, 0).isAir());
    EXPECT_TRUE(at(0, 4, 0).isAir());
    EXPECT_TRUE(at(0, 5, -1).isAir());
}

TEST_F(BlockMover, WhatCantBeDraggedStaysAndDoesntStopTheMove) {
    put(0, 5, 0, "glue");
    put(0, 6, 0, "wall");
    put(0, 4, 0, "flower");
    put(0, 5, 1, "tile");
    auto p = plan(BlockCoord(0, 5, 0), Face::PosX);
    ASSERT_TRUE(p);
    EXPECT_EQ(p->moves.size(), 1u);
    EXPECT_TRUE(p->breaks.empty()) << "a block beside is dragged, not pushed, so nothing breaks";
    applyMove(world, *p, Face::PosX);
    EXPECT_EQ(at(1, 5, 0), id("glue"));
    EXPECT_EQ(at(0, 6, 0), id("wall"));
    EXPECT_EQ(at(0, 4, 0), id("flower"));
    EXPECT_EQ(at(0, 5, 1), id("tile"));

    put(2, 5, 0, "tile");  // ahead: pushed, so it goes
    ASSERT_TRUE(move(BlockCoord(1, 5, 0), Face::PosX));
    EXPECT_EQ(at(3, 5, 0), id("tile"));
}

TEST_F(BlockMover, ADraggedBlockWithAFixedOneAheadStopsTheWholeMove) {
    put(0, 5, 0, "glue");
    put(0, 6, 0, "stone");
    put(1, 6, 0, "wall");
    EXPECT_FALSE(move(BlockCoord(0, 5, 0), Face::PosX));
    EXPECT_EQ(at(0, 5, 0), id("glue"));
    EXPECT_EQ(at(0, 6, 0), id("stone"));
}

TEST_F(BlockMover, GripsHoldTheirOwnGroupButNotAnother) {
    put(0, 5, 0, "glue");
    put(0, 6, 0, "glue");
    put(0, 7, 0, "stone");  // held by the upper glue
    put(0, 5, 1, "gum");
    put(0, 5, 2, "stone");  // held by the gum, which doesn't go
    ASSERT_TRUE(move(BlockCoord(0, 5, 0), Face::PosX));
    EXPECT_EQ(at(1, 5, 0), id("glue"));
    EXPECT_EQ(at(1, 6, 0), id("glue"));
    EXPECT_EQ(at(1, 7, 0), id("stone"));
    EXPECT_EQ(at(0, 5, 1), id("gum"));
    EXPECT_EQ(at(0, 5, 2), id("stone"));
    EXPECT_TRUE(at(1, 5, 1).isAir());
}

TEST_F(BlockMover, AGumPushedAheadStillGoes) {
    put(0, 5, 0, "glue");
    put(1, 5, 0, "gum");
    ASSERT_TRUE(move(BlockCoord(0, 5, 0), Face::PosX));
    EXPECT_EQ(at(1, 5, 0), id("glue"));
    EXPECT_EQ(at(2, 5, 0), id("gum"));
}

TEST_F(BlockMover, APullDragsTheFirstBlock) {
    // The mover at x=0 pulls toward itself (west) from x=2; x=1 is clear
    const BlockCoord mover(0, 5, 0);
    put(0, 5, 0, "stone");
    put(2, 5, 0, "tile");
    auto p = plan(BlockCoord(2, 5, 0), Face::NegX, true, 64, mover);
    ASSERT_TRUE(p);
    EXPECT_TRUE(p->moves.empty()) << "no-drag: a pull leaves it";

    put(2, 5, 0, "flower");
    p = plan(BlockCoord(2, 5, 0), Face::NegX, true, 64, mover);
    ASSERT_TRUE(p);
    EXPECT_TRUE(p->moves.empty() && p->breaks.empty()) << "a pull doesn't break";

    put(2, 5, 0, "glue");
    put(2, 6, 0, "stone");  // dragged by the glue
    put(1, 6, 0, "stone");  // pushed by that
    put(0, 6, 0, "glue");   // pushed in turn; it grips the mover, right below it
    put(0, 7, 0, "stone");  // dragged by the upper glue
    ASSERT_TRUE(move(BlockCoord(2, 5, 0), Face::NegX, true, 64, mover));
    EXPECT_EQ(at(1, 5, 0), id("glue"));
    EXPECT_EQ(at(1, 6, 0), id("stone"));
    EXPECT_EQ(at(0, 6, 0), id("stone"));
    EXPECT_EQ(at(-1, 6, 0), id("glue"));
    EXPECT_EQ(at(-1, 7, 0), id("stone"));
    EXPECT_EQ(at(0, 5, 0), id("stone")) << "the glue's grip doesn't take the mover";
    EXPECT_TRUE(at(-1, 5, 0).isAir());
}

TEST_F(BlockMover, APushIntoTheAnchorStopsTheMove) {
    // Glue wrapped round so a dragged block would go into the mover's cell
    const BlockCoord mover(0, 5, 0);
    put(0, 5, 0, "stone");
    put(1, 5, 0, "glue");
    put(1, 6, 0, "glue");
    put(0, 6, 0, "glue");
    put(-1, 6, 0, "glue");
    put(-1, 5, 0, "stone");  // dragged east by the glue above it, into the mover
    EXPECT_FALSE(plan(BlockCoord(1, 5, 0), Face::PosX, false, 64, mover));
}

TEST_F(BlockMover, MoreThanTheLimitMovesNothing) {
    put(0, 5, 0, "stone");
    put(1, 5, 0, "stone");
    put(2, 5, 0, "stone");
    EXPECT_FALSE(plan(BlockCoord(0, 5, 0), Face::PosX, false, 2));
    EXPECT_TRUE(plan(BlockCoord(0, 5, 0), Face::PosX, false, 3));
}

TEST_F(BlockMover, RotationAndDataGoWithTheirBlocks) {
    put(0, 5, 0, "glue");
    put(0, 5, 1, "stone");
    put(1, 5, 0, "stone");
    SubChunk* sc = world.getSubChunk(ChunkPos(0, 0, 0));
    ASSERT_NE(sc, nullptr);
    sc->setRotationIndex(BlockCoord(0, 5, 1).localIndex(), 5);
    sc->getOrCreateBlockData(BlockCoord(0, 5, 0).localIndex()).set("k", 7);
    sc->getOrCreateBlockData(BlockCoord(1, 5, 0).localIndex()).set("k", 9);
    ASSERT_TRUE(move(BlockCoord(0, 5, 0), Face::PosX));
    sc = world.getSubChunk(ChunkPos(0, 0, 0));
    EXPECT_EQ(sc->getRotationIndex(BlockCoord(1, 5, 1).localIndex()), 5);
    EXPECT_EQ(sc->getRotationIndex(BlockCoord(0, 5, 1).localIndex()), 0);
    ASSERT_NE(sc->blockData(BlockCoord(1, 5, 0).localIndex()), nullptr);
    EXPECT_EQ(sc->blockData(BlockCoord(1, 5, 0).localIndex())->get<int64_t>("k", 0), 7)
        << "the glue's data, not the stone's it replaced";
    ASSERT_NE(sc->blockData(BlockCoord(2, 5, 0).localIndex()), nullptr);
    EXPECT_EQ(sc->blockData(BlockCoord(2, 5, 0).localIndex())->get<int64_t>("k", 0), 9);
    EXPECT_EQ(sc->blockData(BlockCoord(0, 5, 0).localIndex()), nullptr);
}

TEST(MoveRuleNames, ReadTheirModelSpelling) {
    EXPECT_EQ(moveRuleNamed("moves"), MoveRule::Moves);
    EXPECT_EQ(moveRuleNamed("fixed"), MoveRule::Fixed);
    EXPECT_EQ(moveRuleNamed("breaks"), MoveRule::Breaks);
    EXPECT_EQ(moveRuleNamed("no-drag"), MoveRule::NoDrag);
    EXPECT_FALSE(moveRuleNamed("sideways"));
}
