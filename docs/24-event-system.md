# 24. Event System Design

[Back to Index](INDEX.md) | [Previous: Distance and Loading](23-distance-and-loading.md)

---

## 24.1 Overview

The event system provides a unified mechanism for delivering block-related events to various handlers and subsystems. Events originate from block changes, player interactions, tick updates, and other game actions. They are delivered to:

1. **Block handlers** - For block-specific behavior (existing BlockHandler interface)
2. **Lighting system** - For light propagation updates (separate thread)
3. **Physics system** - For collision updates
4. **Network system** - For multiplayer synchronization (future)

### Design Goals

- **No per-block-change locking** - Game logic thread owns all event queues
- **Inbox/outbox pattern** - Clean separation of processing phases
- **Decoupled lighting** - Light updates can lag without blocking game logic
- **Version tracking** - Mesher knows when to rebuild
- **Extensible** - Easy to add new event types and handlers

---

## 24.2 Thread Model

```
External Input (thread-safe, may block)
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│  Game Logic Thread                                       │
│                                                          │
│  ┌─────────┐     ┌─────────┐                           │
│  │  Inbox  │◄───►│ Outbox  │   (swap when inbox empty) │
│  └────┬────┘     └────▲────┘                           │
│       │               │                                  │
│       │ process       │ new events                       │
│       ▼               │ (neighbor updates, etc.)         │
│  ┌─────────────────────────┐                            │
│  │  Event Processor        │                            │
│  │  - calls setBlock()     │───► SubChunk.blockVersion++│
│  │  - invokes handlers     │                            │
│  │  - generates neighbors  │───► outbox                  │
│  │  - lighting events      │───► lighting thread queue   │
│  └─────────────────────────┘                            │
│                                                          │
│  Loop: process inbox → swap → repeat until both empty   │
│  Then: block on external input                           │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │ Lighting Thread   │
                    │ (consolidating    │
                    │  queue, can lag)  │
                    └──────────────────┘
```

---

## 24.3 Event Types

```cpp
enum class EventType : uint8_t {
    None = 0,

    // Block lifecycle events
    BlockPlaced,        // Block was placed/replaced in the world
    BlockBroken,        // Block is being broken/removed
    BlockChanged,       // Block state changed (rotation, data)

    // Tick events
    TickScheduled,      // Scheduled tick fired
    TickRepeat,         // Repeating tick fired
    TickRandom,         // Random tick fired

    // Neighbor events
    NeighborChanged,    // Adjacent block changed

    // Interaction events
    PlayerUse,          // Player right-clicked
    PlayerHit,          // Player left-clicked

    // Chunk events
    ChunkLoaded,        // Chunk was loaded
    ChunkUnloaded,      // Chunk is being unloaded

    // Visual events
    RepaintRequested,   // Block needs visual update
};
```

---

## 24.4 BlockEvent Structure

```cpp
/**
 * @brief Unified event container for block-related events
 *
 * Contains all data needed for any event type. Unused fields default
 * to "no value" sentinels to avoid unnecessary copying.
 *
 * Thread safety: Read-only after construction. Safe to pass between threads.
 */
struct BlockEvent {
    // Event identification
    EventType type = EventType::None;

    // Location (always valid)
    BlockPos pos{0, 0, 0};
    BlockPos localPos{0, 0, 0};  // Position within subchunk
    ChunkPos chunkPos{0, 0, 0};

    // Block information (valid for block events)
    BlockTypeId blockType;        // Current/new block type
    BlockTypeId previousType;     // Previous block type
    Rotation rotation;            // Block rotation (if applicable)

    // Interaction data (valid for PlayerUse/PlayerHit)
    Face face = Face::PosY;       // Which face was interacted with

    // For NeighborChanged
    Face changedFace = Face::PosY;  // Which neighbor changed

    // For tick events
    TickType tickType = TickType::Scheduled;

    // Timestamp (for ordering and debugging)
    uint64_t timestamp = 0;

    // ========================================================================
    // Factory Methods
    // ========================================================================

    static BlockEvent blockPlaced(BlockPos pos, BlockTypeId newType,
                                  BlockTypeId oldType, Rotation rot = Rotation::IDENTITY);
    static BlockEvent blockBroken(BlockPos pos, BlockTypeId oldType);
    static BlockEvent blockChanged(BlockPos pos, BlockTypeId oldType, BlockTypeId newType);
    static BlockEvent neighborChanged(BlockPos pos, Face changedFace);
    static BlockEvent tick(BlockPos pos, TickType type);
    static BlockEvent playerUse(BlockPos pos, Face face);
    static BlockEvent playerHit(BlockPos pos, Face face);

    // ========================================================================
    // Sentinel Checks
    // ========================================================================

    // "No value" defaults for quick checking
    [[nodiscard]] bool hasBlockType() const { return blockType.isValid(); }
    [[nodiscard]] bool hasPreviousType() const { return previousType.isValid(); }
    [[nodiscard]] bool isBlockEvent() const {
        return type == EventType::BlockPlaced ||
               type == EventType::BlockBroken ||
               type == EventType::BlockChanged;
    }
    [[nodiscard]] bool isInteractionEvent() const {
        return type == EventType::PlayerUse || type == EventType::PlayerHit;
    }
    [[nodiscard]] bool isTickEvent() const {
        return type == EventType::TickScheduled ||
               type == EventType::TickRepeat ||
               type == EventType::TickRandom;
    }
};
```

---

## 24.5 BlockContext for Handlers

```cpp
/**
 * @brief Context passed to block event handlers
 *
 * Provides access to block state and allows handlers to modify
 * the block or generate additional events.
 */
class BlockContext {
public:
    BlockContext(World& world, SubChunk& subChunk,
                 BlockPos pos, BlockPos localPos);

    // Location
    [[nodiscard]] World& world() { return world_; }
    [[nodiscard]] SubChunk& subChunk() { return subChunk_; }
    [[nodiscard]] BlockPos pos() const { return pos_; }
    [[nodiscard]] BlockPos localPos() const { return localPos_; }

    // Current block state
    [[nodiscard]] BlockTypeId blockType() const;
    [[nodiscard]] Rotation rotation() const;
    [[nodiscard]] DataContainer* data();

    // Previous state (for place/break events)
    [[nodiscard]] BlockTypeId previousType() const { return previousType_; }
    [[nodiscard]] const DataContainer* previousData() const { return previousData_.get(); }

    // Modify block (used by handlers to alter/undo placement)
    void setBlock(BlockTypeId type);
    void setRotation(Rotation rot);

    // Neighbor access
    [[nodiscard]] BlockTypeId getNeighbor(Face face) const;

private:
    World& world_;
    SubChunk& subChunk_;
    BlockPos pos_;
    BlockPos localPos_;
    BlockTypeId previousType_;
    std::unique_ptr<DataContainer> previousData_;
};
```

---

## 24.6 Event Processing Flow

### Block Place Event

```cpp
void EventProcessor::processBlockPlaceEvent(BlockEvent& event) {
    SubChunk* subchunk = getSubChunk(event.chunkPos);
    int localIdx = toLocalIndex(event.localPos);

    // 1. Capture old state (including extra data)
    BlockTypeId oldType = subchunk->getBlock(localIdx);
    auto oldData = subchunk->extractExtraData(localIdx);  // moves ownership

    // 2. Make the change (setBlock increments blockVersion internally)
    subchunk->setBlock(localIdx, event.blockType);

    // 3. Build context with previous state
    BlockContext ctx(world_, *subchunk, event.pos, event.localPos);
    ctx.setPreviousType(oldType);
    ctx.setPreviousData(std::move(oldData));

    // 4. Call handler (may modify via setBlock, can alter/undo placement)
    BlockHandler* handler = getHandler(event.blockType);
    if (handler) {
        handler->onPlace(ctx);
    }

    // 5. Always enqueue lighting event (lighting thread handles no-ops)
    BlockTypeId finalType = subchunk->getBlock(localIdx);
    enqueueLightingUpdate(event.pos, oldType, finalType);

    // 6. Generate neighbor updates → outbox
    for (Face face : ALL_FACES) {
        outbox_.push(BlockEvent::neighborChanged(
            event.pos.neighbor(face), oppositeFace(face)));
    }
}
```

### Block Break Event

```cpp
void EventProcessor::processBlockBreakEvent(BlockEvent& event) {
    SubChunk* subchunk = getSubChunk(event.chunkPos);
    int localIdx = toLocalIndex(event.localPos);

    // 1. Capture old state
    BlockTypeId oldType = subchunk->getBlock(localIdx);
    auto oldData = subchunk->extractExtraData(localIdx);

    // 2. Build context (block still exists for handler inspection)
    BlockContext ctx(world_, *subchunk, event.pos, event.localPos);
    ctx.setPreviousType(oldType);
    ctx.setPreviousData(std::move(oldData));

    // 3. Call handler BEFORE removal (can cancel by returning false)
    BlockHandler* handler = getHandler(oldType);
    if (handler && !handler->onBreak(ctx)) {
        // Cancelled - restore extra data if we moved it
        if (ctx.previousData()) {
            subchunk->setExtraData(localIdx, /* restore */);
        }
        return;
    }

    // 4. Make the change (setBlock increments blockVersion internally)
    subchunk->setBlock(localIdx, AIR_BLOCK_TYPE);

    // 5. Always enqueue lighting event
    enqueueLightingUpdate(event.pos, oldType, AIR_BLOCK_TYPE);

    // 6. Generate neighbor updates → outbox
    for (Face face : ALL_FACES) {
        outbox_.push(BlockEvent::neighborChanged(
            event.pos.neighbor(face), oppositeFace(face)));
    }
}
```

### Main Event Loop

```cpp
void EventProcessor::processEvents() {
    // Drain external input into inbox (thread-safe)
    drainExternalInput();

    // Process until stable
    while (!inbox_.empty() || !outbox_.empty()) {
        // Process all events in inbox
        while (!inbox_.empty()) {
            BlockEvent event = inbox_.pop();
            processEvent(event);
        }

        // Swap inbox/outbox (just pointer swap)
        std::swap(inbox_, outbox_);
    }

    // Block waiting for more external input
    waitForExternalInput();
}
```

---

## 24.7 Handler Semantics

### onBreak (called BEFORE removal)

```cpp
// Handler can:
// - Inspect the block being broken (ctx.blockType())
// - Access extra data (ctx.data())
// - Drop items
// - Return false to cancel the break
bool BlockHandler::onBreak(BlockContext& ctx) {
    // Drop item
    dropItem(ctx.pos(), getDropFor(ctx.blockType()));

    // Return true to allow break, false to cancel
    return true;
}
```

### onPlace (called AFTER placement)

```cpp
// Handler can:
// - Inspect what was placed (ctx.blockType())
// - See what was there before (ctx.previousType(), ctx.previousData())
// - Modify the placement via ctx.setBlock()
// - Undo by setting back to previousType
void BlockHandler::onPlace(BlockContext& ctx) {
    // Example: torch needs solid surface below
    if (!canSupportTorch(ctx.getNeighbor(Face::NegY))) {
        // Undo placement
        ctx.setBlock(ctx.previousType());
        return;
    }

    // Example: waterlogged variant
    if (isWaterlogged(ctx.previousType())) {
        ctx.setBlock(BlockTypeId::fromName("torch_waterlogged"));
    }
}
```

---

## 24.8 Lighting Thread Integration

The lighting thread has its own consolidating queue that can lag behind game logic without blocking it.

```cpp
/**
 * @brief Lighting update event (lightweight)
 */
struct LightingUpdate {
    BlockPos pos;
    BlockTypeId oldType;
    BlockTypeId newType;
};

/**
 * @brief Consolidating queue for lighting thread
 *
 * If falling behind, only processes latest update per block position.
 * This prevents unbounded queue growth during heavy activity.
 */
class LightingQueue {
public:
    void enqueue(LightingUpdate update);

    // Bulk dequeue for efficiency
    std::vector<LightingUpdate> dequeueBatch(size_t maxCount);

    [[nodiscard]] bool empty() const;

private:
    // Consolidates by position - newer updates overwrite older
    std::unordered_map<BlockPos, LightingUpdate> pending_;
    std::mutex mutex_;
    std::condition_variable cv_;
};
```

### Lighting Thread Optimization

```cpp
void LightingThread::processUpdate(const LightingUpdate& update) {
    uint8_t oldEmission = getEmission(update.oldType);
    uint8_t newEmission = getEmission(update.newType);
    uint8_t oldAttenuation = getAttenuation(update.oldType);
    uint8_t newAttenuation = getAttenuation(update.newType);

    // Quick check: opaque non-emitter → opaque non-emitter is no-op
    if (oldAttenuation == 15 && newAttenuation == 15 &&
        oldEmission == 0 && newEmission == 0) {
        return;  // No light change possible
    }

    // Otherwise do full propagation...
    if (oldEmission > 0 || newEmission > 0) {
        updateBlockLight(update.pos, oldEmission, newEmission);
    }
    if (oldAttenuation != newAttenuation) {
        updateSkyLight(update.pos);
    }

    // Increment light version for affected subchunks
    markLightVersionChanged(update.pos);
}
```

---

## 24.9 Version Tracking

```cpp
class SubChunk {
public:
    // Block version - incremented by setBlock()
    [[nodiscard]] uint64_t blockVersion() const {
        return blockVersion_.load(std::memory_order_acquire);
    }

    // Light version - incremented by lighting thread
    [[nodiscard]] uint64_t lightVersion() const {
        return lightVersion_.load(std::memory_order_acquire);
    }

    // Called internally by setBlock
    void incrementBlockVersion() {
        blockVersion_.fetch_add(1, std::memory_order_relaxed);
    }

    // Called by lighting thread after updates complete
    void incrementLightVersion() {
        lightVersion_.fetch_add(1, std::memory_order_release);
    }

private:
    std::atomic<uint64_t> blockVersion_{1};
    std::atomic<uint64_t> lightVersion_{1};
};
```

### Mesher Version Check

```cpp
struct MeshCacheEntry {
    uint64_t builtWithBlockVersion;
    uint64_t builtWithLightVersion;
    // ... mesh data ...
};

bool MeshWorker::needsRebuild(const SubChunk& chunk,
                               const MeshCacheEntry& cached) {
    return chunk.blockVersion() != cached.builtWithBlockVersion ||
           chunk.lightVersion() != cached.builtWithLightVersion;
}
```

---

## 24.10 LightEngine Integration with World

```cpp
class World {
public:
    // Optional lighting - created based on config
    void enableLighting();
    void disableLighting();
    [[nodiscard]] LightEngine* lightEngine() { return lightEngine_.get(); }

    // Called by event processor
    void enqueueLightingUpdate(BlockPos pos, BlockTypeId oldType, BlockTypeId newType) {
        if (lightEngine_) {
            lightEngine_->enqueue({pos, oldType, newType});
        }
    }

private:
    std::unique_ptr<LightEngine> lightEngine_;  // null if disabled
};

class LightEngine {
public:
    explicit LightEngine(World& world);

    // Enqueue update (called from game logic thread)
    void enqueue(LightingUpdate update);

    // Start/stop lighting thread
    void start();
    void stop();

    // Lazy sky light initialization (called by mesher if needed)
    void ensureColumnInitialized(ColumnPos pos);

private:
    World& world_;  // Back reference
    LightingQueue queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
```

---

## 24.11 Lazy Sky Light Initialization

Sky light is initialized lazily when a chunk is first meshed, not when loaded.

```cpp
class ChunkColumn {
public:
    [[nodiscard]] bool lightInitialized() const { return lightInitialized_; }
    void markLightInitialized() { lightInitialized_ = true; }

private:
    bool lightInitialized_ = false;
};

// In mesh builder's light provider:
uint8_t getLightForMeshing(BlockPos pos) {
    ChunkColumn* col = world.getColumn(ColumnPos::fromBlock(pos));
    if (!col) return 0;

    // Lazy initialization
    if (!col->lightInitialized() && world.lightEngine()) {
        world.lightEngine()->initializeSkyLight(col);
        col->markLightInitialized();
    }

    return world.lightEngine()->getCombinedLight(pos);
}
```

If light data was loaded from disk (serialization), the column is already marked as initialized.

---

## 24.12 Implementation Notes

### Memory Efficiency

- BlockEvent is ~64 bytes, fits in a cache line
- Inbox/outbox are simple vectors, swap is O(1)
- Lighting queue consolidates by position, bounded size
- "No value" sentinels avoid copying unused fields

### Thread Safety

- Game logic thread owns inbox/outbox (no locking needed)
- External input queue is thread-safe (producer/consumer)
- Lighting queue is thread-safe with consolidation
- SubChunk versions use atomics for cross-thread visibility
- setBlock() increments version automatically

### Performance Characteristics

- Common case: lighting finishes before mesh starts → single correct mesh
- Busy case: mesh proceeds with slightly stale light → remesh later
- Lighting can lag without blocking game logic
- Consolidation prevents lighting thread from falling infinitely behind

---

## 24.13 Future Extensions

- **Network events** - Replicate events to remote players
- **Entity events** - Entity movement, damage, spawning
- **Undo/redo** - Event log for world edit history
- **Priority levels** - Urgent events (explosions) before routine ticks

---

[Next: TBD]
