#include "finevox/render/overlay_render_layer.hpp"

#include <finevk/engine/overlay2d.hpp>

namespace finevox::render {

OverlayRenderLayer::OverlayRenderLayer(finevk::Overlay2D& overlay)
    : overlay_(overlay) {}

void OverlayRenderLayer::render(const finevox::RenderContext& ctx) {
    if (!ctx.commandBuffer) return;

    overlay_.beginFrame(ctx.frameIndex, ctx.width, ctx.height);

    float centerX = static_cast<float>(ctx.width) / 2.0f;
    float centerY = static_cast<float>(ctx.height) / 2.0f;
    overlay_.drawCrosshair(centerX, centerY, crosshairSize_,
                           crosshairThickness_, crosshairColor_);

    overlay_.render(*ctx.commandBuffer);
}

}  // namespace finevox::render
