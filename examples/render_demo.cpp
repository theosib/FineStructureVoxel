/**
 * @file render_demo.cpp
 * @brief WorldRenderer demo - renders a manually-placed voxel world
 *
 * Demonstrates:
 * - World creation with block placement
 * - WorldRenderer setup with BlockAtlas
 * - Debug camera offset for frustum culling visualization
 * - View-relative rendering at large coordinates
 * - Greedy meshing optimization
 * - Smooth lighting with LightEngine (sky + block light)
 *
 * Command line:
 * - --worldgen: Use procedural world generation instead of test world
 * - --single-block: Place only a single block for testing
 * - --large-coords: Start at large coordinates (tests precision)
 * - --async/--sync: Toggle async meshing mode
 *
 * Controls:
 * - WASD: Move camera
 * - Mouse: Look around
 * - F1: Toggle debug camera offset (shows frustum culling edges)
 * - F2: Teleport to large coordinates (tests precision)
 * - F3: Teleport to origin
 * - F4: Toggle hidden face culling (debug)
 * - F6: Toggle async meshing (background mesh generation)
 * - B: Toggle smooth lighting (sky + block light)
 * - C: Toggle frustum culling (off = render all chunks for profiling)
 * - G: Toggle greedy meshing (compare vertex counts)
 * - L: Toggle LOD system (off = all LOD0, no merging)
 * - M: Cycle LOD merge mode (FullHeight vs HeightLimited)
 * - V: Print mesh statistics (vertices, indices)
 * - F7: Toggle debug stats overlay
 * - ` (backtick): Toggle console
 * - Escape: Toggle pause menu
 */

#include <finevox/core/world.hpp>
#include <finevox/core/game_session.hpp>
#include <finevox/render/world_renderer.hpp>
#include <finevox/render/block_atlas.hpp>
#include <finevox/core/block_type.hpp>
#include <finevox/core/block_model.hpp>
#include <finevox/core/block_model_loader.hpp>
#include <finevox/core/string_interner.hpp>
#include <finevox/core/resource_locator.hpp>
#include <finevox/core/light_engine.hpp>
#include <finevox/core/event_queue.hpp>
#include <finevox/core/physics.hpp>
#include <finevox/core/entity_manager.hpp>
#include <finevox/core/entity_state.hpp>
#include <finevox/core/graphics_event_queue.hpp>
#include <finevox/core/config.hpp>
#include <finevox/core/player_controller.hpp>
#include <finevox/core/input_context.hpp>
#include <finevox/core/key_bindings.hpp>
#include <finevox/core/world_time.hpp>
#include <finevox/core/sky.hpp>
#include <finevox/render/frame_fence_waiter.hpp>
#include <finevox/worldgen/world_generator.hpp>
#include <finevox/worldgen/generation_passes.hpp>
#include <finevox/worldgen/biome_loader.hpp>
#include <finevox/worldgen/feature_loader.hpp>
#include <finevox/worldgen/feature_registry.hpp>

#include <finevk/finevk.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/engine/camera.hpp>
#include <finevk/engine/overlay2d.hpp>
#include <finevk/engine/input_manager.hpp>

#ifdef FINEVOX_HAS_GUI
#include <finegui/finegui.hpp>
#include <imgui.h>
#endif

#ifdef FINEVOX_HAS_SCRIPT_GUI
#include <finegui/map_renderer.hpp>
#include <finegui/script_gui.hpp>
#include <finegui/script_gui_manager.hpp>
#include <finegui/script_bindings.hpp>
#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <finescript/value.h>
#include <finescript/map_data.h>
#include <finescript/resource_finder.h>
#endif

#ifdef FINEVOX_HAS_AUDIO
#include <finevox/audio/audio_engine.hpp>
#include <finevox/audio/footstep_tracker.hpp>
#include <finevox/audio/sound_loader.hpp>
#endif

#include <iostream>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace finevox;
using namespace finevox::worldgen;
using namespace finevox::render;

#ifdef FINEVOX_HAS_SCRIPT_GUI
// Adapter: bridges finescript's ResourceFinder to finevox's ResourceLocator
class VoxelResourceFinder : public finescript::ResourceFinder {
public:
    std::filesystem::path resolve(std::string_view name) override {
        auto s = std::string(name);
        auto p = ResourceLocator::instance().resolve("game/" + s);
        if (std::filesystem::exists(p)) return p;
        p = ResourceLocator::instance().resolve("game/" + s + ".fs");
        if (std::filesystem::exists(p)) return p;
        return {};
    }
};
#endif

// Get the block position to place a block adjacent to the hit face
BlockCoord getPlacePosition(const BlockCoord& hitPos, Face face) {
    switch (face) {
        case Face::PosX: return BlockCoord(hitPos.x + 1, hitPos.y, hitPos.z);
        case Face::NegX: return BlockCoord(hitPos.x - 1, hitPos.y, hitPos.z);
        case Face::PosY: return BlockCoord(hitPos.x, hitPos.y + 1, hitPos.z);
        case Face::NegY: return BlockCoord(hitPos.x, hitPos.y - 1, hitPos.z);
        case Face::PosZ: return BlockCoord(hitPos.x, hitPos.y, hitPos.z + 1);
        case Face::NegZ: return BlockCoord(hitPos.x, hitPos.y, hitPos.z - 1);
        default: return hitPos;
    }
}

// Load all block definitions from spec files
// Returns a map of block geometries for blocks with custom meshes
std::unordered_map<uint32_t, BlockGeometry> loadBlockDefinitions() {
    std::unordered_map<uint32_t, BlockGeometry> blockGeometries;
    auto& registry = BlockRegistry::global();

    // Set up model loader with file resolver (returns paths, not content)
    // Try multiple search paths: blocks/ first (for includes), then root
    BlockModelLoader modelLoader;
    modelLoader.setFileResolver([](const std::string& path) -> std::string {
        // Try blocks/ directory first (for includes like "base/solid_cube")
        auto resolved = ResourceLocator::instance().resolve("game/blocks/" + path);
        if (std::filesystem::exists(resolved)) {
            return resolved.string();
        }
        // Fall back to game root (for shapes/ etc.)
        resolved = ResourceLocator::instance().resolve("game/" + path);
        if (std::filesystem::exists(resolved)) {
            return resolved.string();
        }
        // Return empty to trigger relative path fallback in loader
        return "";
    });

    // List of blocks to load
    std::vector<std::string> blockNames = {
        "stone", "dirt", "grass", "cobble", "luminite",
        "slab", "stairs", "wedge"
    };

    for (const auto& name : blockNames) {
        // Resolve the model file path
        auto modelPath = ResourceLocator::instance().resolve("game/blocks/" + name + ".model");
        std::cout << "Loading block '" << name << "' from: " << modelPath.string() << "\n";

        auto model = modelLoader.loadModel(modelPath.string());
        if (!model) {
            std::cout << "  Warning: Failed to load model: " << modelLoader.lastError() << "\n";
            continue;
        }

        // Create BlockType from model properties
        BlockTypeId blockId = BlockTypeId::fromName(name);
        if (!registry.hasType(blockId)) {
            BlockType blockType;

            // Set collision shape
            if (model->hasExplicitCollision() || model->hasCustomGeometry()) {
                blockType.setCollisionShape(model->resolvedCollision());
            }

            // Set light properties
            if (model->lightEmission() > 0) {
                blockType.setLightEmission(model->lightEmission());
            }
            blockType.setLightAttenuation(model->lightAttenuation());

            // Mark as custom mesh if it has non-cube geometry
            if (model->hasCustomGeometry()) {
                blockType.setHasCustomMesh(true);
            }

            // Set sound set from model (e.g., "stone", "grass")
            if (!model->sounds().empty()) {
                blockType.setSoundSet(SoundSetId::fromName(model->sounds()));
            }

            registry.registerType(blockId, blockType);
            std::cout << "  Registered block type with "
                      << (model->hasCustomGeometry() ? "custom" : "standard") << " geometry\n";
        }

        // Store geometry for custom mesh blocks
        if (model->hasCustomGeometry()) {
            blockGeometries[blockId.id] = model->geometry();
            std::cout << "  Stored geometry with " << model->geometry().faces().size() << " faces\n";
        }
    }

    std::cout << "Loaded " << blockNames.size() << " block definitions, "
              << blockGeometries.size() << " with custom geometry\n";

    return blockGeometries;
}

void buildTestWorld(World& world, bool singleBlock = false, bool largeCoords = false) {
    // Get block type IDs (using string interner)
    auto stone = BlockTypeId::fromName("stone");
    auto dirt = BlockTypeId::fromName("dirt");
    auto grass = BlockTypeId::fromName("grass");
    auto cobble = BlockTypeId::fromName("cobble");
    auto luminite = BlockTypeId::fromName("luminite");

    std::cout << "Building test world...\n";
    std::cout << "  Block IDs: stone=" << stone.id << " dirt=" << dirt.id
              << " grass=" << grass.id << " cobble=" << cobble.id
              << " luminite=" << luminite.id << "\n";

    // Base offset for large coordinate testing
    // At 1,000,000 blocks, float32 has ~0.06 block precision loss
    // View-relative rendering should compensate for this
    int32_t baseX = largeCoords ? 1000000 : 0;
    int32_t baseZ = largeCoords ? 1000000 : 0;

    if (largeCoords) {
        std::cout << "  Large coordinates mode: base offset (" << baseX << ", " << baseZ << ")\n";
    }

    if (singleBlock) {
        // Two adjacent blocks to test hidden face removal
        world.setBlock({baseX, 0, baseZ}, stone);
        world.setBlock({baseX + 1, 0, baseZ}, dirt);  // Adjacent block
        std::cout << "  Single block mode: stone at (" << baseX << ",0," << baseZ << ")\n";
    } else {
        // Create a flat ground plane
        for (int x = -32; x < 32; x++) {
            for (int z = -32; z < 32; z++) {
                // Bedrock layer
                world.setBlock({baseX + x, 0, baseZ + z}, stone);

                // Dirt layers
                for (int y = 1; y < 4; y++) {
                    world.setBlock({baseX + x, y, baseZ + z}, dirt);
                }

                // Grass top
                world.setBlock({baseX + x, 4, baseZ + z}, grass);
            }
        }

        // Build some structures
        // A small house
        for (int x = 0; x < 8; x++) {
            for (int z = 0; z < 8; z++) {
                for (int y = 5; y < 9; y++) {
                    // Walls only
                    if (x == 0 || x == 7 || z == 0 || z == 7) {
                        world.setBlock({baseX + x, y, baseZ + z}, cobble);
                    }
                }
            }
        }

        // Add luminite lights inside the house and scattered around
        world.setBlock({baseX + 3, 7, baseZ + 3}, luminite);  // Inside house ceiling
        world.setBlock({baseX + 5, 7, baseZ + 5}, luminite);  // Inside house ceiling
        world.setBlock({baseX + 20, 50, baseZ + 20}, luminite);  // On top of tower
        world.setBlock({baseX - 10, 5, baseZ - 10}, luminite);  // Standalone

        // A tall tower for frustum culling testing
        for (int y = 5; y < 50; y++) {
            world.setBlock({baseX + 20, y, baseZ + 20}, stone);
            world.setBlock({baseX + 21, y, baseZ + 20}, stone);
            world.setBlock({baseX + 20, y, baseZ + 21}, stone);
            world.setBlock({baseX + 21, y, baseZ + 21}, stone);
        }

        // Scattered blocks for culling verification
        for (int i = 0; i < 20; i++) {
            int x = (i * 7) % 60 - 30;
            int z = (i * 11) % 60 - 30;
            for (int y = 5; y < 10 + (i % 5); y++) {
                world.setBlock({baseX + x, y, baseZ + z}, stone);
            }
        }
    }

    std::cout << "World built.\n";

    // Verify a block was actually set
    auto testBlock = world.getBlock({baseX, 0, baseZ});
    std::cout << "  Test read: block at (" << baseX << ",0," << baseZ << ") = " << testBlock.id << "\n";
}

void buildGeneratedWorld(World& world, const std::string& resourceDir) {
    std::cout << "Building procedurally generated world...\n";

    uint64_t worldSeed = 42;

    // Load biomes from resource files
    std::string biomesDir = resourceDir + "/biomes";
    size_t biomeCount = BiomeLoader::loadDirectory(biomesDir, "demo");
    std::cout << "  Loaded " << biomeCount << " biomes from " << biomesDir << "\n";

    // Load features from resource files
    std::string featuresDir = resourceDir + "/features";
    size_t featureCount = FeatureLoader::loadDirectory(featuresDir, "demo");
    std::cout << "  Loaded " << featureCount << " features from " << featuresDir << "\n";

    // Add placement rules for loaded features
    if (FeatureRegistry::global().getFeature("demo:oak_tree")) {
        FeaturePlacement treePlacement;
        treePlacement.featureName = "demo:oak_tree";
        treePlacement.density = 0.02f;
        treePlacement.requiresSurface = true;
        FeatureRegistry::global().addPlacement(treePlacement);
    }
    if (FeatureRegistry::global().getFeature("demo:iron_ore")) {
        FeaturePlacement orePlacement;
        orePlacement.featureName = "demo:iron_ore";
        orePlacement.density = 0.03f;
        orePlacement.minHeight = 0;
        orePlacement.maxHeight = 48;
        FeatureRegistry::global().addPlacement(orePlacement);
    }
    if (FeatureRegistry::global().getFeature("demo:coal_ore")) {
        FeaturePlacement orePlacement;
        orePlacement.featureName = "demo:coal_ore";
        orePlacement.density = 0.04f;
        orePlacement.minHeight = 0;
        orePlacement.maxHeight = 64;
        FeatureRegistry::global().addPlacement(orePlacement);
    }

    // Build pipeline
    GenerationPipeline pipeline;
    pipeline.setWorldSeed(worldSeed);
    pipeline.addPass(std::make_unique<TerrainPass>(worldSeed));
    pipeline.addPass(std::make_unique<SurfacePass>());
    pipeline.addPass(std::make_unique<CavePass>(worldSeed));
    pipeline.addPass(std::make_unique<OrePass>());
    pipeline.addPass(std::make_unique<StructurePass>());
    pipeline.addPass(std::make_unique<DecorationPass>());

    std::cout << "  Pipeline: " << pipeline.passCount() << " passes\n";

    // Create biome map
    BiomeMap biomeMap(worldSeed, BiomeRegistry::global());

    // Generate a 6x6 area of columns (96x96 blocks)
    int32_t genRadius = 3;
    int32_t columnsGenerated = 0;
    for (int32_t cx = -genRadius; cx < genRadius; ++cx) {
        for (int32_t cz = -genRadius; cz < genRadius; ++cz) {
            auto& col = world.getOrCreateColumn(ColumnPos(cx, cz));
            pipeline.generateColumn(col, world, biomeMap);
            ++columnsGenerated;
        }
    }

    std::cout << "  Generated " << columnsGenerated << " columns\n";
    std::cout << "  Total non-air blocks: " << world.totalNonAirBlocks() << "\n";
    std::cout << "World generation complete.\n";
}

int main(int argc, char* argv[]) {
    std::cout << "FineVox Render Demo\n";
    std::cout << "==================\n\n";

    // Parse command line
    bool startAtLargeCoords = false;
    bool singleBlockMode = false;
    bool useWorldGen = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--large-coords") {
            startAtLargeCoords = true;
        }
        if (std::string(argv[i]) == "--single-block") {
            singleBlockMode = true;
        }
        if (std::string(argv[i]) == "--worldgen") {
            useWorldGen = true;
        }
    }

    try {
        // Setup resource locator - handle running from build directory
        std::string resourcePath = "resources";
        if (!std::filesystem::exists(resourcePath) && std::filesystem::exists("../resources")) {
            resourcePath = "../resources";
            std::cout << "Running from build directory, using " << resourcePath << "\n";
        }
        ResourceLocator::instance().setGameRoot(resourcePath);

        // Create Vulkan instance
        auto instance = finevk::Instance::create()
            .applicationName("FineVox Render Demo")
            .applicationVersion(1, 0, 0)
            .enableValidation(true)
            .build();

        // Create window
        auto window = finevk::Window::create(instance)
            .title("FineVox Render Demo")
            .size(1280, 720)
            .resizable(true)
            .build();

        // Select GPU and create device
        auto physicalDevice = instance->selectPhysicalDevice(window);
        std::cout << "GPU: " << physicalDevice.name() << "\n";

        auto device = physicalDevice.createLogicalDevice()
            .surface(window->surface())
            .enableAnisotropy()
            .build();

        window->bindDevice(device);

        // Create renderer with depth buffer
        finevk::RendererConfig renderConfig;
        renderConfig.enableDepthBuffer = true;
        renderConfig.msaa = finevk::MSAALevel::Medium;
        auto renderer = finevk::SimpleRenderer::create(window, renderConfig);

        // Create 2D overlay for crosshair
        auto overlay = finevk::Overlay2D::create(device.get(), renderer->renderPass())
            .msaaSamples(renderer->msaaSamples())
            .build();

        // Create GUI system
#ifdef FINEVOX_HAS_GUI
        finegui::GuiConfig guiConfig;
        guiConfig.fontSize = 16.0f;
        guiConfig.msaaSamples = renderer->msaaSamples();
        if (window->isHighDPI()) {
            guiConfig.dpiScale = window->contentScale().x;
        }
        finegui::GuiSystem gui(device.get(), guiConfig);
        gui.initialize(renderer.get());
        std::cout << "GUI system initialized\n";
#endif

#ifdef FINEVOX_HAS_SCRIPT_GUI
        // Script engine for GUI scripts and console (separate from GameScriptEngine)
        finescript::ScriptEngine guiEngine;
        finegui::registerGuiBindings(guiEngine);
        finegui::MapRenderer mapRenderer(guiEngine);
        finegui::ScriptGuiManager guiManager(guiEngine, mapRenderer);

        // Resource finder: bridges finescript source directive to finevox ResourceLocator
        VoxelResourceFinder voxelResourceFinder;
        guiEngine.setResourceFinder(&voxelResourceFinder);

        // Pre-intern symbol IDs for per-frame field updates
        auto& uiSyms = mapRenderer.syms();
        uint32_t symText = guiEngine.intern("text");
        uint32_t symWindowPosX = guiEngine.intern("window_pos_x");
        uint32_t symWindowPosY = guiEngine.intern("window_pos_y");
        uint32_t symWindowSizeW = guiEngine.intern("window_size_w");
        uint32_t symWindowSizeH = guiEngine.intern("window_size_h");
        uint32_t symChildren = guiEngine.intern("children");

        // UI tree IDs (-1 = not shown)
        int dimOverlayId = -1;
        int mainMenuId = -1;
        int settingsMenuId = -1;
        int debugOverlayId = -1;
        int coordsOverlayId = -1;
        int consoleWindowId = -1;

        // Console state
        struct ConsoleEntry {
            std::string text;
            bool isError = false;
        };
        std::deque<ConsoleEntry> consoleLog;
        std::vector<std::string> commandHistory;
        int historyIndex = -1;
        static constexpr size_t MAX_CONSOLE_LINES = 500;

        // Shared execution context for UI script definitions
        finescript::ExecutionContext uiCtx(guiEngine);
        // Separate context for console command execution (retains variables across commands)
        finescript::ExecutionContext consoleCtx(guiEngine);

        // Deferred UI actions (for callbacks that run during renderAll)
        std::vector<std::function<void()>> deferredUiActions;

        // Load UI scripts
        auto loadUiScript = [&](const char* name) {
            auto path = guiEngine.resolveScript(name);
            if (!path.empty()) {
                auto* compiled = guiEngine.loadScript(path);
                auto result = guiEngine.execute(*compiled, uiCtx);
                if (!result.success) {
                    std::cerr << "Error loading " << name << ": " << result.error << "\n";
                }
            } else {
                std::cerr << "Could not find UI script: " << name << "\n";
            }
        };
        loadUiScript("ui/pause_menu.fs");
        loadUiScript("ui/overlays.fs");
        loadUiScript("ui/console.fs");

        // Show coords overlay at startup (immediate = skip warmup)
        auto coordsTree = uiCtx.get("coords_overlay");
        if (!coordsTree.isNil()) {
            coordsOverlayId = mapRenderer.show(coordsTree, uiCtx, true);
        }

        std::cout << "Script-driven GUI engine initialized\n";
#endif

        // Create input manager
        auto inputManager = finevk::InputManager::create(window.get());

        // Load key bindings from config (falls back to defaults if not configured)
        auto keyBindings = loadKeyBindings();
        for (const auto& binding : keyBindings) {
            if (binding.isMouse) {
                inputManager->mapActionToMouse(binding.action, binding.keyCode);
            } else {
                inputManager->mapAction(binding.action, binding.keyCode);
            }
        }

        // Load all block definitions from spec files
        // This registers block types and loads custom geometries
        std::unordered_map<uint32_t, BlockGeometry> blockGeometries = loadBlockDefinitions();

        // Create game session (owns World, UpdateScheduler, LightEngine, EntityManager, WorldTime, event queues)
        auto session = GameSession::createLocal();
        World& world = session->world();

        if (useWorldGen) {
            buildGeneratedWorld(world, resourcePath);
        } else {
            buildTestWorld(world, singleBlockMode, startAtLargeCoords);
        }

        // Debug: Check world state
        std::cout << "World columns: " << world.columnCount() << std::endl;
        std::cout << "Total non-air blocks: " << world.totalNonAirBlocks() << std::endl;
        auto subchunks = world.getAllSubChunkPositions();
        std::cout << "Subchunks with data: " << subchunks.size() << std::endl;
        for (size_t i = 0; i < std::min(subchunks.size(), size_t(5)); ++i) {
            std::cout << "  - (" << subchunks[i].x << ", " << subchunks[i].y << ", " << subchunks[i].z << ")\n";
        }
        if (subchunks.size() > 5) {
            std::cout << "  ... and " << (subchunks.size() - 5) << " more\n";
        }

        // Create world renderer
        WorldRendererConfig worldConfig;
        worldConfig.viewDistance = 128.0f;
        worldConfig.debugCameraOffset = false;
        worldConfig.debugOffset = glm::vec3(0.0f, 0.0f, -32.0f);
        worldConfig.meshCapacityMultiplier = 1.0f;  // DEBUG: No extra capacity to rule out uninitialized data

        WorldRenderer worldRenderer(device.get(), renderer.get(), world, worldConfig);

        // Load shaders (from build output directory)
        worldRenderer.loadShaders("shaders/chunk.vert.spv", "shaders/chunk.frag.spv");

        // Create a simple placeholder atlas (16x16 tiles of solid colors)
        BlockAtlas atlas;
        atlas.createPlaceholderAtlas(device.get(), renderer->commandPool(), 16, 16);

        // Map block types to atlas positions
        atlas.setBlockTexture(BlockTypeId::fromName("stone"), 0, 0);    // Gray
        atlas.setBlockTexture(BlockTypeId::fromName("dirt"), 1, 0);     // Brown
        atlas.setBlockTexture(BlockTypeId::fromName("grass"), 2, 0);    // Green (top)
        atlas.setBlockTexture(BlockTypeId::fromName("cobble"), 3, 0);   // Dark gray
        atlas.setBlockTexture(BlockTypeId::fromName("luminite"), 4, 0); // Yellow (light source)
        atlas.setBlockTexture(BlockTypeId::fromName("slab"), 5, 0);     // Slab (use distinct color)
        atlas.setBlockTexture(BlockTypeId::fromName("stairs"), 6, 0);   // Stairs
        atlas.setBlockTexture(BlockTypeId::fromName("wedge"), 7, 0);    // Wedge

        worldRenderer.setBlockAtlas(atlas.texture());
        worldRenderer.setTextureProvider(atlas.createProvider());

        // Set geometry provider for custom mesh blocks (geometries loaded earlier)
        worldRenderer.setGeometryProvider([&blockGeometries](BlockTypeId type) -> const BlockGeometry* {
            auto it = blockGeometries.find(type.id);
            if (it != blockGeometries.end()) {
                return &it->second;
            }
            return nullptr;
        });

        // Set face occludes provider for directional face culling
        // This handles slabs/stairs where only some faces are solid
        worldRenderer.setFaceOccludesProvider([&world, &blockGeometries](const BlockCoord& pos, Face face) -> bool {
            BlockTypeId blockType = world.getBlock(pos);
            if (blockType.isAir()) {
                return false;  // Air doesn't occlude anything
            }

            // Check if this block has custom geometry
            auto it = blockGeometries.find(blockType.id);
            if (it != blockGeometries.end()) {
                // Custom geometry - check if this specific face is solid
                uint8_t solidMask = it->second.solidFacesMask();
                return (solidMask & (1 << static_cast<int>(face))) != 0;
            }

            // Standard opaque block - all faces occlude
            return true;
        });

        worldRenderer.initialize();

        // Convenience references to session subsystems
        LightEngine& lightEngine = session->lightEngine();
        WorldTime& worldTime = session->worldTime();
        EntityManager& entityManager = session->entities();

        // Start lighting thread (processes lighting updates asynchronously)
        lightEngine.start();
        std::cout << "Lighting thread started (async lighting updates enabled)\n";

        // Start game thread (processes commands + ticks at configured rate)
        session->startGameThread();
        std::cout << "Game thread started (20 TPS)\n";
        std::cout << "Entity system initialized\n";

        worldTime.setTimeSpeed(1.0f);
        bool timeFrozen = false;
        float timeSpeedMultiplier = 1.0f;
        std::cout << "World time initialized (20 ticks/sec, speed 1x)\n";

        // Audio system
#ifdef FINEVOX_HAS_AUDIO
        // Load sound set definitions
        auto soundDir = ResourceLocator::instance().resolve("game/sounds");
        if (std::filesystem::exists(soundDir)) {
            size_t soundSets = finevox::audio::SoundLoader::loadDirectory(soundDir.string());
            std::cout << "Loaded " << soundSets << " sound sets\n";
        }

        // Audio engine reads from session's sound event queue
        finevox::audio::AudioEngine audioEngine(session->soundEvents(), SoundRegistry::global());
        if (audioEngine.initialize()) {
            std::cout << "Audio engine initialized\n";
        } else {
            std::cout << "Warning: Audio engine failed to initialize\n";
        }

        finevox::audio::FootstepTracker footstepTracker(session->soundEvents(), world);
#endif

        // Lighting modes: 0 = off, 1 = flat (raw L1 ball), 2 = smooth (interpolated)
        int lightingMode = 2;  // Start with smooth lighting
        auto applyLightingMode = [&]() {
            worldRenderer.setSmoothLighting(lightingMode == 2);
            worldRenderer.setFlatLighting(lightingMode == 1);
            const char* modeNames[] = {"OFF", "FLAT (raw L1 ball)", "SMOOTH (interpolated)"};
            std::cout << "Lighting mode: " << modeNames[lightingMode] << "\n";
        };

        // Skip sky light for now - it fills the open world with light level 15,
        // which overwhelms block light and makes testing difficult.
        // TODO: Re-enable when testing underground areas or night time
        // std::cout << "Initializing sky light...\n";
        // for (const auto& pos : world.getAllSubChunkPositions()) {
        //     ColumnPos colPos{pos.x, pos.z};
        //     auto* column = world.getColumn(colPos);
        //     if (column && !column->isLightInitialized()) {
        //         lightEngine.initializeSkyLight(colPos);
        //         column->markLightInitialized();
        //     }
        // }

        // Propagate block light from luminite blocks
        // Note: We can't use recalculateSubChunk in a loop because it clears light,
        // which destroys cross-chunk propagation. Instead, directly propagate from
        // known light source positions.
        int32_t baseX = startAtLargeCoords ? 1000000 : 0;
        int32_t baseZ = startAtLargeCoords ? 1000000 : 0;
        if (!singleBlockMode) {
            lightEngine.propagateBlockLight({baseX + 3, 7, baseZ + 3}, 15);
            lightEngine.propagateBlockLight({baseX + 5, 7, baseZ + 5}, 15);
            lightEngine.propagateBlockLight({baseX + 20, 50, baseZ + 20}, 15);
            lightEngine.propagateBlockLight({baseX - 10, 5, baseZ - 10}, 15);
        }
        std::cout << "Block light propagated.\n";

        // Set up light provider for lighting calculations
        // Returns packed byte: sky in high nibble, block in low nibble
        worldRenderer.setLightProvider([&lightEngine](const BlockCoord& pos) -> uint8_t {
            uint8_t sky = lightEngine.getSkyLight(pos);
            uint8_t block = lightEngine.getBlockLight(pos);
            return static_cast<uint8_t>((sky << 4) | block);
        });
        applyLightingMode();

        // Connect MeshRebuildQueue to LightEngine and World for push-based meshing
        std::cout << "Mesh worker pool: "
                  << worldRenderer.meshWorkerPool()->threadCount() << " worker threads\n";

        lightEngine.setMeshRebuildQueue(worldRenderer.meshRebuildQueue());
        world.setMeshRebuildQueue(worldRenderer.meshRebuildQueue());
        // Conditional defer: only defer if lighting queue is empty
        // Set to true to always defer (prevents visual blink but adds latency)
        world.setAlwaysDeferMeshRebuild(false);
        std::cout << "Push-based meshing enabled (lighting thread handles remesh requests)\n";

        // Set up fence-wait thread: overlaps mesh processing with GPU fence wait
        FrameFenceWaiter fenceWaiter;
        fenceWaiter.setRenderer(renderer.get());
        fenceWaiter.attach(worldRenderer.wakeSignal());
        fenceWaiter.start();
        std::cout << "Fence-wait thread started\n";

        // Mark all chunks as dirty to generate initial meshes
        worldRenderer.rebuildAllMeshes();

        // Camera setup - use FineVK's Camera with double-precision support
        PlayerController playerController;
        finevk::Camera camera;
        camera.setPerspective(70.0f, float(window->width()) / float(window->height()), 0.1f, 500.0f);

        if (singleBlockMode) {
            // Position camera to look at the single block at origin
            // Block is at (0,0,0) to (1,1,1), center is (0.5, 0.5, 0.5)
            camera.moveTo(glm::dvec3(3.0, 2.0, 3.0));
            // Look at block center
            glm::dvec3 toBlock = glm::dvec3(0.5, 0.5, 0.5) - camera.positionD();
            playerController.setYaw(static_cast<float>(std::atan2(toBlock.x, toBlock.z)));
            playerController.setPitch(static_cast<float>(std::atan2(toBlock.y, glm::length(glm::dvec2(toBlock.x, toBlock.z)))));
            std::cout << "Single block mode: camera at (3,2,3) looking at block\n";
        } else if (startAtLargeCoords) {
            // Start at large coordinates to test precision
            camera.moveTo(glm::dvec3(1000000.0, 32.0, 1000000.0));
            std::cout << "Starting at large coordinates for precision testing\n";
        } else {
            // Start above the test world
            camera.moveTo(glm::dvec3(0.0, 32.0, 0.0));
        }

        // Input state (mouse capture is handled by InputManager)

        // Block placement state
        BlockTypeId selectedBlock = BlockTypeId::fromName("stone");
        int selectedBlockIndex = 0;
        std::vector<BlockTypeId> blockPalette = {
            BlockTypeId::fromName("stone"),
            BlockTypeId::fromName("dirt"),
            BlockTypeId::fromName("grass"),
            BlockTypeId::fromName("cobble"),
            BlockTypeId::fromName("luminite"),
            BlockTypeId::fromName("slab"),
            BlockTypeId::fromName("stairs"),
            BlockTypeId::fromName("wedge")
        };

        // Input context for routing (gameplay vs menu vs chat)
        InputContext inputContext = InputContext::Gameplay;

        // Debug overlay toggle (F7)
        bool showDebugOverlay = false;
        int lastFps = 0;

        // Settings state
        float mouseSensitivity = 1.0f;
        float fovDegrees = 70.0f;

        // Sync player controller fly position with initial camera position
        playerController.setFlyPosition(camera.positionD());

        // Block shape provider for raycasting - uses BlockRegistry for collision shapes
        BlockShapeProvider shapeProvider = [&world](const BlockCoord& pos, RaycastMode mode) -> const CollisionShape* {
            BlockTypeId blockType = world.getBlock(pos);
            if (blockType.isAir()) {
                return nullptr;  // No collision
            }
            const BlockType& type = BlockRegistry::global().getType(blockType);
            if (mode == RaycastMode::Collision) {
                return type.hasCollision() ? &type.collisionShape() : nullptr;
            } else {
                return type.hasHitShape() ? &type.hitShape() : nullptr;
            }
        };

        // Physics system and player body
        PhysicsSystem physicsSystem(shapeProvider);
        glm::dvec3 startPos = camera.positionD();
        SimplePhysicsBody playerBody(
            Vec3(static_cast<float>(startPos.x), static_cast<float>(startPos.y - playerController.eyeHeight()), static_cast<float>(startPos.z)),
            Vec3(0.35f, 0.925f, 0.35f)  // Player half-extents (0.7 x 1.85 x 0.7 full size)
        );

        // Connect player controller to physics
        playerController.setPhysics(&playerBody, &physicsSystem);

        // Spawn player entity in the entity system
        EntityId playerId = entityManager.spawnPlayer(playerBody.position());
        entityManager.setLocalPlayerId(playerId);
        std::cout << "Player entity spawned with ID " << playerId << "\n";

        // Context switching helper — coordinates CursorMode, GuiMode, and input clearing
        auto setInputContext = [&](InputContext ctx) {
            inputContext = ctx;
            switch (ctx) {
                case InputContext::Gameplay:
                    inputManager->setCursorMode(finevk::CursorMode::Disabled);
#ifdef FINEVOX_HAS_GUI
                    gui.setGuiMode(finegui::GuiMode::Passive);
#endif
                    break;
                case InputContext::Menu:
                    inputManager->setCursorMode(finevk::CursorMode::Normal);
#ifdef FINEVOX_HAS_GUI
                    gui.setGuiMode(finegui::GuiMode::Exclusive);
#endif
                    break;
                case InputContext::Chat:
                    inputManager->setCursorMode(finevk::CursorMode::Normal);
#ifdef FINEVOX_HAS_GUI
                    gui.setGuiMode(finegui::GuiMode::Auto);
#endif
                    break;
            }
            playerController.clearInput();
        };

        // --- Input listener chain (priority-ordered) ---

        // Priority 200 (Menu): Context switching — handles Escape and backtick
        inputManager->addListener([&](const finevk::InputEvent& e) -> finevk::ListenerResult {
            if (e.type != finevk::InputEventType::KeyPress)
                return finevk::ListenerResult::Reject;

            if (e.key == GLFW_KEY_ESCAPE) {
                switch (inputContext) {
                    case InputContext::Gameplay:
#ifdef FINEVOX_HAS_SCRIPT_GUI
                    {
                        // Show dim overlay + main menu
                        auto winSz = window->windowSize();
                        float w = static_cast<float>(winSz.x);
                        float h = static_cast<float>(winSz.y);
                        auto dimTree = uiCtx.get("dim_overlay");
                        if (!dimTree.isNil()) {
                            auto& dm = dimTree.asMap();
                            dm.set(symWindowPosX, finescript::Value::number(0));
                            dm.set(symWindowPosY, finescript::Value::number(0));
                            dm.set(symWindowSizeW, finescript::Value::number(w));
                            dm.set(symWindowSizeH, finescript::Value::number(h));
                            dimOverlayId = mapRenderer.show(dimTree, uiCtx, true);
                        }
                        auto mainTree = uiCtx.get("main_menu");
                        if (!mainTree.isNil()) {
                            mainTree.asMap().set(symWindowPosX, finescript::Value::number(w / 2.0f));
                            mainTree.asMap().set(symWindowPosY, finescript::Value::number(h / 2.0f));
                            mainMenuId = mapRenderer.show(mainTree, uiCtx);
                        }
                    }
#endif
                        setInputContext(InputContext::Menu);
                        return finevk::ListenerResult::Consumed;
                    case InputContext::Menu:
#ifdef FINEVOX_HAS_SCRIPT_GUI
                        // Hide all menu windows
                        if (dimOverlayId >= 0) { mapRenderer.hide(dimOverlayId); dimOverlayId = -1; }
                        if (mainMenuId >= 0) { mapRenderer.hide(mainMenuId); mainMenuId = -1; }
                        if (settingsMenuId >= 0) { mapRenderer.hide(settingsMenuId); settingsMenuId = -1; }
#endif
                        setInputContext(InputContext::Gameplay);
                        return finevk::ListenerResult::Consumed;
                    case InputContext::Chat:
#ifdef FINEVOX_HAS_SCRIPT_GUI
                        // Hide console
                        if (consoleWindowId >= 0) { mapRenderer.hide(consoleWindowId); consoleWindowId = -1; }
#endif
                        setInputContext(InputContext::Gameplay);
                        return finevk::ListenerResult::Consumed;
                }
            }
#ifdef FINEVOX_HAS_SCRIPT_GUI
            // Backtick toggles console (Gameplay <-> Chat)
            if (e.key == GLFW_KEY_GRAVE_ACCENT) {
                if (inputContext == InputContext::Gameplay) {
                    auto cWinSz = window->windowSize();
                    float w = static_cast<float>(cWinSz.x);
                    float h = static_cast<float>(cWinSz.y);
                    float consoleHeight = h * 0.4f;
                    auto consoleTree = uiCtx.get("console_window");
                    if (!consoleTree.isNil()) {
                        auto& cm = consoleTree.asMap();
                        cm.set(uiSyms.visible, finescript::Value::boolean(true)); // reset after X-close
                        cm.set(symWindowPosX, finescript::Value::number(0));
                        cm.set(symWindowPosY, finescript::Value::number(h - consoleHeight));
                        cm.set(symWindowSizeW, finescript::Value::number(w));
                        cm.set(symWindowSizeH, finescript::Value::number(consoleHeight));
                        consoleWindowId = mapRenderer.show(consoleTree, uiCtx, true);
                        mapRenderer.setFocus("console_input");
                    }
                    setInputContext(InputContext::Chat);
                } else if (inputContext == InputContext::Chat) {
                    if (consoleWindowId >= 0) { mapRenderer.hide(consoleWindowId); consoleWindowId = -1; }
                    setInputContext(InputContext::Gameplay);
                }
                return finevk::ListenerResult::Consumed;
            }
#endif
            return finevk::ListenerResult::Reject;
        }, finevk::InputPriority::Menu);

        // Priority 300 (HUD): finegui input routing (respects GuiMode)
#ifdef FINEVOX_HAS_GUI
        gui.connectToInputManager(*inputManager);
#endif

        // Priority 500 (Game): Gameplay input handling
        inputManager->addListener([&](const finevk::InputEvent& e) -> finevk::ListenerResult {
            // Only handle input in Gameplay context
            if (inputContext != InputContext::Gameplay) {
                return finevk::ListenerResult::Reject;
            }

            // Key press events
            if (e.type == finevk::InputEventType::KeyPress) {
                // Jump in physics mode (space key)
                if (e.key == GLFW_KEY_SPACE && !playerController.flyMode()) {
                    playerController.requestJump();
                    return finevk::ListenerResult::Consumed;
                }

                // Debug controls
                if (e.key == GLFW_KEY_F1) {
                    bool enabled = !worldRenderer.debugCameraOffset();
                    worldRenderer.setDebugCameraOffset(enabled);
                    std::cout << "Debug camera offset: " << (enabled ? "ON" : "OFF") << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_F2) {
                    glm::dvec3 teleportPos(1000000.0, 32.0, 1000000.0);
                    camera.moveTo(teleportPos);
                    playerController.setFlyPosition(teleportPos);
                    playerBody.setPosition(Vec3(1000000.0f, 32.0f - playerController.eyeHeight(), 1000000.0f));
                    playerBody.setVelocity(Vec3(0.0f));
                    std::cout << "Teleported to large coordinates (1M, 32, 1M)\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_F3) {
                    glm::dvec3 teleportPos(0.0, 32.0, 0.0);
                    camera.moveTo(teleportPos);
                    playerController.setFlyPosition(teleportPos);
                    playerBody.setPosition(Vec3(0.0f, 32.0f - playerController.eyeHeight(), 0.0f));
                    playerBody.setVelocity(Vec3(0.0f));
                    std::cout << "Teleported to origin\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_F4 || e.key == GLFW_KEY_4) {
                    bool disabled = !worldRenderer.disableFaceCulling();
                    worldRenderer.setDisableFaceCulling(disabled);
                    worldRenderer.rebuildAllMeshes();
                    std::cout << "Hidden face culling: " << (disabled ? "DISABLED (debug)" : "ENABLED") << std::endl;
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_F5) {
                    bool wasFlying = playerController.flyMode();
                    playerController.setFlyMode(!wasFlying);
                    if (!playerController.flyMode()) {
                        camera.moveTo(playerController.eyePosition());
                        std::cout << "Physics mode: ON (gravity, collision, step-climbing)\n";
                    } else {
                        std::cout << "Physics mode: OFF (free-fly camera)\n";
                    }
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_F7) {
                    showDebugOverlay = !showDebugOverlay;
#ifdef FINEVOX_HAS_SCRIPT_GUI
                    if (showDebugOverlay) {
                        auto debugTree = uiCtx.get("debug_overlay");
                        if (!debugTree.isNil()) {
                            // Position at upper-right
                            float w = static_cast<float>(window->windowSize().x);
                            debugTree.asMap().set(symWindowPosX, finescript::Value::number(w - 10));
                            debugTree.asMap().set(symWindowPosY, finescript::Value::number(10));
                            debugTree.asMap().set(guiEngine.intern("window_pivot_x"), finescript::Value::number(1.0));
                            debugTree.asMap().set(guiEngine.intern("window_pivot_y"), finescript::Value::number(0.0));
                            debugOverlayId = mapRenderer.show(debugTree, uiCtx, true);
                        }
                    } else {
                        if (debugOverlayId >= 0) { mapRenderer.hide(debugOverlayId); debugOverlayId = -1; }
                    }
#endif
                    std::cout << "Debug overlay: " << (showDebugOverlay ? "ON" : "OFF") << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_G) {
                    bool enabled = !worldRenderer.greedyMeshing();
                    worldRenderer.setGreedyMeshing(enabled);
                    worldRenderer.rebuildAllMeshes();
                    std::cout << "Greedy meshing: " << (enabled ? "ON" : "OFF") << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_V) {
                    std::cout << "\n=== Mesh Stats ===\n";
                    std::cout << "  Loaded meshes: " << worldRenderer.loadedMeshCount() << "\n";
                    std::cout << "  Total vertices: " << worldRenderer.totalVertexCount() << "\n";
                    std::cout << "  Total indices: " << worldRenderer.totalIndexCount() << "\n";
                    std::cout << "  Frustum culling: " << (worldRenderer.frustumCullingEnabled() ? "ON" : "OFF") << "\n";
                    std::cout << "  Greedy meshing: " << (worldRenderer.greedyMeshing() ? "ON" : "OFF") << "\n";
                    std::cout << "  LOD system: " << (worldRenderer.lodEnabled() ? "ON" : "OFF") << "\n";
                    if (worldRenderer.lodEnabled()) {
                        const char* mergeModeName = "Unknown";
                        switch (worldRenderer.lodMergeMode()) {
                            case LODMergeMode::FullHeight: mergeModeName = "FullHeight"; break;
                            case LODMergeMode::HeightLimited: mergeModeName = "HeightLimited"; break;
                            case LODMergeMode::NoMerge: mergeModeName = "NoMerge"; break;
                        }
                        std::cout << "  LOD merge mode: " << mergeModeName << "\n";
                        auto lodStats = worldRenderer.getLODStats();
                        std::cout << "  LOD distribution:\n";
                        for (int i = 0; i < 5; ++i) {
                            if (lodStats.chunksPerLevel[i] > 0) {
                                std::cout << "    LOD" << i << ": " << lodStats.chunksPerLevel[i] << " chunks\n";
                            }
                        }
                    }
                    std::cout << "==================\n\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_M) {
                    auto currentMode = worldRenderer.lodMergeMode();
                    LODMergeMode nextMode;
                    const char* modeName;
                    switch (currentMode) {
                        case LODMergeMode::FullHeight:
                            nextMode = LODMergeMode::HeightLimited;
                            modeName = "HeightLimited (smoother transitions)";
                            break;
                        case LODMergeMode::HeightLimited:
                            nextMode = LODMergeMode::FullHeight;
                            modeName = "FullHeight (best culling)";
                            break;
                        default:
                            nextMode = LODMergeMode::FullHeight;
                            modeName = "FullHeight (best culling)";
                            break;
                    }
                    worldRenderer.setLODMergeMode(nextMode);
                    std::cout << "LOD merge mode: " << modeName << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_L) {
                    bool enabled = !worldRenderer.lodEnabled();
                    worldRenderer.setLODEnabled(enabled);
                    worldRenderer.rebuildAllMeshes();
                    std::cout << "LOD system: " << (enabled ? "ON" : "OFF (all LOD0, no merging)") << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_C) {
                    bool enabled = !worldRenderer.frustumCullingEnabled();
                    worldRenderer.setFrustumCullingEnabled(enabled);
                    std::cout << "Frustum culling: " << (enabled ? "ON" : "OFF (render all chunks)") << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_T) {
                    if (timeFrozen) {
                        timeFrozen = false;
                        timeSpeedMultiplier = 1.0f;
                        worldTime.setFrozen(false);
                        worldTime.setTimeSpeed(1.0f);
                        std::cout << "Time: 1x speed\n";
                    } else if (timeSpeedMultiplier < 5.0f) {
                        timeSpeedMultiplier = 10.0f;
                        worldTime.setTimeSpeed(10.0f);
                        std::cout << "Time: 10x speed\n";
                    } else if (timeSpeedMultiplier < 50.0f) {
                        timeSpeedMultiplier = 100.0f;
                        worldTime.setTimeSpeed(100.0f);
                        std::cout << "Time: 100x speed\n";
                    } else {
                        timeFrozen = true;
                        worldTime.setFrozen(true);
                        std::cout << "Time: FROZEN\n";
                    }
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_B) {
                    lightingMode = (lightingMode + 1) % 3;
                    applyLightingMode();
                    worldRenderer.rebuildAllMeshes();
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key == GLFW_KEY_TAB) {
                    selectedBlockIndex = (selectedBlockIndex + 1) % static_cast<int>(blockPalette.size());
                    selectedBlock = blockPalette[selectedBlockIndex];
                    std::cout << "Selected block: " << StringInterner::global().lookup(selectedBlock.id) << "\n";
                    return finevk::ListenerResult::Consumed;
                }

                if (e.key >= GLFW_KEY_1 && e.key <= GLFW_KEY_9) {
                    int index = e.key - GLFW_KEY_1;
                    if (index < static_cast<int>(blockPalette.size())) {
                        selectedBlockIndex = index;
                        selectedBlock = blockPalette[selectedBlockIndex];
                        std::cout << "Selected block: " << StringInterner::global().lookup(selectedBlock.id) << "\n";
                    }
                    return finevk::ListenerResult::Consumed;
                }
            }

            // Mouse button events
            if (e.type == finevk::InputEventType::MouseButtonPress) {
                if (e.mouseButton == GLFW_MOUSE_BUTTON_LEFT) {
                    // Left click = break block
                    glm::dvec3 camPos = camera.positionD();
                    Vec3 origin(static_cast<float>(camPos.x), static_cast<float>(camPos.y), static_cast<float>(camPos.z));
                    Vec3 direction = playerController.forwardVector();

                    RaycastResult result = raycastBlocks(origin, direction, 10.0f, RaycastMode::Interaction, shapeProvider);
                    if (result.hit) {
                        session->actions().breakBlock(result.blockPos);
                        std::cout << "Breaking block at (" << result.blockPos.x << "," << result.blockPos.y << "," << result.blockPos.z << ")\n";
                    }
                    return finevk::ListenerResult::Consumed;
                }

                if (e.mouseButton == GLFW_MOUSE_BUTTON_RIGHT) {
                    // Right click = place block
                    glm::dvec3 camPos = camera.positionD();
                    Vec3 origin(static_cast<float>(camPos.x), static_cast<float>(camPos.y), static_cast<float>(camPos.z));
                    Vec3 direction = playerController.forwardVector();

                    RaycastResult result = raycastBlocks(origin, direction, 10.0f, RaycastMode::Interaction, shapeProvider);
                    if (result.hit) {
                        BlockCoord placePos = getPlacePosition(result.blockPos, result.face);

                        if (wouldBlockIntersectBody(placePos, playerBody)) {
                            auto mode = ConfigManager::instance().blockPlacementMode();
                            if (mode == "block") {
                                std::cout << "Cannot place block at (" << placePos.x << "," << placePos.y << "," << placePos.z
                                          << ") - would intersect player\n";
                            } else {
                                session->actions().placeBlock(placePos, selectedBlock);
                                std::cout << "Placing " << StringInterner::global().lookup(selectedBlock.id)
                                          << " at (" << placePos.x << "," << placePos.y << "," << placePos.z
                                          << ") - pushing player\n";
                            }
                        } else {
                            session->actions().placeBlock(placePos, selectedBlock);
                            std::cout << "Placing " << StringInterner::global().lookup(selectedBlock.id)
                                      << " at (" << placePos.x << "," << placePos.y << "," << placePos.z << ")\n";
                        }
                    }
                    return finevk::ListenerResult::Consumed;
                }
            }

            return finevk::ListenerResult::Reject;
        }, finevk::InputPriority::Game);

#ifdef FINEVOX_HAS_SCRIPT_GUI
        // ====================================================================
        // UI control native functions (called from finescript button callbacks)
        // ====================================================================
        guiEngine.registerFunction("resume_game",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>&) -> finescript::Value {
                deferredUiActions.push_back([&]() {
                    if (dimOverlayId >= 0) { mapRenderer.hide(dimOverlayId); dimOverlayId = -1; }
                    if (mainMenuId >= 0) { mapRenderer.hide(mainMenuId); mainMenuId = -1; }
                    if (settingsMenuId >= 0) { mapRenderer.hide(settingsMenuId); settingsMenuId = -1; }
                    setInputContext(InputContext::Gameplay);
                });
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("quit_game",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>&) -> finescript::Value {
                deferredUiActions.push_back([&]() { window->close(); });
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("show_settings",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>&) -> finescript::Value {
                deferredUiActions.push_back([&]() {
                    if (mainMenuId >= 0) { mapRenderer.hide(mainMenuId); mainMenuId = -1; }
                    auto settingsTree = uiCtx.get("settings_menu");
                    if (!settingsTree.isNil()) {
                        auto sWinSz = window->windowSize();
                        settingsTree.asMap().set(symWindowPosX, finescript::Value::number(sWinSz.x / 2.0f));
                        settingsTree.asMap().set(symWindowPosY, finescript::Value::number(sWinSz.y / 2.0f));
                        settingsMenuId = mapRenderer.show(settingsTree, uiCtx);
                        // Update slider values to current state
                        auto v = mapRenderer.findById("view_dist");
                        if (!v.isNil()) v.asMap().set(uiSyms.value, finescript::Value::number(worldRenderer.viewDistance()));
                        v = mapRenderer.findById("fov");
                        if (!v.isNil()) v.asMap().set(uiSyms.value, finescript::Value::number(fovDegrees));
                        v = mapRenderer.findById("sensitivity");
                        if (!v.isNil()) v.asMap().set(uiSyms.value, finescript::Value::number(mouseSensitivity));
                        v = mapRenderer.findById("time_speed");
                        if (!v.isNil()) v.asMap().set(uiSyms.value, finescript::Value::number(worldTime.timeSpeed()));
                        v = mapRenderer.findById("freeze_time");
                        if (!v.isNil()) v.asMap().set(uiSyms.value, finescript::Value::boolean(worldTime.isFrozen()));
                        v = mapRenderer.findById("lighting");
                        if (!v.isNil()) v.asMap().set(uiSyms.selected, finescript::Value::integer(lightingMode));
                    }
                });
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("show_main_menu",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>&) -> finescript::Value {
                deferredUiActions.push_back([&]() {
                    if (settingsMenuId >= 0) { mapRenderer.hide(settingsMenuId); settingsMenuId = -1; }
                    auto mainTree = uiCtx.get("main_menu");
                    if (!mainTree.isNil()) {
                        auto mWinSz = window->windowSize();
                        mainTree.asMap().set(symWindowPosX, finescript::Value::number(mWinSz.x / 2.0f));
                        mainTree.asMap().set(symWindowPosY, finescript::Value::number(mWinSz.y / 2.0f));
                        mainMenuId = mapRenderer.show(mainTree, uiCtx);
                    }
                });
                return finescript::Value::nil();
            });

        // Settings change callbacks
        guiEngine.registerFunction("set_view_distance",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (!args.empty()) worldRenderer.setViewDistance(static_cast<float>(args[0].asNumber()));
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("set_fov",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (!args.empty()) {
                    fovDegrees = static_cast<float>(args[0].asNumber());
                    auto winSize = window->size();
                    if (winSize.x > 0 && winSize.y > 0) {
                        camera.setPerspective(fovDegrees, float(winSize.x) / float(winSize.y), 0.1f, 500.0f);
                    }
                }
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("set_sensitivity",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (!args.empty()) mouseSensitivity = static_cast<float>(args[0].asNumber());
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("set_time_speed",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (!args.empty()) {
                    float speed = static_cast<float>(args[0].asNumber());
                    worldTime.setTimeSpeed(speed);
                    timeSpeedMultiplier = speed;
                }
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("set_freeze_time",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (!args.empty()) {
                    bool frozen = args[0].asBool();
                    worldTime.setFrozen(frozen);
                    timeFrozen = frozen;
                }
                return finescript::Value::nil();
            });

        guiEngine.registerFunction("set_lighting",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (!args.empty()) {
                    lightingMode = static_cast<int>(args[0].asInt());
                    applyLightingMode();
                    worldRenderer.rebuildAllMeshes();
                }
                return finescript::Value::nil();
            });

        // Console native functions
        guiEngine.registerFunction("submit_command",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (args.empty()) return finescript::Value::nil();
                std::string command = args[0].toString();
                if (command.empty()) return finescript::Value::nil();

                // Add to history
                commandHistory.push_back(command);
                historyIndex = -1;

                // Echo command
                consoleLog.push_back({"> " + command, false});

                // Pre-load game state map
                {
                    auto gameMap = finescript::Value::map();
                    auto& gm = gameMap.asMap();
                    gm.set(guiEngine.intern("fps"), finescript::Value::integer(lastFps));
                    gm.set(guiEngine.intern("time"), finescript::Value::number(worldTime.timeOfDay()));
                    gm.set(guiEngine.intern("day"), finescript::Value::integer(worldTime.dayNumber()));
                    gm.set(guiEngine.intern("ticks"), finescript::Value::integer(worldTime.totalTicks()));
                    auto posMap = finescript::Value::map();
                    glm::dvec3 camPos = camera.positionD();
                    auto& pm = posMap.asMap();
                    pm.set(guiEngine.intern("x"), finescript::Value::number(camPos.x));
                    pm.set(guiEngine.intern("y"), finescript::Value::number(camPos.y));
                    pm.set(guiEngine.intern("z"), finescript::Value::number(camPos.z));
                    gm.set(guiEngine.intern("pos"), std::move(posMap));
                    gm.set(guiEngine.intern("fly"), finescript::Value::boolean(playerController.flyMode()));
                    gm.set(guiEngine.intern("chunks"), finescript::Value::integer(worldRenderer.loadedChunkCount()));
                    consoleCtx.set("game", std::move(gameMap));
                }

                // Execute command
                auto result = guiEngine.executeCommand(command, consoleCtx);
                if (result.success) {
                    if (!result.returnValue.isNil()) {
                        consoleLog.push_back({result.returnValue.toString(), false});
                    }
                } else {
                    consoleLog.push_back({result.error, true});
                }
                if (consoleLog.size() > MAX_CONSOLE_LINES) consoleLog.pop_front();

                // Update console output widget children
                deferredUiActions.push_back([&]() {
                    auto output = mapRenderer.findById("console_output");
                    if (!output.isNil()) {
                        auto children = finescript::Value::array(std::vector<finescript::Value>{});
                        auto& arr = children.asArrayMut();
                        for (const auto& entry : consoleLog) {
                            auto textWidget = finescript::Value::map();
                            auto& tw = textWidget.asMap();
                            if (entry.isError) {
                                tw.set(guiEngine.intern("type"), finescript::Value::symbol(uiSyms.sym_text_colored));
                                std::vector<finescript::Value> colorVec;
                                colorVec.push_back(finescript::Value::number(1.0));
                                colorVec.push_back(finescript::Value::number(0.3));
                                colorVec.push_back(finescript::Value::number(0.3));
                                colorVec.push_back(finescript::Value::number(1.0));
                                tw.set(guiEngine.intern("color"), finescript::Value::array(std::move(colorVec)));
                            } else {
                                tw.set(guiEngine.intern("type"), finescript::Value::symbol(uiSyms.sym_text_wrapped));
                            }
                            tw.set(symText, finescript::Value::string(entry.text));
                            arr.push_back(std::move(textWidget));
                        }
                        output.asMap().set(symChildren, std::move(children));
                    }
                    // Clear input and re-focus
                    auto input = mapRenderer.findById("console_input");
                    if (!input.isNil()) {
                        input.asMap().set(uiSyms.value, finescript::Value::string(""));
                    }
                    mapRenderer.setFocus("console_input");
                });

                return finescript::Value::nil();
            });

        guiEngine.registerFunction("get_history",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (args.empty() || commandHistory.empty()) return finescript::Value::nil();
                int dir = static_cast<int>(args[0].asInt());
                if (dir < 0) {
                    // Up arrow
                    if (historyIndex < 0) historyIndex = static_cast<int>(commandHistory.size()) - 1;
                    else if (historyIndex > 0) historyIndex--;
                } else {
                    // Down arrow
                    if (historyIndex >= 0) historyIndex++;
                    if (historyIndex >= static_cast<int>(commandHistory.size())) historyIndex = -1;
                }
                if (historyIndex >= 0 && historyIndex < static_cast<int>(commandHistory.size())) {
                    return finescript::Value::string(commandHistory[historyIndex]);
                }
                return finescript::Value::string("");
            });

        guiEngine.registerFunction("close_console",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>&) -> finescript::Value {
                deferredUiActions.push_back([&]() {
                    if (consoleWindowId >= 0) { mapRenderer.hide(consoleWindowId); consoleWindowId = -1; }
                    if (inputContext == InputContext::Chat) setInputContext(InputContext::Gameplay);
                    // Reset visible so re-show works
                    auto ct = uiCtx.get("console_window");
                    if (!ct.isNil()) ct.asMap().set(uiSyms.visible, finescript::Value::boolean(true));
                });
                return finescript::Value::nil();
            });

        // Console command functions
        guiEngine.registerFunction("tp",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (args.size() < 3) return finescript::Value::string("Usage: tp x y z");
                float x = static_cast<float>(args[0].asNumber());
                float y = static_cast<float>(args[1].asNumber());
                float z = static_cast<float>(args[2].asNumber());
                playerBody.setPosition(Vec3(x, y - playerController.eyeHeight(), z));
                playerBody.setVelocity(Vec3(0.0f));
                playerController.setFlyPosition(glm::dvec3(x, y, z));
                camera.moveTo(glm::dvec3(x, y, z));
                return finescript::Value::string("Teleported to (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            });

        guiEngine.registerFunction("set_time",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (args.empty()) return finescript::Value::string("Usage: set_time <ticks>");
                int64_t ticks = args[0].asInt();
                session->actions().setWorldTime(ticks);
                return finescript::Value::string("World time set to " + std::to_string(ticks));
            });

        guiEngine.registerFunction("place",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (args.size() < 4) return finescript::Value::string("Usage: place x y z \"type\"");
                int x = static_cast<int>(args[0].asInt());
                int y = static_cast<int>(args[1].asInt());
                int z = static_cast<int>(args[2].asInt());
                std::string typeName = args[3].asString();
                auto typeId = BlockTypeId::fromName(typeName);
                session->actions().placeBlock(BlockCoord(x, y, z), typeId);
                return finescript::Value::string("Placed " + typeName + " at (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            });

        guiEngine.registerFunction("break_block",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                if (args.size() < 3) return finescript::Value::string("Usage: break_block x y z");
                int x = static_cast<int>(args[0].asInt());
                int y = static_cast<int>(args[1].asInt());
                int z = static_cast<int>(args[2].asInt());
                session->actions().breakBlock(BlockCoord(x, y, z));
                return finescript::Value::string("Breaking block at (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
            });

        // Override print to route to console output
        guiEngine.registerFunction("print",
            [&](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) -> finescript::Value {
                std::string text;
                for (size_t i = 0; i < args.size(); i++) {
                    if (i > 0) text += " ";
                    text += args[i].toString();
                }
                consoleLog.push_back({text, false});
                if (consoleLog.size() > MAX_CONSOLE_LINES) consoleLog.pop_front();
                return finescript::Value::nil();
            });
#endif

        // Start in gameplay mode with cursor captured
        setInputContext(InputContext::Gameplay);

        // Resize callback
        window->onResize([&](uint32_t width, uint32_t height) {
            if (width > 0 && height > 0) {
                camera.setPerspective(fovDegrees, float(width) / float(height), 0.1f, 500.0f);
            }
        });

        std::cout << "\nControls:\n";
        std::cout << "  WASD + Mouse: Move and look\n";
        std::cout << "  Space: Jump (physics) / Up (fly)\n";
        std::cout << "  Shift: Down (fly mode)\n";
        std::cout << "  Left Click: Break block\n";
        std::cout << "  Right Click: Place block\n";
        std::cout << "  1-8 / Tab: Select block type\n";
        std::cout << "    1=stone 2=dirt 3=grass 4=cobble 5=luminite\n";
        std::cout << "    6=slab 7=stairs 8=wedge (non-cube blocks)\n";
        std::cout << "  F1: Toggle debug camera offset\n";
        std::cout << "  F2: Teleport to large coords (1M)\n";
        std::cout << "  F3: Teleport to origin\n";
        std::cout << "  F4: Toggle hidden face culling (debug)\n";
        std::cout << "  F5: Toggle physics mode (gravity, collision)\n";
        std::cout << "  F6: Toggle async meshing\n";
        std::cout << "  F7: Toggle debug stats overlay\n";
        std::cout << "  T: Cycle time speed (1x/10x/100x/frozen)\n";
        std::cout << "  B: Toggle smooth lighting\n";
        std::cout << "  C: Toggle frustum culling\n";
        std::cout << "  G: Toggle greedy meshing\n";
        std::cout << "  L: Toggle LOD (off = no merging)\n";
        std::cout << "  M: Cycle LOD merge mode\n";
        std::cout << "  V: Print mesh statistics\n";
        std::cout << "  Escape: Toggle pause menu\n";
        std::cout << "  ` (backtick): Toggle console\n";
        std::cout << "\nFlags: --single-block, --large-coords, --sync (async is default)\n\n";

        // Timing
        auto lastTime = std::chrono::high_resolution_clock::now();
        uint32_t frameCount = 0;
        float fpsTimer = 0.0f;

        // Main loop
        while (window->isOpen()) {
            // ================================================================
            // Phase 1: Fence wait + mesh processing overlap
            // ================================================================
            // Kick the fence wait thread, then process meshes while the GPU
            // finishes the previous frame. This eliminates dead time: instead
            // of blocking on vkWaitForFences, we upload meshes concurrently.
            {
                fenceWaiter.kickWait();

                // Process meshes while fence is pending (no deadline yet)
                while (!fenceWaiter.isReady()) {
                    if (!worldRenderer.waitForMeshUploads(
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(5))) {
                        break;  // Shutdown signaled
                    }
                    worldRenderer.updateMeshes(0);
                }

                // Drain any meshes that arrived right as fence completed
                worldRenderer.updateMeshes(0);

                // Detach from WakeSignal during render — prevents spurious wakes
                fenceWaiter.detach();
            }

            // ================================================================
            // Phase 2: Input, world updates, acquire frame, deadline meshes
            // ================================================================
            window->pollEvents();

            // Query mouse delta BEFORE update() clears it
            glm::vec2 mouseDelta = inputManager->mouseDelta();

            // Update input manager - clears per-frame state (mouseDelta, key press/release sets)
            inputManager->update();

            // Update movement state from action mappings (only in gameplay context)
            if (inputContext == InputContext::Gameplay) {
                playerController.setMoveForward(inputManager->isActionActive("forward"));
                playerController.setMoveBack(inputManager->isActionActive("back"));
                playerController.setMoveLeft(inputManager->isActionActive("left"));
                playerController.setMoveRight(inputManager->isActionActive("right"));
                playerController.setMoveDown(inputManager->isActionActive("down"));
                if (playerController.flyMode()) {
                    playerController.setMoveUp(inputManager->isActionActive("up"));
                }

                if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                    playerController.look(mouseDelta.x * mouseSensitivity, mouseDelta.y * mouseSensitivity);
                }
            }

            // Record frame start and get estimated frame period (tracks vsync timing)
            auto framePeriod = worldRenderer.recordFrameStart();

            // Calculate delta time
            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            // Compute sky parameters (worldTime advanced by game thread; reads are thread-safe)
            auto skyParams = computeSkyParameters(worldTime.timeOfDay());
            worldRenderer.setSkyParameters(skyParams);

            // Use dynamic fog color from sky
            if (worldRenderer.fogDynamicColor()) {
                worldRenderer.setFogColor(skyParams.fogColor);
            }

            // FPS counter
            frameCount++;
            fpsTimer += dt;
            if (fpsTimer >= 1.0f) {
                lastFps = frameCount;
                float tod = worldTime.timeOfDay();
                const char* phase = worldTime.isDaytime() ? "Day" : "Night";
                std::cout << "FPS: " << frameCount
                          << " | Chunks: " << worldRenderer.renderedChunkCount()
                          << "/" << worldRenderer.loadedChunkCount()
                          << " | Culled: " << worldRenderer.culledChunkCount()
                          << " | Tris: " << worldRenderer.renderedTriangleCount()
                          << " | Time: " << phase << " " << static_cast<int>(tod * 36000) << "/36000"
                          << " | Frame: " << framePeriod.count() / 1000.0f << "ms"
                          << "\r" << std::flush;
                frameCount = 0;
                fpsTimer = 0.0f;
            }

            // Update player/camera position
            playerController.update(dt);

#ifdef FINEVOX_HAS_AUDIO
            footstepTracker.update(dt, playerController, camera.positionD());
#endif

            if (playerController.flyMode()) {
                // Fly mode: apply position delta to camera
                camera.move(playerController.flyPositionDelta());
            } else {
                // Physics mode: camera follows body
                Vec3 playerPos = playerBody.position();
                Vec3 desiredCameraPos(playerPos.x, playerPos.y + playerController.eyeHeight(), playerPos.z);

                // Safe origin is body center (inside the collision volume)
                Vec3 safeOrigin(playerPos.x, playerPos.y + 0.9f, playerPos.z);

                // Adjust camera to prevent near-plane clipping through walls
                Vec3 adjustedCameraPos = adjustCameraForWallCollision(
                    safeOrigin, desiredCameraPos, shapeProvider);

                camera.moveTo(glm::dvec3(adjustedCameraPos.x, adjustedCameraPos.y, adjustedCameraPos.z));
            }
            camera.setOrientation(playerController.forwardVector(), glm::vec3(0, 1, 0));
            camera.updateState();

            // Send player state to game thread via command queue
            {
                EntityState state;
                state.position = glm::dvec3(playerBody.position());
                state.velocity = glm::dvec3(playerBody.velocity());
                state.onGround = playerBody.isOnGround();
                state.yaw = glm::degrees(playerController.yaw());
                state.pitch = glm::degrees(playerController.pitch());
                session->actions().sendPlayerState(playerId, state);
            }

            // Drain graphics event queue (for now, just discard the events)
            // In a full implementation, we'd use these for entity interpolation
            [[maybe_unused]] auto graphicsEvents = session->graphicsEvents().drainAll();

#ifdef FINEVOX_HAS_AUDIO
            audioEngine.update(camera.positionD(), playerController.forwardVector(), glm::vec3(0, 1, 0));
#endif

            // Update world renderer - camera.positionD() provides high-precision position
            worldRenderer.updateCamera(camera.state(), camera.positionD());

            // Deadline-based mesh processing: catch input-driven changes in the same frame.
            // The deadline starts HERE (after frame acquire + input), giving mesh workers
            // time to respond to block placements etc. triggered by pollEvents().
            {
                auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(framePeriod.count() / 2);

                while (std::chrono::steady_clock::now() < deadline) {
                    if (!worldRenderer.waitForMeshUploads(deadline)) {
                        break;  // Shutdown signaled
                    }
                    worldRenderer.updateMeshes(0);
                }
            }

            // ================================================================
            // Phase 3: Render + submit
            // ================================================================
            if (auto frame = renderer->beginFrame(true)) {
                frame.beginRenderPass(skyParams.skyColor);  // Dynamic sky color

                // Render the world
                worldRenderer.render(frame);

                // Draw crosshair at screen center (only in gameplay)
                overlay->beginFrame(frame.frameIndex(), frame.extent.width, frame.extent.height);
                if (inputContext == InputContext::Gameplay) {
                    overlay->drawCrosshair(
                        frame.extent.width / 2.0f, frame.extent.height / 2.0f,
                        20.0f,   // size
                        2.0f,    // thickness
                        {1.0f, 1.0f, 1.0f, 0.8f}  // white with slight transparency
                    );
                }
                overlay->render(frame);

                // GUI overlays
#ifdef FINEVOX_HAS_GUI
                gui.beginFrame();

#ifdef FINEVOX_HAS_SCRIPT_GUI
                // Per-frame overlay text updates via finegui mapRenderer
                {
                    // Coordinates overlay
                    glm::dvec3 pos = camera.positionD();
                    char buf[64];
                    auto updateText = [&](const char* id, const char* fmt, auto... args) {
                        snprintf(buf, sizeof(buf), fmt, args...);
                        auto widget = mapRenderer.findById(id);
                        if (!widget.isNil()) widget.asMap().set(symText, finescript::Value::string(buf));
                    };
                    updateText("pos_x", "X: %.1f", pos.x);
                    updateText("pos_y", "Y: %.1f", pos.y);
                    updateText("pos_z", "Z: %.1f", pos.z);

                    // Debug overlay text (only if shown)
                    if (debugOverlayId >= 0) {
                        updateText("fps", "FPS: %d", lastFps);
                        updateText("chunks", "Chunks: %u/%u (culled %u)",
                            worldRenderer.renderedChunkCount(), worldRenderer.loadedChunkCount(),
                            worldRenderer.culledChunkCount());
                        updateText("tris", "Tris: %u", worldRenderer.renderedTriangleCount());
                        float tod = worldTime.timeOfDay();
                        const char* phase = worldTime.isDaytime() ? "Day" : "Night";
                        updateText("time", "Time: %s %d/36000", phase, static_cast<int>(tod * 36000));
                        updateText("mode", "Mode: %s", playerController.flyMode() ? "Fly" : "Physics");
                        updateText("lod", "LOD: %s  Greedy: %s",
                            worldRenderer.lodEnabled() ? "ON" : "OFF",
                            worldRenderer.greedyMeshing() ? "ON" : "OFF");

                        // Update debug overlay position (upper-right, may change on resize)
                        auto debugTree = uiCtx.get("debug_overlay");
                        if (!debugTree.isNil()) {
                            float w = static_cast<float>(window->windowSize().x);
                            debugTree.asMap().set(symWindowPosX, finescript::Value::number(w - 10));
                        }
                    }

                    // Update dim overlay size (in case of window resize)
                    if (dimOverlayId >= 0) {
                        auto dimTree = uiCtx.get("dim_overlay");
                        if (!dimTree.isNil()) {
                            auto dSz = window->windowSize();
                            dimTree.asMap().set(symWindowSizeW, finescript::Value::number(static_cast<float>(dSz.x)));
                            dimTree.asMap().set(symWindowSizeH, finescript::Value::number(static_cast<float>(dSz.y)));
                        }
                    }

                    // Update console position/size (in case of window resize)
                    if (consoleWindowId >= 0) {
                        auto consoleTree = uiCtx.get("console_window");
                        if (!consoleTree.isNil()) {
                            auto cSz = window->windowSize();
                            float w = static_cast<float>(cSz.x);
                            float h = static_cast<float>(cSz.y);
                            float consoleHeight = h * 0.4f;
                            auto& cm = consoleTree.asMap();
                            cm.set(symWindowPosX, finescript::Value::number(0));
                            cm.set(symWindowPosY, finescript::Value::number(h - consoleHeight));
                            cm.set(symWindowSizeW, finescript::Value::number(w));
                            cm.set(symWindowSizeH, finescript::Value::number(consoleHeight));
                        }
                    }
                }

                // Render all finegui map trees
                guiManager.processPendingMessages();
                mapRenderer.renderAll();

                // Process deferred UI actions (from button callbacks)
                for (auto& action : deferredUiActions) action();
                deferredUiActions.clear();
#endif

                // Hotbar (bottom center) — only in gameplay
                if (inputContext == InputContext::Gameplay) {
                    const char* slotNames[] = {
                        "Stone", "Dirt", "Grass", "Cobble",
                        "Glow", "Slab", "Stair", "Wedge", ""
                    };
                    const int slotCount = 8;
                    const float slotSize = 48.0f;
                    const float slotPad = 4.0f;
                    const float barWidth = slotCount * (slotSize + slotPad) + slotPad;

                    ImGuiIO& io = ImGui::GetIO();
                    ImGui::SetNextWindowPos(
                        ImVec2((io.DisplaySize.x - barWidth) / 2.0f, io.DisplaySize.y - slotSize - slotPad * 4));
                    ImGui::Begin("##hotbar", nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);

                    for (int i = 0; i < slotCount; i++) {
                        if (i > 0) ImGui::SameLine(0, slotPad);

                        bool selected = (i == selectedBlockIndex);
                        if (selected) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 0.9f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 0.9f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.7f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.7f));
                        }

                        char label[32];
                        snprintf(label, sizeof(label), "%s\n[%d]", slotNames[i], i + 1);
                        ImGui::Button(label, ImVec2(slotSize, slotSize));

                        ImGui::PopStyleColor(2);
                    }
                    ImGui::End();
                }

                gui.endFrame();
                gui.render(frame);
#endif

                frame.endRenderPass();
                renderer->endFrame();

                // Re-attach fence waiter for next frame's fence wait
                fenceWaiter.attach(worldRenderer.wakeSignal());
            }
        }

        std::cout << "\n\nShutting down...\n";

        // Two-phase shutdown: signal threads to stop first, then join.
        // Fence waiter uses requestStop()/join() so it can exit its timeout
        // loop concurrently while other threads shut down.

        // Signal fence waiter to stop (non-blocking, exits within ~100ms)
        fenceWaiter.requestStop();

#ifdef FINEVOX_HAS_AUDIO
        audioEngine.shutdown();
        std::cout << "Audio engine stopped.\n";
#endif

        // Stop game thread first (flushes pending commands)
        session->stopGameThread();
        std::cout << "Game thread stopped.\n";

        // Stop lighting thread before destroying world
        lightEngine.stop();
        std::cout << "Lighting thread stopped.\n";

        // Join fence waiter (should already be done by now)
        fenceWaiter.join();
        std::cout << "Fence-wait thread stopped.\n";

        renderer->waitIdle();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
