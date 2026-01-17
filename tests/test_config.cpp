#include <gtest/gtest.h>
#include "finevox/config.hpp"
#include <filesystem>
#include <thread>

using namespace finevox;

class ConfigTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "finevox_test_config";
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        // Reset ConfigManager before deleting temp directory to avoid
        // destructor trying to save to a deleted path
        ConfigManager::instance().reset();
        std::filesystem::remove_all(tempDir);
    }
};

// ============================================================================
// ConfigManager Tests
// ============================================================================

TEST_F(ConfigTest, InitWithDefaults) {
    auto configPath = tempDir / "config.cbor";

    ConfigManager::instance().init(configPath);
    EXPECT_TRUE(ConfigManager::instance().isInitialized());

    // Check default values
    EXPECT_TRUE(ConfigManager::instance().compressionEnabled());
    EXPECT_FALSE(ConfigManager::instance().debugLogging());
    EXPECT_EQ(ConfigManager::instance().ioThreadCount(), 2);
    EXPECT_EQ(ConfigManager::instance().maxOpenRegions(), 16);
    EXPECT_EQ(ConfigManager::instance().columnCacheSize(), 64);
}

TEST_F(ConfigTest, ModifyAndSave) {
    auto configPath = tempDir / "config.cbor";

    ConfigManager::instance().init(configPath);

    // Modify settings
    ConfigManager::instance().setCompressionEnabled(false);
    ConfigManager::instance().setDebugLogging(true);
    ConfigManager::instance().setIOThreadCount(4);

    // Save
    EXPECT_TRUE(ConfigManager::instance().save());
    EXPECT_TRUE(std::filesystem::exists(configPath));
}

TEST_F(ConfigTest, GenericSetGet) {
    auto configPath = tempDir / "config.cbor";

    ConfigManager::instance().init(configPath);

    // Set custom values
    ConfigManager::instance().set("custom.string", std::string("hello"));
    ConfigManager::instance().set("custom.int", static_cast<int64_t>(42));
    ConfigManager::instance().set("custom.float", 3.14);
    ConfigManager::instance().set("custom.bool", true);

    // Get them back
    EXPECT_EQ(ConfigManager::instance().get<std::string>("custom.string"), "hello");
    EXPECT_EQ(ConfigManager::instance().get<int64_t>("custom.int"), 42);
    EXPECT_NEAR(ConfigManager::instance().get<double>("custom.float").value_or(0), 3.14, 0.001);
    EXPECT_EQ(ConfigManager::instance().get<bool>("custom.bool"), true);

    // Non-existent key
    EXPECT_FALSE(ConfigManager::instance().get<std::string>("nonexistent").has_value());

    // Has/remove
    EXPECT_TRUE(ConfigManager::instance().has("custom.string"));
    ConfigManager::instance().remove("custom.string");
    EXPECT_FALSE(ConfigManager::instance().has("custom.string"));
}

// ============================================================================
// WorldConfig Tests
// ============================================================================

TEST_F(ConfigTest, WorldConfigDefaults) {
    auto worldDir = tempDir / "world1";

    WorldConfig config(worldDir);

    EXPECT_EQ(config.worldName(), "New World");
    EXPECT_EQ(config.seed(), 0);
    EXPECT_GT(config.createdTimestamp(), 0);
    EXPECT_GT(config.lastPlayedTimestamp(), 0);
}

TEST_F(ConfigTest, WorldConfigSetGet) {
    auto worldDir = tempDir / "world1";

    WorldConfig config(worldDir);

    config.setWorldName("My World");
    config.setSeed(12345);

    EXPECT_EQ(config.worldName(), "My World");
    EXPECT_EQ(config.seed(), 12345);
}

TEST_F(ConfigTest, WorldConfigSaveLoad) {
    auto worldDir = tempDir / "world1";

    // Create and save
    {
        WorldConfig config(worldDir);
        config.setWorldName("Test World");
        config.setSeed(99999);
        EXPECT_TRUE(config.save());
    }

    // Load and verify
    {
        WorldConfig config(worldDir);
        EXPECT_EQ(config.worldName(), "Test World");
        EXPECT_EQ(config.seed(), 99999);
    }
}

TEST_F(ConfigTest, WorldConfigCompressionOverride) {
    auto configPath = tempDir / "config.cbor";
    auto worldDir = tempDir / "world1";

    // Initialize global config
    ConfigManager::instance().init(configPath);
    ConfigManager::instance().setCompressionEnabled(true);

    WorldConfig worldConfig(worldDir);

    // Should use global setting by default
    EXPECT_TRUE(worldConfig.compressionEnabled());

    // Override for this world
    worldConfig.setCompressionEnabled(false);
    EXPECT_FALSE(worldConfig.compressionEnabled());

    // Global is still true
    EXPECT_TRUE(ConfigManager::instance().compressionEnabled());

    // Clear override - should fall back to global
    worldConfig.clearCompressionOverride();
    EXPECT_TRUE(worldConfig.compressionEnabled());
}

TEST_F(ConfigTest, WorldConfigGenericData) {
    auto worldDir = tempDir / "world1";

    WorldConfig config(worldDir);

    config.set("custom.value", std::string("test"));
    config.set("player.spawn.x", static_cast<int64_t>(100));

    EXPECT_EQ(config.get<std::string>("custom.value"), "test");
    EXPECT_EQ(config.get<int64_t>("player.spawn.x"), 100);

    config.save();

    // Reload and verify
    WorldConfig config2(worldDir);
    EXPECT_EQ(config2.get<std::string>("custom.value"), "test");
    EXPECT_EQ(config2.get<int64_t>("player.spawn.x"), 100);
}

TEST_F(ConfigTest, WorldConfigUpdateLastPlayed) {
    auto worldDir = tempDir / "world1";

    WorldConfig config(worldDir);

    auto initial = config.lastPlayedTimestamp();

    // updateLastPlayed should update the timestamp to current time
    // Since timestamps are in seconds, verify it's >= initial (same second is OK)
    config.updateLastPlayed();

    EXPECT_GE(config.lastPlayedTimestamp(), initial);

    // Also verify the timestamp is recent (within last minute)
    auto now = std::chrono::system_clock::now();
    auto nowTs = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();
    EXPECT_LE(std::abs(config.lastPlayedTimestamp() - nowTs), 60);
}
