# 15. FineStructureVK Integration

[Back to Index](INDEX.md) | [Previous: Threading Model](14-threading.md)

---

## 15.1 How We'll Use FineStructureVK

| FineStructureVK Feature | Voxel Engine Usage |
|-------------------------|-------------------|
| **Window** | Main game window, input callbacks, frame sync |
| **GameLoop** | Fixed timestep physics, variable rate rendering |
| **Material** | Block textures, uniform buffers for lighting |
| **RenderTarget** | Main scene render target with depth |
| **GraphicsPipeline** | Block rendering, entity rendering, UI |
| **Mesh** | Chunk meshes (generated), entity meshes (loaded) |
| **Texture** | Texture atlas, skybox |
| **Buffer** | Uniform buffers, vertex/index buffers |
| **CommandBuffer** | Per-frame command recording |
| **AssetLoader** | Background texture loading |
| **Camera** | View/projection matrices, frustum |
| **DeferredDisposer** | Safe cleanup of old chunk meshes |

---

## 15.2 Pipeline Setup

> **Note:** The current WorldRenderer uses `SimpleRenderer`, `UniformBuffer<CameraUniform>`,
> and direct descriptor set management rather than the `Material` abstraction.
> See `src/world_renderer.cpp` for the actual implementation.

### FineVK API Patterns

**Material (manages uniform buffers and textures):**
```cpp
// Material creates and manages its own uniform buffers
auto material = finevk::Material::create(device, framesInFlight)
    .uniform<MVPUniform>(0, VK_SHADER_STAGE_VERTEX_BIT)  // Creates internal UBO
    .texture(1, VK_SHADER_STAGE_FRAGMENT_BIT)            // Declares texture binding
    .build();

// Update per frame
material->setTexture(1, texture, sampler);
material->update<MVPUniform>(0, uniformData);
```

**GraphicsPipeline Builder:**
```cpp
// Path-based shader loading (builder owns shader modules)
auto pipeline = finevk::GraphicsPipeline::create(device, renderPass, pipelineLayout)
    .vertexShader("shaders/chunk.vert.spv")
    .fragmentShader("shaders/chunk.frag.spv")
    .vertexInput<ChunkVertex>()  // Requires static getBindingDescription/getAttributeDescriptions
    .enableDepth()
    .cullBack()                   // Convenience for cullMode(VK_CULL_MODE_BACK_BIT)
    .dynamicViewportAndScissor()
    .build();

// Or explicit vertex binding (as used in current WorldRenderer):
auto& builder = finevk::GraphicsPipeline::create(device, renderPass, pipelineLayout)
    .vertexShader(vertexShaderModule)
    .fragmentShader(fragmentShaderModule)
    .vertexBinding(0, sizeof(ChunkVertex), VK_VERTEX_INPUT_RATE_VERTEX);

for (const auto& attr : attributes) {
    builder.vertexAttribute(attr.location, attr.binding, attr.format, attr.offset);
}

auto pipeline = builder.enableDepth().cullBack().build();
```

**Camera with Double-Precision (for large world coordinates):**
```cpp
finevk::Camera camera;
camera.setPerspective(70.0f, aspect, 0.1f, 500.0f);

// Double-precision position for large worlds (avoids jitter at >10000 blocks)
camera.moveTo(glm::dvec3(1000000.0, 64.0, 1000000.0));
camera.setOrientation(forward, up);
camera.updateState();

// Use view-relative matrix for rendering (camera at origin)
const auto& state = camera.state();
glm::mat4 viewRelative = state.viewRelative;  // Rotation only, camera at origin
glm::dvec3 cameraPos = camera.positionD();    // Double-precision position

// Per-chunk: compute view-relative offset with doubles, convert to float for GPU
glm::vec3 chunkOffset = glm::vec3(chunkWorldPosD - cameraPos);
```

---

## 15.3 Frame Rendering

The actual WorldRenderer implementation uses view-relative rendering to avoid float precision issues at large world coordinates:

```cpp
// Per-frame update
void WorldRenderer::updateCamera(const finevk::CameraState& state, const glm::dvec3& highPrecisionPos) {
    // Store high-precision position for view-relative calculations
    highPrecisionCameraPos_ = highPrecisionPos;

    // Use FineVK's view-relative matrix (camera at origin, rotation only)
    finevk::CameraUniform uniform{};
    uniform.view = state.viewRelative;
    uniform.projection = state.projection;
    uniform.viewProjection = state.projection * state.viewRelative;
    uniform.position = glm::vec3(highPrecisionPos);  // For lighting

    cameraUniform_->update(currentFrame, uniform);
}

// Per-chunk rendering
void WorldRenderer::render(finevk::CommandBuffer& cmd) {
    pipeline_->bind(cmd.handle());

    for (auto& [pos, view] : views_) {
        // Frustum culling using double-precision AABB
        if (!isInFrustum(pos)) continue;

        // Calculate view-relative offset with double precision
        glm::dvec3 chunkWorldPosD(pos.x * 16.0, pos.y * 16.0, pos.z * 16.0);
        glm::vec3 offset = glm::vec3(chunkWorldPosD - highPrecisionCameraPos_);

        // Push per-chunk offset
        ChunkPushConstants pc{ .chunkOffset = offset };
        pipelineLayout_->pushConstants(cmd.handle(), VK_SHADER_STAGE_VERTEX_BIT, pc);

        view->bind(cmd);
        view->draw(cmd);
    }
}
```

See `src/world_renderer.cpp` for the complete implementation.

---

[Next: FineStructureVK Critique](16-finestructurevk-critique.md)
