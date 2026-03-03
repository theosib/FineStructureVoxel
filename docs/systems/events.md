# System: Events & Block Handlers

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/block_event.hpp`, `block_context.hpp`, `update_scheduler.hpp`
**Old docs:** [old_docs/24-event-system.md](../../old_docs/24-event-system.md)

---

## Overview

Game logic runs through `UpdateScheduler`, which owns an inbox/outbox queue pair. Block changes generate `BlockEvent` objects processed by `BlockHandler` callbacks. Three-queue architecture: external input + timer queues → event processing → consolidated outbox. Lighting runs on its own thread with a consolidating queue.

---

## Key Types

| Type | Description |
|------|-------------|
| `BlockEvent` | Unified event: type, pos, blockType, previousType, face mask, tick type, etc. |
| `EventType` | BlockPlaced, BlockBroken, NeighborChanged, Tick, Interact, Strike, Repaint, etc. |
| `BlockContext` | Ephemeral context passed to handlers; wraps World + event data |
| `BlockHandler` | Virtual interface for stateless block behavior |
| `TickType` | `GameTick`, `RandomTick`, `ScheduledTick` |
| `UpdateScheduler` | Owns inbox/outbox; processes events; calls handlers |
| `EventOutbox` | Consolidates events by (position, type); merges NeighborChanged face masks |
| `LightingQueue` | Separate consolidating queue for lighting thread; can lag without blocking game logic |

---

## BlockEvent Factories

```cpp
#include <finevox/core/block_event.hpp>

BlockEvent e;
e = BlockEvent::blockPlaced(pos, newType, oldType, rotation);
e = BlockEvent::blockBroken(pos, oldType);
e = BlockEvent::neighborChanged(pos, changedFace);
e = BlockEvent::tick(pos, TickType::GameTick);
e = BlockEvent::interact(pos, playerEntityId);
e = BlockEvent::strike(pos, playerEntityId);

// Face mask helpers
bool changed = e.hasNeighborChanged(Face::PosX);
e.forEachChangedNeighbor([](Face f){ /* process f */ });

// Fluid events
e.eventType == EventType::FluidPlaced;
e.eventType == EventType::FluidRemoved;
```

---

## BlockContext

Passed to every handler callback. Ephemeral — do not store.

```cpp
#include <finevox/core/block_context.hpp>

// In a BlockHandler override:
void onPlace(BlockContext& ctx) {
    BlockTypeId current = ctx.blockType();
    BlockTypeId previous = ctx.previousType();    // what was there before
    BlockCoord pos = ctx.position();
    uint8_t rot = ctx.rotation();

    // Undo the placement:
    ctx.setBlock(previous);

    // Change rotation:
    ctx.setRotation(newRotation);

    // Read neighbors:
    BlockTypeId neighbor = ctx.getNeighbor(Face::PosX);

    // Schedule a tick (N game ticks from now):
    ctx.scheduleTick(20);

    // Trigger neighbor update:
    ctx.notifyNeighbors();

    // Extra data:
    DataContainer* data = ctx.data();
    DataContainer& data = ctx.getOrCreateData();
}
```

---

## BlockHandler Interface

```cpp
#include <finevox/core/block_handler.hpp>

class MyHandler : public BlockHandler {
public:
    // Called AFTER placement (can undo with ctx.setBlock(ctx.previousType()))
    void onPlace(BlockContext& ctx) override;

    // Called BEFORE removal (can inspect the block being broken)
    void onDestroy(BlockContext& ctx) override;

    // Called on neighbor block change
    void onNeighborUpdated(BlockContext& ctx, Face changedFace) override;

    // Called on scheduled/random/game ticks
    void onTick(BlockContext& ctx, TickType type) override;

    // Called on player right-click
    void onInteract(BlockContext& ctx) override;

    // Called on player left-click
    void onStrike(BlockContext& ctx) override;

    // Called when mesh needs visual update (not game logic)
    void onRepaint(BlockContext& ctx) override;

    // Called when fluid adjacent changes (return true to suppress default behavior)
    bool onFluidUpdate(BlockContext& ctx, FluidTypeId fluid, uint8_t level) override;
    bool onNeighborFluidUpdate(BlockContext& ctx, Face face, FluidTypeId fluid, uint8_t level) override;
};
```

---

## UpdateScheduler

```cpp
#include <finevox/core/update_scheduler.hpp>

// External event input (thread-safe — from game thread or other sources)
scheduler.pushExternalEvent(event);

// Game thread processing
scheduler.processEvents();  // drain external, process inbox, swap to outbox, repeat until stable
scheduler.advanceGameTick(); // fires: game tick events for registered blocks, random ticks, scheduled alarms

// Scheduled ticks
scheduler.cancelScheduledTicks(pos);  // called on block break

// Configuration
TickConfig config;
config.gameTickIntervalMs = 50;        // 20 TPS default (can be 33ms = 30 TPS)
config.randomTicksPerSubchunk = 3;     // random ticks per subchunk per game tick
scheduler.setTickConfig(config);
```

---

## Game Tick Registration

Blocks with `BlockType::wantsGameTicks() == true` are automatically tracked per-SubChunk:
- Auto-register when block placed (`setBlockType` detects it)
- Auto-unregister when block broken
- Registry rebuilt on chunk load (`ChunkColumn::rebuildGameTickRegistries()`)
- NOT serialized — rebuilt deterministically from block palette

```cpp
// In BlockType setup:
blockType.setWantsGameTicks(true);  // opt in to game tick events
```

---

## Random Ticks

No registration needed. Every block in every subchunk CAN receive random ticks. The tick fires `N` random positions per subchunk per game tick (configurable). Handler returns early if the specific block doesn't use them:

```cpp
void onTick(BlockContext& ctx, TickType type) override {
    if (type != TickType::RandomTick) return;
    // ... process random tick
}
```

---

## Event Consolidation Rules

| Rule | Behavior |
|------|---------|
| Same (pos, EventType) | Keep latest; newer wins |
| NeighborChanged | Merge face masks (bitwise OR) |
| Different EventType at same pos | Both kept separately |
| Place + Break at same pos | Both kept (in order) |

Auto-generated: Place/Break events generate `NeighborChanged` for all 6 adjacent positions.

---

## Lighting Integration

Lighting runs on a separate thread with its own consolidating queue. After block changes:
1. `UpdateScheduler` enqueues lighting update to `LightingQueue`
2. Lighting thread consumes queue with BFS propagation
3. Light changes increment `SubChunk::lightVersion_` (atomic)
4. Mesh workers detect stale light version and rebuild mesh

Lighting can lag behind block changes — this is intentional. The game thread is never blocked by lighting.

---

## Version Tracking

Both `blockVersion_` and `lightVersion_` are atomic uint32_t counters in SubChunk:

```cpp
// Mesh staleness detection
uint32_t cachedBlockVer = cacheEntry.blockVersion;
uint32_t cachedLightVer = cacheEntry.lightVersion;
if (sc.blockVersion() != cachedBlockVer || sc.lightVersion() != cachedLightVer) {
    // rebuild mesh
}
```

---

## Gotchas

- `onDestroy` called **BEFORE** block is removed — handler can inspect the block being broken
- `onPlace` called **AFTER** placement — use `ctx.previousType()` + `ctx.setBlock()` to undo
- `LightingUpdate` extended in Phase 21 with `FluidTypeId oldFluid`/`newFluid` fields
- Scheduled tick persistence across save/load is **not yet implemented** (see ROADMAP.md)
- `EventType::SetWorldTime` + `GameActions::setWorldTime()` added in Phase 19
- `EventType::FluidPlaced` / `FluidRemoved` added in Phase 21
