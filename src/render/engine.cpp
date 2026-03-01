#include "finevox/render/engine.hpp"
#include "finevox/render/frame_fence_waiter.hpp"

#include <finevk/finevk.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/engine/input_manager.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace finevox {

// ============================================================================
// Engine::Impl
// ============================================================================

struct Engine::Impl {
    // Vulkan infrastructure (order matters for destruction)
    finevk::InstancePtr instance;
    finevk::WindowPtr window;
    finevk::PhysicalDevice physicalDevice;  // Must outlive LogicalDevice (stores raw pointer)
    finevk::LogicalDevicePtr device;
    std::unique_ptr<finevk::SimpleRenderer> renderer;

    // Input
    std::unique_ptr<finevk::InputManager> inputManager;

    // Fence waiter for frame overlap
    FrameFenceWaiter fenceWaiter;
    WakeSignal* fenceWakeSignal = nullptr;

    // Render layers (sorted by phase/priority)
    std::vector<std::shared_ptr<RenderLayer>> layers;

    // Callbacks
    FrameCallbacks callbacks;

    // State
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};

    // Timing
    std::chrono::high_resolution_clock::time_point lastFrameTime;

    // Sort layers by (phase, priority)
    void sortLayers() {
        std::sort(layers.begin(), layers.end(),
            [](const std::shared_ptr<RenderLayer>& a,
               const std::shared_ptr<RenderLayer>& b) {
                if (a->phase() != b->phase())
                    return static_cast<uint8_t>(a->phase()) <
                           static_cast<uint8_t>(b->phase());
                return a->priority() < b->priority();
            });
    }
};

// ============================================================================
// Engine lifecycle
// ============================================================================

Engine::Engine(const EngineConfig& config) : impl_(std::make_unique<Impl>()) {
    // Create Vulkan instance
    impl_->instance = finevk::Instance::create()
        .applicationName(config.windowTitle)
        .applicationVersion(1, 0, 0)
        .enableValidation(config.enableValidation)
        .build();

    // Create window
    impl_->window = finevk::Window::create(impl_->instance)
        .title(config.windowTitle)
        .size(config.windowWidth, config.windowHeight)
        .resizable(true)
        .build();

    // Select GPU and create device (stored in Impl — LogicalDevice holds raw PhysicalDevice*)
    impl_->physicalDevice = impl_->instance->selectPhysicalDevice(impl_->window);
    impl_->device = impl_->physicalDevice.createLogicalDevice()
        .surface(impl_->window->surface())
        .enableAnisotropy()
        .build();

    impl_->window->bindDevice(impl_->device);

    // Create renderer with depth buffer
    finevk::RendererConfig renderConfig;
    renderConfig.enableDepthBuffer = true;
    renderConfig.msaa = finevk::MSAALevel::Medium;
    impl_->renderer = finevk::SimpleRenderer::create(
        impl_->window.get(), renderConfig);

    // Create input manager
    impl_->inputManager = finevk::InputManager::create(impl_->window.get());

    // Set up fence waiter
    impl_->fenceWaiter.setRenderer(impl_->renderer.get());

    // Resize callback
    impl_->window->onResize([this](uint32_t width, uint32_t height) {
        if (width > 0 && height > 0) {
            // Notify layers
            for (auto& layer : impl_->layers) {
                layer->onResize(width, height);
            }
            // Notify game code
            if (impl_->callbacks.onResize) {
                impl_->callbacks.onResize(width, height);
            }
        }
    });
}

Engine::~Engine() {
    // Stop the frame loop if running
    requestStop();

    // Detach layers in reverse order
    for (auto it = impl_->layers.rbegin(); it != impl_->layers.rend(); ++it) {
        (*it)->onDetach();
    }

    // Stop fence waiter
    impl_->fenceWaiter.stop();

    // Wait for GPU to finish before destroying resources
    if (impl_->renderer) {
        impl_->renderer->waitIdle();
    }
}

// ============================================================================
// Layer management
// ============================================================================

void Engine::addRenderLayer(std::shared_ptr<RenderLayer> layer) {
    layer->onAttach(*impl_->device, *impl_->renderer);
    impl_->layers.push_back(std::move(layer));
    impl_->sortLayers();
}

void Engine::removeRenderLayer(const std::shared_ptr<RenderLayer>& layer) {
    auto it = std::find(impl_->layers.begin(), impl_->layers.end(), layer);
    if (it != impl_->layers.end()) {
        (*it)->onDetach();
        impl_->layers.erase(it);
    }
}

const std::vector<std::shared_ptr<RenderLayer>>& Engine::renderLayers() const {
    return impl_->layers;
}

// ============================================================================
// Configuration
// ============================================================================

void Engine::setFrameCallbacks(FrameCallbacks callbacks) {
    impl_->callbacks = std::move(callbacks);
}

void Engine::setFenceWakeSignal(WakeSignal* signal) {
    impl_->fenceWakeSignal = signal;
}

// ============================================================================
// Accessors
// ============================================================================

finevk::Window& Engine::window() const { return *impl_->window; }
finevk::LogicalDevice& Engine::device() const { return *impl_->device; }
finevk::SimpleRenderer& Engine::renderer() const { return *impl_->renderer; }
finevk::InputManager& Engine::inputManager() const { return *impl_->inputManager; }

// ============================================================================
// Frame loop
// ============================================================================

void Engine::run() {
    impl_->running.store(true, std::memory_order_release);
    impl_->stopRequested.store(false, std::memory_order_release);

    // Start fence waiter thread
    if (impl_->fenceWakeSignal) {
        impl_->fenceWaiter.attach(impl_->fenceWakeSignal);
    }
    impl_->fenceWaiter.start();

    impl_->lastFrameTime = std::chrono::high_resolution_clock::now();

    while (impl_->window->isOpen() &&
           !impl_->stopRequested.load(std::memory_order_acquire)) {

        // ================================================================
        // Phase 1: Fence wait + preRender overlap
        // ================================================================
        if (impl_->fenceWakeSignal) {
            // Async fence wait with mesh processing overlap
            impl_->fenceWaiter.kickWait();

            // Call preRender repeatedly while fence is pending.
            // This allows WorldRenderLayer to process mesh uploads
            // during GPU idle time (fence wait overlap pattern).
            RenderContext preCtx{};
            while (!impl_->fenceWaiter.isReady()) {
                for (auto& layer : impl_->layers) {
                    if (layer->isEnabled()) {
                        layer->preRender(preCtx);
                    }
                }
            }

            // Final drain: process any remaining work
            for (auto& layer : impl_->layers) {
                if (layer->isEnabled()) {
                    layer->preRender(preCtx);
                }
            }

            // Detach during render to prevent spurious wakes
            impl_->fenceWaiter.detach();
        }

        // ================================================================
        // Phase 2: Input + update
        // ================================================================
        impl_->window->pollEvents();

        // Calculate delta time
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - impl_->lastFrameTime).count();
        impl_->lastFrameTime = now;

        // Game update callback runs before inputManager->update() so that
        // onUpdate can read mouseDelta() before it is cleared.
        if (impl_->callbacks.onUpdate) {
            impl_->callbacks.onUpdate(dt);
        }

        // Dispatch accumulated input events to listeners and clear per-frame state
        impl_->inputManager->update();

        // ================================================================
        // Phase 3: Render
        // ================================================================
        glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        if (impl_->callbacks.getClearColor) {
            clearColor = impl_->callbacks.getClearColor();
        }

        bool skipFenceWait = (impl_->fenceWakeSignal != nullptr);
        if (auto frame = impl_->renderer->beginFrame(skipFenceWait)) {
            // Build render context
            RenderContext ctx{};
            ctx.commandBuffer = &static_cast<finevk::CommandBuffer&>(frame);
            ctx.width = frame.extent.width;
            ctx.height = frame.extent.height;
            ctx.frameIndex = frame.frameIndex();
            ctx.deltaTime = dt;

            frame.beginRenderPass(clearColor);

            // Dispatch render() on all enabled layers
            for (auto& layer : impl_->layers) {
                if (layer->isEnabled()) {
                    layer->render(ctx);
                }
            }

            frame.endRenderPass();
            impl_->renderer->endFrame();

            // Re-attach fence waiter for next frame
            if (impl_->fenceWakeSignal) {
                impl_->fenceWaiter.attach(impl_->fenceWakeSignal);
            }
        }

        // Post-render callback
        if (impl_->callbacks.onPostRender) {
            impl_->callbacks.onPostRender();
        }
    }

    // Shutdown fence waiter
    impl_->fenceWaiter.requestStop();
    impl_->fenceWaiter.join();

    impl_->running.store(false, std::memory_order_release);
}

void Engine::requestStop() {
    impl_->stopRequested.store(true, std::memory_order_release);
}

bool Engine::isRunning() const {
    return impl_->running.load(std::memory_order_acquire);
}

}  // namespace finevox
