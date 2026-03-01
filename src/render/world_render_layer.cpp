#include "finevox/render/world_render_layer.hpp"
#include "finevox/render/world_renderer.hpp"

#include <chrono>

namespace finevox::render {

WorldRenderLayer::WorldRenderLayer(WorldRenderer& renderer)
    : renderer_(renderer) {}

void WorldRenderLayer::preRender(const finevox::RenderContext& /*ctx*/) {
    // Process mesh uploads during fence wait overlap.
    // The Engine calls preRender repeatedly while the GPU fence is pending,
    // so each call processes one batch with a short timeout.
    renderer_.waitForMeshUploads(std::chrono::milliseconds(5));
    renderer_.updateMeshes(0);
}

void WorldRenderLayer::render(const finevox::RenderContext& ctx) {
    if (ctx.commandBuffer) {
        renderer_.render(*ctx.commandBuffer);
    }
}

}  // namespace finevox::render
