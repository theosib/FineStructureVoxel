#include <gtest/gtest.h>

#include "finevox/render/block_atlas.hpp"

#include <finevk/core/instance.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/device/physical_device.hpp>

#include <stb_image_write.h>

#include <filesystem>
#include <set>
#include <vector>

using namespace finevox;
using namespace finevox::render;
namespace fs = std::filesystem;

namespace {

struct Device {
    finevk::InstancePtr instance;
    finevk::PhysicalDevice physical;
    finevk::LogicalDevicePtr logical;
};

Device makeDevice() {
    Device d;
    d.instance = finevk::Instance::create().applicationName("finevox_block_atlas_test").headless().build();
    d.physical = d.instance->selectPhysicalDevice();
    d.logical = d.physical.createLogicalDevice().build();
    return d;
}

void writePng(const fs::path& path, int size, uint8_t shade) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4, shade);
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    fs::create_directories(path.parent_path());
    stbi_write_png(path.string().c_str(), size, size, 4, pixels.data(), size * 4);
}

class BlockAtlasTest : public ::testing::Test {
protected:
    fs::path dir;
    std::unordered_map<uint32_t, BlockTextureSet> blocks;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("finevox_block_atlas_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(dir);
        BlockTextureSet grass;
        for (auto& f : grass.faces) f = "grass_side";
        grass.faces[static_cast<int>(Face::PosY)] = "grass_top";
        grass.faces[static_cast<int>(Face::NegY)] = "dirt";
        blocks[BlockTypeId::fromName("atlas_test_grass").id] = grass;
        BlockTextureSet stone;
        for (auto& f : stone.faces) f = "stone";
        blocks[BlockTypeId::fromName("atlas_test_stone").id] = stone;
    }
    void TearDown() override { fs::remove_all(dir); }

    BlockAtlas build(finevk::LogicalDevice* device, const fs::path& root, std::set<std::string>* asked = nullptr) {
        BlockAtlas atlas;
        atlas.buildFromTextures(device, device->defaultCommandPool(),
                                [&](const std::string& name) {
                                    if (asked) asked->insert(name);
                                    return (root / (name + ".png")).string();
                                },
                                blocks, {"marker@90"});
        return atlas;
    }
};

}  // namespace

TEST(BlockAtlasTileSize, IsThePowerOfTwoThatHoldsTheLargestPicture) {
    EXPECT_EQ(BlockAtlas::tileSizeFor(0), 32) << "nothing loaded";
    EXPECT_EQ(BlockAtlas::tileSizeFor(16), 32) << "never below the old size";
    EXPECT_EQ(BlockAtlas::tileSizeFor(32), 32);
    EXPECT_EQ(BlockAtlas::tileSizeFor(33), 64);
    EXPECT_EQ(BlockAtlas::tileSizeFor(64), 64);
    EXPECT_EQ(BlockAtlas::tileSizeFor(100), 128);
    EXPECT_EQ(BlockAtlas::tileSizeFor(128), 128);
    EXPECT_EQ(BlockAtlas::tileSizeFor(4096), 128) << "capped";
}

// The promise a texture pack relies on: the same names land at the same UVs whatever the size of
// the pictures, so swapping a pack never needs new meshes
TEST_F(BlockAtlasTest, TheSameNamesLineUpAtEverySize) {
    Device d = makeDevice();
    for (const char* name : {"grass_side", "grass_top", "dirt", "stone", "marker"}) {
        writePng(dir / "small" / (std::string(name) + ".png"), 32, 100);
        writePng(dir / "mixed" / (std::string(name) + ".png"), std::string(name) == "dirt" ? 64 : 32, 100);
        writePng(dir / "huge" / (std::string(name) + ".png"), 512, 100);
    }
    std::set<std::string> asked;
    BlockAtlas small = build(d.logical.get(), dir / "small", &asked);
    BlockAtlas mixed = build(d.logical.get(), dir / "mixed");
    BlockAtlas huge = build(d.logical.get(), dir / "huge");

    EXPECT_EQ(small.tileSize(), 32);
    EXPECT_EQ(mixed.tileSize(), 64);
    EXPECT_EQ(huge.tileSize(), 128);
    EXPECT_EQ(small.width(), small.gridWidth() * 36u);
    EXPECT_EQ(mixed.width(), mixed.gridWidth() * 72u);
    EXPECT_EQ(huge.width(), huge.gridWidth() * 144u);

    EXPECT_TRUE(small.sameLayout(mixed));
    EXPECT_TRUE(small.sameLayout(huge));
    EXPECT_EQ(small.getUV(BlockTypeId::fromName("atlas_test_grass"), Face::PosY),
              huge.getUV(BlockTypeId::fromName("atlas_test_grass"), Face::PosY));
    EXPECT_EQ(asked, (std::set<std::string>{"grass_side", "grass_top", "dirt", "stone", "marker"}))
        << "each picture asked for once, by its name without a turn";
}

TEST_F(BlockAtlasTest, ADifferentSetOfNamesIsADifferentLayout) {
    Device d = makeDevice();
    for (const char* name : {"grass_side", "grass_top", "dirt", "stone", "marker", "extra"}) {
        writePng(dir / (std::string(name) + ".png"), 32, 100);
    }
    BlockAtlas a = build(d.logical.get(), dir);
    BlockTextureSet more;
    for (auto& f : more.faces) f = "extra";
    blocks[BlockTypeId::fromName("atlas_test_extra").id] = more;
    BlockAtlas b = build(d.logical.get(), dir);
    EXPECT_FALSE(a.sameLayout(b));
}

// A missing picture is drawn magenta at the tile size, not a reason to fail
TEST_F(BlockAtlasTest, AMissingPictureStillBuilds) {
    Device d = makeDevice();
    writePng(dir / "stone.png", 64, 100);
    BlockAtlas atlas = build(d.logical.get(), dir);
    EXPECT_TRUE(atlas.isLoaded());
    EXPECT_EQ(atlas.tileSize(), 64);
}
