// Biomes carried by subchunks, and faces coloured by them

#include <gtest/gtest.h>

#include "finevox/core/content_pack_loader.hpp"
#include "finevox/core/serialization.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"
#include "finevox/net/subchunk_codec.hpp"

#include <filesystem>
#include <fstream>

using namespace finevox;
namespace fs = std::filesystem;

namespace {

std::vector<uint32_t> stripes(uint32_t a, uint32_t b) {
    std::vector<uint32_t> ids(SubChunk::BIOME_CELLS);
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) ids[x + z * 16] = x < 8 ? a : b;
    }
    return ids;
}

uint32_t red(uint32_t c) { return c & 0xFF; }
uint32_t green(uint32_t c) { return (c >> 8) & 0xFF; }

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

}  // namespace

TEST(SubChunkBiomes, SetReadAndClear) {
    SubChunk sub;
    EXPECT_FALSE(sub.hasBiomes());
    EXPECT_EQ(sub.biome(3, 4), 0u);
    uint32_t a = StringInterner::global().intern("t:marsh");
    uint32_t b = StringInterner::global().intern("t:heath");
    uint64_t before = sub.lightVersion();
    sub.setBiomeData(stripes(a, b));
    EXPECT_TRUE(sub.hasBiomes());
    EXPECT_EQ(sub.biome(7, 15), a);
    EXPECT_EQ(sub.biome(8, 0), b);
    EXPECT_GT(sub.lightVersion(), before);  // so what's drawn is drawn again

    sub.setBiomeData(std::vector<uint32_t>(5, a));  // the wrong size is refused
    EXPECT_EQ(sub.biome(8, 0), b);
    sub.setBiomeData({});
    EXPECT_FALSE(sub.hasBiomes());
}

TEST(SubChunkBiomes, SurviveASave) {
    SubChunk sub;
    sub.setBlock(0, 0, 0, BlockTypeId::fromName("t:rock"));
    uint32_t a = StringInterner::global().intern("t:marsh");
    uint32_t b = StringInterner::global().intern("t:heath");
    sub.setBiomeData(stripes(a, b));
    auto restored = SubChunkSerializer::fromCBOR(SubChunkSerializer::toCBOR(sub, 3));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->biomeData(), sub.biomeData());

    SubChunk plain;
    plain.setBlock(0, 0, 0, BlockTypeId::fromName("t:rock"));
    auto none = SubChunkSerializer::fromCBOR(SubChunkSerializer::toCBOR(plain, 0));
    ASSERT_NE(none, nullptr);
    EXPECT_FALSE(none->hasBiomes());
}

TEST(SubChunkBiomes, CrossTheWire) {
    net::SubchunkData data;
    data.pos = ChunkPos(1, 2, 3);
    data.palette = {0};
    data.light.fill(0xF0);
    data.biomes = stripes(StringInterner::global().intern("t:marsh"), StringInterner::global().intern("t:heath"));
    for (bool compress : {false, true}) {
        net::SubchunkData back;
        ASSERT_TRUE(net::decodeSubchunk(net::encodeSubchunk(data, compress), back));
        EXPECT_EQ(back.biomes, data.biomes);
    }
    data.biomes.clear();
    net::SubchunkData back;
    ASSERT_TRUE(net::decodeSubchunk(net::encodeSubchunk(data, false), back));
    EXPECT_TRUE(back.biomes.empty());
}

class BiomeTint : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("finevox_tint_" + std::to_string(::getpid()));
        fs::remove_all(root_);
        // Two biomes with grass of different reds; a block tinted on top only, one tinted a
        // fixed colour, and one that isn't tinted at all
        write(root_ / "biomes/marsh.biome", "tint:grass: 1 0 0\n");
        write(root_ / "biomes/heath.biome", "tint:grass: 0 0 0\n");
        write(root_ / "blocks/turf.model", "texture: t\ntint: grass\ntint-color: 0 1 0\ntinted: up\n");
        write(root_ / "blocks/fir.model", "texture: t\ntint-color: 0 0 1\ntinted: all\n");
        write(root_ / "blocks/rock.model", "texture: t\n");
        loader_.loadBlocks(root_);
        loader_.loadBiomeTints(root_);
    }
    void TearDown() override { fs::remove_all(root_); }

    void fill(World& world, uint32_t west, uint32_t east) {
        // A ground of subchunks around the origin, west half one biome and east the other
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cz = -1; cz <= 1; ++cz) {
                world.setBlock(BlockCoord(cx * 16, 0, cz * 16), BlockTypeId::fromName("t:rock"));
                std::vector<uint32_t> ids(SubChunk::BIOME_CELLS, cx < 0 ? west : east);
                if (cx == 0) ids = stripes(west, east);
                world.getSubChunk(ChunkPos(cx, 0, cz))->setBiomeData(ids);
            }
        }
    }

    fs::path root_;
    ContentPackLoader loader_{"t"};
};

TEST_F(BiomeTint, BlendsTheBiomesAround) {
    World world;
    uint32_t marsh = StringInterner::global().intern("t:marsh");
    uint32_t heath = StringInterner::global().intern("t:heath");
    fill(world, marsh, heath);
    auto tint = loader_.tintProvider(world);
    ASSERT_TRUE(tint);
    BlockTypeId turf = BlockTypeId::fromName("t:turf");
    const int up = static_cast<int>(Face::PosY);

    // Deep in the marsh, all red; deep in the heath, black; at the border, between
    EXPECT_EQ(red(tint(turf, up, -1, BlockCoord(-8, 0, 0))), 255u);
    EXPECT_EQ(red(tint(turf, up, -1, BlockCoord(14, 0, 0))), 0u);
    uint32_t edge = red(tint(turf, up, -1, BlockCoord(8, 0, 0)));
    EXPECT_GT(edge, 0u);
    EXPECT_LT(edge, 255u);

    // Only the faces it says, and only blocks that are tinted
    EXPECT_EQ(tint(turf, static_cast<int>(Face::PosX), -1, BlockCoord(-8, 0, 0)), 0xFFFFFFFFu);
    EXPECT_EQ(tint(BlockTypeId::fromName("t:rock"), up, -1, BlockCoord(-8, 0, 0)), 0xFFFFFFFFu);
}

TEST_F(BiomeTint, FixedColourAndFallback) {
    World world;
    auto tint = loader_.tintProvider(world);
    // No biomes known anywhere: the block's own colour
    EXPECT_EQ(green(tint(BlockTypeId::fromName("t:turf"), static_cast<int>(Face::PosY), -1, BlockCoord(0, 0, 0))), 255u);
    uint32_t fir = tint(BlockTypeId::fromName("t:fir"), static_cast<int>(Face::NegX), -1, BlockCoord(0, 0, 0));
    EXPECT_EQ((fir >> 16) & 0xFF, 255u);
    EXPECT_EQ(red(fir), 0u);
}
