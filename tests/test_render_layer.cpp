#include <gtest/gtest.h>
#include "finevox/core/render_layer.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace finevox;

// ============================================================================
// Mock render layer for testing
// ============================================================================

class MockRenderLayer : public RenderLayer {
public:
    MockRenderLayer(std::string n, RenderPhase p, int32_t pri,
                    std::vector<std::string>& log)
        : name_(std::move(n)), phase_(p), priority_(pri), log_(log) {}

    std::string_view name() const override { return name_; }
    RenderPhase phase() const override { return phase_; }
    int32_t priority() const override { return priority_; }
    bool isEnabled() const override { return enabled_; }

    void render(const RenderContext& ctx) override {
        log_.push_back(name_ + ":render");
    }

    void preRender(const RenderContext& ctx) override {
        log_.push_back(name_ + ":preRender");
    }

    void onResize(uint32_t w, uint32_t h) override {
        log_.push_back(name_ + ":resize");
        lastWidth_ = w;
        lastHeight_ = h;
    }

    bool enabled_ = true;
    uint32_t lastWidth_ = 0;
    uint32_t lastHeight_ = 0;

private:
    std::string name_;
    RenderPhase phase_;
    int32_t priority_;
    std::vector<std::string>& log_;
};

// ============================================================================
// RenderPhase ordering tests
// ============================================================================

TEST(RenderLayerTest, PhaseOrdering) {
    EXPECT_LT(static_cast<uint8_t>(RenderPhase::Opaque3D),
              static_cast<uint8_t>(RenderPhase::Translucent3D));
    EXPECT_LT(static_cast<uint8_t>(RenderPhase::Translucent3D),
              static_cast<uint8_t>(RenderPhase::Overlay2D));
    EXPECT_LT(static_cast<uint8_t>(RenderPhase::Overlay2D),
              static_cast<uint8_t>(RenderPhase::GUI));
}

// ============================================================================
// Layer sorting tests
// ============================================================================

TEST(RenderLayerTest, SortByPhase) {
    std::vector<std::string> log;

    auto gui = std::make_shared<MockRenderLayer>("GUI", RenderPhase::GUI, 100, log);
    auto opaque = std::make_shared<MockRenderLayer>("Opaque", RenderPhase::Opaque3D, 100, log);
    auto overlay = std::make_shared<MockRenderLayer>("Overlay", RenderPhase::Overlay2D, 100, log);
    auto trans = std::make_shared<MockRenderLayer>("Trans", RenderPhase::Translucent3D, 100, log);

    std::vector<std::shared_ptr<RenderLayer>> layers = {gui, opaque, overlay, trans};

    std::sort(layers.begin(), layers.end(),
        [](const auto& a, const auto& b) {
            if (a->phase() != b->phase())
                return static_cast<uint8_t>(a->phase()) < static_cast<uint8_t>(b->phase());
            return a->priority() < b->priority();
        });

    EXPECT_EQ(layers[0]->name(), "Opaque");
    EXPECT_EQ(layers[1]->name(), "Trans");
    EXPECT_EQ(layers[2]->name(), "Overlay");
    EXPECT_EQ(layers[3]->name(), "GUI");
}

TEST(RenderLayerTest, SortByPriorityWithinPhase) {
    std::vector<std::string> log;

    auto high = std::make_shared<MockRenderLayer>("High", RenderPhase::Opaque3D, 200, log);
    auto low = std::make_shared<MockRenderLayer>("Low", RenderPhase::Opaque3D, 50, log);
    auto mid = std::make_shared<MockRenderLayer>("Mid", RenderPhase::Opaque3D, 100, log);

    std::vector<std::shared_ptr<RenderLayer>> layers = {high, low, mid};

    std::sort(layers.begin(), layers.end(),
        [](const auto& a, const auto& b) {
            if (a->phase() != b->phase())
                return static_cast<uint8_t>(a->phase()) < static_cast<uint8_t>(b->phase());
            return a->priority() < b->priority();
        });

    EXPECT_EQ(layers[0]->name(), "Low");
    EXPECT_EQ(layers[1]->name(), "Mid");
    EXPECT_EQ(layers[2]->name(), "High");
}

// ============================================================================
// Dispatch simulation tests
// ============================================================================

TEST(RenderLayerTest, RenderDispatchOrder) {
    std::vector<std::string> log;

    auto gui = std::make_shared<MockRenderLayer>("GUI", RenderPhase::GUI, 100, log);
    auto opaque = std::make_shared<MockRenderLayer>("Opaque", RenderPhase::Opaque3D, 100, log);
    auto trans = std::make_shared<MockRenderLayer>("Trans", RenderPhase::Translucent3D, 100, log);

    std::vector<std::shared_ptr<RenderLayer>> layers = {gui, opaque, trans};

    std::sort(layers.begin(), layers.end(),
        [](const auto& a, const auto& b) {
            if (a->phase() != b->phase())
                return static_cast<uint8_t>(a->phase()) < static_cast<uint8_t>(b->phase());
            return a->priority() < b->priority();
        });

    RenderContext ctx{};
    for (auto& layer : layers) {
        if (layer->isEnabled()) {
            layer->render(ctx);
        }
    }

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "Opaque:render");
    EXPECT_EQ(log[1], "Trans:render");
    EXPECT_EQ(log[2], "GUI:render");
}

TEST(RenderLayerTest, DisabledLayerSkipped) {
    std::vector<std::string> log;

    auto a = std::make_shared<MockRenderLayer>("A", RenderPhase::Opaque3D, 100, log);
    auto b = std::make_shared<MockRenderLayer>("B", RenderPhase::Opaque3D, 200, log);
    b->enabled_ = false;

    std::vector<std::shared_ptr<RenderLayer>> layers = {a, b};

    RenderContext ctx{};
    for (auto& layer : layers) {
        if (layer->isEnabled()) {
            layer->render(ctx);
        }
    }

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "A:render");
}

TEST(RenderLayerTest, PreRenderCalledBeforeRender) {
    std::vector<std::string> log;
    auto layer = std::make_shared<MockRenderLayer>("World", RenderPhase::Opaque3D, 100, log);

    RenderContext ctx{};
    layer->preRender(ctx);
    layer->render(ctx);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "World:preRender");
    EXPECT_EQ(log[1], "World:render");
}

// ============================================================================
// RenderContext tests
// ============================================================================

TEST(RenderLayerTest, RenderContextDefaults) {
    RenderContext ctx{};

    EXPECT_EQ(ctx.commandBuffer, nullptr);
    EXPECT_EQ(ctx.width, 0u);
    EXPECT_EQ(ctx.height, 0u);
    EXPECT_EQ(ctx.frameIndex, 0u);
    EXPECT_EQ(ctx.cameraState, nullptr);
    EXPECT_FLOAT_EQ(ctx.deltaTime, 0.0f);
    EXPECT_FLOAT_EQ(ctx.timeOfDay, 0.0f);
    EXPECT_EQ(ctx.world, nullptr);
}

TEST(RenderLayerTest, OnResizeReceivesDimensions) {
    std::vector<std::string> log;
    auto layer = std::make_shared<MockRenderLayer>("Layer", RenderPhase::Opaque3D, 100, log);

    layer->onResize(1920, 1080);
    EXPECT_EQ(layer->lastWidth_, 1920u);
    EXPECT_EQ(layer->lastHeight_, 1080u);
}

// ============================================================================
// Default implementations
// ============================================================================

TEST(RenderLayerTest, DefaultPriority) {
    class MinimalLayer : public RenderLayer {
    public:
        std::string_view name() const override { return "Minimal"; }
        RenderPhase phase() const override { return RenderPhase::Opaque3D; }
        void render(const RenderContext&) override {}
    };

    MinimalLayer layer;
    EXPECT_EQ(layer.priority(), 100);
    EXPECT_TRUE(layer.isEnabled());
}

TEST(RenderLayerTest, SharedFromThis) {
    std::vector<std::string> log;
    auto layer = std::make_shared<MockRenderLayer>("Test", RenderPhase::Opaque3D, 100, log);

    auto shared = layer->shared_from_this();
    EXPECT_EQ(shared.get(), layer.get());
}
