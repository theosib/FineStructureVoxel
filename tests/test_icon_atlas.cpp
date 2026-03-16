#include <gtest/gtest.h>

#include "finevox/render/icon_atlas.hpp"
#include "finevox/core/string_interner.hpp"

using namespace finevox;
using namespace finevox::render;

class IconAtlasTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure block type names are interned
        stoneId = BlockTypeId::fromName("stone");
        dirtId = BlockTypeId::fromName("dirt");
        grassId = BlockTypeId::fromName("grass");
    }

    BlockTypeId stoneId, dirtId, grassId;
};

TEST_F(IconAtlasTest, ManualRegistration) {
    IconAtlas atlas;
    atlas.registerIcon("stone", {0.0f, 0.0f}, {0.0625f, 0.0625f});

    auto* icon = atlas.getIcon("stone");
    ASSERT_NE(icon, nullptr);
    EXPECT_FLOAT_EQ(icon->uv0.x, 0.0f);
    EXPECT_FLOAT_EQ(icon->uv0.y, 0.0f);
    EXPECT_FLOAT_EQ(icon->uv1.x, 0.0625f);
    EXPECT_FLOAT_EQ(icon->uv1.y, 0.0625f);
}

TEST_F(IconAtlasTest, LookupMissingReturnsNull) {
    IconAtlas atlas;
    EXPECT_EQ(atlas.getIcon("nonexistent"), nullptr);
}

TEST_F(IconAtlasTest, HasIcon) {
    IconAtlas atlas;
    atlas.registerIcon("cobble", {0.1f, 0.0f}, {0.2f, 0.1f});

    EXPECT_TRUE(atlas.hasIcon("cobble"));
    EXPECT_FALSE(atlas.hasIcon("gold"));
}

TEST_F(IconAtlasTest, Size) {
    IconAtlas atlas;
    EXPECT_EQ(atlas.size(), 0u);

    atlas.registerIcon("stone", {0.0f, 0.0f}, {0.1f, 0.1f});
    atlas.registerIcon("dirt", {0.1f, 0.0f}, {0.2f, 0.1f});
    EXPECT_EQ(atlas.size(), 2u);
}

TEST_F(IconAtlasTest, DefaultTextureName) {
    IconAtlas atlas;
    EXPECT_EQ(atlas.textureName(), "block_atlas");
}

TEST_F(IconAtlasTest, CustomTextureName) {
    IconAtlas atlas;
    atlas.setTextureName("custom_icons");
    EXPECT_EQ(atlas.textureName(), "custom_icons");
}

TEST_F(IconAtlasTest, OverwriteIcon) {
    IconAtlas atlas;
    atlas.registerIcon("stone", {0.0f, 0.0f}, {0.1f, 0.1f});
    atlas.registerIcon("stone", {0.5f, 0.5f}, {0.6f, 0.6f});

    auto* icon = atlas.getIcon("stone");
    ASSERT_NE(icon, nullptr);
    EXPECT_FLOAT_EQ(icon->uv0.x, 0.5f);
    EXPECT_FLOAT_EQ(icon->uv1.x, 0.6f);
}
