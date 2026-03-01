#pragma once

/**
 * @file render_layer.hpp
 * @brief Pluggable render layer system
 *
 * RenderLayer provides a registration-based rendering pipeline.
 * Layers are sorted by (phase, priority) and dispatched by the Engine.
 *
 * Phases:
 *   Opaque3D      - World geometry with depth writes
 *   Translucent3D - Water/glass with alpha blending (depth-write OFF)
 *   Overlay2D     - HUD, crosshair, debug overlays
 *   GUI           - finegui/ImGui (drawn last)
 */

#include "finevox/core/sky.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

// Forward declarations for finevk types (pointers only in RenderContext)
namespace finevk {
class CommandBuffer;
class LogicalDevice;
class SimpleRenderer;
struct CameraState;
}  // namespace finevk

namespace finevox {

// Forward declarations
class World;

// ============================================================================
// RenderPhase
// ============================================================================

enum class RenderPhase : uint8_t {
    Opaque3D      = 0,  ///< World geometry, depth write ON
    Translucent3D = 1,  ///< Water/glass, depth write OFF, alpha blend
    Overlay2D     = 2,  ///< HUD, crosshair, debug
    GUI           = 3,  ///< finegui (drawn last)
};

// ============================================================================
// RenderContext
// ============================================================================

/**
 * @brief Per-frame rendering context passed to each RenderLayer
 *
 * Provides everything a layer needs to render: command buffer,
 * frame dimensions, camera state, sky parameters, and world access.
 */
struct RenderContext {
    finevk::CommandBuffer* commandBuffer = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameIndex = 0;

    const finevk::CameraState* cameraState = nullptr;
    glm::dvec3 cameraPositionD{0.0};

    SkyParameters sky{};
    float deltaTime = 0.0f;
    float timeOfDay = 0.0f;

    const World* world = nullptr;
};

// ============================================================================
// RenderLayer
// ============================================================================

/**
 * @brief Base class for pluggable render layers
 *
 * Layers are registered with the Engine and dispatched each frame
 * in (phase, priority) order. Lower priority values run first.
 *
 * Lifecycle:
 *   1. onAttach() - called when added to Engine (GPU resources setup)
 *   2. preRender() - called before render pass begins (compute, uploads)
 *   3. render() - called inside render pass (draw commands)
 *   4. onResize() - called when framebuffer size changes
 *   5. onDetach() - called when removed or Engine shuts down
 */
class RenderLayer : public std::enable_shared_from_this<RenderLayer> {
public:
    virtual ~RenderLayer() = default;

    /// Human-readable name for debugging
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Which render phase this layer belongs to
    [[nodiscard]] virtual RenderPhase phase() const = 0;

    /// Priority within phase. Lower values run first.
    [[nodiscard]] virtual int32_t priority() const { return 100; }

    /// Whether this layer is currently enabled
    [[nodiscard]] virtual bool isEnabled() const { return true; }

    // === Lifecycle ===

    /// Called when the layer is added to the Engine
    virtual void onAttach(finevk::LogicalDevice& device,
                          finevk::SimpleRenderer& renderer) {}

    /// Called when the layer is removed from the Engine
    virtual void onDetach() {}

    /// Called before the render pass begins (compute dispatches, uploads)
    virtual void preRender(const RenderContext& ctx) {}

    /// Called inside the render pass (draw commands)
    virtual void render(const RenderContext& ctx) = 0;

    /// Called when the framebuffer is resized
    virtual void onResize(uint32_t width, uint32_t height) {}
};

}  // namespace finevox
