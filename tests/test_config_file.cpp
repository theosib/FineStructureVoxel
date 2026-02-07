#include <gtest/gtest.h>
#include "finevox/core/config_file.hpp"
#include <fstream>
#include <filesystem>

using namespace finevox;

class ConfigFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "finevox_config_file_test";
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir);
    }

    std::filesystem::path tempDir;
};

TEST_F(ConfigFileTest, LoadAndSave) {
    auto path = tempDir / "test.conf";

    // Create initial file
    {
        std::ofstream file(path);
        file << "name: Test\n";
        file << "count: 42\n";
    }

    // Load, modify, save
    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    EXPECT_EQ(config.getString("name"), "Test");
    EXPECT_EQ(config.getInt("count"), 42);

    config.set("count", int64_t(100));
    EXPECT_TRUE(config.save());

    // Reload and verify
    ConfigFile config2;
    EXPECT_TRUE(config2.load(path));
    EXPECT_EQ(config2.getString("name"), "Test");
    EXPECT_EQ(config2.getInt("count"), 100);
}

TEST_F(ConfigFileTest, PreservesComments) {
    auto path = tempDir / "comments.conf";

    // Create file with comments
    {
        std::ofstream file(path);
        file << "# This is a header comment\n";
        file << "# Another comment line\n";
        file << "\n";
        file << "name: Original\n";
        file << "\n";
        file << "# Comment before count\n";
        file << "count: 10\n";
        file << "\n";
        file << "# End comment\n";
    }

    // Load, modify, save
    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    config.set("count", int64_t(20));
    EXPECT_TRUE(config.save());

    // Read raw file and verify comments are preserved
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("# This is a header comment"), std::string::npos);
    EXPECT_NE(content.find("# Another comment line"), std::string::npos);
    EXPECT_NE(content.find("# Comment before count"), std::string::npos);
    EXPECT_NE(content.find("# End comment"), std::string::npos);
    EXPECT_NE(content.find("count: 20"), std::string::npos);
}

TEST_F(ConfigFileTest, PreservesOrdering) {
    auto path = tempDir / "ordering.conf";

    // Create file with specific order
    {
        std::ofstream file(path);
        file << "zebra: z\n";
        file << "apple: a\n";
        file << "middle: m\n";
    }

    // Load, modify middle value, save
    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    config.set("middle", std::string_view("modified"));
    EXPECT_TRUE(config.save());

    // Read raw file and check ordering is preserved
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto zebraPos = content.find("zebra:");
    auto applePos = content.find("apple:");
    auto middlePos = content.find("middle:");

    EXPECT_LT(zebraPos, applePos);
    EXPECT_LT(applePos, middlePos);
}

TEST_F(ConfigFileTest, NewKeysAppendedAtEnd) {
    auto path = tempDir / "append.conf";

    // Create file
    {
        std::ofstream file(path);
        file << "first: 1\n";
        file << "second: 2\n";
    }

    // Load, add new key, save
    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    config.set("third", int64_t(3));
    EXPECT_TRUE(config.save());

    // Verify new key is at end
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto firstPos = content.find("first:");
    auto secondPos = content.find("second:");
    auto thirdPos = content.find("third:");

    EXPECT_LT(firstPos, secondPos);
    EXPECT_LT(secondPos, thirdPos);
}

TEST_F(ConfigFileTest, RemoveCommentsOutLine) {
    auto path = tempDir / "remove.conf";

    // Create file
    {
        std::ofstream file(path);
        file << "keep: value\n";
        file << "remove: me\n";
    }

    // Load, remove key, save
    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    config.remove("remove");
    EXPECT_TRUE(config.save());

    // Verify key is commented out
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("keep: value"), std::string::npos);
    EXPECT_NE(content.find("# remove: me"), std::string::npos);
    EXPECT_FALSE(config.has("remove"));
}

TEST_F(ConfigFileTest, BooleanValues) {
    auto path = tempDir / "bools.conf";

    ConfigFile config;
    config.set("enabled", true);
    config.set("disabled", false);
    EXPECT_TRUE(config.saveAs(path));

    ConfigFile config2;
    EXPECT_TRUE(config2.load(path));
    EXPECT_TRUE(config2.getBool("enabled"));
    EXPECT_FALSE(config2.getBool("disabled"));
}

TEST_F(ConfigFileTest, FloatValues) {
    auto path = tempDir / "floats.conf";

    ConfigFile config;
    config.set("pi", 3.14159);
    EXPECT_TRUE(config.saveAs(path));

    ConfigFile config2;
    EXPECT_TRUE(config2.load(path));
    EXPECT_NEAR(config2.getFloat("pi"), 3.14159, 0.0001);
}

TEST_F(ConfigFileTest, HexValues) {
    auto path = tempDir / "hex.conf";

    // Create file with hex values
    {
        std::ofstream file(path);
        file << "color: 0xFF00FF\n";
        file << "small: 0x10\n";
    }

    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    EXPECT_EQ(config.getInt("color"), 0xFF00FF);
    EXPECT_EQ(config.getInt("small"), 16);
}

TEST_F(ConfigFileTest, DefaultValues) {
    ConfigFile config;  // Not loaded from file

    EXPECT_EQ(config.getString("missing", "default"), "default");
    EXPECT_EQ(config.getInt("missing", 42), 42);
    EXPECT_NEAR(config.getFloat("missing", 1.5), 1.5, 0.001);
    EXPECT_TRUE(config.getBool("missing", true));
}

TEST_F(ConfigFileTest, HasMethod) {
    auto path = tempDir / "has.conf";

    {
        std::ofstream file(path);
        file << "exists: yes\n";
    }

    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    EXPECT_TRUE(config.has("exists"));
    EXPECT_FALSE(config.has("missing"));
}

TEST_F(ConfigFileTest, IsDirtyTracking) {
    auto path = tempDir / "dirty.conf";

    {
        std::ofstream file(path);
        file << "value: 1\n";
    }

    ConfigFile config;
    EXPECT_TRUE(config.load(path));
    EXPECT_FALSE(config.isDirty());

    config.set("value", int64_t(2));
    EXPECT_TRUE(config.isDirty());

    config.save();
    EXPECT_FALSE(config.isDirty());
}
