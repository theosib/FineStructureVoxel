#include <gtest/gtest.h>

#include "finevox/script/finevox_interner.hpp"
#include "finevox/script/script_cache.hpp"
#include "finevox/script/block_context_proxy.hpp"
#include "finevox/script/data_container_proxy.hpp"
#include "finevox/script/script_block_handler.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/content_pack_loader.hpp"
#include "finevox/core/event_bus.hpp"
#include "finevox/core/block_events.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/block_model.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <finescript/map_data.h>

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

on :destroy do
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

// ============================================================================
// Phase R0 Tests: ud->blockCtx wiring, lazy DataContainer, DataContainerProxy
// ============================================================================

TEST_F(ScriptIntegrationTest, ScriptBlockHandlerWiresBlockCtxAndLazyDataContainer) {
    World world;
    GameScriptEngine gse(world);

    // Handler that calls ctx.* natives WITHOUT writing to data
    auto readOnlyPath = writeScript("readonly_block.fsc", R"(
on :place do
    ctx.set_rotation 5
    ctx.request_rebuild
end
)");

    auto* roHandler = gse.loadBlockScript(readOnlyPath, "test:ro_script_block");
    ASSERT_NE(roHandler, nullptr);

    BlockTypeId roBlockId = BlockTypeId::fromName("test:ro_script_block");
    BlockCoord pos{2, 3, 4};
    world.setBlock(pos, roBlockId);

    SubChunk* sc = world.getSubChunk(ChunkPos::fromBlock(pos));
    ASSERT_NE(sc, nullptr);
    uint64_t versionBefore = sc->blockVersion();

    // Negative control: before onPlace, rotation is 0 and no block data exists
    EXPECT_EQ(sc->getRotationIndex(pos.local()), 0);
    EXPECT_FALSE(sc->hasBlockData(pos.localIndex()));

    BlockContext ctx(world, *sc, pos, pos.local());
    roHandler->onPlace(ctx);

    // ctx.set_rotation and ctx.request_rebuild succeeded via ud->blockCtx
    EXPECT_EQ(sc->getRotationIndex(pos.local()), 5);
    EXPECT_GT(sc->blockVersion(), versionBefore);
    // And no empty DataContainer was allocated since data was not written!
    EXPECT_FALSE(sc->hasBlockData(pos.localIndex()));

    // Positive control: a handler that writes to data DOES lazily allocate DataContainer
    auto writeDataPath = writeScript("writedata_block.fsc", R"(
on :place do
    set data.active true
    set data.power 0
end
)");
    auto* rwHandler = gse.loadBlockScript(writeDataPath, "test:rw_script_block");
    ASSERT_NE(rwHandler, nullptr);

    BlockCoord pos2{5, 3, 4};
    world.setBlock(pos2, roBlockId);
    BlockContext ctx2(world, *sc, pos2, pos2.local());
    rwHandler->onPlace(ctx2);

    EXPECT_TRUE(sc->hasBlockData(pos2.localIndex()));
    const DataContainer* dc = sc->blockData(pos2.localIndex());
    ASSERT_NE(dc, nullptr);
    EXPECT_TRUE(dc->get<bool>("active", false));
    EXPECT_EQ(dc->get<int64_t>("power", -1), 0);
}

TEST_F(ScriptIntegrationTest, DataContainerProxyBoolArraysAndNestedMapsRoundTrip) {
    DataContainer container;
    DataContainerProxy proxy(container);
    auto& si = StringInterner::global();

    // 1. Booleans round-trip as Value::Type::Bool (not Int)
    uint32_t boolTrueKey = si.intern("flag_true");
    uint32_t boolFalseKey = si.intern("flag_false");
    proxy.set(boolTrueKey, finescript::Value::boolean(true));
    proxy.set(boolFalseKey, finescript::Value::boolean(false));

    auto vTrue = proxy.get(boolTrueKey);
    auto vFalse = proxy.get(boolFalseKey);
    EXPECT_TRUE(vTrue.isBool());
    EXPECT_FALSE(vTrue.isInt());
    EXPECT_TRUE(vTrue.asBool());
    EXPECT_TRUE(vFalse.isBool());
    EXPECT_FALSE(vFalse.asBool());

    // 2. Integer, float, and string arrays round-trip
    uint32_t intArrKey = si.intern("ints");
    proxy.set(intArrKey, finescript::Value::array({
        finescript::Value::integer(10),
        finescript::Value::integer(0),
        finescript::Value::integer(-5)
    }));
    auto vInts = proxy.get(intArrKey);
    ASSERT_TRUE(vInts.isArray());
    ASSERT_EQ(vInts.asArray().size(), 3u);
    EXPECT_EQ(vInts.asArray()[0].asInt(), 10);
    EXPECT_EQ(vInts.asArray()[1].asInt(), 0);
    EXPECT_EQ(vInts.asArray()[2].asInt(), -5);

    uint32_t fltArrKey = si.intern("floats");
    proxy.set(fltArrKey, finescript::Value::array({
        finescript::Value::number(1.5),
        finescript::Value::number(0.0)
    }));
    auto vFlts = proxy.get(fltArrKey);
    ASSERT_TRUE(vFlts.isArray());
    ASSERT_EQ(vFlts.asArray().size(), 2u);
    EXPECT_DOUBLE_EQ(vFlts.asArray()[0].asFloat(), 1.5);
    EXPECT_DOUBLE_EQ(vFlts.asArray()[1].asFloat(), 0.0);

    uint32_t strArrKey = si.intern("strings");
    proxy.set(strArrKey, finescript::Value::array({
        finescript::Value::string("alpha"),
        finescript::Value::string("")
    }));
    auto vStrs = proxy.get(strArrKey);
    ASSERT_TRUE(vStrs.isArray());
    ASSERT_EQ(vStrs.asArray().size(), 2u);
    EXPECT_EQ(vStrs.asArray()[0].asString(), "alpha");
    EXPECT_EQ(vStrs.asArray()[1].asString(), "");

    // 3. Nested maps round-trip as ProxyMap
    uint32_t nestedKey = si.intern("nested");
    uint32_t innerKey = si.intern("count");
    auto nestedMap = finescript::Value::map();
    nestedMap.asMap().set(innerKey, finescript::Value::integer(42));
    proxy.set(nestedKey, nestedMap);

    auto vNested = proxy.get(nestedKey);
    ASSERT_TRUE(vNested.isMap());
    EXPECT_EQ(vNested.asMap().get(innerKey).asInt(), 42);
}

TEST_F(ScriptIntegrationTest, BlockContextProxyExposesActorToScripts) {
    World world;
    GameScriptEngine gse(world);

    auto path = writeScript("actor_block.fsc", R"(
on :interact do
    set data.last_actor ctx.actor
end
)");

    auto* handler = gse.loadBlockScript(path, "test:actor_script_block");
    ASSERT_NE(handler, nullptr);

    BlockTypeId blockId = BlockTypeId::fromName("test:actor_script_block");
    BlockCoord pos{3, 4, 5};
    world.setBlock(pos, blockId);

    SubChunk* sc = world.getSubChunk(ChunkPos::fromBlock(pos));
    ASSERT_NE(sc, nullptr);

    BlockContext ctx(world, *sc, pos, pos.local());
    ctx.setActor(777);
    handler->onInteract(ctx, Face::PosY);

    const DataContainer* dc = sc->blockData(pos.localIndex());
    ASSERT_NE(dc, nullptr);
    EXPECT_EQ(dc->get<int64_t>("last_actor", 0), 777);
}

TEST_F(ScriptIntegrationTest, ContentPackLoaderBindsBlockScriptsAndRoutesUseBlock) {
    std::vector<std::filesystem::path> candidates = {
        "resources",
        "../resources",
        "../../resources",
        "/Users/theosib/projects/FineStructure/FineStructureVoxel/resources"
    };
    std::filesystem::path resDir;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c / "blocks" / "workbench.model")) {
            resDir = std::filesystem::canonical(c);
            break;
        }
    }
    ASSERT_FALSE(resDir.empty()) << "Failed to locate resources/blocks/workbench.model";

    auto session = GameSession::createLocal();
    World& world = session->world();

    {
        GameScriptEngine gse(world);
        ContentPackLoader loader;
        auto stats = loader.loadAll(resDir, &gse);
        EXPECT_EQ(stats.blocksLoaded, 10u);
        EXPECT_GE(stats.scriptedBlocksDiscovered, 1u);

        // Both "workbench" and "finevox:workbench" have handlers registered in BlockRegistry
        EXPECT_TRUE(BlockRegistry::global().hasHandler("workbench"));
        EXPECT_TRUE(BlockRegistry::global().hasHandler("finevox:workbench"));

        BlockCoord wbPos{4, 5, 6};
        world.setBlock(wbPos, BlockTypeId::fromName("finevox:workbench"));

        bool playerUseReceived = false;
        EntityId usedActor = INVALID_ENTITY_ID;
        BlockCoord usedPos{};
        session->eventBus().subscribe<PlayerUseEvent>([&](const PlayerUseEvent& ev) {
            playerUseReceived = true;
            usedPos = ev.pos;
            usedActor = ev.actor;
        });

        EntityId playerId = 42;
        EXPECT_TRUE(session->actions().useBlock(wbPos, Face::PosY, playerId));
        session->tick(1.0f / 30.0f);

        EXPECT_TRUE(playerUseReceived);
        EXPECT_EQ(usedPos, wbPos);
        EXPECT_EQ(usedActor, playerId);

        // Also verify ScriptBlockHandler::onInteract returns true on the workbench block
        SubChunk* sc = world.getSubChunk(ChunkPos::fromBlock(wbPos));
        ASSERT_NE(sc, nullptr);
        BlockContext ctx(world, *sc, wbPos, wbPos.local());
        ctx.setActor(playerId);
        BlockHandler* handler = BlockRegistry::global().getHandler("finevox:workbench");
        ASSERT_NE(handler, nullptr);
        EXPECT_TRUE(handler->onInteract(ctx, Face::PosY));
    }

    // After GameScriptEngine is destroyed, its external handlers are unregistered cleanly
    EXPECT_FALSE(BlockRegistry::global().hasHandler("workbench"));
    EXPECT_FALSE(BlockRegistry::global().hasHandler("finevox:workbench"));
}



