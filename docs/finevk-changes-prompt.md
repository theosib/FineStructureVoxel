# FineStructureVK Changes Prompt

Use this prompt when working on FineStructureVK to implement the changes identified during the FineVox design review.

---

## Context

FineStructureVoxel (finevox) is a voxel game engine built on top of FineStructureVK. During the Phase 4 implementation (basic rendering), we discovered some features that would benefit from being in FineVK rather than duplicated in finevox.

---

## 1. High-Priority: Double-Precision Camera Position

### Problem

When rendering at large world coordinates (e.g., 1,000,000 blocks from origin), float32 precision causes visible jitter. The camera position loses precision - at 1,000,000, float32 only has ~0.06 block precision.

### Current Workaround in FineVox

FineVox implements view-relative rendering with double-precision position:

1. **Double-precision position storage** - Camera position stored as `glm::dvec3`
2. **View-relative offsets** - CPU computes `(chunkWorldPos - cameraPos)` using doubles, result is small and safe for float32
3. **View matrix at origin** - View matrix has no translation (camera at origin), only rotation
4. **Push constants** - Each subchunk receives its view-relative offset

```cpp
// In finevox WorldRenderer:
void WorldRenderer::updateCamera(const finevk::CameraState& cameraState, const glm::dvec3& highPrecisionPos) {
    highPrecisionCameraPos_ = highPrecisionPos;

    // Create view-relative view matrix (camera at origin, rotation only)
    glm::vec3 forward = -glm::vec3(cameraState.view[0][2], cameraState.view[1][2], cameraState.view[2][2]);
    glm::vec3 up = glm::vec3(cameraState.view[0][1], cameraState.view[1][1], cameraState.view[2][1]);
    glm::mat4 viewRelativeView = glm::lookAt(glm::vec3(0.0f), forward, up);

    // Update uniform with view-relative matrices
    uniform.view = viewRelativeView;
    // ...
}

glm::vec3 WorldRenderer::calculateViewRelativeOffset(ChunkPos pos) const {
    // Double-precision subtraction, then convert to float
    glm::dvec3 worldPosD(pos.x * 16.0, pos.y * 16.0, pos.z * 16.0);
    glm::dvec3 offsetD = worldPosD - highPrecisionCameraPos_;
    return glm::vec3(offsetD);
}
```

### Proposed FineVK Changes

Add optional double-precision support to the Camera class:

```cpp
class Camera {
public:
    // Existing float API (unchanged)
    void moveTo(const glm::vec3& position);
    const glm::vec3& position() const;

    // NEW: High-precision position for large worlds
    void setHighPrecisionPosition(const glm::dvec3& position);
    const glm::dvec3& highPrecisionPosition() const;
    bool hasHighPrecisionPosition() const;

    // NEW: Get view-relative view matrix (camera at origin)
    // Use this when doing view-relative rendering
    glm::mat4 viewRelativeViewMatrix() const;

    // updateState() behavior:
    // - If highPrecisionPosition is set, use it for float position (truncated)
    // - CameraState.position reflects the float32 position
    // - viewRelativeViewMatrix() returns rotation-only view matrix

private:
    glm::vec3 position_{0.0f};           // Float position (for backward compat)
    glm::dvec3 highPrecisionPos_{0.0};   // Optional double-precision
    bool useHighPrecision_ = false;
};
```

### CameraState Changes

```cpp
struct CameraState {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::vec3 position{0.0f};            // Float position (for GPU uniforms)

    // NEW: For view-relative rendering
    glm::mat4 viewRelativeView{1.0f};    // View matrix with camera at origin

    std::array<glm::vec4, 6> frustumPlanes;
};
```

### Usage Pattern

```cpp
// Application code with large world:
Camera camera;
camera.setPerspective(75.0f, aspect, 0.1f, 1000.0f);

// Use double-precision position
glm::dvec3 playerPos{1000000.0, 64.0, 1000000.0};
camera.setHighPrecisionPosition(playerPos);
camera.setOrientation(forward, up);
camera.updateState();

// Renderer uses view-relative matrices
auto viewRelView = camera.state().viewRelativeView;  // Rotation only
auto projection = camera.state().projection;

// Per-object: compute view-relative offset on CPU with doubles
glm::dvec3 objectWorldPos = ...;
glm::vec3 viewRelOffset = glm::vec3(objectWorldPos - camera.highPrecisionPosition());
// Pass viewRelOffset to shader as push constant or instance data
```

---

## 2. Medium-Priority: API Documentation Clarifications

The FineVox integration doc (15-finestructurevk-integration.md) shows some API patterns that may not match current FineVK. Please verify and update FineVK docs if needed:

### Buffer Creation

Doc shows:
```cpp
globalUBO_ = finevk::Buffer::createUniform<GlobalUBO>(device, framesInFlight_);
```

Actual API appears to be:
```cpp
// Option 1: Raw buffer
auto buffer = Buffer::createUniformBuffer(device, sizeof(GlobalUBO));

// Option 2: Templated wrapper
auto uniformBuffer = UniformBuffer<GlobalUBO>::create(device, framesInFlight);
```

**Action:** Verify which pattern is preferred and ensure it's documented.

### Material Builder

Doc shows:
```cpp
blockMaterial_ = finevk::Material::create(device, framesInFlight_)
    .uniformBuffer(0, globalUBO_)
    .sampledImage(1, texture)
    .build();
```

Actual API appears to use `.uniform<T>()` and `.texture()`:
```cpp
auto material = Material::create(device, framesInFlight)
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();
```

**Action:** Verify the correct builder method names and update docs.

### GraphicsPipeline Builder

Doc shows:
```cpp
opaquePipeline_ = finevk::GraphicsPipeline::create(device, target, layout)
    .vertexShader("shaders/block_vertex.spv")
    .fragmentShader("shaders/block_fragment.spv")
    .vertexInput<ChunkVertex>()
    .enableDepth()
    .cullBack()
    .pushConstants<ChunkPushConstants>()
    .build();
```

**Action:** Verify these method names match current API. Specifically:
- Is it `vertexShader()` or `shader(VK_SHADER_STAGE_VERTEX_BIT, ...)`?
- Is `vertexInput<T>()` supported or does it need explicit binding/attribute descriptions?
- Is `cullBack()` the correct method or `cullMode(VK_CULL_MODE_BACK_BIT)`?

---

## 3. Low-Priority: Consider Adding These Utilities

### Ring Buffer / Frame-Cycling Helper

Currently users must manually track frame indices for per-frame resources. Consider:

```cpp
template<typename T>
class PerFrameResource {
public:
    static PerFrameResource create(LogicalDevice* device, uint32_t framesInFlight);

    T& current();              // Get resource for current frame
    void advance();            // Called by GameLoop automatically
    uint32_t frameIndex() const;
};
```

### Viewport/Scissor Helper

FineVK already has `CommandBuffer::setViewportAndScissor(width, height)` which is good. Just noting that FineVox uses this.

---

## Summary of Changes

| Priority | Change | Effort |
|----------|--------|--------|
| High | Double-precision camera position | Medium |
| High | View-relative view matrix in CameraState | Low |
| Medium | Verify/update API documentation | Low |
| Low | Per-frame resource helper | Medium |

---

## Testing

After implementing double-precision camera:

1. Create a test that positions camera at (1000000, 100, 1000000)
2. Render objects near the camera
3. Move camera slightly - verify no jitter
4. Compare with float-only position - should show obvious jitter at large coords

The finevox `render_demo --large-coords` test can serve as a reference.
