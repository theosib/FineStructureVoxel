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
 * - Escape: Exit
 */

#include <finevox/world.hpp>
#include <finevox/world_renderer.hpp>
#include <finevox/block_atlas.hpp>
#include <finevox/block_type.hpp>
#include <finevox/block_model.hpp>
#include <finevox/block_model_loader.hpp>
#include <finevox/string_interner.hpp>
#include <finevox/resource_locator.hpp>
#include <finevox/light_engine.hpp>
#include <finevox/event_queue.hpp>
#include <finevox/physics.hpp>
#include <finevox/entity_manager.hpp>
#include <finevox/graphics_event_queue.hpp>
#include <finevox/config.hpp>

#include <finevk/finevk.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/engine/camera.hpp>
#include <finevk/engine/overlay2d.hpp>
#include <finevk/engine/input_manager.hpp>

#ifdef FINEVOX_HAS_GUI
#include <finegui/finegui.hpp>
#include <imgui.h>
#endif

#include <iostream>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace finevox;

// Get the block position to place a block adjacent to the hit face
BlockPos getPlacePosition(const BlockPos& hitPos, Face face) {
    switch (face) {
        case Face::PosX: return BlockPos(hitPos.x + 1, hitPos.y, hitPos.z);
        case Face::NegX: return BlockPos(hitPos.x - 1, hitPos.y, hitPos.z);
        case Face::PosY: return BlockPos(hitPos.x, hitPos.y + 1, hitPos.z);
        case Face::NegY: return BlockPos(hitPos.x, hitPos.y - 1, hitPos.z);
        case Face::PosZ: return BlockPos(hitPos.x, hitPos.y, hitPos.z + 1);
        case Face::NegZ: return BlockPos(hitPos.x, hitPos.y, hitPos.z - 1);
        default: return hitPos;
    }
}

// Simple first-person camera input handler
// Uses FineVK's Camera with double-precision position support
struct CameraInput {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 10.0f;
    float lookSensitivity = 0.002f;
    float jumpVelocity = 8.0f;  // Jump impulse (blocks/sec)

    bool moveForward = false;
    bool moveBack = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool moveUp = false;      // Used for fly mode up
    bool moveDown = false;    // Used for fly mode down / crouch
    bool jumpRequested = false;  // Jump in physics mode

    void look(float dx, float dy) {
        yaw -= dx * lookSensitivity;
        pitch -= dy * lookSensitivity;
        pitch = glm::clamp(pitch, -1.5f, 1.5f);
    }

    // Get forward vector from yaw/pitch
    glm::vec3 forwardVec() const {
        return glm::vec3{
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw)
        };
    }

    // Get horizontal movement direction from input (XZ plane)
    Vec3 getMoveDirection() const {
        if (!moveForward && !moveBack && !moveLeft && !moveRight) {
            return Vec3(0.0f);
        }

        // Use same approach as fly mode for consistency
        Vec3 horizontalForward(std::sin(yaw), 0.0f, std::cos(yaw));
        Vec3 right = glm::normalize(glm::cross(horizontalForward, Vec3(0, 1, 0)));

        Vec3 dir(0.0f);
        if (moveForward) dir += horizontalForward;
        if (moveBack) dir -= horizontalForward;
        if (moveRight) dir += right;
        if (moveLeft) dir -= right;

        if (glm::length(dir) > 0.0f) {
            return glm::normalize(dir);
        }
        return Vec3(0.0f);
    }

    // Apply physics-based movement to player body
    void applyPhysicsMovement(PhysicsBody& body, PhysicsSystem& physics, float dt) {
        // Get horizontal movement direction
        Vec3 moveDir = getMoveDirection();

        // Set horizontal velocity from input
        Vec3 vel = body.velocity();
        if (glm::length(moveDir) > 0.0f) {
            vel.x = moveDir.x * moveSpeed;
            vel.z = moveDir.z * moveSpeed;
        } else {
            // Apply friction when not moving (quick stop)
            vel.x *= 0.8f;
            vel.z *= 0.8f;
            if (std::abs(vel.x) < 0.1f) vel.x = 0.0f;
            if (std::abs(vel.z) < 0.1f) vel.z = 0.0f;
        }

        // Handle jumping
        if (jumpRequested && body.isOnGround()) {
            vel.y = jumpVelocity;
            jumpRequested = false;  // Consume jump request
        }

        body.setVelocity(vel);

        // Apply gravity and move with collision
        physics.update(body, dt);
    }

    // Apply free-fly movement to camera using double-precision (original behavior)
    void applyFlyMovement(finevk::Camera& camera, float dt) {
        // Horizontal forward (XZ plane only, ignoring pitch)
        glm::dvec3 horizontalForward{
            std::sin(yaw),
            0.0,
            std::cos(yaw)
        };
        glm::dvec3 right = glm::normalize(glm::cross(horizontalForward, glm::dvec3(0, 1, 0)));

        glm::dvec3 velocity{0.0};
        if (moveForward) velocity += horizontalForward;
        if (moveBack) velocity -= horizontalForward;
        if (moveRight) velocity += right;
        if (moveLeft) velocity -= right;
        if (moveUp) velocity.y += 1.0;
        if (moveDown) velocity.y -= 1.0;

        if (glm::length(velocity) > 0.0) {
            velocity = glm::normalize(velocity) * static_cast<double>(moveSpeed);
        }

        // Use FineVK's double-precision move
        camera.move(velocity * static_cast<double>(dt));
    }
};

// Eye height offset from player position (bottom of bounding box)
constexpr float PLAYER_EYE_HEIGHT = 1.62f;  // Minecraft-style eye height

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
        "stone", "dirt", "grass", "cobble", "glowstone",
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
    auto glowstone = BlockTypeId::fromName("glowstone");

    std::cout << "Building test world...\n";
    std::cout << "  Block IDs: stone=" << stone.id << " dirt=" << dirt.id
              << " grass=" << grass.id << " cobble=" << cobble.id
              << " glowstone=" << glowstone.id << "\n";

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

        // Add glowstone lights inside the house and scattered around
        world.setBlock({baseX + 3, 7, baseZ + 3}, glowstone);  // Inside house ceiling
        world.setBlock({baseX + 5, 7, baseZ + 5}, glowstone);  // Inside house ceiling
        world.setBlock({baseX + 20, 50, baseZ + 20}, glowstone);  // On top of tower
        world.setBlock({baseX - 10, 5, baseZ - 10}, glowstone);  // Standalone

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

int main(int argc, char* argv[]) {
    std::cout << "FineVox Render Demo\n";
    std::cout << "==================\n\n";

    // Parse command line
    bool startAtLargeCoords = false;
    bool singleBlockMode = false;
    bool useAsyncMeshing = true;  // Async is default (prevents lighting blink artifacts)
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--large-coords") {
            startAtLargeCoords = true;
        }
        if (std::string(argv[i]) == "--single-block") {
            singleBlockMode = true;
        }
        if (std::string(argv[i]) == "--async") {
            useAsyncMeshing = true;
        }
        if (std::string(argv[i]) == "--sync") {
            useAsyncMeshing = false;
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

        // Create input manager
        auto inputManager = finevk::InputManager::create(window.get());

        // Set up action mappings for movement controls
        inputManager->mapAction("forward", GLFW_KEY_W);
        inputManager->mapAction("back", GLFW_KEY_S);
        inputManager->mapAction("left", GLFW_KEY_A);
        inputManager->mapAction("right", GLFW_KEY_D);
        inputManager->mapAction("up", GLFW_KEY_SPACE);
        inputManager->mapAction("down", GLFW_KEY_LEFT_SHIFT);
        inputManager->mapActionToMouse("break", GLFW_MOUSE_BUTTON_LEFT);
        inputManager->mapActionToMouse("place", GLFW_MOUSE_BUTTON_RIGHT);

        // Load all block definitions from spec files
        // This registers block types and loads custom geometries
        std::unordered_map<uint32_t, BlockGeometry> blockGeometries = loadBlockDefinitions();

        // Create finevox world
        World world;
        buildTestWorld(world, singleBlockMode, startAtLargeCoords);

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
        atlas.setBlockTexture(BlockTypeId::fromName("glowstone"), 4, 0); // Yellow (light source)
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
        worldRenderer.setFaceOccludesProvider([&world, &blockGeometries](const BlockPos& pos, Face face) -> bool {
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

        // Create UpdateScheduler for event-driven block changes
        UpdateScheduler scheduler(world);

        // Create LightEngine for smooth lighting
        LightEngine lightEngine(world);
        // Increase propagation limit to allow full L1 ball (default 256 is too small)
        lightEngine.setMaxPropagationDistance(10000);

        // Wire up systems to World for event-driven block changes
        world.setLightEngine(&lightEngine);
        world.setUpdateScheduler(&scheduler);

        // Start lighting thread (processes lighting updates asynchronously)
        lightEngine.start();
        std::cout << "Lighting thread started (async lighting updates enabled)\n";

        // Create entity system (game thread to graphics thread communication)
        GraphicsEventQueue graphicsEventQueue;
        EntityManager entityManager(world, graphicsEventQueue);
        std::cout << "Entity system initialized\n";

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

        // Propagate block light from glowstone blocks
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
        worldRenderer.setLightProvider([&lightEngine](const BlockPos& pos) -> uint8_t {
            return lightEngine.getCombinedLight(pos);
        });
        applyLightingMode();

        // Enable async meshing if requested
        if (useAsyncMeshing) {
            worldRenderer.enableAsyncMeshing();
            std::cout << "Async meshing enabled with "
                      << worldRenderer.meshWorkerPool()->threadCount() << " worker threads\n";

            // Connect MeshRebuildQueue to LightEngine and World for push-based meshing
            if (worldRenderer.meshRebuildQueue()) {
                lightEngine.setMeshRebuildQueue(worldRenderer.meshRebuildQueue());
                world.setMeshRebuildQueue(worldRenderer.meshRebuildQueue());
                // Conditional defer: only defer if lighting queue is empty
                // Set to true to always defer (prevents visual blink but adds latency)
                world.setAlwaysDeferMeshRebuild(false);
                std::cout << "Push-based meshing enabled (lighting thread handles remesh requests)\n";
            }
        }

        // Mark all chunks as dirty to generate initial meshes
        worldRenderer.markAllDirty();

        // Camera setup - use FineVK's Camera with double-precision support
        CameraInput input;
        finevk::Camera camera;
        camera.setPerspective(70.0f, float(window->width()) / float(window->height()), 0.1f, 500.0f);

        if (singleBlockMode) {
            // Position camera to look at the single block at origin
            // Block is at (0,0,0) to (1,1,1), center is (0.5, 0.5, 0.5)
            camera.moveTo(glm::dvec3(3.0, 2.0, 3.0));
            // Look at block center
            glm::dvec3 toBlock = glm::dvec3(0.5, 0.5, 0.5) - camera.positionD();
            input.yaw = std::atan2(toBlock.x, toBlock.z);
            input.pitch = std::atan2(toBlock.y, glm::length(glm::dvec2(toBlock.x, toBlock.z)));
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
            BlockTypeId::fromName("glowstone"),
            BlockTypeId::fromName("slab"),
            BlockTypeId::fromName("stairs"),
            BlockTypeId::fromName("wedge")
        };

        // Physics state (declared early so callbacks can reference it)
        bool physicsEnabled = false;  // Start in fly mode for familiarity

        // Block shape provider for raycasting - uses BlockRegistry for collision shapes
        BlockShapeProvider shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) -> const CollisionShape* {
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
            Vec3(static_cast<float>(startPos.x), static_cast<float>(startPos.y - PLAYER_EYE_HEIGHT), static_cast<float>(startPos.z)),
            Vec3(0.3f, 0.9f, 0.3f)  // Player half-extents (0.6 x 1.8 x 0.6 full size)
        );

        // Spawn player entity in the entity system
        EntityId playerId = entityManager.spawnPlayer(playerBody.position());
        entityManager.setLocalPlayerId(playerId);
        std::cout << "Player entity spawned with ID " << playerId << "\n";

        // Input event callback - handles all discrete input events
        inputManager->setEventCallback([&](const finevk::InputEvent& e) {
#ifdef FINEVOX_HAS_GUI
            gui.processInput(finegui::InputAdapter::fromFineVK(e));
#endif
            // Key press events for toggles and actions
            if (e.type == finevk::InputEventType::KeyPress) {
                if (e.key == GLFW_KEY_ESCAPE) {
                    if (inputManager->isMouseCaptured()) {
                        inputManager->setMouseCaptured(false);
                    } else {
                        window->close();
                    }
                }

                // Jump in physics mode (space key)
                if (e.key == GLFW_KEY_SPACE && physicsEnabled && inputManager->isMouseCaptured()) {
                    input.jumpRequested = true;
                }

                // Debug controls
                if (e.key == GLFW_KEY_F1) {
                    bool enabled = !worldRenderer.debugCameraOffset();
                    worldRenderer.setDebugCameraOffset(enabled);
                    std::cout << "Debug camera offset: " << (enabled ? "ON" : "OFF") << "\n";
                }

                if (e.key == GLFW_KEY_F2) {
                    // Teleport to large coordinates
                    camera.moveTo(glm::dvec3(1000000.0, 32.0, 1000000.0));
                    playerBody.setPosition(Vec3(1000000.0f, 32.0f - PLAYER_EYE_HEIGHT, 1000000.0f));
                    playerBody.setVelocity(Vec3(0.0f));
                    std::cout << "Teleported to large coordinates (1M, 32, 1M)\n";
                }

                if (e.key == GLFW_KEY_F3) {
                    // Teleport back to origin
                    camera.moveTo(glm::dvec3(0.0, 32.0, 0.0));
                    playerBody.setPosition(Vec3(0.0f, 32.0f - PLAYER_EYE_HEIGHT, 0.0f));
                    playerBody.setVelocity(Vec3(0.0f));
                    std::cout << "Teleported to origin\n";
                }

                if (e.key == GLFW_KEY_F4 || e.key == GLFW_KEY_4) {
                    // Toggle hidden face culling (debug)
                    bool disabled = !worldRenderer.disableFaceCulling();
                    worldRenderer.setDisableFaceCulling(disabled);
                    worldRenderer.markAllDirty();  // Rebuild meshes
                    std::cout << "Hidden face culling: " << (disabled ? "DISABLED (debug)" : "ENABLED") << std::endl;
                }

                if (e.key == GLFW_KEY_F5) {
                    // Toggle physics mode
                    physicsEnabled = !physicsEnabled;
                    if (physicsEnabled) {
                        // Sync player body to current camera position
                        glm::dvec3 camPos = camera.positionD();
                        playerBody.setPosition(Vec3(
                            static_cast<float>(camPos.x),
                            static_cast<float>(camPos.y - PLAYER_EYE_HEIGHT),
                            static_cast<float>(camPos.z)
                        ));
                        playerBody.setVelocity(Vec3(0.0f));
                        std::cout << "Physics mode: ON (gravity, collision, step-climbing)\n";
                    } else {
                        std::cout << "Physics mode: OFF (free-fly camera)\n";
                    }
                }

                if (e.key == GLFW_KEY_F6) {
                    // Toggle async meshing
                    if (worldRenderer.asyncMeshingEnabled()) {
                        worldRenderer.disableAsyncMeshing();
                        std::cout << "Async meshing: OFF (synchronous mode)\n";
                    } else {
                        worldRenderer.enableAsyncMeshing();
                        std::cout << "Async meshing: ON ("
                                  << worldRenderer.meshWorkerPool()->threadCount() << " worker threads)\n";
                    }
                }

                if (e.key == GLFW_KEY_G) {
                    // Toggle greedy meshing
                    bool enabled = !worldRenderer.greedyMeshing();
                    worldRenderer.setGreedyMeshing(enabled);
                    worldRenderer.markAllDirty();  // Rebuild meshes
                    std::cout << "Greedy meshing: " << (enabled ? "ON" : "OFF") << "\n";
                }

                if (e.key == GLFW_KEY_V) {
                    // Print mesh stats
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

                        // Show LOD distribution
                        auto lodStats = worldRenderer.getLODStats();
                        std::cout << "  LOD distribution:\n";
                        for (int i = 0; i < 5; ++i) {
                            if (lodStats.chunksPerLevel[i] > 0) {
                                std::cout << "    LOD" << i << ": " << lodStats.chunksPerLevel[i] << " chunks\n";
                            }
                        }
                    }
                    std::cout << "==================\n\n";
                }

                if (e.key == GLFW_KEY_M) {
                    // Cycle LOD merge mode
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
                }

                if (e.key == GLFW_KEY_L) {
                    // Toggle LOD system (off = all chunks at LOD0, no merging)
                    bool enabled = !worldRenderer.lodEnabled();
                    worldRenderer.setLODEnabled(enabled);
                    worldRenderer.markAllDirty();
                    std::cout << "LOD system: " << (enabled ? "ON" : "OFF (all LOD0, no merging)") << "\n";
                }

                if (e.key == GLFW_KEY_C) {
                    // Toggle frustum culling (for profiling)
                    bool enabled = !worldRenderer.frustumCullingEnabled();
                    worldRenderer.setFrustumCullingEnabled(enabled);
                    std::cout << "Frustum culling: " << (enabled ? "ON" : "OFF (render all chunks)") << "\n";
                }

                if (e.key == GLFW_KEY_B) {
                    // Cycle lighting mode: OFF -> FLAT -> SMOOTH -> OFF
                    lightingMode = (lightingMode + 1) % 3;
                    applyLightingMode();
                    worldRenderer.markAllDirty();  // Rebuild meshes with new lighting
                }

                if (e.key == GLFW_KEY_TAB) {
                    // Cycle through block palette
                    selectedBlockIndex = (selectedBlockIndex + 1) % static_cast<int>(blockPalette.size());
                    selectedBlock = blockPalette[selectedBlockIndex];
                    std::cout << "Selected block: " << StringInterner::global().lookup(selectedBlock.id) << "\n";
                }

                if (e.key >= GLFW_KEY_1 && e.key <= GLFW_KEY_9) {
                    // Number keys 1-9 select block directly
                    int index = e.key - GLFW_KEY_1;
                    if (index < static_cast<int>(blockPalette.size())) {
                        selectedBlockIndex = index;
                        selectedBlock = blockPalette[selectedBlockIndex];
                        std::cout << "Selected block: " << StringInterner::global().lookup(selectedBlock.id) << "\n";
                    }
                }
            }

            // Mouse button events
            if (e.type == finevk::InputEventType::MouseButtonPress) {
                if (e.mouseButton == GLFW_MOUSE_BUTTON_LEFT) {
                    if (!inputManager->isMouseCaptured()) {
                        // First click captures mouse
                        inputManager->setMouseCaptured(true);
                    } else {
                        // Left click while captured = break block
                        glm::dvec3 camPos = camera.positionD();
                        Vec3 origin(static_cast<float>(camPos.x), static_cast<float>(camPos.y), static_cast<float>(camPos.z));
                        Vec3 direction = input.forwardVec();

                        RaycastResult result = raycastBlocks(origin, direction, 10.0f, RaycastMode::Interaction, shapeProvider);
                        if (result.hit) {
                            // Use external API - triggers events, lighting, etc.
                            world.breakBlock(result.blockPos);
                            std::cout << "Breaking block at (" << result.blockPos.x << "," << result.blockPos.y << "," << result.blockPos.z << ")\n";
                        }
                    }
                }

                if (e.mouseButton == GLFW_MOUSE_BUTTON_RIGHT && inputManager->isMouseCaptured()) {
                    // Right click = place block
                    glm::dvec3 camPos = camera.positionD();
                    Vec3 origin(static_cast<float>(camPos.x), static_cast<float>(camPos.y), static_cast<float>(camPos.z));
                    Vec3 direction = input.forwardVec();

                    RaycastResult result = raycastBlocks(origin, direction, 10.0f, RaycastMode::Interaction, shapeProvider);
                    if (result.hit) {
                        BlockPos placePos = getPlacePosition(result.blockPos, result.face);

                        // Check if block would intersect player (uses COLLISION_MARGIN for precision)
                        // Config: physics.block_placement_mode = "block" (default) or "push"
                        if (wouldBlockIntersectBody(placePos, playerBody)) {
                            auto mode = ConfigManager::instance().blockPlacementMode();
                            if (mode == "block") {
                                // Default: prevent placement
                                std::cout << "Cannot place block at (" << placePos.x << "," << placePos.y << "," << placePos.z
                                          << ") - would intersect player\n";
                            } else {
                                // "push" mode: place block and push player
                                // TODO: Implement push logic (requires finding safe position)
                                world.placeBlock(placePos, selectedBlock);
                                std::cout << "Placing " << StringInterner::global().lookup(selectedBlock.id)
                                          << " at (" << placePos.x << "," << placePos.y << "," << placePos.z
                                          << ") - pushing player\n";
                            }
                        } else {
                            // No intersection, place normally
                            world.placeBlock(placePos, selectedBlock);
                            std::cout << "Placing " << StringInterner::global().lookup(selectedBlock.id)
                                      << " at (" << placePos.x << "," << placePos.y << "," << placePos.z << ")\n";
                        }
                    }
                }
            }
        });

        // Resize callback
        window->onResize([&](uint32_t width, uint32_t height) {
            if (width > 0 && height > 0) {
                camera.setPerspective(70.0f, float(width) / float(height), 0.1f, 500.0f);
            }
        });

        std::cout << "\nControls:\n";
        std::cout << "  WASD + Mouse: Move and look\n";
        std::cout << "  Space: Jump (physics) / Up (fly)\n";
        std::cout << "  Shift: Down (fly mode)\n";
        std::cout << "  Left Click: Break block (uses event system)\n";
        std::cout << "  Right Click: Place block (uses event system)\n";
        std::cout << "  1-8 / Tab: Select block type\n";
        std::cout << "    1=stone 2=dirt 3=grass 4=cobble 5=glowstone\n";
        std::cout << "    6=slab 7=stairs 8=wedge (non-cube blocks)\n";
        std::cout << "  F1: Toggle debug camera offset\n";
        std::cout << "  F2: Teleport to large coords (1M)\n";
        std::cout << "  F3: Teleport to origin\n";
        std::cout << "  F4: Toggle hidden face culling (debug)\n";
        std::cout << "  F5: Toggle physics mode (gravity, collision)\n";
        std::cout << "  F6: Toggle async meshing\n";
        std::cout << "  B: Toggle smooth lighting\n";
        std::cout << "  C: Toggle frustum culling\n";
        std::cout << "  G: Toggle greedy meshing\n";
        std::cout << "  L: Toggle LOD (off = no merging)\n";
        std::cout << "  M: Cycle LOD merge mode\n";
        std::cout << "  V: Print mesh statistics\n";
        std::cout << "  Click: Capture mouse\n";
        std::cout << "  Escape: Release mouse / Exit\n";
        std::cout << "\nFlags: --single-block, --large-coords, --sync (async is default)\n\n";

        // Timing
        auto lastTime = std::chrono::high_resolution_clock::now();
        uint32_t frameCount = 0;
        float fpsTimer = 0.0f;

        // Main loop
        while (window->isOpen()) {
            window->pollEvents();

            // Query mouse delta BEFORE update() clears it
            // (update() clears per-frame state like mouseDelta after dispatching events)
            glm::vec2 mouseDelta = inputManager->mouseDelta();

            // Update input manager - dispatches queued events to callback, clears per-frame state
            inputManager->update();

            // Update movement state from action mappings (only when mouse captured)
            if (inputManager->isMouseCaptured()) {
                input.moveForward = inputManager->isActionActive("forward");
                input.moveBack = inputManager->isActionActive("back");
                input.moveLeft = inputManager->isActionActive("left");
                input.moveRight = inputManager->isActionActive("right");
                input.moveDown = inputManager->isActionActive("down");
                // Space is handled specially: jump in physics mode, fly-up in fly mode
                if (!physicsEnabled) {
                    input.moveUp = inputManager->isActionActive("up");
                }

                // Handle mouse look (using delta captured before update())
                if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                    input.look(mouseDelta.x, mouseDelta.y);
                }
            } else {
                // Clear movement when not captured
                input.moveForward = false;
                input.moveBack = false;
                input.moveLeft = false;
                input.moveRight = false;
                input.moveUp = false;
                input.moveDown = false;
                input.jumpRequested = false;
            }

            // Record frame start and get estimated frame period (tracks vsync timing)
            auto framePeriod = worldRenderer.recordFrameStart();

            // Calculate delta time
            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            // FPS counter
            frameCount++;
            fpsTimer += dt;
            if (fpsTimer >= 1.0f) {
                std::cout << "FPS: " << frameCount
                          << " | Chunks: " << worldRenderer.renderedChunkCount()
                          << "/" << worldRenderer.loadedChunkCount()
                          << " | Culled: " << worldRenderer.culledChunkCount()
                          << " | Tris: " << worldRenderer.renderedTriangleCount()
                          << " | Frame: " << framePeriod.count() / 1000.0f << "ms"
                          << "\r" << std::flush;
                frameCount = 0;
                fpsTimer = 0.0f;
            }

            // Update player/camera position
            if (physicsEnabled) {
                // Physics mode: apply physics-based movement
                input.applyPhysicsMovement(playerBody, physicsSystem, dt);

                // Calculate desired camera position (player feet + eye height)
                Vec3 playerPos = playerBody.position();
                Vec3 desiredCameraPos(playerPos.x, playerPos.y + PLAYER_EYE_HEIGHT, playerPos.z);

                // Safe origin is body center (inside the collision volume)
                // Body extends from feet (playerPos.y) to feet + 1.8 (2 * halfExtents.y)
                Vec3 safeOrigin(playerPos.x, playerPos.y + 0.9f, playerPos.z);

                // Adjust camera to prevent near-plane clipping through walls
                Vec3 adjustedCameraPos = adjustCameraForWallCollision(
                    safeOrigin, desiredCameraPos, shapeProvider);

                camera.moveTo(glm::dvec3(adjustedCameraPos.x, adjustedCameraPos.y, adjustedCameraPos.z));
            } else {
                // Fly mode: direct camera movement (no collision)
                input.applyFlyMovement(camera, dt);
            }
            camera.setOrientation(input.forwardVec(), glm::vec3(0, 1, 0));
            camera.updateState();

            // Sync player entity position with physics body
            if (Entity* player = entityManager.getLocalPlayer()) {
                player->setPosition(playerBody.position());
                player->setVelocity(playerBody.velocity());
                player->setOnGround(playerBody.isOnGround());
                player->setLook(glm::degrees(input.yaw), glm::degrees(input.pitch));
            }

            // Tick the entity manager (publishes snapshots to graphics queue)
            entityManager.tick(dt);

            // Drain graphics event queue (for now, just discard the events)
            // In a full implementation, we'd use these for entity interpolation
            [[maybe_unused]] auto graphicsEvents = graphicsEventQueue.drainAll();

            // Process events from external API (block place/break)
            // This triggers handlers, lighting updates, neighbor notifications
            size_t eventsProcessed = scheduler.processEvents();
            if (eventsProcessed > 0 && !worldRenderer.asyncMeshingEnabled()) {
                // For sync meshing, mark chunks dirty for rebuild
                // Async meshing uses push-based system (lighting thread handles remesh)
                worldRenderer.markAllDirty();
            }

            // Update world renderer - camera.positionD() provides high-precision position
            worldRenderer.updateCamera(camera.state(), camera.positionD());

            // For async meshing: wait for mesh uploads with deadline, processing as they arrive
            // This allows the graphics thread to sleep efficiently instead of busy-polling.
            // We use half the frame period as a safety margin for render/submit.
            if (worldRenderer.asyncMeshingEnabled()) {
                // Calculate deadline once - this is the fixed point we must render by
                auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(framePeriod.count() / 2);

                // Loop: wait for meshes, process them, repeat until deadline
                // When woken by a mesh arriving, we process and go back to waiting
                // on the SAME deadline. Loop exits when deadline reached.
                while (std::chrono::steady_clock::now() < deadline) {
                    // Wait returns true if woken normally (mesh or deadline), false on shutdown
                    if (!worldRenderer.waitForMeshUploads(deadline)) {
                        break;  // Shutdown signaled
                    }

                    // Process whatever meshes are ready (non-blocking)
                    // This drains the queue each time we wake
                    worldRenderer.updateMeshes(0);  // 0 = unlimited, drain queue
                }
            } else {
                // Sync meshing: just process directly
                worldRenderer.updateMeshes(16);
            }

            // Render
            if (auto frame = renderer->beginFrame()) {
                frame.beginRenderPass({0.2f, 0.3f, 0.4f, 1.0f});  // Sky blue

                // Render the world
                worldRenderer.render(frame);

                // Draw crosshair at screen center
                overlay->beginFrame(frame.frameIndex(), frame.extent.width, frame.extent.height);
                overlay->drawCrosshair(
                    frame.extent.width / 2.0f, frame.extent.height / 2.0f,
                    20.0f,   // size
                    2.0f,    // thickness
                    {1.0f, 1.0f, 1.0f, 0.8f}  // white with slight transparency
                );
                overlay->render(frame);

                // GUI overlays
#ifdef FINEVOX_HAS_GUI
                gui.beginFrame();

                // Coordinates overlay (upper left)
                {
                    glm::dvec3 pos = camera.positionD();
                    ImGui::SetNextWindowPos(ImVec2(10, 10));
                    ImGui::Begin("##coords", nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
                    ImGui::TextColored(ImVec4(1, 1, 1, 0.85f), "X: %.1f", pos.x);
                    ImGui::TextColored(ImVec4(1, 1, 1, 0.85f), "Y: %.1f", pos.y);
                    ImGui::TextColored(ImVec4(1, 1, 1, 0.85f), "Z: %.1f", pos.z);
                    ImGui::End();
                }

                // Mock hotbar (bottom center)
                {
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
            }
        }

        std::cout << "\n\nShutting down...\n";

        // Stop lighting thread before destroying world
        lightEngine.stop();
        std::cout << "Lighting thread stopped.\n";

        renderer->waitIdle();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
