# FineGUI Design Document

A standalone GUI toolkit built on Dear ImGui with a finevk (Vulkan) backend, designed for game applications with message-passing architecture.

## 1. Overview

### 1.1 Goals

- **Standalone library**: Usable by any finevk-based project, not coupled to finevoxel
- **Message-passing ready**: Clean interface for state updates, suitable for networked games
- **Threading flexible**: Works same-thread (low latency) or separate thread (isolated from GLFW/finevk)
- **3D-in-GUI support**: Render 3D scenes to textures for inventory items, previews, etc.
- **Minimal dependencies**: Only requires finevk and Dear ImGui
- **Abstracted input layer**: Custom input event format, decoupled from GLFW

### 1.2 Non-Goals

- Not a full UI framework (no complex layouts, CSS, data binding)
- Not a replacement for ImGui - it's a wrapper that adds structure
- Not handling game-specific widgets (those belong in the consuming project)
- Not replacing finevk::Overlay2D - they serve different purposes:
  - finegui: Complex interactive UI via ImGui (menus, dialogs, inventory)
  - Overlay2D: Simple HUD elements (crosshairs, health bars) without ImGui overhead
  - Both can coexist; render Overlay2D after finegui in the same pass
- No docking support - video games use fixed menu/overlay positions

### 1.3 Dependency Graph

```
finevk/           (Vulkan abstraction)
    ↓
finegui/          (This library)
    ↓
finevoxel/        (Voxel engine - example consumer)
    └── gui/      (Game-specific: inventory, HUD)
```

## 2. Architecture

### 2.1 Core Components

```
finegui/
├── include/finegui/
│   ├── gui_system.hpp        # Main interface
│   ├── gui_config.hpp        # Configuration types
│   ├── gui_state.hpp         # State update types
│   ├── gui_draw_data.hpp     # Serializable draw output
│   ├── input_adapter.hpp     # Abstracted input layer
│   └── texture_handle.hpp    # 3D texture references
├── src/
│   ├── gui_system.cpp        # ImGui wrapper implementation
│   ├── input_adapter.cpp     # Input translation to ImGui
│   ├── imgui_backend_finevk.cpp  # finevk rendering backend
│   └── imgui_backend_finevk.hpp
└── third_party/
    └── imgui/                # Dear ImGui (vendored or submodule)
```

### 2.2 Data Flow

**Same-thread mode** (recommended default):
```
Input Events → GuiSystem::processInput()
                    ↓
Game State   → GuiSystem::beginFrame() / widgets / endFrame()
                    ↓
             → GuiSystem::render(commandBuffer)
```

**Threaded mode** (GUI isolated from GLFW/finevk):
```
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│ Main Thread │      │ GUI Thread  │      │ Render      │
│ (GLFW)      │─────▶│             │─────▶│ Thread      │
│             │ queue│ ImGui logic │ queue│ finevk cmds │
└─────────────┘      └─────────────┘      └─────────────┘
         InputEvent        GuiDrawData
```

The abstracted input layer allows the GUI thread to operate completely independently of GLFW, receiving events in finegui's own format.

## 3. Core Types

### 3.1 Abstracted Input Events

FineGUI defines its own input event types, decoupled from GLFW and finevk::InputManager. This enables:
- Threading: GUI thread doesn't touch GLFW
- Flexibility: Input can come from any source (GLFW, network replay, testing)
- Isolation: No dependency on window system in GUI logic

```cpp
namespace finegui {

enum class InputEventType {
    MouseMove,
    MouseButton,
    MouseScroll,
    Key,
    Char,
    Focus,
    WindowResize
};

/// Abstracted input event - independent of GLFW/finevk
struct InputEvent {
    InputEventType type;

    // Mouse data
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    int button = 0;        // 0=left, 1=right, 2=middle
    bool pressed = false;

    // Keyboard data
    int keyCode = 0;       // ImGui key code (ImGuiKey_*)
    bool keyPressed = false;
    uint32_t character = 0; // Unicode codepoint for Char events

    // Modifiers (always valid)
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool super = false;

    // Window state
    int windowWidth = 0;
    int windowHeight = 0;
    bool focused = true;

    // Timestamp (optional, for replay)
    double time = 0.0;
};

/// Adapter to convert finevk::InputEvent to finegui::InputEvent
class InputAdapter {
public:
    /// Convert a single finevk event
    static InputEvent fromFineVK(const finevk::InputEvent& event);

    /// Process finevk::InputManager and extract all pending events
    static std::vector<InputEvent> fromInputManager(finevk::InputManager& input);

    /// Convert GLFW key code to ImGui key code
    static int glfwKeyToImGui(int glfwKey);
};

} // namespace finegui
```

**Integration with finevk::InputManager:**

```cpp
// In main thread (same-thread mode)
auto input = finevk::InputManager::create(window.get());

// Each frame
input->update();

// Option 1: Convert events as they're polled
finevk::InputEvent fvEvent;
while (input->pollEvent(fvEvent)) {
    if (!gui.wantCaptureMouse() || fvEvent.type != finevk::InputEventType::MouseMove) {
        // Game handles input
    }
    gui.processInput(finegui::InputAdapter::fromFineVK(fvEvent));
}

// Option 2: Batch conversion for threaded mode
auto guiEvents = finegui::InputAdapter::fromInputManager(*input);
guiInputQueue.pushBatch(guiEvents);
```

### 3.2 State Updates

Game logic sends state updates to GUI rather than GUI querying game state. This decouples systems and enables networking.

```cpp
namespace finegui {

// Base class for state updates
struct GuiStateUpdate {
    virtual ~GuiStateUpdate() = default;
    virtual uint32_t typeId() const = 0;
};

// Type-safe state update with automatic type ID
template<typename T>
struct TypedStateUpdate : GuiStateUpdate {
    static uint32_t staticTypeId();
    uint32_t typeId() const override { return staticTypeId(); }
};

// Example state updates (defined by consuming project)
// struct HealthUpdate : TypedStateUpdate<HealthUpdate> {
//     float current, max;
// };
// struct InventoryUpdate : TypedStateUpdate<InventoryUpdate> {
//     std::vector<ItemStack> items;
// };

} // namespace finegui
```

### 3.3 Texture Handles

For 3D-in-GUI, textures rendered by the main application are referenced by handle.

```cpp
namespace finegui {

// Opaque handle to a texture that can be displayed in GUI
struct TextureHandle {
    uint64_t id = 0;           // Unique identifier
    uint32_t width = 0;
    uint32_t height = 0;

    bool valid() const { return id != 0; }

    // Implicit conversion to ImTextureID for ImGui::Image()
    operator ImTextureID() const { return reinterpret_cast<ImTextureID>(id); }
};

} // namespace finegui
```

### 3.4 Draw Data (for threaded mode)

```cpp
namespace finegui {

// Serializable draw command
struct DrawCommand {
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t vertexOffset;
    TextureHandle texture;
    glm::ivec4 scissorRect;  // x, y, width, height
};

// Complete frame's draw data - can be queued between threads
struct GuiDrawData {
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    std::vector<DrawCommand> commands;
    glm::vec2 displaySize;
    glm::vec2 framebufferScale;

    bool empty() const { return commands.empty(); }
    void clear();
};

} // namespace finegui
```

## 4. Main Interface

### 4.1 Configuration

```cpp
namespace finegui {

struct GuiConfig {
    // Display
    float dpiScale = 0.0f;     // 0 = auto-detect from framebuffer scale
    float fontScale = 1.0f;    // Additional font scaling

    // Font configuration (flexible - use one approach)
    std::string fontPath;      // Load TTF from path (empty = ImGui default)
    float fontSize = 16.0f;    // Font size in pixels
    const void* fontData = nullptr;  // Or provide font data directly
    size_t fontDataSize = 0;

    // Behavior
    bool enableKeyboard = true;
    bool enableGamepad = false;

    // Performance
    uint32_t framesInFlight = 0;     // 0 = auto from device->framesInFlight()
    bool enableDrawDataCapture = false;  // For threaded mode

    // Rendering
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;  // Must match render pass
};

} // namespace finegui
```

### 4.2 GuiSystem

```cpp
namespace finegui {

class GuiSystem {
public:
    // ========================================================================
    // Creation
    // ========================================================================

    /// Create from SimpleRenderer (recommended) - auto-discovers settings
    static std::unique_ptr<GuiSystem> create(finevk::SimpleRenderer* renderer,
                                              const GuiConfig& config = {});
    static std::unique_ptr<GuiSystem> create(finevk::SimpleRenderer& renderer,
                                              const GuiConfig& config = {}) {
        return create(&renderer, config);
    }

    /// Create from device (manual setup required)
    explicit GuiSystem(finevk::LogicalDevice* device, const GuiConfig& config = {});
    ~GuiSystem();

    // Non-copyable, movable
    GuiSystem(const GuiSystem&) = delete;
    GuiSystem& operator=(const GuiSystem&) = delete;
    GuiSystem(GuiSystem&&) noexcept;
    GuiSystem& operator=(GuiSystem&&) noexcept;

    // ========================================================================
    // Setup (only needed if not using create(SimpleRenderer*))
    // ========================================================================

    /// Initialize with render pass info (call once after creation)
    void initialize(finevk::RenderPass* renderPass,
                    finevk::CommandPool* commandPool,
                    uint32_t subpass = 0);

    // Overloads for convenience
    void initialize(finevk::RenderPass& renderPass,
                    finevk::CommandPool& commandPool,
                    uint32_t subpass = 0) {
        initialize(&renderPass, &commandPool, subpass);
    }

    /// Register a texture for use in GUI (returns handle)
    TextureHandle registerTexture(finevk::Texture* texture,
                                  finevk::Sampler* sampler = nullptr);

    /// Unregister a texture
    void unregisterTexture(TextureHandle handle);

    // ========================================================================
    // Per-Frame: Input (abstracted - no GLFW dependency)
    // ========================================================================

    /// Process an input event (finegui's own format)
    void processInput(const InputEvent& event);

    /// Process multiple input events (batch)
    template<typename Container>
    void processInputBatch(const Container& events) {
        for (const auto& e : events) {
            processInput(e);
        }
    }

    // ========================================================================
    // Per-Frame: State Updates
    // ========================================================================

    /// Apply a state update (game → GUI)
    template<typename T>
    void applyState(const T& update);

    /// Register handler for a state update type
    template<typename T>
    void onStateUpdate(std::function<void(const T&)> handler);

    // ========================================================================
    // Per-Frame: Rendering
    // ========================================================================

    /// Begin a new frame - fully automatic (requires create(SimpleRenderer*))
    /// Gets frame index, deltaTime, and screen dimensions from renderer
    void beginFrame();

    /// Begin a new frame - semi-automatic (requires create(SimpleRenderer*))
    /// Gets frame index and screen dimensions from renderer
    void beginFrame(float deltaTime);

    /// Begin a new frame - manual mode
    /// Use when not created from SimpleRenderer, or for custom dimensions
    void beginFrame(float deltaTime, uint32_t width, uint32_t height);

    /// End frame and finalize draw data (call after all ImGui widgets)
    void endFrame();

    /// Render - automatic (requires create(SimpleRenderer*))
    /// Gets current command buffer from renderer
    void render();

    /// Render to command buffer (explicit)
    void render(finevk::CommandBuffer& cmd);
    void render(finevk::CommandBuffer* cmd) { render(*cmd); }

    /// Get draw data for external rendering (threaded mode)
    /// Only valid between endFrame() and next beginFrame()
    const GuiDrawData& getDrawData() const;

    /// Render from captured draw data (threaded mode, render thread)
    void renderDrawData(finevk::CommandBuffer& cmd, const GuiDrawData& data);

    // ========================================================================
    // Utilities
    // ========================================================================

    /// Check if GUI wants to capture mouse input
    bool wantCaptureMouse() const;

    /// Check if GUI wants to capture keyboard input
    bool wantCaptureKeyboard() const;

    /// Get ImGui context (for advanced usage - adding fonts, styles, etc.)
    ImGuiContext* imguiContext();

    /// Rebuild font atlas (call after adding fonts via imguiContext())
    void rebuildFontAtlas();

    /// Get the owning device
    finevk::LogicalDevice* device() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace finegui
```

### 4.3 Usage Example (Same-Thread with finevk::InputManager)

```cpp
// Initialization - create from SimpleRenderer (recommended)
auto input = finevk::InputManager::create(window.get());

finegui::GuiConfig config;
config.fontSize = 18.0f;
// msaaSamples and framesInFlight auto-discovered from renderer

auto gui = finegui::GuiSystem::create(renderer.get(), config);

// Game loop
while (running) {
    // Update finevk input manager
    input->update();

    // Convert and forward events to GUI
    finevk::InputEvent fvEvent;
    while (input->pollEvent(fvEvent)) {
        gui->processInput(finegui::InputAdapter::fromFineVK(fvEvent));

        // Game also handles input (check wantCapture for overlap)
        if (!gui->wantCaptureMouse()) {
            handleGameInput(fvEvent);
        }
    }

    // Apply state from game logic
    gui->applyState(HealthUpdate{player.health, player.maxHealth});

    // Begin render frame
    if (auto frame = renderer->beginFrame()) {
        renderer->beginRenderPass(clearColor);

        // 3D content
        worldRenderer.render(frame);

        // 2D GUI - no frame index or dimensions needed!
        gui->beginFrame();  // Auto-gets deltaTime and screen size from renderer
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::Text("FPS: %.1f", renderer->fps());
        ImGui::End();
        gui->endFrame();
        gui->render();  // Auto-gets command buffer from renderer

        renderer->endRenderPass();
        renderer->endFrame();
    }
}
```

### 4.4 Usage Example (Threaded - GUI Isolated from GLFW)

```cpp
// Shared queues
MessageQueue<finegui::InputEvent> inputQueue;
MessageQueue<finegui::GuiDrawData> drawQueue;

// Main thread: collect input, forward to GUI thread
void mainThread(finevk::InputManager& input) {
    while (running) {
        input->update();

        // Forward all events to GUI thread
        finevk::InputEvent fvEvent;
        while (input->pollEvent(fvEvent)) {
            inputQueue.push(finegui::InputAdapter::fromFineVK(fvEvent));
        }

        // Also push window size changes
        // ...
    }
}

// GUI thread: runs independently of GLFW
void guiThread(finegui::GuiSystem& gui) {
    while (running) {
        // Consume input (no GLFW calls here!)
        finegui::InputEvent event;
        while (inputQueue.tryPop(event)) {
            gui.processInput(event);
        }

        // Build frame
        gui.beginFrame(deltaTime);
        // ... widgets (all fixed positions, no docking) ...
        gui.endFrame();

        // Send draw data to render thread
        drawQueue.push(gui.getDrawData());
    }
}

// Render thread: receives draw data, issues Vulkan commands
void renderThread(finegui::GuiSystem& gui) {
    while (running) {
        auto drawData = drawQueue.pop();  // blocking

        renderer->beginRenderPass(cmd, clearColor);
        // ... render world ...
        gui.renderDrawData(cmd, drawData);
        renderer->endRenderPass(cmd);
    }
}
```

## 5. finevk Backend

The backend uses FineVK's abstractions rather than raw Vulkan calls, ensuring consistency with the rest of the ecosystem.

### 5.1 Resources Managed

- **Vertex/Index buffers**: Per-frame dynamic buffers using `finevk::Buffer`
- **Font texture**: Created from ImGui font atlas using `finevk::Texture`
- **Pipeline**: 2D rendering using `finevk::GraphicsPipeline::Builder`
- **Descriptor sets**: Using `finevk::DescriptorSetLayout`, `finevk::DescriptorPool`, `finevk::DescriptorWriter`
- **Sampler**: Default linear sampler using `finevk::Sampler`

### 5.2 Pipeline Creation (using FineVK)

```cpp
// Create descriptor layout
descriptorLayout_ = finevk::DescriptorSetLayout::create(device_)
    .combinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();

// Create pipeline using FineVK's builder
pipeline_ = finevk::GraphicsPipeline::create(device_, renderPass_)
    .vertexShader("shaders/gui.vert.spv")
    .fragmentShader("shaders/gui.frag.spv")
    .vertexBinding(0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX)
    .vertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos))
    .vertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv))
    .vertexAttribute(2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col))
    .cullNone()
    .alphaBlending()
    .disableDepthTest()
    .dynamicViewportAndScissor()
    .descriptorSetLayout(descriptorLayout_.get())
    .pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant))
    .msaaSamples(config_.msaaSamples)
    .build();
```

### 5.3 Buffer Management (using FineVK)

```cpp
// Per-frame buffers to avoid GPU/CPU synchronization
struct PerFrameData {
    finevk::BufferPtr vertexBuffer;
    finevk::BufferPtr indexBuffer;
    size_t vertexCapacity = 0;
    size_t indexCapacity = 0;
};
std::vector<PerFrameData> frameData_;  // One per framesInFlight

// Resize if needed
void ensureBufferCapacity(uint32_t frameIndex, size_t vertexCount, size_t indexCount) {
    auto& frame = frameData_[frameIndex];

    if (vertexCount > frame.vertexCapacity) {
        frame.vertexCapacity = vertexCount * 2;  // Growth factor
        frame.vertexBuffer = finevk::Buffer::createVertexBuffer(
            device_, frame.vertexCapacity * sizeof(ImDrawVert),
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    if (indexCount > frame.indexCapacity) {
        frame.indexCapacity = indexCount * 2;
        frame.indexBuffer = finevk::Buffer::createIndexBuffer(
            device_, frame.indexCapacity * sizeof(ImDrawIdx),
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
}
```

### 5.4 Descriptor Management (using FineVK)

```cpp
// Create pool from layout (auto-calculates pool sizes)
descriptorPool_ = finevk::DescriptorPool::fromLayout(descriptorLayout_.get(), maxTextures)
    .allowFree(true)
    .build();

// Allocate and write descriptor for a texture
VkDescriptorSet createTextureDescriptor(finevk::Texture* texture, finevk::Sampler* sampler) {
    auto set = descriptorPool_->allocate(descriptorLayout_.get());

    finevk::DescriptorWriter(device_)
        .writeImage(set, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    texture->imageView(), sampler)
        .update();

    return set;
}
```

### 5.5 Shader Interface

**Vertex shader:**
```glsl
#version 450

layout(push_constant) uniform PushConstants {
    vec2 scale;      // 2.0 / displaySize
    vec2 translate;  // -1.0
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;  // Already unpacked by Vulkan

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main() {
    outUV = inUV;
    outColor = inColor;
    gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
}
```

**Fragment shader:**
```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = inColor * texture(texSampler, inUV);
}
```

## 6. 3D-in-GUI Support

### 6.1 Workflow

1. **Application renders 3D scene to texture** (offscreen render pass)
2. **Application registers texture** with `gui.registerTexture(texture, sampler)`
3. **GUI displays texture** via `ImGui::Image(handle, size)`
4. **Backend resolves handle** to Vulkan descriptor during render

### 6.2 Texture Registry

```cpp
class TextureRegistry {
public:
    TextureHandle registerTexture(finevk::Texture* texture, finevk::Sampler* sampler) {
        TextureHandle handle;
        handle.id = nextId_++;
        handle.width = texture->width();
        handle.height = texture->height();

        TextureEntry entry;
        entry.texture = texture;
        entry.sampler = sampler ? sampler : defaultSampler_.get();
        entry.descriptor = createDescriptor(texture, entry.sampler);

        textures_[handle.id] = entry;
        return handle;
    }

    void unregister(TextureHandle handle) {
        auto it = textures_.find(handle.id);
        if (it != textures_.end()) {
            descriptorPool_->free(it->second.descriptor);
            textures_.erase(it);
        }
    }

    VkDescriptorSet getDescriptor(TextureHandle handle) {
        auto it = textures_.find(handle.id);
        return (it != textures_.end()) ? it->second.descriptor : fontDescriptor_;
    }

private:
    struct TextureEntry {
        finevk::Texture* texture;
        finevk::Sampler* sampler;
        VkDescriptorSet descriptor;
    };

    uint64_t nextId_ = 1;
    std::unordered_map<uint64_t, TextureEntry> textures_;
    finevk::SamplerPtr defaultSampler_;
    finevk::DescriptorPoolPtr descriptorPool_;
    VkDescriptorSet fontDescriptor_;  // ImGui font atlas
};
```

### 6.3 Example: Inventory Item Preview

```cpp
// In game code (finevoxel)
class InventoryRenderer {
    finegui::TextureHandle itemPreviewHandle_;
    finevk::TexturePtr itemPreviewTexture_;
    finevk::SamplerPtr sampler_;

    void setup(finegui::GuiSystem& gui) {
        // Create render target texture
        itemPreviewTexture_ = finevk::Texture::create(device)
            .size(128, 128)
            .format(VK_FORMAT_R8G8B8A8_UNORM)
            .usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();

        sampler_ = finevk::Sampler::create(device)
            .filter(VK_FILTER_LINEAR)
            .build();

        itemPreviewHandle_ = gui.registerTexture(itemPreviewTexture_.get(), sampler_.get());
    }

    void renderItemPreview(finevk::CommandBuffer& cmd, const Item& item) {
        // Render item's 3D model to texture (offscreen pass)
        beginOffscreenPass(cmd, itemPreviewTexture_.get());
        renderItemModel(cmd, item);
        endOffscreenPass(cmd);
    }

    void drawInventoryGui() {
        ImGui::Begin("Inventory", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetWindowPos(ImVec2(screenWidth - 300, 50));

        for (const auto& slot : inventory) {
            if (ImGui::ImageButton(itemPreviewHandle_, ImVec2(64, 64))) {
                selectItem(slot);
            }
        }
        ImGui::End();
    }
};
```

## 7. State Update System

### 7.1 Design Rationale

Rather than GUI polling game state, the game pushes state updates. Benefits:
- **Decoupling**: GUI doesn't need references to game objects
- **Networking**: State updates can be serialized and sent over network
- **Threading**: No shared mutable state between game and GUI
- **Testability**: GUI can be tested with mock state updates

### 7.2 Implementation

```cpp
// Type ID generation
template<typename T>
uint32_t TypedStateUpdate<T>::staticTypeId() {
    static uint32_t id = nextTypeId();
    return id;
}

// Handler registry
class StateUpdateDispatcher {
    std::unordered_map<uint32_t, std::function<void(const GuiStateUpdate&)>> handlers_;

public:
    template<typename T>
    void registerHandler(std::function<void(const T&)> handler) {
        handlers_[T::staticTypeId()] = [handler](const GuiStateUpdate& update) {
            handler(static_cast<const T&>(update));
        };
    }

    void dispatch(const GuiStateUpdate& update) {
        auto it = handlers_.find(update.typeId());
        if (it != handlers_.end()) {
            it->second(update);
        }
    }
};
```

### 7.3 Example State Updates

```cpp
// Defined by consuming project (finevoxel)
namespace finevoxel::gui {

struct PlayerHealthUpdate : finegui::TypedStateUpdate<PlayerHealthUpdate> {
    float current;
    float max;
    float regenRate;
};

struct HotbarUpdate : finegui::TypedStateUpdate<HotbarUpdate> {
    std::array<ItemStack, 9> slots;
    int selectedIndex;
};

struct ChatMessageUpdate : finegui::TypedStateUpdate<ChatMessageUpdate> {
    std::string sender;
    std::string message;
    ChatType type;  // Normal, System, Whisper
};

} // namespace finevoxel::gui
```

## 8. Threading Considerations

### 8.1 Same-Thread (Recommended Default)

- All GUI calls on render thread
- Zero latency
- Simplest implementation
- ImGui typically <1ms even for complex UIs

### 8.2 Separate Thread (Optional)

When to consider:
- GUI compute becomes measurably significant (>2ms)
- Need to isolate GUI from frame timing
- Building complex UI with heavy layout calculations

Benefits of abstracted input:
- GUI thread has no GLFW dependency
- Clean message-passing interface
- Enables network-driven UI testing

Costs:
- Added latency (one frame typically)
- Synchronization complexity
- Memory for draw data queue

### 8.3 Thread Safety

- `GuiSystem` is **not** thread-safe internally
- In threaded mode, all `processInput()`, `applyState()`, `beginFrame()`, `endFrame()`, `getDrawData()` calls must be on GUI thread
- Only `renderDrawData()` is called from render thread
- `GuiDrawData` is a plain data struct, safe to pass between threads
- `InputEvent` is a plain data struct, safe to queue between threads

## 9. Implementation Phases

### Phase 1: Core Framework
1. Set up project structure and build system (C++17)
2. Integrate Dear ImGui as dependency
3. Implement abstracted `InputEvent` and `InputAdapter`
4. Implement basic `GuiSystem` (without rendering)
5. Unit tests for input handling

### Phase 2: finevk Backend
1. Create pipeline using `finevk::GraphicsPipeline::Builder`
2. Implement per-frame vertex/index buffers using `finevk::Buffer`
3. Implement font texture using `finevk::Texture`
4. Create descriptor sets using `finevk::DescriptorSetLayout/Pool/Writer`
5. Implement `render()` method
6. Test with simple ImGui demo

### Phase 3: Texture Support
1. Implement `TextureRegistry` with sampler support
2. Add descriptor set management for external textures
3. Implement `registerTexture()`/`unregisterTexture()`
4. Test 3D-in-GUI with simple preview

### Phase 4: State Update System
1. Implement type ID system
2. Implement `StateUpdateDispatcher`
3. Add `applyState()` and `onStateUpdate()` templates
4. Example/test with mock state updates

### Phase 5: Threaded Mode (Optional)
1. Implement `GuiDrawData` serialization
2. Implement `getDrawData()`
3. Implement `renderDrawData()`
4. Example with separate GUI thread

### Phase 6: Polish
1. Font loading options (path, memory, callback)
2. DPI/scaling support (auto-detect + override)
3. Gamepad input (optional)
4. Documentation and examples

## 10. File Manifest

```
finegui/
├── CMakeLists.txt
├── README.md
├── include/finegui/
│   ├── finegui.hpp           # Main include (includes all)
│   ├── gui_system.hpp
│   ├── gui_config.hpp
│   ├── gui_state.hpp
│   ├── gui_draw_data.hpp
│   ├── input_adapter.hpp     # Abstracted input layer
│   └── texture_handle.hpp
├── src/
│   ├── gui_system.cpp
│   ├── input_adapter.cpp
│   ├── state_dispatcher.cpp
│   ├── texture_registry.cpp
│   ├── backend/
│   │   ├── imgui_impl_finevk.cpp
│   │   └── imgui_impl_finevk.hpp
│   └── shaders/
│       ├── gui.vert
│       ├── gui.frag
│       ├── gui.vert.spv      # Precompiled
│       └── gui.frag.spv
├── third_party/
│   └── imgui/                # Git submodule or vendored
├── tests/
│   ├── test_input_adapter.cpp
│   ├── test_state.cpp
│   └── test_draw_data.cpp
└── examples/
    ├── simple_demo.cpp       # Basic usage
    └── threaded_demo.cpp     # Threaded mode
```

## 11. Build Configuration

```cmake
cmake_minimum_required(VERSION 3.16)
project(finegui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)  # Match FineVK
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(finevk REQUIRED)
find_package(Vulkan REQUIRED)

# ImGui sources
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
)

# Library
add_library(finegui
    ${IMGUI_SOURCES}
    src/gui_system.cpp
    src/input_adapter.cpp
    src/state_dispatcher.cpp
    src/texture_registry.cpp
    src/backend/imgui_impl_finevk.cpp
)

target_include_directories(finegui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${IMGUI_DIR}
        src
)

target_link_libraries(finegui
    PUBLIC
        finevk::finevk
        Vulkan::Vulkan
)

# Shader compilation (using FineVK's helper if available)
if(TARGET Vulkan::glslc)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gui.vert.spv
        COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gui.vert
                -o ${CMAKE_CURRENT_BINARY_DIR}/gui.vert.spv
        DEPENDS src/shaders/gui.vert
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gui.frag.spv
        COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/gui.frag
                -o ${CMAKE_CURRENT_BINARY_DIR}/gui.frag.spv
        DEPENDS src/shaders/gui.frag
    )
endif()

# Tests (optional)
option(FINEGUI_BUILD_TESTS "Build tests" ON)
if(FINEGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples (optional)
option(FINEGUI_BUILD_EXAMPLES "Build examples" ON)
if(FINEGUI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

## 12. Design Decisions (Resolved)

| Question | Decision | Rationale |
|----------|----------|-----------|
| Font handling | Flexible: path, memory, or via `imguiContext()` | Different projects have different font needs; support all approaches |
| Style/theming | Expose ImGui style directly via `imguiContext()` | ImGui's style system is mature; wrapping it limits flexibility |
| Docking | Not supported | Video games use fixed menu positions; docking adds complexity without value |
| Keyboard layout | Defer to GLFW/ImGui | They handle it well; adding another layer creates bugs |
| High DPI | Auto-detect via framebuffer scale, with override | Sensible default with escape hatch |
| Input abstraction | Custom `InputEvent` + `InputAdapter` | Enables threading isolation from GLFW; clean message-passing |
| C++ standard | C++17 | Match FineVK for compatibility |

---

*Document version: 2.0*
*Last updated: 2026-02-04*
