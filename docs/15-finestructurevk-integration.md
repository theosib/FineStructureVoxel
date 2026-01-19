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

> **Note:** This is conceptual pseudocode showing the intended usage pattern.
> Actual FineVK API calls may differ slightly - see FineStructureVK headers.
> The current WorldRenderer uses `SimpleRenderer`, `UniformBuffer<CameraUniform>`,
> and direct descriptor set management rather than the `Material` abstraction.

```cpp
void WorldRenderer::createPipelines(finevk::LogicalDevice& device, finevk::RenderTarget& target) {
    // Global uniform buffer (view-proj, lighting, time)
    globalUBO_ = finevk::Buffer::createUniform<GlobalUBO>(device, framesInFlight_);

    // Texture atlas
    textureAtlas_ = TextureAtlas::create(device);
    loadBlockTextures();
    textureAtlas_->finalize();

    // Material for blocks
    blockMaterial_ = finevk::Material::create(device, framesInFlight_)
        .uniformBuffer(0, globalUBO_)
        .sampledImage(1, textureAtlas_->getTexture())
        .build();

    // Pipeline for opaque blocks
    opaquePipeline_ = finevk::GraphicsPipeline::create(device, target, blockMaterial_->layout())
        .vertexShader("shaders/block_vertex.spv")
        .fragmentShader("shaders/block_fragment.spv")
        .vertexInput<ChunkVertex>()
        .enableDepth()
        .cullBack()
        .pushConstants<ChunkPushConstants>()
        .build();

    // Pipeline for translucent blocks
    translucentPipeline_ = finevk::GraphicsPipeline::create(device, target, blockMaterial_->layout())
        .vertexShader("shaders/block_vertex.spv")
        .fragmentShader("shaders/block_fragment_translucent.spv")
        .vertexInput<ChunkVertex>()
        .enableDepth(true, false)  // Depth test yes, depth write no
        .alphaBlending()
        .cullNone()
        .pushConstants<ChunkPushConstants>()
        .build();
}
```

---

## 15.3 Frame Rendering

```cpp
void WorldRenderer::render(finevk::CommandBuffer& cmd, const Camera& camera, World& world) {
    // Update view center
    viewCenter_ = BlockPos::fromVector(camera.position());

    // Update global UBO
    GlobalUBO ubo{};
    ubo.viewProj = camera.viewProjection();
    ubo.viewCenter = glm::vec3(viewCenter_.x, viewCenter_.y, viewCenter_.z);
    ubo.lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    ubo.lightColor = glm::vec3(1.0f);
    ubo.ambientColor = glm::vec3(0.3f);
    ubo.time = getTime();
    globalUBO_->upload(&ubo, sizeof(ubo));

    // Bind material
    blockMaterial_->bind(cmd);

    // Render opaque chunks
    opaquePipeline_->bind(cmd);
    for (auto& [pos, chunk] : world.loadedChunks()) {
        if (auto* view = chunk->getView()) {
            if (view->isInFrustum(camera.frustum(), viewCenter_)) {
                ChunkPushConstants pc{};
                pc.chunkOffset = toViewRelative(chunk->cornerBlockPos());
                cmd.pushConstants(opaquePipeline_->layout(), pc);
                view->renderOpaque(cmd);
            }
        }
    }

    // Render translucent (back to front)
    translucentPipeline_->bind(cmd);
    auto sortedTranslucent = collectAndSortTranslucent(camera.position());
    for (auto& [distance, view, meshIndex] : sortedTranslucent) {
        view->renderTranslucent(cmd, meshIndex);
    }
}
```

---

[Next: FineStructureVK Critique](16-finestructurevk-critique.md)
