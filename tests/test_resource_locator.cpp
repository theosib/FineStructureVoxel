#include <gtest/gtest.h>
#include "finevox/core/resource_locator.hpp"
#include <filesystem>
#include <fstream>
#include <thread>

using namespace finevox;

class ResourceLocatorTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "finevox_test_locator";
        std::filesystem::create_directories(tempDir);

        // Reset locator state before each test
        ResourceLocator::instance().reset();
    }

    void TearDown() override {
        ResourceLocator::instance().reset();
        std::filesystem::remove_all(tempDir);
    }
};

// ============================================================================
// Root path tests
// ============================================================================

TEST_F(ResourceLocatorTest, SetAndGetRoots) {
    auto enginePath = tempDir / "engine";
    auto gamePath = tempDir / "game";
    auto userPath = tempDir / "user";

    ResourceLocator::instance().setEngineRoot(enginePath);
    ResourceLocator::instance().setGameRoot(gamePath);
    ResourceLocator::instance().setUserRoot(userPath);

    EXPECT_EQ(ResourceLocator::instance().engineRoot(), enginePath);
    EXPECT_EQ(ResourceLocator::instance().gameRoot(), gamePath);
    EXPECT_EQ(ResourceLocator::instance().userRoot(), userPath);
}

TEST_F(ResourceLocatorTest, ResolveEngineScope) {
    auto enginePath = tempDir / "engine";
    ResourceLocator::instance().setEngineRoot(enginePath);

    EXPECT_EQ(ResourceLocator::instance().resolve("engine"),
              enginePath);
    EXPECT_EQ(ResourceLocator::instance().resolve("engine/defaults.cbor"),
              enginePath / "defaults.cbor");
    EXPECT_EQ(ResourceLocator::instance().resolve("engine/subdir/file.txt"),
              enginePath / "subdir" / "file.txt");
}

TEST_F(ResourceLocatorTest, ResolveGameScope) {
    auto gamePath = tempDir / "game";
    ResourceLocator::instance().setGameRoot(gamePath);

    EXPECT_EQ(ResourceLocator::instance().resolve("game"),
              gamePath);
    EXPECT_EQ(ResourceLocator::instance().resolve("game/textures/stone.png"),
              gamePath / "textures" / "stone.png");
}

TEST_F(ResourceLocatorTest, ResolveUserScope) {
    auto userPath = tempDir / "user";
    ResourceLocator::instance().setUserRoot(userPath);

    EXPECT_EQ(ResourceLocator::instance().resolve("user"),
              userPath);
    EXPECT_EQ(ResourceLocator::instance().resolve("user/config.cbor"),
              userPath / "config.cbor");
}

TEST_F(ResourceLocatorTest, ResolveUnknownScopeReturnsEmpty) {
    EXPECT_TRUE(ResourceLocator::instance().resolve("unknown/path").empty());
    EXPECT_TRUE(ResourceLocator::instance().resolve("").empty());
}

TEST_F(ResourceLocatorTest, ResolveUnconfiguredScopeReturnsEmpty) {
    // No roots configured
    EXPECT_TRUE(ResourceLocator::instance().resolve("engine/file").empty());
    EXPECT_TRUE(ResourceLocator::instance().resolve("game/file").empty());
    EXPECT_TRUE(ResourceLocator::instance().resolve("user/file").empty());
}

// ============================================================================
// World management tests
// ============================================================================

TEST_F(ResourceLocatorTest, RegisterAndResolveWorld) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);

    EXPECT_TRUE(ResourceLocator::instance().hasWorld("MyWorld"));
    EXPECT_FALSE(ResourceLocator::instance().hasWorld("OtherWorld"));

    EXPECT_EQ(ResourceLocator::instance().worldPath("MyWorld"), worldPath);
    EXPECT_TRUE(ResourceLocator::instance().worldPath("OtherWorld").empty());
}

TEST_F(ResourceLocatorTest, ResolveWorldPaths) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);

    EXPECT_EQ(ResourceLocator::instance().resolve("world/MyWorld"),
              worldPath);
    EXPECT_EQ(ResourceLocator::instance().resolve("world/MyWorld/world.cbor"),
              worldPath / "world.cbor");
    EXPECT_EQ(ResourceLocator::instance().resolve("world/MyWorld/regions"),
              worldPath / "regions");
}

TEST_F(ResourceLocatorTest, UnregisterWorld) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);

    EXPECT_TRUE(ResourceLocator::instance().hasWorld("MyWorld"));

    ResourceLocator::instance().unregisterWorld("MyWorld");

    EXPECT_FALSE(ResourceLocator::instance().hasWorld("MyWorld"));
    EXPECT_TRUE(ResourceLocator::instance().resolve("world/MyWorld").empty());
}

TEST_F(ResourceLocatorTest, RegisteredWorldsList) {
    ResourceLocator::instance().registerWorld("World1", tempDir / "w1");
    ResourceLocator::instance().registerWorld("World2", tempDir / "w2");
    ResourceLocator::instance().registerWorld("World3", tempDir / "w3");

    auto worlds = ResourceLocator::instance().registeredWorlds();

    EXPECT_EQ(worlds.size(), 3);
    EXPECT_TRUE(std::find(worlds.begin(), worlds.end(), "World1") != worlds.end());
    EXPECT_TRUE(std::find(worlds.begin(), worlds.end(), "World2") != worlds.end());
    EXPECT_TRUE(std::find(worlds.begin(), worlds.end(), "World3") != worlds.end());
}

// ============================================================================
// Dimension tests
// ============================================================================

TEST_F(ResourceLocatorTest, OverworldDimensionAutoRegistered) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);

    // Overworld is auto-registered
    EXPECT_TRUE(ResourceLocator::instance().hasDimension("MyWorld", "overworld"));
}

TEST_F(ResourceLocatorTest, RegisterDimension) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);

    ResourceLocator::instance().registerDimension("MyWorld", "nether");
    ResourceLocator::instance().registerDimension("MyWorld", "the_end");

    EXPECT_TRUE(ResourceLocator::instance().hasDimension("MyWorld", "nether"));
    EXPECT_TRUE(ResourceLocator::instance().hasDimension("MyWorld", "the_end"));
    EXPECT_FALSE(ResourceLocator::instance().hasDimension("MyWorld", "unknown"));
}

TEST_F(ResourceLocatorTest, ResolveDimensionPaths) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);
    ResourceLocator::instance().registerDimension("MyWorld", "nether");

    // Dimension path uses dim/<name> by default
    EXPECT_EQ(ResourceLocator::instance().resolve("world/MyWorld/dim/nether"),
              worldPath / "dim" / "nether");
    EXPECT_EQ(ResourceLocator::instance().resolve("world/MyWorld/dim/nether/regions"),
              worldPath / "dim" / "nether" / "regions");
}

TEST_F(ResourceLocatorTest, RegisterDimensionWithCustomSubpath) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);

    // Custom subpath
    ResourceLocator::instance().registerDimension("MyWorld", "custom", "custom_dimension");

    EXPECT_EQ(ResourceLocator::instance().resolve("world/MyWorld/dim/custom"),
              worldPath / "custom_dimension");
}

TEST_F(ResourceLocatorTest, DimensionPath) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);
    ResourceLocator::instance().registerDimension("MyWorld", "nether");

    EXPECT_EQ(ResourceLocator::instance().dimensionPath("MyWorld", "nether"),
              worldPath / "dim" / "nether");
}

TEST_F(ResourceLocatorTest, RegionPath) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);
    ResourceLocator::instance().registerDimension("MyWorld", "nether");

    // Overworld regions are in world root
    EXPECT_EQ(ResourceLocator::instance().regionPath("MyWorld"),
              worldPath / "regions");
    EXPECT_EQ(ResourceLocator::instance().regionPath("MyWorld", "overworld"),
              worldPath / "regions");

    // Other dimensions use dim/<name>/regions
    EXPECT_EQ(ResourceLocator::instance().regionPath("MyWorld", "nether"),
              worldPath / "dim" / "nether" / "regions");
}

TEST_F(ResourceLocatorTest, UnregisterWorldRemovesDimensions) {
    auto worldPath = tempDir / "saves" / "MyWorld";
    ResourceLocator::instance().registerWorld("MyWorld", worldPath);
    ResourceLocator::instance().registerDimension("MyWorld", "nether");

    EXPECT_TRUE(ResourceLocator::instance().hasDimension("MyWorld", "nether"));

    ResourceLocator::instance().unregisterWorld("MyWorld");

    EXPECT_FALSE(ResourceLocator::instance().hasDimension("MyWorld", "nether"));
    EXPECT_FALSE(ResourceLocator::instance().hasDimension("MyWorld", "overworld"));
}

// ============================================================================
// Utility tests
// ============================================================================

TEST_F(ResourceLocatorTest, ExpandHomeDirectory) {
    // Test ~ expansion
    auto expanded = ResourceLocator::expandHome("~/test");
    EXPECT_NE(expanded.string().find("test"), std::string::npos);
    EXPECT_EQ(expanded.string().find("~"), std::string::npos);

    // Non-~ paths unchanged
    auto unchanged = ResourceLocator::expandHome("/absolute/path");
    EXPECT_EQ(unchanged, std::filesystem::path("/absolute/path"));

    auto relative = ResourceLocator::expandHome("relative/path");
    EXPECT_EQ(relative, std::filesystem::path("relative/path"));
}

TEST_F(ResourceLocatorTest, DefaultUserRoot) {
    auto defaultRoot = ResourceLocator::defaultUserRoot();
    EXPECT_FALSE(defaultRoot.empty());

    // Should contain "finevox" somewhere in the path
    EXPECT_NE(defaultRoot.string().find("finevox"), std::string::npos);
}

TEST_F(ResourceLocatorTest, ExistsCheck) {
    auto userPath = tempDir / "user";
    std::filesystem::create_directories(userPath);

    // Create a test file
    std::filesystem::create_directories(userPath);
    {
        std::ofstream f(userPath / "config.cbor");
        f << "test";
    }

    ResourceLocator::instance().setUserRoot(userPath);

    EXPECT_TRUE(ResourceLocator::instance().exists("user/config.cbor"));
    EXPECT_FALSE(ResourceLocator::instance().exists("user/nonexistent.cbor"));
    EXPECT_FALSE(ResourceLocator::instance().exists("unknown/path"));
}

TEST_F(ResourceLocatorTest, Reset) {
    ResourceLocator::instance().setEngineRoot(tempDir / "engine");
    ResourceLocator::instance().registerWorld("TestWorld", tempDir / "world");

    ResourceLocator::instance().reset();

    EXPECT_TRUE(ResourceLocator::instance().engineRoot().empty());
    EXPECT_FALSE(ResourceLocator::instance().hasWorld("TestWorld"));
}

// ============================================================================
// Thread safety (basic check)
// ============================================================================

TEST_F(ResourceLocatorTest, ConcurrentAccess) {
    ResourceLocator::instance().setEngineRoot(tempDir / "engine");
    ResourceLocator::instance().setUserRoot(tempDir / "user");
    ResourceLocator::instance().registerWorld("World", tempDir / "world");

    std::atomic<int> successCount{0};
    const int numThreads = 10;
    const int opsPerThread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < opsPerThread; ++i) {
                // Mix of reads (void cast to suppress nodiscard warnings)
                (void)ResourceLocator::instance().resolve("engine/file");
                (void)ResourceLocator::instance().resolve("user/config.cbor");
                (void)ResourceLocator::instance().resolve("world/World/regions");
                (void)ResourceLocator::instance().hasWorld("World");
                (void)ResourceLocator::instance().engineRoot();
                ++successCount;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(successCount, numThreads * opsPerThread);
}
