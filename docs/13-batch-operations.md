# 13. Batch Operations API

[Back to Index](INDEX.md) | [Previous: Scripting and Command Language](12-scripting.md)

---

## 13.1 The General Batching Pattern

Many engine operations benefit from batching - collecting multiple requests and processing them together. The general pattern is:

1. **Collect** - Accumulate operations into a container
2. **Coalesce** - Remove redundant/superseded operations
3. **Execute** - Process efficiently with minimal overhead

This pattern applies throughout the engine:

| System | What Gets Batched | Coalescing Rule |
|--------|-------------------|-----------------|
| Block modifications | Block changes | Last write wins per position |
| Mesh rebuilds | Dirty chunk marks | Set semantics (only rebuild once) |
| Light updates | Light propagation requests | Set semantics per position |
| Repaint requests | Visual updates | Set semantics per position |
| Save requests | Column saves | Latest version wins |

**Game-Level Note:** This pattern also applies to game logic systems like Minecraft's redstone. Block update propagation (neighbor notifications, signal changes) benefits enormously from batching with coalescing - if a block receives 5 update signals before the tick processes, only 1 update should execute. While block update mechanics belong in the game layer, not the engine, the engine should provide the infrastructure patterns.

---

## 13.2 Problem Statement

Minecraft suffers from significant performance issues when making many block changes, because each change:
1. Acquires locks
2. Triggers neighbor updates
3. Triggers mesh rebuilds
4. May trigger light recalculation

When placing a structure or running a fill command, this overhead is multiplied by every block.

---

## 13.3 Solution: Batched Block Updates

Instead of per-block APIs, we provide batch APIs that accept containers of changes:

```cpp
namespace finevox {

class BatchBuilder {
public:
    explicit BatchBuilder(World& world);

    // Queue block changes (no immediate effect)
    BatchBuilder& setBlock(BlockPos pos, uint16_t typeId, uint8_t rotation = 0);
    BatchBuilder& setBlocks(std::span<const BlockChange> changes);
    BatchBuilder& fillRegion(BlockPos min, BlockPos max, uint16_t typeId);

    // Queue block data changes
    BatchBuilder& setBlockData(BlockPos pos, std::string_view key, DataValue value);

    // Execute all queued changes efficiently
    BatchResult execute();

    // Clear without executing
    void clear();

private:
    World& world_;

    // Grouped by chunk for locality
    std::unordered_map<uint64_t, std::vector<BlockChange>> changesByChunk_;
};

struct BlockChange {
    BlockPos pos;
    uint16_t typeId;
    uint8_t rotation;
    std::optional<DataContainer> data;
};

struct BatchResult {
    int blocksChanged;
    int chunksAffected;
    std::chrono::microseconds duration;
};

}  // namespace finevox
```

---

## 13.4 Internal Optimization

The batch execution optimizes by:

```cpp
BatchResult BatchBuilder::execute() {
    BatchResult result{};
    auto startTime = std::chrono::steady_clock::now();

    // 1. Group changes by chunk (already done during queueing)

    // 2. Sort chunks for memory locality
    std::vector<uint64_t> chunkKeys;
    for (auto& [key, _] : changesByChunk_) {
        chunkKeys.push_back(key);
    }
    std::sort(chunkKeys.begin(), chunkKeys.end());

    // 3. Process each chunk with single lock acquisition
    std::unordered_set<ChunkPos> dirtyChunks;

    for (uint64_t key : chunkKeys) {
        ChunkPos chunkPos = ChunkPos::unpack(key);
        Chunk* chunk = world_.getChunk(chunkPos);
        if (!chunk) continue;

        // Single lock for all changes in this chunk
        auto& changes = changesByChunk_[key];
        for (const auto& change : changes) {
            int localIdx = change.pos.toLocalIndex();
            chunk->setBlockId(localIdx, change.typeId);
            chunk->setRotation(localIdx, change.rotation);
            if (change.data) {
                chunk->setBlockData(localIdx, std::move(*change.data));
            }
            result.blocksChanged++;
        }

        dirtyChunks.insert(chunkPos);
        // Also mark neighbor chunks as potentially needing mesh update
        for (Face f : {Face::NegX, Face::PosX, Face::NegY, Face::PosY, Face::NegZ, Face::PosZ}) {
            dirtyChunks.insert(chunkPos.neighbor(f));
        }
    }

    // 4. Batch lighting update
    world_.lightingSystem().batchUpdate(changesByChunk_);

    // 5. Mark all affected chunks for mesh rebuild (single pass)
    for (const ChunkPos& pos : dirtyChunks) {
        if (Chunk* chunk = world_.getChunk(pos)) {
            chunk->markDirty();
        }
    }

    // 6. Queue neighbor update events (batched)
    world_.queueBatchedNeighborUpdates(changesByChunk_);

    result.chunksAffected = dirtyChunks.size();
    result.duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - startTime
    );

    clear();
    return result;
}
```

---

## 13.5 Usage Example

```cpp
// Placing a 10x10x10 cube of stone
BatchBuilder batch(world);
for (int x = 0; x < 10; x++) {
    for (int y = 0; y < 10; y++) {
        for (int z = 0; z < 10; z++) {
            batch.setBlock(BlockPos(baseX + x, baseY + y, baseZ + z), STONE_ID);
        }
    }
}
auto result = batch.execute();
// result.blocksChanged == 1000
// Much faster than 1000 individual setBlock calls
```

Or using the fill helper:
```cpp
BatchBuilder batch(world);
batch.fillRegion(BlockPos(0, 64, 0), BlockPos(99, 64, 99), GRASS_ID);
batch.execute();  // Places 10,000 blocks efficiently
```

---

## 13.6 Coalescing Update Queue (Engine Utility)

> **Implementation Note:** This design evolved into `BlockingQueue<T>` and then `AlarmQueue<T>` in the actual implementation. The API differs slightly (uses `push()`/`tryPop()` instead of `enqueue()`/`drain()`), and adds alarm-based wakeup for frame synchronization. See `alarm_queue.hpp` for current API.

The engine provides a generic coalescing queue that games can use for their update systems:

```cpp
namespace finevox {

// A set-based queue that coalesces duplicate entries
// Perfect for "block X needs update" type systems
template<typename T, typename Hash = std::hash<T>>
class CoalescingQueue {
public:
    // Add item (no-op if already queued)
    void enqueue(const T& item) {
        std::lock_guard lock(mutex_);
        if (pending_.insert(item).second) {
            queue_.push_back(item);
        }
    }

    // Add multiple items
    void enqueue(std::span<const T> items) {
        std::lock_guard lock(mutex_);
        for (const auto& item : items) {
            if (pending_.insert(item).second) {
                queue_.push_back(item);
            }
        }
    }

    // Take all pending items (clears queue)
    std::vector<T> drain() {
        std::lock_guard lock(mutex_);
        std::vector<T> result = std::move(queue_);
        queue_.clear();
        pending_.clear();
        return result;
    }

    // Take up to N items
    std::vector<T> drainUpTo(size_t maxCount) {
        std::lock_guard lock(mutex_);
        if (queue_.size() <= maxCount) {
            return drain();
        }
        std::vector<T> result(queue_.begin(), queue_.begin() + maxCount);
        queue_.erase(queue_.begin(), queue_.begin() + maxCount);
        for (const auto& item : result) {
            pending_.erase(item);
        }
        return result;
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    bool empty() const { return size() == 0; }

private:
    mutable std::mutex mutex_;
    std::vector<T> queue_;                    // Ordered queue
    std::unordered_set<T, Hash> pending_;     // Fast duplicate check
};

}  // namespace finevox
```

**Game-level usage example (redstone-like updates):**

```cpp
// In game code, not engine
class BlockUpdateSystem {
    finevox::CoalescingQueue<BlockPos> updateQueue_;

public:
    // Called when a block changes and neighbors need notification
    void scheduleUpdate(BlockPos pos) {
        updateQueue_.enqueue(pos);
    }

    // Called when neighbors of a changed block need updates
    void scheduleNeighborUpdates(BlockPos pos) {
        std::array<BlockPos, 6> neighbors = {
            pos.neighbor(Face::NegX), pos.neighbor(Face::PosX),
            pos.neighbor(Face::NegY), pos.neighbor(Face::PosY),
            pos.neighbor(Face::NegZ), pos.neighbor(Face::PosZ)
        };
        updateQueue_.enqueue(neighbors);
    }

    // Called each game tick - processes all pending updates
    void processTick(World& world) {
        // Get all pending updates (coalesced - each position only once)
        auto updates = updateQueue_.drain();

        for (BlockPos pos : updates) {
            Block block = world.getBlock(pos);
            if (block.type()) {
                block.type()->onNeighborChange(block, ...);
            }
        }
    }
};
```

If a redstone signal change causes 50 update signals to the same block, the queue holds only 1 entry - that block gets one `onNeighborChange` call per tick, not 50.

---

[Next: Threading Model](14-threading.md)
