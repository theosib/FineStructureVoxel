#include <gtest/gtest.h>

#include "finevox/core/texture_packs.hpp"

#include <filesystem>
#include <fstream>

using namespace finevox;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& path, const std::string& text = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

class TexturePacksTest : public ::testing::Test {
protected:
    fs::path dir;
    fs::path game, user;

    void SetUp() override {
        dir = fs::temp_directory_path() / ("finevox_packs_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(dir);
        game = dir / "game";
        user = dir / "user";
        touch(game / "textures/blocks/stone.png");
        touch(game / "textures/blocks/sand.png");
        touch(game / "textures/particles/spark.png");
        touch(game / "ui/main.fs");
        touch(game / "ui/skin/hearth.fgs");
    }
    void TearDown() override { fs::remove_all(dir); }
};

}  // namespace

TEST_F(TexturePacksTest, FindsPacksAndReadsTheirConf) {
    touch(game / "packs/smooth/pack.conf",
          "name: Smooth ground\n"
          "description: Edges blend\n"
          "blending: true\n"
          "blend:sand: 30 0.25\n"
          "blend:stone: 12\n"
          "blend:flagstone: none\n");
    fs::create_directories(game / "packs/bare");  // a folder with nothing in it is still a pack
    TexturePacks packs;
    packs.scan({game / "packs"});

    ASSERT_EQ(packs.available().size(), 2u);
    const TexturePack* smooth = nullptr;
    for (const auto& pack : packs.available()) {
        if (pack.id == "smooth") smooth = &pack;
    }
    ASSERT_NE(smooth, nullptr);
    EXPECT_EQ(smooth->name, "Smooth ground");
    EXPECT_EQ(smooth->description, "Edges blend");
    EXPECT_TRUE(smooth->blending);
    ASSERT_EQ(smooth->blends.size(), 3u);
    ASSERT_TRUE(smooth->blends.at("sand"));
    EXPECT_EQ(smooth->blends.at("sand")->priority, 30);
    EXPECT_FLOAT_EQ(smooth->blends.at("sand")->width, 0.25f);
    EXPECT_FLOAT_EQ(smooth->blends.at("stone")->width, 0.35f) << "the default width";
    EXPECT_FALSE(smooth->blends.at("flagstone").has_value()) << "none: hard edges";
    for (const auto& pack : packs.available()) {
        if (pack.id == "bare") {
            EXPECT_EQ(pack.name, "bare") << "named by its folder";
            EXPECT_FALSE(pack.blending);
        }
    }
}

TEST_F(TexturePacksTest, APlayersPackReplacesTheGamesOfTheSameId) {
    touch(game / "packs/hd/pack.conf", "name: From the game\n");
    touch(user / "packs/hd/pack.conf", "name: The player's own\n");
    touch(user / "packs/mine/pack.conf", "name: Mine\n");
    TexturePacks packs;
    packs.scan({game / "packs", user / "packs"});
    ASSERT_EQ(packs.available().size(), 2u);
    ASSERT_TRUE(packs.select("hd"));
    EXPECT_EQ(packs.active()->name, "The player's own");
    EXPECT_EQ(packs.active()->root, user / "packs/hd");
}

TEST_F(TexturePacksTest, OnlyPicturesAndSkinsCanBeReplaced) {
    touch(game / "packs/p/textures/blocks/stone.png", "pack");
    touch(game / "packs/p/ui/main.fs", "pack");
    touch(game / "packs/p/ui/skin/hearth.fgs", "pack");
    touch(game / "packs/p/blocks/stone.model", "pack");
    TexturePacks packs;
    packs.scan({game / "packs"});

    EXPECT_EQ(packs.resolve(game, "textures/blocks/stone.png"), game / "textures/blocks/stone.png")
        << "nothing replaced with no pack chosen";

    ASSERT_TRUE(packs.select("p"));
    EXPECT_EQ(packs.resolve(game, "textures/blocks/stone.png"), game / "packs/p/textures/blocks/stone.png");
    EXPECT_EQ(packs.resolve(game, "textures/blocks/sand.png"), game / "textures/blocks/sand.png")
        << "what the pack hasn't got comes from the game";
    EXPECT_EQ(packs.resolve(game, "ui/skin/hearth.fgs"), game / "packs/p/ui/skin/hearth.fgs");
    EXPECT_EQ(packs.resolve(game, "ui/main.fs"), game / "ui/main.fs") << "never a script";
    EXPECT_EQ(packs.resolve(game, "blocks/stone.model"), game / "blocks/stone.model") << "never a block";

    EXPECT_TRUE(TexturePacks::replaceable("a/b.PNG"));
    EXPECT_TRUE(TexturePacks::replaceable("ui/skin/x.ttf"));
    EXPECT_FALSE(TexturePacks::replaceable("scripts/main.fsc"));
    EXPECT_FALSE(TexturePacks::replaceable("game.conf"));

    ASSERT_TRUE(packs.select(""));
    EXPECT_EQ(packs.active(), nullptr);
    EXPECT_EQ(packs.resolve(game, "textures/blocks/stone.png"), game / "textures/blocks/stone.png");
}

TEST_F(TexturePacksTest, AFolderListsTheGamesPicturesAndThePacks) {
    touch(game / "packs/p/textures/particles/spark.png");
    touch(game / "packs/p/textures/particles/ember.png");
    TexturePacks packs;
    packs.scan({game / "packs"});
    EXPECT_EQ(packs.list(game, "textures/particles", ".png"), (std::vector<std::string>{"spark"}));
    ASSERT_TRUE(packs.select("p"));
    EXPECT_EQ(packs.list(game, "textures/particles", ".png"), (std::vector<std::string>{"ember", "spark"}));
    EXPECT_TRUE(packs.list(game, "textures/nowhere", ".png").empty());
}

TEST_F(TexturePacksTest, AnUnknownPackLeavesNone) {
    touch(game / "packs/p/pack.conf");
    TexturePacks packs;
    packs.scan({game / "packs"});
    ASSERT_TRUE(packs.select("p"));
    EXPECT_FALSE(packs.select("gone"));
    EXPECT_EQ(packs.active(), nullptr);

    // A pack chosen, then taken away, is no longer chosen after the next look
    ASSERT_TRUE(packs.select("p"));
    fs::remove_all(game / "packs/p");
    packs.scan({game / "packs"});
    EXPECT_EQ(packs.active(), nullptr);
    EXPECT_TRUE(packs.available().empty());
}
