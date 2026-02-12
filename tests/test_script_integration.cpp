#include <gtest/gtest.h>

#include "finevox/script/finevox_interner.hpp"
#include "finevox/script/script_cache.hpp"
#include "finevox/script/block_context_proxy.hpp"
#include "finevox/script/data_container_proxy.hpp"
#include "finevox/script/script_block_handler.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/block_model.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Helper: write a temp script file
// ============================================================================

class ScriptIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = std::filesystem::temp_directory_path() / "finevox_script_test";
        std::filesystem::create_directories(tempDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir_);
    }

    std::string writeScript(const std::string& name, const std::string& content) {
        auto path = tempDir_ / name;
        std::ofstream out(path);
        out << content;
        out.close();
        return path.string();
    }

    std::filesystem::path tempDir_;
};

// ============================================================================
// FineVoxInterner Tests
// ============================================================================

TEST_F(ScriptIntegrationTest, InternerBridgeRoundtrip) {
    FineVoxInterner interner;

    // Intern via the adapter
    uint32_t id = interner.intern("test_block");

    // Should match direct StringInterner access
    auto directId = StringInterner::global().intern("test_block");
    EXPECT_EQ(id, directId);

    // Lookup should return the same string
    EXPECT_EQ(interner.lookup(id), "test_block");
}

TEST_F(ScriptIntegrationTest, InternerBridgeSharedSymbols) {
    FineVoxInterner interner;

    // Intern through finevox first
    auto fvId = StringInterner::global().intern("shared_symbol");

    // Should get the same ID through the adapter
    auto fsId = interner.intern("shared_symbol");
    EXPECT_EQ(fvId, fsId);
}

TEST_F(ScriptIntegrationTest, InternerBridgeWithScriptEngine) {
    finescript::ScriptEngine engine;
    FineVoxInterner interner;
    engine.setInterner(&interner);

    // Intern through the engine
    uint32_t engineId = engine.intern("engine_symbol");

    // Should match direct access
    auto directId = StringInterner::global().intern("engine_symbol");
    EXPECT_EQ(engineId, directId);

    // Lookup through engine
    EXPECT_EQ(engine.lookupSymbol(engineId), "engine_symbol");
}

// ============================================================================
// DataContainerProxy Tests
// ============================================================================

TEST_F(ScriptIntegrationTest, DataContainerProxyGetSet) {
    DataContainer container;
    DataContainerProxy proxy(container);

    auto& si = StringInterner::global();
    uint32_t powerKey = si.intern("power_level");
    uint32_t nameKey = si.intern("owner_name");

    // Set via proxy
    proxy.set(powerKey, finescript::Value::integer(15));
    proxy.set(nameKey, finescript::Value::string("Alice"));

    // Read back via proxy
    auto power = proxy.get(powerKey);
    EXPECT_TRUE(power.isInt());
    EXPECT_EQ(power.asInt(), 15);

    auto name = proxy.get(nameKey);
    EXPECT_TRUE(name.isString());
    EXPECT_EQ(name.asString(), "Alice");

    // Verify underlying DataContainer was updated
    EXPECT_EQ(container.get<int64_t>(powerKey), 15);
    EXPECT_EQ(container.get<std::string>(nameKey), "Alice");
}

TEST_F(ScriptIntegrationTest, DataContainerProxySymbolValue) {
    DataContainer container;
    DataContainerProxy proxy(container);

    auto& si = StringInterner::global();
    uint32_t facingKey = si.intern("facing");
    uint32_t northId = si.intern("north");

    proxy.set(facingKey, finescript::Value::symbol(northId));

    auto val = proxy.get(facingKey);
    EXPECT_TRUE(val.isSymbol());
    EXPECT_EQ(val.asSymbol(), northId);
}

TEST_F(ScriptIntegrationTest, DataContainerProxyRemove) {
    DataContainer container;
    DataContainerProxy proxy(container);

    auto& si = StringInterner::global();
    uint32_t key = si.intern("temp_data");

    proxy.set(key, finescript::Value::integer(42));
    EXPECT_TRUE(proxy.has(key));

    // Setting nil removes the key
    proxy.set(key, finescript::Value::nil());
    EXPECT_FALSE(proxy.has(key));
}

TEST_F(ScriptIntegrationTest, DataContainerProxyKeys) {
    DataContainer container;
    DataContainerProxy proxy(container);

    auto& si = StringInterner::global();
    uint32_t k1 = si.intern("key_a");
    uint32_t k2 = si.intern("key_b");

    proxy.set(k1, finescript::Value::integer(1));
    proxy.set(k2, finescript::Value::integer(2));

    auto ks = proxy.keys();
    EXPECT_EQ(ks.size(), 2u);
    EXPECT_TRUE(std::find(ks.begin(), ks.end(), k1) != ks.end());
    EXPECT_TRUE(std::find(ks.begin(), ks.end(), k2) != ks.end());
}

// ============================================================================
// ScriptCache Tests
// ============================================================================

TEST_F(ScriptIntegrationTest, ScriptCacheLoadAndReload) {
    finescript::ScriptEngine engine;
    FineVoxInterner interner;
    engine.setInterner(&interner);
    ScriptCache cache(engine);

    auto path = writeScript("test.fsc", "set x 42\n");

    auto* script1 = cache.load(path);
    ASSERT_NE(script1, nullptr);

    // Loading again (no change) should succeed
    auto* script2 = cache.load(path);
    ASSERT_NE(script2, nullptr);

    // Modify file and reload
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    writeScript("test.fsc", "set x 99\n");

    // reloadChanged should detect the change
    size_t reloaded = cache.reloadChanged();
    EXPECT_GE(reloaded, 1u);
}

TEST_F(ScriptIntegrationTest, ScriptCacheNonexistentFile) {
    finescript::ScriptEngine engine;
    FineVoxInterner interner;
    engine.setInterner(&interner);
    ScriptCache cache(engine);

    auto* script = cache.load("/nonexistent/path/file.fsc");
    EXPECT_EQ(script, nullptr);
}

// ============================================================================
// Script Execution Tests (using GameScriptEngine)
// ============================================================================

TEST_F(ScriptIntegrationTest, LoadBlockScriptWithHandlers) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("test_block.fsc", R"(
on :place do
    set data.placed true
end

on :break do
    set data.broken true
end
)");

    auto* handler = gse.loadBlockScript(path, "test:scripted_block");
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->hasHandlers());
    EXPECT_EQ(handler->name(), "test:scripted_block");
}

TEST_F(ScriptIntegrationTest, LoadBlockScriptNoHandlers) {
    World world;
    GameScriptEngine gse(world);

    // Script with no event handlers
    auto path = writeScript("no_handlers.fsc", R"(
set x 42
)");

    auto* handler = gse.loadBlockScript(path, "test:no_handlers");
    EXPECT_EQ(handler, nullptr);
}

TEST_F(ScriptIntegrationTest, ScriptEngineInternerShared) {
    World world;
    GameScriptEngine gse(world);

    // Intern through game script engine
    uint32_t id = gse.engine().intern("gse_test_symbol");

    // Should match finevox's StringInterner
    EXPECT_EQ(id, StringInterner::global().intern("gse_test_symbol"));
}

// ============================================================================
// BlockModel script field
// ============================================================================

TEST_F(ScriptIntegrationTest, BlockModelScriptField) {
    BlockModel model;
    EXPECT_FALSE(model.hasScript());
    EXPECT_TRUE(model.script().empty());

    model.setScript("blocks/test_block");
    EXPECT_TRUE(model.hasScript());
    EXPECT_EQ(model.script(), "blocks/test_block");
}
