#include <gtest/gtest.h>

#include "finevox/core/content_pack_loader.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/fluid_interaction.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/label_registry.hpp"
#include "finevox/core/loot_registry.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/resource_locator.hpp"
#include "finevox/core/sound_registry.hpp"
#include "finevox/core/tag_registry.hpp"
#include "finevox/core/unification.hpp"
#include "finevox/core/world.hpp"

#include <filesystem>
#include <fstream>

using namespace finevox;

namespace {

std::filesystem::path findResourcesDir() {
    std::vector<std::filesystem::path> candidates = {
        "resources",
        "../resources",
        "../../resources",
        "/Users/theosib/projects/FineStructure/FineStructureVoxel/resources"
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c / "blocks" / "stone.model") &&
            std::filesystem::exists(c / "sky.conf")) {
            return std::filesystem::canonical(c);
        }
    }
    return {};
}

}  // namespace

class ContentPackLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        resourcesDir_ = findResourcesDir();
        ASSERT_FALSE(resourcesDir_.empty()) << "Failed to locate resources/ directory";

        // Clear global registries so counts reflect what ContentPackLoader loads
        FluidRegistry::global().clear();
        FluidInteractionRegistry::global().clear();
        RecipeRegistry::global().clear();
        LootRegistry::global().clear();
        ParticleTypeRegistry::global().clear();
        EmitterPresetRegistry::global().clear();
        EntityTypeRegistry::global().clear();
        TagRegistry::global().clear();
        UnificationRegistry::global().clear();
        SoundRegistry::global().clear();
        LabelRegistry::global().clear();
    }

    std::filesystem::path resourcesDir_;
};

TEST_F(ContentPackLoaderTest, LoadsFullShippedResourcesPack) {
    ContentPackLoader loader;
    ContentPackLoadStats stats = loader.loadAll(resourcesDir_);

    // Verify per-category counts from shipped resources/
    EXPECT_EQ(stats.blocksLoaded, 10u);
    EXPECT_EQ(stats.customGeometriesLoaded, 4u);
    EXPECT_GE(stats.scriptedBlocksDiscovered, 1u);
    EXPECT_EQ(stats.fluidsLoaded, 2u);
    EXPECT_EQ(stats.recipesLoaded, 7u);
    EXPECT_EQ(stats.lootTablesLoaded, 10u);
    EXPECT_EQ(stats.particleTypesLoaded, 6u);
    EXPECT_EQ(stats.emitterPresetsLoaded, 8u);
    EXPECT_EQ(stats.entityTypesLoaded, 3u);
    EXPECT_EQ(stats.tagFilesLoaded, 1u);
    EXPECT_EQ(stats.soundSetsLoaded, 2u);
    EXPECT_EQ(stats.langFilesLoaded, 1u);
    EXPECT_TRUE(stats.skyConfigLoaded);
    EXPECT_TRUE(loader.hasCustomSkyConfig());

    // Both bare ("stone", "luminite", "slab", "workbench") and "finevox:"-prefixed
    // BlockTypeIds must be registered with identical properties
    auto& blockReg = BlockRegistry::global();
    EXPECT_TRUE(blockReg.hasType("stone"));
    EXPECT_TRUE(blockReg.hasType("finevox:stone"));
    EXPECT_FLOAT_EQ(blockReg.getType("stone").hardness(), 1.5f);
    EXPECT_FLOAT_EQ(blockReg.getType("finevox:stone").hardness(), 1.5f);
    EXPECT_EQ(blockReg.getType("stone").lootTable(), LootTableId::fromName("stone"));
    EXPECT_EQ(blockReg.getType("finevox:stone").lootTable(), LootTableId::fromName("stone"));

    // Luminite light emission and particle override
    EXPECT_EQ(blockReg.getType("luminite").lightEmission(), 15u);
    EXPECT_EQ(blockReg.getType("finevox:luminite").lightEmission(), 15u);
    EXPECT_EQ(blockReg.getType("luminite").particleEmitter(),
              EmitterPresetId::fromName("finevox:ember_burst"));
    EXPECT_EQ(blockReg.getType("finevox:luminite").particleEmitter(),
              EmitterPresetId::fromName("finevox:ember_burst"));

    // Custom geometry blocks registered under both bare and prefixed IDs
    EXPECT_TRUE(blockReg.getType("slab").hasCustomMesh());
    EXPECT_TRUE(blockReg.getType("finevox:slab").hasCustomMesh());
    EXPECT_NE(loader.getGeometry(BlockTypeId::fromName("slab")), nullptr);
    EXPECT_NE(loader.getGeometry(BlockTypeId::fromName("finevox:slab")), nullptr);
    EXPECT_EQ(loader.getGeometry(BlockTypeId::fromName("stone")), nullptr);
    EXPECT_EQ(loader.getGeometry(BlockTypeId::fromName("finevox:stone")), nullptr);

    // Geometry provider and faceOccludesProvider callbacks
    auto geomProvider = loader.geometryProvider();
    ASSERT_NE(geomProvider(BlockTypeId::fromName("stairs")), nullptr);
    ASSERT_NE(geomProvider(BlockTypeId::fromName("finevox:stairs")), nullptr);
    EXPECT_EQ(geomProvider(BlockTypeId::fromName("dirt")), nullptr);

    World world;
    world.setBlock({0, 0, 0}, BlockTypeId::fromName("stone"));
    world.setBlock({1, 0, 0}, BlockTypeId::fromName("finevox:slab"));
    auto occludes = loader.faceOccludesProvider(world);
    // Air does not occlude
    EXPECT_FALSE(occludes({0, 5, 0}, Face::PosY));
    // Standard cube occludes all faces
    EXPECT_TRUE(occludes({0, 0, 0}, Face::PosY));
    EXPECT_TRUE(occludes({0, 0, 0}, Face::NegY));
    // Bottom slab occludes NegY (bottom) but NOT PosY (top)
    EXPECT_TRUE(occludes({1, 0, 0}, Face::NegY));
    EXPECT_FALSE(occludes({1, 0, 0}, Face::PosY));

    // Fluids + fluid interactions
    EXPECT_NE(FluidRegistry::global().getType("water"), nullptr);
    EXPECT_NE(FluidRegistry::global().getType("lava"), nullptr);
    EXPECT_TRUE(FluidInteractionRegistry::global().hasInteraction(
        FluidTypeId::fromName("water"), FluidTypeId::fromName("lava")));

    // Loot tables, recipes, particles, emitters, entities, tags, sounds, lang, sky
    EXPECT_TRUE(LootRegistry::global().hasTable("stone"));
    EXPECT_TRUE(LootRegistry::global().hasTable("workbench"));
    EXPECT_NE(RecipeRegistry::global().getRecipe("finevox:workbench"), nullptr);
    EXPECT_TRUE(ParticleTypeRegistry::global().has("finevox:spark"));
    EXPECT_TRUE(EmitterPresetRegistry::global().has("finevox:ember_burst"));
    EXPECT_NE(EntityTypeRegistry::global().getType("pig"), nullptr);
    EXPECT_NE(EntityTypeRegistry::global().getType("zombie"), nullptr);
    EXPECT_NE(EntityTypeRegistry::global().getType("finevox:player"), nullptr);
    EXPECT_TRUE(TagRegistry::global().isResolved());
    EXPECT_NE(SoundRegistry::global().getSoundSet("stone"), nullptr);
    EXPECT_NE(SoundRegistry::global().getSoundSet("grass"), nullptr);
    EXPECT_EQ(loader.skyConfig().colorKeyframes.size(), 7u);
    EXPECT_EQ(loader.skyConfig().brightnessKeyframes.size(), 4u);
}

TEST_F(ContentPackLoaderTest, LoadFromResourceLocatorAndBindScriptsCallback) {
    ResourceLocator::instance().reset();
    ResourceLocator::instance().setGameRoot(resourcesDir_);

    ContentPackLoader loader;
    ContentPackLoadStats stats = loader.loadFromLocator();
    EXPECT_EQ(stats.blocksLoaded, 10u);
    EXPECT_TRUE(stats.skyConfigLoaded);

    // Workbench model declares script: scripts/workbench.fsc
    ASSERT_FALSE(loader.scriptedBlocks().empty());
    std::vector<std::string> boundNames;
    size_t boundCount = loader.bindScripts(
        [&](const std::string& scriptPath, const std::string& blockName) -> bool {
            EXPECT_TRUE(std::filesystem::exists(scriptPath)) << "Missing script: " << scriptPath;
            boundNames.push_back(blockName);
            return true;
        });
    EXPECT_GE(boundCount, 1u);
    EXPECT_NE(std::find(boundNames.begin(), boundNames.end(), "workbench"), boundNames.end());
    EXPECT_NE(std::find(boundNames.begin(), boundNames.end(), "finevox:workbench"),
              boundNames.end());
}
