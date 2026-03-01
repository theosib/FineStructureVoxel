#pragma once

#include "finevox/core/render_layer.hpp"

namespace finevox::render {

class WorldRenderer;

/**
 * @brief RenderLayer that wraps WorldRenderer for opaque + fluid geometry
 *
 * Handles both opaque and translucent (fluid) rendering in one pass,
 * since WorldRenderer::render() draws both internally.
 *
 * During fence wait overlap, preRender() processes queued mesh uploads
 * to minimize GPU idle time.
 */
class WorldRenderLayer : public finevox::RenderLayer {
public:
    explicit WorldRenderLayer(WorldRenderer& renderer);

    std::string_view name() const override { return "World"; }
    RenderPhase phase() const override { return RenderPhase::Opaque3D; }
    int32_t priority() const override { return 0; }

    void preRender(const finevox::RenderContext& ctx) override;
    void render(const finevox::RenderContext& ctx) override;

private:
    WorldRenderer& renderer_;
};

}  // namespace finevox::render
