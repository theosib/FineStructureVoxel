# GameSession: Network-Ready Abstraction Layer

## Context

Every new gameplay feature that directly calls `world.placeBlock()` or `world.getBlock()` adds another callsite that must be retrofitted when implementing client/server networking. Currently render_demo has only 3 gameplay mutation callsites, but upcoming features (crafting, fluids, entity AI) will add dozens more. The existing design docs (26-network-protocol.md) specify a comprehensive client/server architecture with command/event message passing.

**Goal:** Introduce a full session boundary NOW — before building more features — so that all gameplay code routes through `GameSession` and `GameActions`. In single-player, these delegate locally. When networking arrives, only the transport layer changes — all gameplay code stays the same.

**Key insight:** The UpdateScheduler three-queue architecture and GraphicsEventQueue are already designed for message-passing. We're completing the picture by adding the inbound command interface (GameActions) and wrapping everything in a session that owns and coordinates all game state.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  render_demo (gameplay code)                            │
│  ├── Sends commands: session.actions().breakBlock(pos)  │
│  ├── Reads events: session.soundEvents().drain()        │
│  └── Reads state: session.world() (for rendering/phys)  │
├─────────────────────────────────────────────────────────┤
│  GameSession  ← owns all game state                     │
│  ├── GameActions (abstract command interface)            │
│  │   └── LocalGameActions (single-player impl)          │
│  ├── World                                              │
│  ├── UpdateScheduler                                    │
│  ├── LightEngine                                        │
│  ├── EntityManager                                      │
│  ├── WorldTime                                          │
│  ├── SoundEventQueue ← events flow OUT                  │
│  └── GraphicsEventQueue ← events flow OUT               │
├─────────────────────────────────────────────────────────┤
│  Future: NetworkGameSession                              │
│  ├── NetworkGameActions → serialize → send to server     │
│  ├── Client-side World (chunk cache from server)         │
│  └── Events arrive from network → queues                │
└─────────────────────────────────────────────────────────┘
```

### What Changes

| Before (current) | After (this plan) |
|---|---|
| render_demo calls `world.breakBlock(pos)` directly | render_demo calls `session.actions().breakBlock(pos)` |
| render_demo pushes sound events manually | GameSession generates sound events as command side-effects |
| render_demo owns World, UpdateScheduler, LightEngine separately | GameSession owns and coordinates all of these |
| render_demo wires `world.setUpdateScheduler()` etc. manually | GameSession handles all wiring internally |
| Sound lookup + push happens in input listener | Sound is a response to the command, handled by LocalGameActions |

### What Stays the Same

- **Block reads** (getBlock, raycast, shape queries) — still direct World access. In networking, client has its own local World populated from server. Reads stay fast.
- **Renderer** — still reads from World via provider lambdas. WorldRenderer takes `World&` regardless.
- **Physics** — still local prediction. PhysicsSystem reads blocks from local World.
- **PlayerController** — still updates from InputManager. Movement is local prediction.
- **Input listener chain** — stays exactly as implemented. Only the block break/place handlers change to use GameActions.

## New Files

### `include/finevox/core/game_actions.hpp` — Command Interface

```cpp
#pragma once
#include "finevox/core/block_pos.hpp"
#include "finevox/core/block_type_id.hpp"
#include "finevox/core/face.hpp"

namespace finevox {

/// Abstract command interface for gameplay mutations.
/// All gameplay code routes through this instead of calling World directly.
/// In single-player: delegates to World/UpdateScheduler.
/// In multiplayer: serializes commands to server.
class GameActions {
public:
    virtual ~GameActions() = default;

    /// Break a block. Returns true if the action was accepted.
    virtual bool breakBlock(BlockPos pos) = 0;

    /// Place a block. Returns true if the action was accepted.
    virtual bool placeBlock(BlockPos pos, BlockTypeId type) = 0;

    /// Right-click interaction with a block. Returns true if block had a handler.
    virtual bool useBlock(BlockPos pos, Face face) = 0;

    /// Left-click hit on a block (non-break, e.g. note block). Returns true if handled.
    virtual bool hitBlock(BlockPos pos, Face face) = 0;
};

}  // namespace finevox
```

### `include/finevox/core/game_session.hpp` — Session Boundary

```cpp
#pragma once
#include "finevox/core/game_actions.hpp"
#include <memory>

namespace finevox {

// Forward declarations
class World;
class UpdateScheduler;
class LightEngine;
class EntityManager;
class WorldTime;
class PhysicsSystem;
struct SoundEvent;
template<typename T> class Queue;
using SoundEventQueue = Queue<SoundEvent>;
struct GraphicsEvent;
using GraphicsEventQueue = Queue<GraphicsEvent>;
struct BlockShapeProvider;

/// Configuration for creating a GameSession
struct GameSessionConfig {
    bool enableLighting = true;
    bool enableSound = true;
    float gravity = -14.0f;
    uint32_t tickRate = 20;           // TPS
    uint32_t randomTicksPerChunk = 3;
};

/// Owns all game state and provides the session boundary.
/// Gameplay code interacts ONLY through:
///   - actions()       → send commands (mutations)
///   - world()         → read state (rendering, physics, raycasting)
///   - soundEvents()   → receive sound events
///   - graphicsEvents()→ receive entity/visual events
///   - tick()          → advance game time
class GameSession {
public:
    /// Create a local (single-player) session
    static std::unique_ptr<GameSession> createLocal(const GameSessionConfig& config = {});

    ~GameSession();

    // Non-copyable, non-movable
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // === Command Interface (mutations go IN) ===
    GameActions& actions();

    // === State Access (reads, for rendering/physics) ===
    World& world();
    const World& world() const;

    // === Subsystem Access ===
    UpdateScheduler& scheduler();
    LightEngine& lightEngine();
    EntityManager& entities();
    WorldTime& worldTime();
    PhysicsSystem& physics();

    // === Event Channels (events come OUT) ===
    SoundEventQueue& soundEvents();
    GraphicsEventQueue& graphicsEvents();

    // === Tick Processing ===
    /// Advance game state by dt seconds. Processes events, ticks entities, advances time.
    void tick(float dt);

    // === Physics Setup ===
    /// Set the block shape provider (needed for physics/raycasting)
    void setBlockShapeProvider(BlockShapeProvider provider);

private:
    GameSession();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finevox
```

### `src/core/game_session.cpp` — Implementation

Contains:
- `GameSession::Impl` struct with all owned subsystems
- `LocalGameActions` class implementing GameActions
- Wiring logic (setUpdateScheduler, setLightEngine, etc.)
- `tick()` implementation (processEvents, advanceGameTick, entity tick, world time)

**LocalGameActions implementation detail:**
- `breakBlock(pos)`: Looks up block type + sound set BEFORE breaking, pushes SoundEvent::blockBreak to SoundEventQueue, then calls `world.breakBlock(pos)`
- `placeBlock(pos, type)`: Calls `world.placeBlock(pos, type)`, on success pushes SoundEvent::blockPlace
- `useBlock(pos, face)`: Pushes `BlockEvent::playerUse(pos, face)` to UpdateScheduler
- `hitBlock(pos, face)`: Pushes `BlockEvent::playerHit(pos, face)` to UpdateScheduler
- Sound generation is conditional on SoundEventQueue being present (handles FINEVOX_HAS_AUDIO gracefully)

## Modified Files

### `examples/render_demo.cpp` — Major simplification

**1. Replace manual subsystem creation with GameSession:**

Before (~lines 500-540):
```cpp
World world;
LightEngine lightEngine(world);
UpdateScheduler scheduler(world);
world.setLightEngine(&lightEngine);
world.setUpdateScheduler(&scheduler);
// ... etc
```

After:
```cpp
auto session = GameSession::createLocal();
World& world = session->world();  // For rendering/physics reads
```

**2. Replace direct block mutations with GameActions:**

Before (left-click break, ~line 940):
```cpp
#ifdef FINEVOX_HAS_AUDIO
    BlockTypeId breakType = world.getBlock(result.blockPos);
    auto breakSoundSet = BlockRegistry::global().getType(breakType).soundSet();
    if (breakSoundSet.isValid()) {
        soundEventQueue.push(SoundEvent::blockBreak(breakSoundSet, result.blockPos));
    }
#endif
    world.breakBlock(result.blockPos);
```

After:
```cpp
    session->actions().breakBlock(result.blockPos);
```

Before (right-click place, ~lines 962-983, two near-identical blocks):
```cpp
    world.placeBlock(placePos, selectedBlock);
#ifdef FINEVOX_HAS_AUDIO
    auto placeSoundSet = BlockRegistry::global().getType(selectedBlock).soundSet();
    if (placeSoundSet.isValid()) {
        soundEventQueue.push(SoundEvent::blockPlace(placeSoundSet, placePos));
    }
#endif
```

After:
```cpp
    session->actions().placeBlock(placePos, selectedBlock);
```

**3. Replace manual tick processing with session tick:**

Before (~lines 1170-1180):
```cpp
worldTime.advance(dt);
auto skyParams = computeSkyParameters(worldTime);
scheduler.advanceGameTick();
size_t eventsProcessed = scheduler.processEvents();
entityManager.tick(dt);
```

After:
```cpp
session->tick(dt);
auto skyParams = computeSkyParameters(session->worldTime());
```

**4. Replace manual SoundEventQueue creation:**

Before: render_demo creates its own `SoundEventQueue` and manually pushes events.
After: render_demo uses `session->soundEvents()` and only READS from it (for audio playback). Sound events are generated by GameSession as command responses.

**5. EntityManager access:**

Before: `entityManager.spawnPlayer(...)`, `entityManager.getLocalPlayer()`, etc.
After: `session->entities().spawnPlayer(...)`, `session->entities().getLocalPlayer()`, etc.

**6. Provider lambdas capture session->world() instead of raw world:**

The face occlusion provider, light provider, and shape provider lambdas all capture `session->world()` reference — functionally identical since `session->world()` returns the same `World&`.

**7. WorldRenderer still takes `World&`:**

```cpp
WorldRenderer worldRenderer(session->world(), *renderer, camera);
```

No WorldRenderer changes needed.

### `CMakeLists.txt` — Add new source files

Add to `finevox` core library:
- `src/core/game_session.cpp`

Headers (automatically found by include paths):
- `include/finevox/core/game_actions.hpp`
- `include/finevox/core/game_session.hpp`

### `tests/test_game_session.cpp` — New test file

Tests for GameSession:
- Create local session, verify subsystems accessible
- Break block via actions(), verify world state changes
- Place block via actions(), verify world state changes
- Verify sound events are generated on break/place
- Verify tick() advances game time and processes events
- Verify useBlock/hitBlock route through event system
- Test session with sound disabled (no crash on break/place)

## What This Enables for Future Networking

When implementing client/server (doc 26):

1. **`NetworkGameActions`** replaces `LocalGameActions`:
   - `breakBlock(pos)` → serialize `PlayerAction{BreakBlock, pos}` → send to server
   - `placeBlock(pos, type)` → serialize `PlayerAction{PlaceBlock, pos, type}` → send
   - Client still shows optimistic prediction locally

2. **`ClientGameSession`** replaces local session:
   - Owns client-side `World` (chunk cache, not full world)
   - Receives `ChunkVoxelData` from server → populates local World
   - Receives `SoundEvent` from server → pushes to SoundEventQueue
   - Receives `GraphicsEvent` from server → pushes to GraphicsEventQueue
   - No UpdateScheduler needed (server runs game logic)

3. **Renderer, physics, input** — ZERO changes needed. They read from `session->world()` regardless of whether it's locally generated or network-populated.

4. **All gameplay code (render_demo)** — ZERO changes needed. It uses `session->actions()` for mutations and reads events from queues.

---

## finenet Integration

The `finenet` library (at `/Users/theosib/projects/finenet/`) provides the network transport. Its design directly maps to the GameSession architecture, and its local transport means single-player can use the same Connection API as multiplayer.

### Architecture with finenet

```
┌─────────────────────────────────────────────────────────────┐
│  render_demo (gameplay code) — UNCHANGED                     │
│  ├── session.actions().breakBlock(pos)                       │
│  ├── session.soundEvents().drainAll()                        │
│  └── session.world() (rendering, physics)                    │
├─────────────────────────────────────────────────────────────┤
│  GameSession (client-side)                                   │
│  ├── ConnectionGameActions → connection.send(Actions, msg)   │
│  ├── World (local chunk cache, populated from network)       │
│  ├── Event queues (populated from inbound messages)          │
│  └── finenet::Connection (transport-agnostic)                │
│       ├── LocalTransport (single-player, zero-copy MPSC)     │
│       └── UdpTransport (multiplayer, real network)           │
├─────────────────────────────────────────────────────────────┤
│  GameSession (server-side)                                   │
│  ├── World (authoritative)                                   │
│  ├── UpdateScheduler, LightEngine, EntityManager, WorldTime  │
│  ├── Drains actions from connection inbound queue             │
│  └── Pushes events to connection outbound via QueueBridge    │
└─────────────────────────────────────────────────────────────┘
```

### Local Transport Simplifies the Split

finenet's local transport uses in-process MPSC queues — `Connection::createLocalPair()` returns two connected `Connection` objects that share queues. Messages are zero-copy moved. This means:

- **Single-player uses the same `Connection` API as multiplayer.** Only the transport backend differs.
- **`LocalGameActions` becomes optional.** A `ConnectionGameActions` that serializes and sends via `connection.send()` works for both local and network play. `LocalGameActions` is retained only as an optimization to avoid serialization overhead in single-player (skip the `Connection` entirely).
- **Open-to-network transition is trivial.** A single-player game starts with `createLocalPair()`. To host, the server starts a `ConnectionListener`; remote clients get network-backed `Connection` objects. The server loop treats all connections uniformly.

### How Event Queues Map to Channels

finenet's `QueueBridge<T>` wires local `Queue<T>` to network channels:

```cpp
// Server side: sound events flow out
QueueBridge<SoundEvent> soundBridge(
    serverSession->soundEvents(), *connection,
    Channels::Effects, MessageType::SoundEvent);

soundBridge.setEncoder([](const SoundEvent& ev) {
    return MessageBuilder(MessageType::SoundEvent)
        .writeU32(ev.soundSet.id)
        .writeU8(static_cast<uint8_t>(ev.action))
        .writeVec3(ev.posX, ev.posY, ev.posZ)
        .build();
});

// In server tick:
soundBridge.flushToNetwork();  // Drains queue → serializes → sends
```

```cpp
// Client side: sound events arrive from network
soundBridge.setDecoder([](MessageReader& r) {
    SoundEvent ev;
    ev.soundSet.id = r.readU32();
    ev.action = static_cast<SoundAction>(r.readU8());
    r.readVec3(ev.posX, ev.posY, ev.posZ);
    return ev;
});

// In client update:
// QueueBridge receives messages → decodes → pushes to local queue
// render_demo drains session->soundEvents() as before
```

### GameSession Event Queues → finenet Channel Mapping

| GameSession Queue | finenet Channel | Direction | Notes |
|---|---|---|---|
| `SoundEventQueue` | Effects (ch 4) | S→C | Unreliable, fire-and-forget |
| `GraphicsEventQueue` | Entities (ch 3) | S→C | Reliable, entity snapshots |
| GameActions commands | Actions (ch 2) | C→S | ReliableOrdered |
| Block/light changes | World (ch 5) | S→C | ReliableOrdered, compressed |
| UI state | UI (ch 6) | Both | ReliableOrdered |

### Threading with finenet

finenet uses wake-on-message threading. The server game logic thread blocks on `WakeSignal` and wakes when:
- Inbound messages arrive from any connection
- A tick deadline fires (20 TPS via `setAlarm()`)

```cpp
// Server game loop
WakeSignal serverWake;
for (auto& conn : connections) {
    conn->inboundQueue().attach(&serverWake);
}

while (running) {
    serverWake.setDeadline(nextTickTime);
    serverWake.wait();  // Wakes on message OR tick deadline

    // Drain inbound from all connections
    for (auto& conn : connections) {
        for (auto& msg : conn->inboundQueue().drainAll()) {
            dispatchAction(conn, msg);
        }
    }

    // Advance game state if tick deadline reached
    if (now >= nextTickTime) {
        session->tick(1.0f / tickRate);
        nextTickTime += tickPeriod;
    }

    // Flush outbound events to all connections
    for (auto& bridge : bridges) {
        bridge->flushToNetwork();
    }
}
```

### Queue Type Migration

When finenet is integrated as a mandatory dependency:
- `finevox::Queue<T>` → `finenet::Queue<T>`
- `finevox::KeyedQueue<K,D>` → `finenet::KeyedQueue<K,D>`
- `finevox::WakeSignal` → `finenet::WakeSignal`
- `finevox::SimpleQueue<T>` and `finevox::CoalescingQueue<K,D>` → deprecated or aliased

The API is identical — only namespace and include paths change. `GameSession` header forward-declares the queue types, so the migration is a find-and-replace on includes + `using` aliases.

### Implementation Phases

**Phase A (this plan):** GameSession with LocalGameActions. No finenet dependency. Cleans up render_demo, establishes the session boundary.

**Phase B (after finenet core exists):** Add `ConnectionGameActions` that routes through `Connection::send()`. GameSession gains a `createNetworked(Connection&)` factory. render_demo unchanged.

**Phase C (server):** Extract server-side game loop from GameSession. Server drains inbound commands from connections, runs authoritative game state, pushes events via QueueBridge. Client GameSession receives events and populates local World.

**Phase D (open-to-network):** Single-player starts with `createLocalPair()`. "Host Game" starts `ConnectionListener`, keeps local connection, accepts remote connections. All connections uniform.

## Implementation Order

1. Create `include/finevox/core/game_actions.hpp` (interface only, ~30 lines)
2. Create `include/finevox/core/game_session.hpp` (class declaration, ~60 lines)
3. Create `src/core/game_session.cpp` (LocalGameActions + GameSession::Impl, ~200 lines)
4. Update `CMakeLists.txt` (add source file)
5. Create `tests/test_game_session.cpp` (~150 lines)
6. Migrate `render_demo.cpp`:
   a. Replace subsystem creation with `GameSession::createLocal()`
   b. Replace 3 block mutation callsites with `session->actions()`
   c. Replace manual tick processing with `session->tick(dt)`
   d. Replace manual sound event pushing (remove `#ifdef FINEVOX_HAS_AUDIO` blocks from input handler)
   e. Wire EntityManager, PhysicsSystem through session
7. Build and test

## Key API References

- `World::placeBlock(BlockPos, BlockTypeId)` → `bool` — [world.hpp](include/finevox/core/world.hpp)
- `World::breakBlock(BlockPos)` → `bool` — [world.hpp](include/finevox/core/world.hpp)
- `World::setUpdateScheduler(UpdateScheduler*)` — [world.hpp](include/finevox/core/world.hpp)
- `World::setLightEngine(LightEngine*)` — [world.hpp](include/finevox/core/world.hpp)
- `UpdateScheduler::pushExternalEvent(BlockEvent)` — [event_queue.hpp](include/finevox/core/event_queue.hpp)
- `UpdateScheduler::processEvents()` → `size_t` — [event_queue.hpp](include/finevox/core/event_queue.hpp)
- `UpdateScheduler::advanceGameTick()` — [event_queue.hpp](include/finevox/core/event_queue.hpp)
- `BlockEvent::playerUse(BlockPos, Face)` — [block_event.hpp](include/finevox/core/block_event.hpp)
- `BlockEvent::playerHit(BlockPos, Face)` — [block_event.hpp](include/finevox/core/block_event.hpp)
- `SoundEvent::blockBreak(SoundSetId, BlockPos)` — used in render_demo
- `SoundEvent::blockPlace(SoundSetId, BlockPos)` — used in render_demo
- `BlockRegistry::global().getType(id).soundSet()` — sound set lookup
- `EntityManager(World&, GraphicsEventQueue&)` — [entity_manager.hpp](include/finevox/core/entity_manager.hpp)

## Verification

1. **Build:** `cmake --build build`
2. **Tests:** `cd build && ctest` — all existing tests pass + new game_session tests
3. **Run:** `./build/render_demo --worldgen`
   - Block break/place still works (with sounds)
   - Movement, camera, physics all work
   - Pause menu works
   - Debug keys (F1-F6, G, V, M, L, C, T, B) all work
   - Day/night cycle advances
   - Entity system ticks
4. **Verify abstraction:** grep render_demo.cpp for `world.breakBlock`, `world.placeBlock` — should find ZERO direct calls (only `session->actions()`)
