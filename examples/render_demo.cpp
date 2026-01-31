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
#include <finevox/string_interner.hpp>
#include <finevox/resource_locator.hpp>
#include <finevox/light_engine.hpp>
#include <finevox/event_queue.hpp>
#include <finevox/physics.hpp>

#include <finevk/finevk.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/engine/camera.hpp>
#include <finevk/engine/overlay2d.hpp>

#include <iostream>
#include <cmath>

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

    bool moveForward = false;
    bool moveBack = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool moveUp = false;
    bool moveDown = false;

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

    // Apply movement to camera using double-precision
    void applyMovement(finevk::Camera& camera, float dt) {
        glm::dvec3 forward{
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw)
        };
        glm::dvec3 right = glm::normalize(glm::cross(forward, glm::dvec3(0, 1, 0)));

        glm::dvec3 velocity{0.0};
        if (moveForward) velocity += forward;
        if (moveBack) velocity -= forward;
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

void buildTestWorld(World& world, bool singleBlock = false, bool largeCoords = false) {
    // Get block type IDs (using string interner)
    auto stone = BlockTypeId::fromName("stone");
    auto dirt = BlockTypeId::fromName("dirt");
    auto grass = BlockTypeId::fromName("grass");
    auto cobble = BlockTypeId::fromName("cobble");
    auto glowstone = BlockTypeId::fromName("glowstone");

    // Register glowstone as a light-emitting block
    auto& registry = BlockRegistry::global();
    if (!registry.hasType(glowstone)) {
        BlockType glowstoneType;
        glowstoneType.setLightEmission(15);  // Maximum brightness
        glowstoneType.setLightAttenuation(0);  // Doesn't block light
        registry.registerType(glowstone, glowstoneType);
    }

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
    bool useAsyncMeshing = false;
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
    }

    try {
        // Setup resource locator
        ResourceLocator::instance().setGameRoot("resources");

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

        worldRenderer.setBlockAtlas(atlas.texture());
        worldRenderer.setTextureProvider(atlas.createProvider());
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

        // Input state
        bool cursorCaptured = false;
        double lastMouseX = 0, lastMouseY = 0;

        // Block placement state
        BlockTypeId selectedBlock = BlockTypeId::fromName("stone");
        int selectedBlockIndex = 0;
        std::vector<BlockTypeId> blockPalette = {
            BlockTypeId::fromName("stone"),
            BlockTypeId::fromName("dirt"),
            BlockTypeId::fromName("grass"),
            BlockTypeId::fromName("cobble"),
            BlockTypeId::fromName("glowstone")
        };

        // Key callback
        window->onKey([&](finevk::Key key, finevk::Action action, finevk::Modifier) {
            bool pressed = (action == finevk::Action::Press || action == finevk::Action::Repeat);

            if (key == GLFW_KEY_ESCAPE && action == finevk::Action::Press) {
                if (cursorCaptured) {
                    window->setMouseCaptured(false);
                    cursorCaptured = false;
                } else {
                    window->close();
                }
            }

            // Movement - only when mouse is captured
            if (cursorCaptured) {
                if (key == GLFW_KEY_W) input.moveForward = pressed;
                if (key == GLFW_KEY_S) input.moveBack = pressed;
                if (key == GLFW_KEY_A) input.moveLeft = pressed;
                if (key == GLFW_KEY_D) input.moveRight = pressed;
                if (key == GLFW_KEY_SPACE) input.moveUp = pressed;
                if (key == GLFW_KEY_LEFT_SHIFT) input.moveDown = pressed;
            } else {
                // Clear movement when not captured
                input.moveForward = false;
                input.moveBack = false;
                input.moveLeft = false;
                input.moveRight = false;
                input.moveUp = false;
                input.moveDown = false;
            }

            // Debug controls
            if (key == GLFW_KEY_F1 && action == finevk::Action::Press) {
                bool enabled = !worldRenderer.debugCameraOffset();
                worldRenderer.setDebugCameraOffset(enabled);
                std::cout << "Debug camera offset: " << (enabled ? "ON" : "OFF") << "\n";
            }

            if (key == GLFW_KEY_F2 && action == finevk::Action::Press) {
                // Teleport to large coordinates
                camera.moveTo(glm::dvec3(1000000.0, 32.0, 1000000.0));
                std::cout << "Teleported to large coordinates (1M, 32, 1M)\n";
            }

            if (key == GLFW_KEY_F3 && action == finevk::Action::Press) {
                // Teleport back to origin
                camera.moveTo(glm::dvec3(0.0, 32.0, 0.0));
                std::cout << "Teleported to origin\n";
            }

            if ((key == GLFW_KEY_F4 || key == GLFW_KEY_4) && action == finevk::Action::Press) {
                // Toggle hidden face culling (debug)
                bool disabled = !worldRenderer.disableFaceCulling();
                worldRenderer.setDisableFaceCulling(disabled);
                worldRenderer.markAllDirty();  // Rebuild meshes
                std::cout << "Hidden face culling: " << (disabled ? "DISABLED (debug)" : "ENABLED") << std::endl;
            }

            if (key == GLFW_KEY_F5 && action == finevk::Action::Press) {
                // Request screenshot on next frame
                std::cout << "Screenshot requested (will save to screenshot.ppm)\n";
            }

            if (key == GLFW_KEY_F6 && action == finevk::Action::Press) {
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

            if (key == GLFW_KEY_G && action == finevk::Action::Press) {
                // Toggle greedy meshing
                bool enabled = !worldRenderer.greedyMeshing();
                worldRenderer.setGreedyMeshing(enabled);
                worldRenderer.markAllDirty();  // Rebuild meshes
                std::cout << "Greedy meshing: " << (enabled ? "ON" : "OFF") << "\n";
            }

            if (key == GLFW_KEY_V && action == finevk::Action::Press) {
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
                }
                std::cout << "==================\n\n";
            }

            if (key == GLFW_KEY_M && action == finevk::Action::Press) {
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

            if (key == GLFW_KEY_L && action == finevk::Action::Press) {
                // Toggle LOD system (off = all chunks at LOD0, no merging)
                bool enabled = !worldRenderer.lodEnabled();
                worldRenderer.setLODEnabled(enabled);
                worldRenderer.markAllDirty();
                std::cout << "LOD system: " << (enabled ? "ON" : "OFF (all LOD0, no merging)") << "\n";
            }

            if (key == GLFW_KEY_C && action == finevk::Action::Press) {
                // Toggle frustum culling (for profiling)
                bool enabled = !worldRenderer.frustumCullingEnabled();
                worldRenderer.setFrustumCullingEnabled(enabled);
                std::cout << "Frustum culling: " << (enabled ? "ON" : "OFF (render all chunks)") << "\n";
            }

            if (key == GLFW_KEY_B && action == finevk::Action::Press) {
                // Cycle lighting mode: OFF -> FLAT -> SMOOTH -> OFF
                lightingMode = (lightingMode + 1) % 3;
                applyLightingMode();
                worldRenderer.markAllDirty();  // Rebuild meshes with new lighting
            }

            if (key == GLFW_KEY_TAB && action == finevk::Action::Press) {
                // Cycle through block palette
                selectedBlockIndex = (selectedBlockIndex + 1) % static_cast<int>(blockPalette.size());
                selectedBlock = blockPalette[selectedBlockIndex];
                std::cout << "Selected block: " << StringInterner::global().lookup(selectedBlock.id) << "\n";
            }

            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5 && action == finevk::Action::Press) {
                // Number keys 1-5 select block directly
                int index = key - GLFW_KEY_1;
                if (index < static_cast<int>(blockPalette.size())) {
                    selectedBlockIndex = index;
                    selectedBlock = blockPalette[selectedBlockIndex];
                    std::cout << "Selected block: " << StringInterner::global().lookup(selectedBlock.id) << "\n";
                }
            }
        });

        // Block shape provider for raycasting - full block collision for non-air blocks
        BlockShapeProvider shapeProvider = [&world](const BlockPos& pos, RaycastMode) -> const CollisionShape* {
            BlockTypeId blockType = world.getBlock(pos);
            if (blockType.isAir()) {
                return nullptr;  // No collision
            }
            return &CollisionShape::FULL_BLOCK;
        };

        // Mouse button callback
        window->onMouseButton([&](finevk::MouseButton button, finevk::Action action, finevk::Modifier) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == finevk::Action::Press) {
                if (!cursorCaptured) {
                    // First click captures mouse
                    window->setMouseCaptured(true);
                    cursorCaptured = true;
                    auto mousePos = window->mousePosition();
                    lastMouseX = mousePos.x;
                    lastMouseY = mousePos.y;
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
            if (button == GLFW_MOUSE_BUTTON_RIGHT && action == finevk::Action::Press && cursorCaptured) {
                // Right click = place block
                glm::dvec3 camPos = camera.positionD();
                Vec3 origin(static_cast<float>(camPos.x), static_cast<float>(camPos.y), static_cast<float>(camPos.z));
                Vec3 direction = input.forwardVec();

                RaycastResult result = raycastBlocks(origin, direction, 10.0f, RaycastMode::Interaction, shapeProvider);
                if (result.hit) {
                    BlockPos placePos = getPlacePosition(result.blockPos, result.face);
                    // Use external API - triggers events, lighting, etc.
                    world.placeBlock(placePos, selectedBlock);
                    std::cout << "Placing " << StringInterner::global().lookup(selectedBlock.id)
                              << " at (" << placePos.x << "," << placePos.y << "," << placePos.z << ")\n";
                }
            }
        });

        // Mouse move callback
        window->onMouseMove([&](double x, double y) {
            if (cursorCaptured) {
                float dx = static_cast<float>(x - lastMouseX);
                float dy = static_cast<float>(y - lastMouseY);
                input.look(dx, dy);
            }
            lastMouseX = x;
            lastMouseY = y;
        });

        // Resize callback
        window->onResize([&](uint32_t width, uint32_t height) {
            if (width > 0 && height > 0) {
                camera.setPerspective(70.0f, float(width) / float(height), 0.1f, 500.0f);
            }
        });

        std::cout << "\nControls:\n";
        std::cout << "  WASD + Mouse: Move and look\n";
        std::cout << "  Space/Shift: Up/Down\n";
        std::cout << "  Left Click: Break block (uses event system)\n";
        std::cout << "  Right Click: Place block (uses event system)\n";
        std::cout << "  1-5 / Tab: Select block type\n";
        std::cout << "  F1: Toggle debug camera offset\n";
        std::cout << "  F2: Teleport to large coords (1M)\n";
        std::cout << "  F3: Teleport to origin\n";
        std::cout << "  F4: Toggle hidden face culling (debug)\n";
        std::cout << "  F6: Toggle async meshing\n";
        std::cout << "  B: Toggle smooth lighting\n";
        std::cout << "  C: Toggle frustum culling\n";
        std::cout << "  G: Toggle greedy meshing\n";
        std::cout << "  L: Toggle LOD (off = no merging)\n";
        std::cout << "  M: Cycle LOD merge mode\n";
        std::cout << "  V: Print mesh statistics\n";
        std::cout << "  Click: Capture mouse\n";
        std::cout << "  Escape: Release mouse / Exit\n";
        std::cout << "\nFlags: --single-block, --large-coords, --async\n\n";

        // Timing
        auto lastTime = std::chrono::high_resolution_clock::now();
        uint32_t frameCount = 0;
        float fpsTimer = 0.0f;

        // Main loop
        while (window->isOpen()) {
            window->pollEvents();

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
                          << "\r" << std::flush;
                frameCount = 0;
                fpsTimer = 0.0f;
            }

            // Update camera position using input handler with double-precision
            input.applyMovement(camera, dt);
            camera.setOrientation(input.forwardVec(), glm::vec3(0, 1, 0));
            camera.updateState();

            // Process events from external API (block place/break)
            // This triggers handlers, lighting updates, neighbor notifications
            size_t eventsProcessed = scheduler.processEvents();
            if (eventsProcessed > 0) {
                // Mark affected chunks dirty for mesh rebuild
                worldRenderer.markAllDirty();  // TODO: More targeted dirty marking
            }

            // Update world renderer - camera.positionD() provides high-precision position
            worldRenderer.updateCamera(camera.state(), camera.positionD());
            worldRenderer.updateMeshes(16);  // Max 16 mesh updates per frame

            // Render
            if (auto frame = renderer->beginFrame()) {
                renderer->beginRenderPass({0.2f, 0.3f, 0.4f, 1.0f});  // Sky blue

                // Render the world
                worldRenderer.render(frame);

                // Draw crosshair at screen center
                auto extent = renderer->extent();
                overlay->beginFrame(renderer->currentFrame(), extent.width, extent.height);
                overlay->drawCrosshair(
                    extent.width / 2.0f, extent.height / 2.0f,
                    20.0f,   // size
                    2.0f,    // thickness
                    {1.0f, 1.0f, 1.0f, 0.8f}  // white with slight transparency
                );
                overlay->render(frame);

                renderer->endRenderPass();
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
