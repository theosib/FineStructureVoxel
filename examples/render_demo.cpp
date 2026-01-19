/**
 * @file render_demo.cpp
 * @brief WorldRenderer demo - renders a manually-placed voxel world
 *
 * Demonstrates:
 * - World creation with block placement
 * - WorldRenderer setup with BlockAtlas
 * - Debug camera offset for frustum culling visualization
 * - View-relative rendering at large coordinates
 *
 * Controls:
 * - WASD: Move camera
 * - Mouse: Look around
 * - F1: Toggle debug camera offset (shows frustum culling edges)
 * - F2: Teleport to large coordinates (tests precision)
 * - Escape: Exit
 */

#include <finevox/world.hpp>
#include <finevox/world_renderer.hpp>
#include <finevox/block_atlas.hpp>
#include <finevox/block_type.hpp>
#include <finevox/string_interner.hpp>
#include <finevox/resource_locator.hpp>

#include <finevk/finevk.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/engine/camera.hpp>

#include <iostream>
#include <cmath>

using namespace finevox;

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

    std::cout << "Building test world...\n";
    std::cout << "  Block IDs: stone=" << stone.id << " dirt=" << dirt.id
              << " grass=" << grass.id << " cobble=" << cobble.id << "\n";

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
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--large-coords") {
            startAtLargeCoords = true;
        }
        if (std::string(argv[i]) == "--single-block") {
            singleBlockMode = true;
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

        worldRenderer.setBlockAtlas(atlas.texture());
        worldRenderer.setTextureProvider(atlas.createProvider());
        worldRenderer.initialize();

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
        });

        // Mouse button callback
        window->onMouseButton([&](finevk::MouseButton button, finevk::Action action, finevk::Modifier) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == finevk::Action::Press) {
                if (!cursorCaptured) {
                    window->setMouseCaptured(true);
                    cursorCaptured = true;
                    auto mousePos = window->mousePosition();
                    lastMouseX = mousePos.x;
                    lastMouseY = mousePos.y;
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
        std::cout << "  F1: Toggle debug camera offset\n";
        std::cout << "  F2: Teleport to large coords (1M)\n";
        std::cout << "  F3: Teleport to origin\n";
        std::cout << "  F4: Toggle hidden face culling (debug)\n";
        std::cout << "  Click: Capture mouse\n";
        std::cout << "  Escape: Release mouse / Exit\n";
        std::cout << "\nUse --single-block flag for minimal test with one block\n\n";

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

            // Update world renderer - camera.positionD() provides high-precision position
            worldRenderer.updateCamera(camera.state(), camera.positionD());
            worldRenderer.updateMeshes(16);  // Max 16 mesh updates per frame

            // Render
            auto result = renderer->beginFrame();
            if (result.success) {
                renderer->beginRenderPass({0.2f, 0.3f, 0.4f, 1.0f});  // Sky blue

                // Render the world
                worldRenderer.render(*result.commandBuffer);

                renderer->endRenderPass();
                renderer->endFrame();
            }
        }

        std::cout << "\n\nShutting down...\n";
        renderer->waitIdle();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
