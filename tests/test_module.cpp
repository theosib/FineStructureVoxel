#include <gtest/gtest.h>
#include "finevox/core/module.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_handler.hpp"
#include "finevox/core/entity_registry.hpp"
#include "finevox/core/item_registry.hpp"

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

    bool onUse(BlockContext& ctx, Face face) override {
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
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("blockgame:stone"));
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("mymod:custom_block"));
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("a:b"));
    EXPECT_TRUE(BlockRegistry::isValidNamespacedName("Test123:Block456"));
}

TEST(BlockRegistryNamespaceTest, InvalidNamespacedNames) {
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("stone"));           // No colon
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName(":stone"));          // Empty namespace
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("blockgame:"));      // Empty local name
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("a:b:c"));           // Multiple colons
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("my-mod:block"));    // Hyphen not allowed
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName("my.mod:block"));    // Dot not allowed
    EXPECT_FALSE(BlockRegistry::isValidNamespacedName(""));                // Empty string
}

TEST(BlockRegistryNamespaceTest, GetNamespace) {
    EXPECT_EQ(BlockRegistry::getNamespace("blockgame:stone"), "blockgame");
    EXPECT_EQ(BlockRegistry::getNamespace("mymod:block"), "mymod");
    EXPECT_EQ(BlockRegistry::getNamespace("stone"), "");  // No namespace
}

TEST(BlockRegistryNamespaceTest, GetLocalName) {
    EXPECT_EQ(BlockRegistry::getLocalName("blockgame:stone"), "stone");
    EXPECT_EQ(BlockRegistry::getLocalName("mymod:custom_block"), "custom_block");
    EXPECT_EQ(BlockRegistry::getLocalName("stone"), "stone");  // No namespace, returns full name
}

TEST(BlockRegistryNamespaceTest, MakeQualifiedName) {
    EXPECT_EQ(BlockRegistry::makeQualifiedName("blockgame", "stone"), "blockgame:stone");
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
