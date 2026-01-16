# 14. Threading Model

[Back to Index](INDEX.md) | [Previous: Batch Operations API](13-batch-operations.md)

---

## 14.1 Thread Responsibilities

| Thread | Responsibility |
|--------|----------------|
| **Main Thread** | Window events, rendering, input processing |
| **Chunk Load/Save** | Disk I/O, chunk serialization |
| **Chunk Generation** | Terrain generation (CPU-intensive) |
| **Mesh Builder** | Greedy meshing, vertex generation |
| **Light Propagation** | BFS light updates (can be integrated with mesh) |

---

## 14.2 Thread Communication

```cpp
namespace finevox {

// Thread-safe work queue
template<typename T>
class WorkQueue {
public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        cv_.notify_one();
    }

    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool waitPop(T& item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// Chunk load request/result
struct ChunkLoadRequest {
    ChunkPos pos;
    int priority;
};

struct ChunkLoadResult {
    ChunkPos pos;
    std::unique_ptr<Chunk> chunk;
    bool fromDisk;  // vs generated
};

// Mesh build request/result
struct MeshBuildRequest {
    Chunk* chunk;
};

struct MeshBuildResult {
    Chunk* chunk;
    std::unordered_map<TextureId, std::vector<ChunkVertex>> opaqueMeshes;
    std::vector<std::pair<BlockPos, std::vector<ChunkVertex>>> translucentMeshes;
};

}  // namespace finevox
```

---

## 14.3 Integration with FineStructureVK GameLoop

```cpp
class VoxelGame : public finevk::GameLoop {
public:
    VoxelGame(finevk::Window& window);

protected:
    void onSetup() override {
        // Initialize world, load initial chunks
        world_ = std::make_unique<World>();
        // Start worker threads
    }

    void onFixedUpdate(float dt) override {
        // Physics, entity updates (fixed 50Hz)
        physicsSystem_->applyGravity(*player_, dt);
        playerController_->update(dt);
        world_->tickChunks();
    }

    void onUpdate(float dt) override {
        // Non-physics updates (variable rate)
        world_->processUpdateQueue();
        world_->processRepaintQueue();
        collectCompletedMeshes();
    }

    void onRender(float dt, float interpolation) override {
        // Render world
        auto frameInfo = window_.beginFrame();
        if (!frameInfo) return;

        worldRenderer_->render(frameInfo->commandBuffer, camera_, *world_);

        window_.endFrame(*frameInfo);
    }

    void onShutdown() override {
        // Stop worker threads, save world
    }

private:
    std::unique_ptr<World> world_;
    std::unique_ptr<WorldRenderer> worldRenderer_;
    std::unique_ptr<PhysicsSystem> physicsSystem_;
    std::unique_ptr<PlayerController> playerController_;
    std::unique_ptr<Entity> player_;
    Camera camera_;
};
```

---

[Next: FineStructureVK Integration](15-finestructurevk-integration.md)
