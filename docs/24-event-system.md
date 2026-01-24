# 24. Event System Design

[Back to Index](INDEX.md) | [Previous: Distance and Loading](23-distance-and-loading.md)

---

## 24.1 Overview

The event system provides a unified mechanism for delivering block-related events to various handlers and subsystems. Events originate from block changes, player interactions, tick updates, and other game actions. They are delivered to:

1. **Block handlers** - For block-specific behavior (existing BlockHandler interface)
2. **Lighting system** - For light propagation updates
3. **Physics system** - For collision updates
4. **Network system** - For multiplayer synchronization (future)

### Design Goals

- **Unified event container** - Single struct carries all event data
- **Dual-dispatch pattern** - Single entry point + type-specific methods
- **Zero-copy where possible** - "No value" defaults for unused fields
- **Thread-safe queuing** - Lock-free FIFO for async systems
- **Extensible** - Easy to add new event types and handlers

---

## 24.2 Event Types

```cpp
enum class EventType : uint8_t {
    None = 0,

    // Block lifecycle events
    BlockPlaced,        // Block was placed in the world
    BlockBroken,        // Block was broken/removed
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

## 24.3 BlockEvent Structure

Extends the existing BlockContext concept into a unified event container:

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
    BlockTypeId blockType;        // Current block type (or new type for Place)
    BlockTypeId previousType;     // Previous block type (for Change/Break)
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

    static BlockEvent blockPlaced(BlockPos pos, BlockTypeId newType, Rotation rot = Rotation::IDENTITY);
    static BlockEvent blockBroken(BlockPos pos, BlockTypeId oldType);
    static BlockEvent blockChanged(BlockPos pos, BlockTypeId oldType, BlockTypeId newType);
    static BlockEvent neighborChanged(BlockPos pos, Face changedFace);
    static BlockEvent tick(BlockPos pos, TickType type);
    static BlockEvent playerUse(BlockPos pos, Face face);
    static BlockEvent playerHit(BlockPos pos, Face face);

    // ========================================================================
    // Sentinel Values
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

## 24.4 Event Handler Interface

The dual-dispatch pattern provides both a generic entry point and type-specific methods:

```cpp
/**
 * @brief Interface for systems that handle block events
 *
 * Handlers implement specific methods for event types they care about.
 * The default generic handleEvent() dispatches to specific methods.
 */
class EventHandler {
public:
    virtual ~EventHandler() = default;

    // ========================================================================
    // Generic Entry Point
    // ========================================================================

    /**
     * @brief Handle any event type
     *
     * Default implementation dispatches to specific methods based on type.
     * Override for custom dispatch logic or to handle all events uniformly.
     *
     * @param event The event to handle
     * @param world World containing the block
     */
    virtual void handleEvent(const BlockEvent& event, World& world) {
        switch (event.type) {
            case EventType::BlockPlaced:
                onBlockPlaced(event, world);
                break;
            case EventType::BlockBroken:
                onBlockBroken(event, world);
                break;
            case EventType::BlockChanged:
                onBlockChanged(event, world);
                break;
            case EventType::NeighborChanged:
                onNeighborChanged(event, world);
                break;
            case EventType::TickScheduled:
            case EventType::TickRepeat:
            case EventType::TickRandom:
                onTick(event, world);
                break;
            case EventType::PlayerUse:
                onPlayerUse(event, world);
                break;
            case EventType::PlayerHit:
                onPlayerHit(event, world);
                break;
            case EventType::ChunkLoaded:
                onChunkLoaded(event, world);
                break;
            case EventType::ChunkUnloaded:
                onChunkUnloaded(event, world);
                break;
            case EventType::RepaintRequested:
                onRepaintRequested(event, world);
                break;
            default:
                break;
        }
    }

    // ========================================================================
    // Type-Specific Methods (override what you need)
    // ========================================================================

    virtual void onBlockPlaced(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onBlockBroken(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onBlockChanged(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onNeighborChanged(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onTick(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onPlayerUse(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onPlayerHit(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onChunkLoaded(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onChunkUnloaded(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
    virtual void onRepaintRequested(const BlockEvent& event, World& world) {
        (void)event; (void)world;
    }
};
```

---

## 24.5 Event Queue

Lock-free FIFO queue for delivering events to async systems like lighting:

```cpp
/**
 * @brief Thread-safe event queue for async event delivery
 *
 * Multiple producers can enqueue events; single consumer dequeues.
 * Uses lock-free implementation for minimal contention.
 */
class EventQueue {
public:
    explicit EventQueue(size_t capacity = 16384);

    /**
     * @brief Enqueue an event (producer side)
     * @return true if enqueued, false if queue is full
     */
    bool enqueue(BlockEvent event);

    /**
     * @brief Dequeue an event (consumer side)
     * @param out Output parameter for the event
     * @return true if an event was dequeued, false if queue is empty
     */
    bool dequeue(BlockEvent& out);

    /**
     * @brief Check if queue is empty
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief Get approximate number of pending events
     */
    [[nodiscard]] size_t size() const;

    /**
     * @brief Clear all pending events
     */
    void clear();

private:
    // Lock-free ring buffer implementation
    std::vector<std::atomic<BlockEvent*>> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
    size_t capacity_;
};
```

---

## 24.6 Event Dispatcher

Central dispatcher routes events to registered handlers:

```cpp
/**
 * @brief Central event dispatcher
 *
 * Routes events to registered handlers. Can dispatch synchronously
 * (for same-thread handlers) or enqueue to async queues.
 */
class EventDispatcher {
public:
    EventDispatcher();

    // ========================================================================
    // Handler Registration
    // ========================================================================

    /**
     * @brief Register a synchronous event handler
     *
     * Handler will be called on the dispatching thread.
     */
    void registerHandler(EventHandler* handler);

    /**
     * @brief Unregister a handler
     */
    void unregisterHandler(EventHandler* handler);

    /**
     * @brief Register an async event queue
     *
     * Events will be enqueued rather than dispatched synchronously.
     * Used for systems running on separate threads (lighting, etc.).
     */
    void registerAsyncQueue(EventQueue* queue);

    /**
     * @brief Unregister an async queue
     */
    void unregisterAsyncQueue(EventQueue* queue);

    // ========================================================================
    // Event Dispatch
    // ========================================================================

    /**
     * @brief Dispatch an event to all handlers
     *
     * Synchronous handlers are called immediately.
     * Async queues receive a copy of the event.
     */
    void dispatch(const BlockEvent& event, World& world);

    /**
     * @brief Dispatch events for a block change
     *
     * Convenience method that creates appropriate events and dispatches.
     */
    void onBlockChange(BlockPos pos, BlockTypeId oldType, BlockTypeId newType, World& world);

private:
    std::vector<EventHandler*> handlers_;
    std::vector<EventQueue*> asyncQueues_;
    std::mutex mutex_;  // Protects handler/queue registration
};
```

---

## 24.7 Lighting System Integration

The lighting system uses an async event queue:

```cpp
class LightingEventHandler : public EventHandler {
public:
    explicit LightingEventHandler(LightEngine& engine);

    // Process events from the queue (called on lighting thread)
    void processQueue();

    // EventHandler overrides
    void onBlockPlaced(const BlockEvent& event, World& world) override;
    void onBlockBroken(const BlockEvent& event, World& world) override;
    void onChunkLoaded(const BlockEvent& event, World& world) override;

private:
    LightEngine& engine_;
    EventQueue queue_;
};

// Lighting thread loop
void lightingThreadMain(LightingEventHandler& handler, std::atomic<bool>& running) {
    while (running.load()) {
        handler.processQueue();

        // Sleep briefly if queue was empty to avoid spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

---

## 24.8 BlockHandler Adapter

Adapts the existing BlockHandler interface to the event system:

```cpp
/**
 * @brief Adapter that routes events to BlockHandler methods
 *
 * Bridges the new event system with existing BlockHandler interface.
 */
class BlockHandlerAdapter : public EventHandler {
public:
    void onBlockPlaced(const BlockEvent& event, World& world) override {
        // Get handler for this block type
        BlockHandler* handler = getHandler(event.blockType);
        if (!handler) return;

        // Create BlockContext and call handler
        SubChunk* subChunk = world.getSubChunk(event.chunkPos);
        if (!subChunk) return;

        BlockContext ctx(world, *subChunk, event.pos, event.localPos);
        handler->onPlace(ctx);
    }

    void onBlockBroken(const BlockEvent& event, World& world) override {
        BlockHandler* handler = getHandler(event.previousType);
        if (!handler) return;

        SubChunk* subChunk = world.getSubChunk(event.chunkPos);
        if (!subChunk) return;

        BlockContext ctx(world, *subChunk, event.pos, event.localPos);
        handler->onBreak(ctx);
    }

    void onNeighborChanged(const BlockEvent& event, World& world) override {
        BlockHandler* handler = getHandler(event.blockType);
        if (!handler) return;

        SubChunk* subChunk = world.getSubChunk(event.chunkPos);
        if (!subChunk) return;

        BlockContext ctx(world, *subChunk, event.pos, event.localPos);
        handler->onNeighborChanged(ctx, event.changedFace);
    }

    void onTick(const BlockEvent& event, World& world) override {
        BlockHandler* handler = getHandler(event.blockType);
        if (!handler) return;

        SubChunk* subChunk = world.getSubChunk(event.chunkPos);
        if (!subChunk) return;

        BlockContext ctx(world, *subChunk, event.pos, event.localPos);
        handler->onTick(ctx, event.tickType);
    }

    void onPlayerUse(const BlockEvent& event, World& world) override {
        BlockHandler* handler = getHandler(event.blockType);
        if (!handler) return;

        SubChunk* subChunk = world.getSubChunk(event.chunkPos);
        if (!subChunk) return;

        BlockContext ctx(world, *subChunk, event.pos, event.localPos);
        handler->onUse(ctx, event.face);
    }

private:
    BlockHandler* getHandler(BlockTypeId type) {
        // Look up in BlockRegistry
        return BlockRegistry::global().getHandler(type);
    }
};
```

---

## 24.9 Lock-Free Light Access

For rendering, light values must be accessible without locks:

```cpp
// Light data is stored directly in SubChunk
// Uses atomic operations for thread-safe access

class SubChunk {
public:
    // Light accessors use relaxed memory ordering for rendering
    // (rendering doesn't need strict ordering, just eventual consistency)

    uint8_t getSkyLight(int index) const {
        return unpackSkyLight(light_[index]);
    }

    uint8_t getBlockLight(int index) const {
        return unpackBlockLight(light_[index]);
    }

    // Light version allows mesh builder to detect changes
    uint64_t lightVersion() const {
        return lightVersion_.load(std::memory_order_acquire);
    }

private:
    std::array<uint8_t, VOLUME> light_;  // Non-atomic: single writer (lighting thread)
    std::atomic<uint64_t> lightVersion_{1};  // Incremented after light updates
};
```

---

## 24.10 Usage Example

```cpp
// Setup
EventDispatcher dispatcher;
LightingEventHandler lightHandler(lightEngine);
BlockHandlerAdapter blockAdapter;

dispatcher.registerHandler(&blockAdapter);
dispatcher.registerAsyncQueue(lightHandler.queue());

// When a block changes
void World::setBlock(BlockPos pos, BlockTypeId type) {
    BlockTypeId oldType = getBlock(pos);

    // Actually set the block
    // ...

    // Dispatch events
    dispatcher_.onBlockChange(pos, oldType, type, *this);
}

// The dispatcher handles:
// 1. Calling BlockHandler::onBreak for oldType (synchronous)
// 2. Calling BlockHandler::onPlace for newType (synchronous)
// 3. Queuing light update for lighting thread (async)
// 4. Notifying neighbors (synchronous, triggers more events)
```

---

## 24.11 Implementation Notes

### Memory Efficiency

- BlockEvent is ~64 bytes, fits in a cache line
- Event queue uses ring buffer, no dynamic allocation during normal operation
- "No value" sentinels avoid copying unused fields

### Thread Safety

- EventDispatcher holds mutex only during handler registration
- Event dispatch is lock-free for async queues
- Light data in SubChunk uses version numbers for change detection
- Rendering reads light values without locks (eventual consistency is fine)

### Future Extensions

- **Network events** - Send events to remote players
- **Entity events** - Entity movement, damage, etc.
- **Chunk boundary events** - For cross-chunk updates
- **Priority queues** - Urgent events (explosions) before routine ticks

---

[Next: TBD]
