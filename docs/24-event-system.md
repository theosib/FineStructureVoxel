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

    // For NeighborChanged (supports consolidation via bitmask)
    Face changedFace = Face::PosY;  // Primary face that changed (for single-face events)
    uint8_t neighborFaceMask = 0;   // Bitmask of all changed faces (1 << Face value)

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

    // ========================================================================
    // NeighborChanged Face Mask Helpers
    // ========================================================================

    [[nodiscard]] bool hasNeighborChanged(Face f) const {
        return neighborFaceMask & (1 << static_cast<uint8_t>(f));
    }

    void addNeighborFace(Face f) {
        neighborFaceMask |= (1 << static_cast<uint8_t>(f));
    }

    // Iterate all changed faces
    template<typename Func>
    void forEachChangedNeighbor(Func&& func) const {
        for (int i = 0; i < 6; ++i) {
            if (neighborFaceMask & (1 << i)) {
                func(static_cast<Face>(i));
            }
        }
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

## 24.13 Three-Queue Architecture

The full event processing uses three queues with distinct purposes:

```
┌─────────────────────────────────────────────────────────────────┐
│                        Input Queue                               │
│  (AlarmQueue-style: blocks until external or timer event)       │
│  - External events (user input, network)                         │
│  - Timer events (generated internally by alarm)                  │
│    - Game tick alarms (regular interval, e.g., 50ms)            │
│    - Scheduled tick alarms (specific future time)               │
└─────────────────────────┬───────────────────────────────────────┘
                          │ highest priority
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Event Processing                             │
│  process(event) → handlers may push to outbox                   │
└─────────────────────────┬───────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
        ┌──────────┐            ┌──────────┐
        │  Inbox   │◄── swap ───│  Outbox  │
        │(2nd pri) │            │(staging) │
        └──────────┘            └──────────┘
```

### Processing Priority

1. **Input queue** (external + timer events) - highest priority
2. **Inbox** - second priority, processes block-generated events
3. When processing any event, handlers put new events in **outbox**
4. When input AND inbox empty, swap outbox→inbox, continue
5. When all three empty, block on input queue

### Outbox Consolidation

The outbox consolidates events by block position to avoid redundant processing:

```cpp
class EventOutbox {
public:
    void push(BlockEvent event) {
        auto [it, inserted] = pending_.try_emplace(event.pos, event);
        if (!inserted) {
            // Merge with existing event for this position
            it->second = mergeEvents(it->second, event);
        }
    }

    // Swap contents to inbox (clears outbox)
    void swapTo(std::vector<BlockEvent>& inbox) {
        inbox.clear();
        inbox.reserve(pending_.size());
        for (auto& [pos, event] : pending_) {
            inbox.push_back(std::move(event));
        }
        pending_.clear();
    }

private:
    std::unordered_map<BlockPos, BlockEvent> pending_;

    static BlockEvent mergeEvents(const BlockEvent& existing, const BlockEvent& incoming) {
        // Same event type: keep latest (incoming)
        if (existing.type == incoming.type) {
            return incoming;
        }

        // NeighborChanged from multiple faces: combine face masks
        if (existing.type == EventType::NeighborChanged &&
            incoming.type == EventType::NeighborChanged) {
            BlockEvent merged = incoming;
            merged.neighborFaceMask |= existing.neighborFaceMask;
            return merged;
        }

        // Different event types: prioritize by importance
        // BlockPlaced/BlockBroken > NeighborChanged > Tick
        return higherPriority(existing, incoming) ? existing : incoming;
    }
};
```

**Consolidation rules:**
- **Same event type, same position**: Keep latest (newer data wins)
- **Multiple NeighborChanged**: Merge face masks (block needs to check all changed neighbors)
- **Different event types**: Higher priority event wins (block changes > neighbor updates > ticks)
- **BlockPlaced then BlockBroken**: Could cancel, but safer to process both in sequence

This prevents scenarios like:
- Block receives 6 separate NeighborChanged events (one per face) → consolidated to 1
- Rapid block updates causing unbounded event queue growth

### Timer Event Generation

The input queue internally generates timer events:
- **Game tick**: Regular interval alarm generates `TickType::GameTick` events
- **Scheduled ticks**: Per-block scheduled alarms stored in priority queue

```cpp
void InputQueue::waitForEvent() {
    // Calculate next alarm time
    auto nextAlarm = std::min(nextGameTick_, scheduledTicks_.top().targetTime);

    // Block until event or alarm
    auto event = externalQueue_.waitUntil(nextAlarm);

    if (event) {
        return *event;  // External event
    }

    // Alarm fired - generate appropriate tick events
    if (now >= nextGameTick_) {
        generateGameTickEvents();
        nextGameTick_ += gameTickInterval_;
    }

    while (!scheduledTicks_.empty() && scheduledTicks_.top().targetTime <= now) {
        auto tick = scheduledTicks_.pop();
        return BlockEvent::tick(tick.pos, TickType::Scheduled);
    }
}
```

---

## 24.14 Game Tick and Random Tick System

### Tick Types

| Tick Type | Registration | When Fired | Use Case |
|-----------|--------------|------------|----------|
| **Game Tick** | Per-subchunk registry (from BlockType) | Every game tick interval | Hoppers, observers, active machines |
| **Random Tick** | None (any block can receive) | N random per subchunk per game tick | Crop growth, grass spread, decay |
| **Scheduled Tick** | Explicit via `scheduleTick()` | At specific future time | Redstone delays, piston retraction |

### Game Tick Registry

Blocks auto-register for game ticks based on their BlockType property:

```cpp
// In BlockType
class BlockType {
public:
    BlockType& setWantsGameTicks(bool wants);
    [[nodiscard]] bool wantsGameTicks() const { return wantsGameTicks_; }

private:
    bool wantsGameTicks_ = false;
};

// In SubChunk - auto-populated from BlockType on place/load
class SubChunk {
public:
    void registerGameTick(int32_t index);
    void unregisterGameTick(int32_t index);
    [[nodiscard]] const std::vector<uint16_t>& gameTickBlocks() const;

private:
    std::vector<uint16_t> gameTickBlocks_;  // Block indices wanting game ticks
};
```

**Auto-registration flow:**
1. On chunk load: scan all blocks, register those with `wantsGameTicks()`
2. On block place: if `wantsGameTicks()`, add to registry
3. On block break: remove from registry

The registry is **not serialized** - it's rebuilt from block types on load.

### Random Ticks (No Registration)

Random ticks don't require registration. Every block can receive them:

```cpp
void processGameTick(World& world) {
    for (auto& [pos, column] : world.loadedColumns()) {
        column.forEachSubChunk([&](int32_t y, SubChunk& subchunk) {
            // Game ticks to registered blocks
            for (uint16_t idx : subchunk.gameTickBlocks()) {
                BlockPos blockPos = toWorldPos(pos, y, idx);
                outbox_.push(BlockEvent::tick(blockPos, TickType::GameTick));
            }

            // Random ticks (no registration needed)
            if (config_.randomTicksPerSubchunk > 0) {
                for (int i = 0; i < config_.randomTicksPerSubchunk; ++i) {
                    uint16_t idx = randomBlockIndex();
                    BlockPos blockPos = toWorldPos(pos, y, idx);
                    outbox_.push(BlockEvent::tick(blockPos, TickType::RandomTick));
                }
            }
        });
    }
}
```

**Rationale for no registration:**
- Random ticks are lightweight - handler can just return if nothing to do
- Avoids registration bugs and storage overhead
- Lookup cost is the same either way (must find handler for block type)
- "Always happens" means fewer points of failure

### Scheduled Ticks

Blocks schedule future ticks explicitly via BlockContext:

```cpp
void BlockContext::scheduleTick(int ticksFromNow, TickType type) {
    uint64_t targetTick = world_.currentTick() + ticksFromNow;
    world_.scheduleBlockTick(pos_, targetTick, type);
}

// In World - priority queue of scheduled ticks
struct ScheduledTick {
    BlockPos pos;
    uint64_t targetTick;
    TickType type;

    bool operator>(const ScheduledTick& other) const {
        return targetTick > other.targetTick;  // min-heap
    }
};

class World {
    std::priority_queue<ScheduledTick, std::vector<ScheduledTick>,
                        std::greater<ScheduledTick>> scheduledTicks_;
};
```

### Tick Configuration

```cpp
struct TickConfig {
    // Game tick interval (default: 50ms = 20 ticks/sec like Minecraft)
    std::chrono::milliseconds gameTickInterval{50};

    // Random ticks per subchunk per game tick (0 = disabled)
    int randomTicksPerSubchunk = 3;

    // Optional: deterministic RNG seed for reproducible worlds
    std::optional<uint64_t> randomTickSeed;
};
```

### Serialization

**Scheduled ticks** must be serialized with the world for persistence:

```cpp
// Per-column pending ticks (stored in column extra data on save)
struct PendingTicks {
    std::vector<ScheduledTick> ticks;  // Serialized as CBOR array
};

// On column unload: move pending ticks for this column to column data
// On column load: restore pending ticks to world scheduler
```

**Game tick registry** is NOT serialized - rebuilt from BlockType on load.

---

## 24.15 Future Extensions

- **Network events** - Replicate events to remote players
- **Entity events** - Entity movement, damage, spawning
- **Undo/redo** - Event log for world edit history
- **Priority levels** - Urgent events (explosions) before routine ticks
- **Chunk-level ticks** - Mob spawning, weather effects

---

[Next: TBD]
