#pragma once

/**
 * @file engine.hpp
 * @brief Frame loop and rendering infrastructure
 *
 * Engine owns the Vulkan setup (instance, device, renderer, window),
 * input manager, and fence waiter. It drives the frame loop and dispatches
 * registered RenderLayers by (phase, priority) order.
 *
 * The frame loop:
 *   1. Kick async fence wait
 *   2. Call preRender() on all layers (mesh processing overlaps with fence)
 *   3. Poll input, call onUpdate callback
 *   4. beginFrame(skipFenceWait=true)
 *   5. beginRenderPass
 *   6. Dispatch render() on all layers in order
 *   7. endRenderPass, endFrame
 *   8. Re-attach fence waiter
 */

#include "finevox/core/render_layer.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace finevk {
class Window;
class LogicalDevice;
class SimpleRenderer;
class InputManager;
class Overlay2D;
}  // namespace finevk

namespace finevox {

// Forward declarations
class GameSession;
class WakeSignal;

// ============================================================================
// EngineConfig
// ============================================================================

struct EngineConfig {
    std::string windowTitle = "FineStructureVoxel";
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
    bool enableValidation = false;
};

// ============================================================================
// FrameCallbacks
// ============================================================================

/**
 * @brief Per-frame callbacks from the Engine to game-specific code
 *
 * These run at specific points in the frame loop. All execute on the
 * main/graphics thread.
 */
struct FrameCallbacks {
    /// Called after input polling, before rendering.
    /// Use for: player controller, camera, physics, audio, entity state.
    std::function<void(float dt)> onUpdate;

    /// Called after rendering completes each frame.
    /// Use for: debug stats, frame timing, profiling.
    std::function<void()> onPostRender;

    /// Called when the window is resized.
    /// Use for: camera aspect ratio updates.
    std::function<void(uint32_t width, uint32_t height)> onResize;

    /// Called to determine the clear color for the render pass.
    /// If not set, defaults to black.
    std::function<glm::vec4()> getClearColor;
};

// ============================================================================
// Engine
// ============================================================================

class Engine {
public:
    explicit Engine(const EngineConfig& config = {});
    ~Engine();

    // Non-copyable, non-movable
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // === Setup (before run()) ===

    /// Add a render layer. Sorted by (phase, priority) at insertion time.
    void addRenderLayer(std::shared_ptr<RenderLayer> layer);

    /// Remove a render layer.
    void removeRenderLayer(const std::shared_ptr<RenderLayer>& layer);

    /// Get all registered layers (in render order)
    [[nodiscard]] const std::vector<std::shared_ptr<RenderLayer>>& renderLayers() const;

    /// Set per-frame callbacks
    void setFrameCallbacks(FrameCallbacks callbacks);

    /// Attach a WakeSignal that the fence waiter uses for mesh overlap.
    /// If not set, fence wait overlap is disabled (simpler but less efficient).
    void setFenceWakeSignal(WakeSignal* signal);

    // === Accessors ===

    [[nodiscard]] finevk::Window& window() const;
    [[nodiscard]] finevk::LogicalDevice& device() const;
    [[nodiscard]] finevk::SimpleRenderer& renderer() const;
    [[nodiscard]] finevk::InputManager& inputManager() const;

    // === Frame Loop ===

    /// Run the main loop. Blocks until the window is closed or requestStop().
    void run();

    /// Request the main loop to stop (callable from any thread).
    void requestStop();

    /// Check if the main loop is currently running.
    [[nodiscard]] bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finevox
