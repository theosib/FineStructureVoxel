#include <gtest/gtest.h>
#include "finevox/core/module.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_handler.hpp"
#include "finevox/core/entity_registry.hpp"
#include "finevox/core/item_registry.hpp"
#include "finevox/core/event_bus.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/light_provider.hpp"
#include "finevox/core/world.hpp"

using namespace finevox;

// ============================================================================
// Test Module Implementation
// ============================================================================

class TestModule : public GameModule {
public:
    explicit TestModule(std::string_view name, std::string_view version = "1.0.0")
        : name_(name), version_(version) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] std::string_view version() const override { return version_; }

    [[nodiscard]] std::vector<std::string_view> dependencies() const override {
        return dependencies_;
    }

    void onLoad(ModuleRegistry& registry) override {
        loadCalled = true;
        loadedNamespace = std::string(registry.moduleNamespace());
    }

    void onRegister(ModuleRegistry& registry) override {
        registerCalled = true;

        // Register a test block type
        BlockType testBlock;
        testBlock.setShape(CollisionShape::FULL_BLOCK);
        registry.blocks().registerType(registry.qualifiedName("test_block"), testBlock);
    }

    void onUnload() override {
        unloadCalled = true;
    }

    void addDependency(std::string_view dep) {
        depStrings_.push_back(std::string(dep));
        dependencies_.clear();
        for (const auto& s : depStrings_) {
            dependencies_.push_back(s);
        }
    }

    bool loadCalled = false;
    bool registerCalled = false;
    bool unloadCalled = false;
    std::string loadedNamespace;

private:
    std::string name_;
    std::string version_;
    std::vector<std::string> depStrings_;
    std::vector<std::string_view> dependencies_;
};

// ============================================================================
// Test Block Handler
// ============================================================================

class TestBlockHandler : public BlockHandler {
public:
    explicit TestBlockHandler(std::string_view name) : name_(name) {}

    [[nodiscard]] std::string_view name() const override { return name_; }

    void onPlace(BlockContext& ctx) override {
        placeCalled = true;
        (void)ctx;
    }

    bool onInteract(BlockContext& ctx, Face face) override {
        useCalled = true;
        lastUseFace = face;
        (void)ctx;
        return true;
    }

    bool placeCalled = false;
    bool useCalled = false;
    Face lastUseFace = Face::PosY;

private:
    std::string name_;
};

// ============================================================================
// Namespace Utility Tests
// ============================================================================

TEST(BlockRegistryNamespaceTest, ValidNamespacedNames) {
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("finevox:stone"));
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("mymod:custom_block"));
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("a:b"));
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("Test123:Block456"));
}

TEST(BlockRegistryNamespaceTest, InvalidNamespacedNames) {
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("stone"));           // No colon
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName(":stone"));          // Empty namespace
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("finevox:"));      // Empty local name
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("a:b:c"));           // Multiple colons
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("my-mod:block"));    // Hyphen not allowed
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("my.mod:block"));    // Dot not allowed
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName(""));                // Empty string
}

TEST(BlockRegistryNamespaceTest, GetNamespace) {
    EXPECT_EQ(BlockRegistry::getNamespace("finevox:stone"), "finevox");
    EXPECT_EQ(BlockRegistry::getNamespace("mymod:block"), "mymod");
    EXPECT_EQ(BlockRegistry::getNamespace("stone"), "");  // No namespace
}

TEST(BlockRegistryNamespaceTest, GetLocalName) {
    EXPECT_EQ(BlockRegistry::getLocalName("finevox:stone"), "stone");
    EXPECT_EQ(BlockRegistry::getLocalName("mymod:custom_block"), "custom_block");
    EXPECT_EQ(BlockRegistry::getLocalName("stone"), "stone");  // No namespace, returns full name
}

TEST(BlockRegistryNamespaceTest, MakeQualifiedName) {
    EXPECT_EQ(BlockRegistry::makeQualifiedName("finevox", "stone"), "finevox:stone");
    EXPECT_EQ(BlockRegistry::makeQualifiedName("mymod", "test"), "mymod:test");
}

// ============================================================================
// Block Handler Tests
// ============================================================================

TEST(BlockHandlerTest, RegisterAndRetrieveHandler) {
    auto handler = std::make_unique<TestBlockHandler>("testmod:handler_block");
    TestBlockHandler* rawPtr = handler.get();

    bool registered = BlockRegistry::global().registerHandler("testmod:handler_block", std::move(handler));
    EXPECT_TRUE(registered);

    BlockHandler* retrieved = BlockRegistry::global().getHandler("testmod:handler_block");
    EXPECT_EQ(retrieved, rawPtr);
}

TEST(BlockHandlerTest, CannotRegisterDuplicateHandler) {
    auto handler1 = std::make_unique<TestBlockHandler>("testmod:dup_handler");
    auto handler2 = std::make_unique<TestBlockHandler>("testmod:dup_handler");

    bool first = BlockRegistry::global().registerHandler("testmod:dup_handler", std::move(handler1));
    EXPECT_TRUE(first);

    bool second = BlockRegistry::global().registerHandler("testmod:dup_handler", std::move(handler2));
    EXPECT_FALSE(second);
}

TEST(BlockHandlerTest, HandlerFactory) {
    static int factoryCalls = 0;

    BlockRegistry::global().registerHandlerFactory("testmod:lazy_handler", []() {
        ++factoryCalls;
        return std::make_unique<TestBlockHandler>("testmod:lazy_handler");
    });

    EXPECT_EQ(factoryCalls, 0);  // Factory not called yet

    // First access triggers factory
    BlockHandler* handler = BlockRegistry::global().getHandler("testmod:lazy_handler");
    EXPECT_NE(handler, nullptr);
    EXPECT_EQ(factoryCalls, 1);

    // Second access returns cached handler (factory not called again)
    BlockHandler* handler2 = BlockRegistry::global().getHandler("testmod:lazy_handler");
    EXPECT_EQ(handler, handler2);
    EXPECT_EQ(factoryCalls, 1);
}

TEST(BlockHandlerTest, HasHandler) {
    EXPECT_FALSE(BlockRegistry::global().hasHandler("testmod:nonexistent"));

    auto handler = std::make_unique<TestBlockHandler>("testmod:has_handler_test");
    BlockRegistry::global().registerHandler("testmod:has_handler_test", std::move(handler));

    EXPECT_TRUE(BlockRegistry::global().hasHandler("testmod:has_handler_test"));
}

TEST(BlockHandlerTest, GetHandlerByBlockTypeId) {
    auto handler = std::make_unique<TestBlockHandler>("testmod:byid_block");
    TestBlockHandler* rawPtr = handler.get();
    BlockRegistry::global().registerHandler("testmod:byid_block", std::move(handler));

    BlockTypeId id = BlockTypeId::fromName("testmod:byid_block");
    BlockHandler* retrieved = BlockRegistry::global().getHandler(id);
    EXPECT_EQ(retrieved, rawPtr);
}

// ============================================================================
// Module Loader Tests
// ============================================================================

TEST(ModuleLoaderTest, RegisterBuiltinModule) {
    ModuleLoader loader;

    auto module = std::make_unique<TestModule>("testmod_builtin");
    TestModule* rawPtr = module.get();

    bool registered = loader.registerBuiltin(std::move(module));
    EXPECT_TRUE(registered);

    EXPECT_TRUE(loader.hasModule("testmod_builtin"));
    EXPECT_EQ(loader.getModule("testmod_builtin"), rawPtr);
    EXPECT_EQ(loader.moduleCount(), 1);
}

TEST(ModuleLoaderTest, CannotRegisterDuplicateModule) {
    ModuleLoader loader;

    auto module1 = std::make_unique<TestModule>("testmod_dup");
    auto module2 = std::make_unique<TestModule>("testmod_dup");

    EXPECT_TRUE(loader.registerBuiltin(std::move(module1)));
    EXPECT_FALSE(loader.registerBuiltin(std::move(module2)));
}

TEST(ModuleLoaderTest, InitializeCallsLifecycleMethods) {
    ModuleLoader loader;

    auto module = std::make_unique<TestModule>("testmod_lifecycle");
    TestModule* rawPtr = module.get();
    loader.registerBuiltin(std::move(module));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    EXPECT_FALSE(rawPtr->loadCalled);
    EXPECT_FALSE(rawPtr->registerCalled);

    bool success = loader.initializeAll(blocks, entities, items);
    EXPECT_TRUE(success);

    EXPECT_TRUE(rawPtr->loadCalled);
    EXPECT_TRUE(rawPtr->registerCalled);
    EXPECT_EQ(rawPtr->loadedNamespace, "testmod_lifecycle");
}

TEST(ModuleLoaderTest, ShutdownCallsUnload) {
    ModuleLoader loader;

    auto module = std::make_unique<TestModule>("testmod_shutdown");
    TestModule* rawPtr = module.get();
    loader.registerBuiltin(std::move(module));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    loader.initializeAll(blocks, entities, items);
    EXPECT_FALSE(rawPtr->unloadCalled);

    loader.shutdownAll();
    EXPECT_TRUE(rawPtr->unloadCalled);
}

TEST(ModuleLoaderTest, DependencyResolution) {
    ModuleLoader loader;

    // Create modules with dependencies: C depends on B, B depends on A
    auto moduleA = std::make_unique<TestModule>("testmod_a");
    auto moduleB = std::make_unique<TestModule>("testmod_b");
    auto moduleC = std::make_unique<TestModule>("testmod_c");

    moduleB->addDependency("testmod_a");
    moduleC->addDependency("testmod_b");

    TestModule* ptrA = moduleA.get();
    TestModule* ptrB = moduleB.get();
    TestModule* ptrC = moduleC.get();

    // Register in reverse order
    loader.registerBuiltin(std::move(moduleC));
    loader.registerBuiltin(std::move(moduleB));
    loader.registerBuiltin(std::move(moduleA));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    bool success = loader.initializeAll(blocks, entities, items);
    EXPECT_TRUE(success);

    // All should be initialized
    EXPECT_TRUE(ptrA->registerCalled);
    EXPECT_TRUE(ptrB->registerCalled);
    EXPECT_TRUE(ptrC->registerCalled);

    // Check load order via loadedModules()
    auto loaded = loader.loadedModules();
    EXPECT_EQ(loaded.size(), 3);

    // A should be before B, B should be before C
    size_t posA = 0, posB = 0, posC = 0;
    for (size_t i = 0; i < loaded.size(); ++i) {
        if (loaded[i] == "testmod_a") posA = i;
        if (loaded[i] == "testmod_b") posB = i;
        if (loaded[i] == "testmod_c") posC = i;
    }
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posC);
}

TEST(ModuleLoaderTest, MissingDependencyFails) {
    ModuleLoader loader;

    auto module = std::make_unique<TestModule>("testmod_missing_dep");
    module->addDependency("nonexistent_module");
    loader.registerBuiltin(std::move(module));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    // Suppress stderr for this test
    bool success = loader.initializeAll(blocks, entities, items);
    EXPECT_FALSE(success);
}

// ============================================================================
// ModuleRegistry Tests
// ============================================================================

TEST(ModuleRegistryTest, QualifiedName) {
    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    ModuleRegistry registry("mymodule", blocks, entities, items);

    EXPECT_EQ(registry.moduleNamespace(), "mymodule");
    EXPECT_EQ(registry.qualifiedName("block"), "mymodule:block");
    EXPECT_EQ(registry.qualifiedName("item"), "mymodule:item");
}

// ============================================================================
// Entity Registry Stub Tests
// ============================================================================

TEST(EntityRegistryTest, RegisterAndQuery) {
    EntityRegistry& registry = EntityRegistry::global();

    EXPECT_FALSE(registry.hasType("testmod:zombie"));

    bool registered = registry.registerType("testmod:zombie");
    EXPECT_TRUE(registered);

    EXPECT_TRUE(registry.hasType("testmod:zombie"));
}

TEST(EntityRegistryTest, CannotRegisterDuplicate) {
    EntityRegistry& registry = EntityRegistry::global();

    bool first = registry.registerType("testmod:entity_dup");
    EXPECT_TRUE(first);

    bool second = registry.registerType("testmod:entity_dup");
    EXPECT_FALSE(second);
}

// ============================================================================
// Item Registry Stub Tests
// ============================================================================

TEST(ItemRegistryTest, RegisterAndQuery) {
    ItemRegistry& registry = ItemRegistry::global();

    EXPECT_FALSE(registry.hasType("testmod:diamond_sword"));

    bool registered = registry.registerType("testmod:diamond_sword");
    EXPECT_TRUE(registered);

    EXPECT_TRUE(registry.hasType("testmod:diamond_sword"));
}

TEST(ItemRegistryTest, CannotRegisterDuplicate) {
    ItemRegistry& registry = ItemRegistry::global();

    bool first = registry.registerType("testmod:item_dup");
    EXPECT_TRUE(first);

    bool second = registry.registerType("testmod:item_dup");
    EXPECT_FALSE(second);
}

// ============================================================================
// Extended Module Lifecycle Tests
// ============================================================================

class ExtendedTestModule : public GameModule {
public:
    explicit ExtendedTestModule(std::string_view name) : name_(name) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] std::string_view version() const override { return "1.0.0"; }

    void onRegister(ModuleRegistry&) override {
        registerCalled = true;
    }

    void onRegisterEvents(ModuleRegistry& registry) override {
        eventsCalled = true;
        hasEventBus = (registry.eventBus() != nullptr);
    }

    void onRegisterSubsystems(ModuleRegistry& registry) override {
        subsystemsCalled = true;
        hasSession = (registry.session() != nullptr);
    }

    void onRegisterLightProviders(ModuleRegistry& registry) override {
        lightProvidersCalled = true;
        hasLightEngine = (registry.lightEngine() != nullptr);
    }

    void onRegisterRenderLayers(ModuleRegistry& registry) override {
        renderLayersCalled = true;
    }

    bool registerCalled = false;
    bool eventsCalled = false;
    bool subsystemsCalled = false;
    bool lightProvidersCalled = false;
    bool renderLayersCalled = false;
    bool hasEventBus = false;
    bool hasSession = false;
    bool hasLightEngine = false;

private:
    std::string name_;
};

TEST(ExtendedModuleTest, ExtendedPhasesCalledInOrder) {
    ModuleLoader loader;

    auto module = std::make_unique<ExtendedTestModule>("testmod_extended");
    ExtendedTestModule* rawPtr = module.get();
    loader.registerBuiltin(std::move(module));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    // Phase 1-2: basic initialization
    bool success = loader.initializeAll(blocks, entities, items);
    EXPECT_TRUE(success);
    EXPECT_TRUE(rawPtr->registerCalled);
    EXPECT_FALSE(rawPtr->eventsCalled);

    // Phase 3-6: extended initialization
    auto session = GameSession::createLocal();
    EventBus& bus = session->eventBus();
    World& world = session->world();
    LightEngine lightEngine(world);

    ModuleLoader::ExtendedContext ctx;
    ctx.eventBus = &bus;
    ctx.session = session.get();
    ctx.lightEngine = &lightEngine;

    success = loader.initializeExtended(blocks, entities, items, ctx);
    EXPECT_TRUE(success);

    EXPECT_TRUE(rawPtr->eventsCalled);
    EXPECT_TRUE(rawPtr->subsystemsCalled);
    EXPECT_TRUE(rawPtr->lightProvidersCalled);
    EXPECT_TRUE(rawPtr->renderLayersCalled);
}

TEST(ExtendedModuleTest, ExtendedContextAvailable) {
    ModuleLoader loader;

    auto module = std::make_unique<ExtendedTestModule>("testmod_ctx");
    ExtendedTestModule* rawPtr = module.get();
    loader.registerBuiltin(std::move(module));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    loader.initializeAll(blocks, entities, items);

    auto session = GameSession::createLocal();
    World& world = session->world();
    LightEngine lightEngine(world);

    ModuleLoader::ExtendedContext ctx;
    ctx.eventBus = &session->eventBus();
    ctx.session = session.get();
    ctx.lightEngine = &lightEngine;

    loader.initializeExtended(blocks, entities, items, ctx);

    EXPECT_TRUE(rawPtr->hasEventBus);
    EXPECT_TRUE(rawPtr->hasSession);
    EXPECT_TRUE(rawPtr->hasLightEngine);
}

TEST(ExtendedModuleTest, PartialContextSkipsPhases) {
    ModuleLoader loader;

    auto module = std::make_unique<ExtendedTestModule>("testmod_partial");
    ExtendedTestModule* rawPtr = module.get();
    loader.registerBuiltin(std::move(module));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();

    loader.initializeAll(blocks, entities, items);

    // Only provide eventBus - no session or lightEngine
    auto session = GameSession::createLocal();
    ModuleLoader::ExtendedContext ctx;
    ctx.eventBus = &session->eventBus();

    loader.initializeExtended(blocks, entities, items, ctx);

    EXPECT_TRUE(rawPtr->eventsCalled);
    EXPECT_FALSE(rawPtr->subsystemsCalled);   // No session provided
    EXPECT_FALSE(rawPtr->lightProvidersCalled); // No lightEngine provided
    EXPECT_TRUE(rawPtr->renderLayersCalled);   // Always called
}

TEST(ExtendedModuleTest, ConvenienceAddSubsystem) {
    auto session = GameSession::createLocal();
    size_t beforeCount = session->subsystems().size();

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();
    ModuleRegistry registry("testmod_convenience", blocks, entities, items);
    registry.setSession(session.get());

    // Create a minimal subsystem
    class TestSubsystem : public GameSubsystem {
    public:
        std::string_view name() const override { return "TestConvenience"; }
        TickPhase phase() const override { return TickPhase::Tick; }
        void tick(float) override {}
    };

    registry.addSubsystem(std::make_shared<TestSubsystem>());
    EXPECT_EQ(session->subsystems().size(), beforeCount + 1);
}

TEST(ExtendedModuleTest, ConvenienceAddLightProvider) {
    World world;
    LightEngine lightEngine(world);
    EXPECT_EQ(lightEngine.lightProviders().size(), 0u);

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();
    ModuleRegistry registry("testmod_lightconv", blocks, entities, items);
    registry.setLightEngine(&lightEngine);

    registry.addLightProvider(createBlockLightProvider());
    EXPECT_EQ(lightEngine.lightProviders().size(), 1u);
}

TEST(ExtendedModuleTest, ConvenienceNullSafetySubsystem) {
    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();
    ModuleRegistry registry("testmod_nullsafe", blocks, entities, items);

    // session not set - should be a safe no-op
    class TestSubsystem : public GameSubsystem {
    public:
        std::string_view name() const override { return "Null"; }
        TickPhase phase() const override { return TickPhase::Tick; }
        void tick(float) override {}
    };

    // Should not crash
    registry.addSubsystem(std::make_shared<TestSubsystem>());
}

TEST(ExtendedModuleTest, ConvenienceNullSafetyLightProvider) {
    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();
    ModuleRegistry registry("testmod_nullsafe2", blocks, entities, items);

    // lightEngine not set - should be a safe no-op
    registry.addLightProvider(createBlockLightProvider());
}

TEST(ExtendedModuleTest, DependencyOrderPreservedInExtended) {
    ModuleLoader loader;

    std::vector<std::string> callOrder;

    class OrderedModule : public GameModule {
    public:
        OrderedModule(std::string n, std::vector<std::string>& log)
            : name_(std::move(n)), log_(log) {}

        std::string_view name() const override { return name_; }
        std::string_view version() const override { return "1.0.0"; }

        std::vector<std::string_view> dependencies() const override { return deps_; }

        void onRegister(ModuleRegistry&) override {}
        void onRegisterEvents(ModuleRegistry&) override {
            log_.push_back(name_ + ":events");
        }
        void onRegisterSubsystems(ModuleRegistry&) override {
            log_.push_back(name_ + ":subsystems");
        }

        void addDep(std::string_view dep) {
            depStrings_.push_back(std::string(dep));
            deps_.clear();
            for (const auto& s : depStrings_) deps_.push_back(s);
        }

        std::string name_;
        std::vector<std::string> depStrings_;
        std::vector<std::string_view> deps_;
        std::vector<std::string>& log_;
    };

    auto modA = std::make_unique<OrderedModule>("testmod_order_a", callOrder);
    auto modB = std::make_unique<OrderedModule>("testmod_order_b", callOrder);
    modB->addDep("testmod_order_a");

    loader.registerBuiltin(std::move(modB));
    loader.registerBuiltin(std::move(modA));

    BlockRegistry& blocks = BlockRegistry::global();
    EntityRegistry& entities = EntityRegistry::global();
    ItemRegistry& items = ItemRegistry::global();
    loader.initializeAll(blocks, entities, items);

    auto session = GameSession::createLocal();
    ModuleLoader::ExtendedContext ctx;
    ctx.eventBus = &session->eventBus();
    ctx.session = session.get();
    loader.initializeExtended(blocks, entities, items, ctx);

    // A's events should come before B's events
    // A's subsystems should come before B's subsystems
    auto findIdx = [&](const std::string& s) -> int {
        for (int i = 0; i < (int)callOrder.size(); i++) {
            if (callOrder[i] == s) return i;
        }
        return -1;
    };

    int aEvents = findIdx("testmod_order_a:events");
    int bEvents = findIdx("testmod_order_b:events");
    int aSubsystems = findIdx("testmod_order_a:subsystems");
    int bSubsystems = findIdx("testmod_order_b:subsystems");

    ASSERT_NE(aEvents, -1);
    ASSERT_NE(bEvents, -1);
    ASSERT_NE(aSubsystems, -1);
    ASSERT_NE(bSubsystems, -1);

    // Within each phase, dependency order is preserved
    EXPECT_LT(aEvents, bEvents);
    EXPECT_LT(aSubsystems, bSubsystems);

    // All events come before all subsystems (phase ordering)
    EXPECT_LT(aEvents, aSubsystems);
    EXPECT_LT(bEvents, bSubsystems);
}
