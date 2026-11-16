// The list of process-wide registries that content fills, and emptying them all at once

#include <gtest/gtest.h>

#include "finevox/core/block_properties.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/content_pack_loader.hpp"
#include "finevox/core/entity_registry.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/fluid_interaction.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/global_registries.hpp"
#include "finevox/core/item_registry.hpp"
#include "finevox/core/label_registry.hpp"
#include "finevox/core/loot_registry.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/signal_block.hpp"
#include "finevox/core/sound_registry.hpp"
#include "finevox/core/spawn_predicate.hpp"
#include "finevox/core/tag_registry.hpp"
#include "finevox/core/unification.hpp"
#include "finevox/worldgen/biome.hpp"
#include "finevox/worldgen/clipboard_manager.hpp"
#include "finevox/worldgen/feature_registry.hpp"
#include "finevox/worldgen/schematic.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

using namespace finevox;
using namespace finevox::worldgen;

namespace {

const std::filesystem::path SOURCE = FINEVOX_SOURCE_DIR;

/// Make every registry, so each has joined the list
void touchAll() {
    (void)BlockPropertyRegistry::global();
    (void)BlockRegistry::global();
    (void)EntityRegistry::global();
    (void)EntityTypeRegistry::global();
    (void)FluidInteractionRegistry::global();
    (void)FluidRegistry::global();
    (void)ItemRegistry::global();
    (void)LabelRegistry::global();
    (void)LootRegistry::global();
    (void)ParticleTypeRegistry::global();
    (void)EmitterPresetRegistry::global();
    (void)RecipeRegistry::global();
    (void)SignalBlockRegistry::global();
    (void)SoundRegistry::global();
    (void)SpawnPredicateRegistry::global();
    (void)TagRegistry::global();
    (void)UnificationRegistry::global();
    (void)BiomeRegistry::global();
    (void)FeatureRegistry::global();
    (void)ClipboardManager::instance();
}

/// The engine's shipped pack, then one of each thing it has none of
ContentPackLoadStats loadEverything() {
    ContentPackLoader loader;
    ContentPackLoadStats stats = loader.loadAll(SOURCE / "resources");

    BlockPropertyRegistry::global().addFamily("reg_test:door", {});
    EntityRegistry::global().registerType("reg_test:walker");
    ItemRegistry::global().registerType("reg_test:stick");
    SignalBlockRegistry::global().set(BlockTypeId::fromName("reg_test:wire"), SignalBlockSpec{});
    SpawnPredicateRegistry::global().registerPredicate(
        "reg_test:always", [](const SpawnRule&, const SpawnContext&) { return true; });
    BiomeRegistry::global().registerBiome("reg_test:plain", BiomeProperties{});
    BiomeRegistry::global().setAllowed({BiomeId::fromName("reg_test:plain")});
    FeatureRegistry::global().addPlacement(FeaturePlacement{});
    ClipboardManager::instance().setClipboard(Schematic(1, 1, 1));
    ClipboardManager::instance().pushHistory(Schematic(1, 1, 1));
    LabelRegistry::global().loadLocaleString("xx", "reg_test.hello = hi\n");
    UnificationRegistry::global().declareGroup(
        ItemTypeId::fromName("reg_test:ingot"),
        {ItemTypeId::fromName("reg_test:ingot"), ItemTypeId::fromName("reg_test:other_ingot")});
    return stats;
}

std::map<std::string, size_t> sizes() {
    std::map<std::string, size_t> out;
    for (auto& [name, size] : GlobalRegistries::sizes()) out[name] += size;
    return out;
}

}  // namespace

TEST(GlobalRegistriesTest, LoadedContentAllGoesWhenCleared) {
    touchAll();
    GlobalRegistries::clearAll();
    ContentPackLoadStats stats = loadEverything();
    ASSERT_GT(stats.blocksLoaded, 0u);

    // Every registry holds something, so emptying each is tested
    for (auto& [name, size] : sizes()) {
        EXPECT_GT(size, 0u) << name << " has nothing in it: fill it in loadEverything()";
    }
    BlockTypeId stone = BlockTypeId::fromName("stone");
    ASSERT_TRUE(BlockRegistry::global().hasType(stone));

    GlobalRegistries::clearAll();

    for (auto& [name, size] : sizes()) {
        EXPECT_EQ(size, 0u) << name << " kept content after clearAll";
    }
    // Air is built in and stays; the pack's blocks don't
    EXPECT_EQ(BlockRegistry::global().size(), 1u);
    EXPECT_TRUE(BlockRegistry::global().hasType(AIR_BLOCK_TYPE));
    EXPECT_FALSE(BlockRegistry::global().getType(AIR_BLOCK_TYPE).hasCollision());
    EXPECT_FALSE(BlockRegistry::global().hasType(stone));
    EXPECT_FALSE(BlockRegistry::global().hasHandler(stone));
    EXPECT_TRUE(BiomeRegistry::global().allowedBiomes().empty());
    EXPECT_EQ(ClipboardManager::instance().clipboard(), nullptr);
}

TEST(GlobalRegistriesTest, ASecondLoadAfterClearingIsTheSameAsTheFirst) {
    touchAll();
    GlobalRegistries::clearAll();
    ContentPackLoadStats first = loadEverything();
    auto firstSizes = sizes();

    GlobalRegistries::clearAll();
    ContentPackLoadStats second = loadEverything();

    EXPECT_EQ(second.blocksLoaded, first.blocksLoaded);
    EXPECT_EQ(second.fluidsLoaded, first.fluidsLoaded);
    EXPECT_EQ(second.recipesLoaded, first.recipesLoaded);
    EXPECT_EQ(second.entityTypesLoaded, first.entityTypesLoaded);
    EXPECT_EQ(sizes(), firstSizes);
    GlobalRegistries::clearAll();
}

TEST(GlobalRegistriesTest, EachRegistryJoinsOnce) {
    touchAll();
    std::vector<std::string> names = GlobalRegistries::names();
    std::set<std::string> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), names.size());
}

// A new process-wide registry has to join the list (or be named here as one that doesn't hold
// a game's content). Finds every `static X& global()` / `static X& instance()` in the headers.
TEST(GlobalRegistriesTest, EverySingletonInTheHeadersIsOnTheList) {
    const std::set<std::string> notContent = {"StringInterner", "ConfigManager", "ResourceLocator"};

    std::set<std::string> declared;
    const std::regex singleton(R"(static\s+(\w+)\s*&\s*(global|instance)\s*\(\s*\)\s*;)");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(SOURCE / "include")) {
        if (entry.path().extension() != ".hpp") continue;
        std::ifstream in(entry.path());
        std::stringstream text;
        text << in.rdbuf();
        std::string s = text.str();
        for (std::sregex_iterator it(s.begin(), s.end(), singleton), end; it != end; ++it) {
            declared.insert((*it)[1]);
        }
    }
    ASSERT_GT(declared.size(), 10u) << "the scan found too little: has the pattern changed?";

    touchAll();
    std::vector<std::string> names = GlobalRegistries::names();
    std::set<std::string> listed(names.begin(), names.end());

    for (const std::string& name : declared) {
        if (notContent.count(name)) {
            EXPECT_EQ(listed.count(name), 0u) << name << " is both excluded and on the list";
            continue;
        }
        EXPECT_EQ(listed.count(name), 1u)
            << name << " has a global() but hasn't joined GlobalRegistries (or isn't touched "
            << "in touchAll())";
    }
    for (const std::string& name : listed) {
        EXPECT_EQ(declared.count(name), 1u) << name << " is on the list but wasn't found";
    }
    for (const std::string& name : notContent) {
        EXPECT_EQ(declared.count(name), 1u) << name << " is excluded but no longer exists";
    }
}
