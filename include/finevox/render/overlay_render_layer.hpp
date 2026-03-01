#pragma once

#include "finevox/core/render_layer.hpp"

#include <glm/vec4.hpp>

namespace finevk {
class Overlay2D;
}

namespace finevox::render {

/**
 * @brief RenderLayer that draws a crosshair overlay
 *
 * Wraps finevk::Overlay2D for 2D overlay rendering.
 * Can be enabled/disabled by game code (e.g., hide during menus).
 */
class OverlayRenderLayer : public finevox::RenderLayer {
public:
    explicit OverlayRenderLayer(finevk::Overlay2D& overlay);

    std::string_view name() const override { return "Overlay"; }
    RenderPhase phase() const override { return RenderPhase::Overlay2D; }
    int32_t priority() const override { return 0; }
    bool isEnabled() const override { return enabled_; }

    void render(const finevox::RenderContext& ctx) override;

    /// Toggle overlay visibility (e.g., hide during menu/chat)
    void setEnabled(bool enabled) { enabled_ = enabled; }

    /// Configure crosshair appearance
    void setCrosshairSize(float size) { crosshairSize_ = size; }
    void setCrosshairThickness(float thickness) { crosshairThickness_ = thickness; }
    void setCrosshairColor(const glm::vec4& color) { crosshairColor_ = color; }

private:
    finevk::Overlay2D& overlay_;
    bool enabled_ = true;
    float crosshairSize_ = 20.0f;
    float crosshairThickness_ = 2.0f;
    glm::vec4 crosshairColor_{1.0f, 1.0f, 1.0f, 0.8f};
};

}  // namespace finevox::render
