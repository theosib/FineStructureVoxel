# System: Game Session

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/game_session.hpp`, `game_actions.hpp`, `entity_state.hpp`
**Old docs:** [old_docs/AI-NOTES.md](../../old_docs/AI-NOTES.md) — Phase 18 section

---

## Overview

`GameSession` is the top-level owner of all game state: World, UpdateScheduler, LightEngine, EntityManager, FluidTickManager, and WorldTime. It manages a dedicated game thread running at 30 TPS with alarm-based wakeup. Commands from the render/input thread are submitted as `finescript::Value` maps via `LocalGameActions::sendAction()` and processed immediately when the game thread wakes (not waiting for next tick). The Value-based command queue makes all commands inherently serializable for multiplayer.

---

## Key Types

| Type | Description |
|------|-------------|
| `GameSession` | Top-level game state owner; factory: `GameSession::createLocal(config)` |
| `GameActions` | Abstract interface for the render thread to submit game commands |
| `LocalGameActions` | Concrete impl; builds `finescript::Value` maps, routes through `Queue<Value>` |
| `EntityState` | Entity snapshot struct with optional `DataContainer extra` (position/velocity as `glm::dvec3`, CBOR serializable) |
| `EntityId` | Defined in `entity_state.hpp` (not `block_event.hpp`) |
| `WorldTime` | Tick-based time; 36000 ticks/day at 30 TPS; has atomic `totalTicks_` for cross-thread reads |

---

## Key APIs

```cpp
// Session creation (wires all subsystems)
auto session = GameSession::createLocal(config);

// Game thread lifecycle (follows LightEngine pattern)
session->startGameThread();
session->stopGameThread();

// Backwards-compat synchronous tick (asserts game thread not running)
session->tick(float dt);

// GameActions — used from render/input thread
GameActions* actions = session->gameActions();
actions->breakBlock(pos);
actions->placeBlock(pos, blockTypeId);
actions->interactBlock(pos);
actions->strikeBlock(pos);
actions->sendPlayerState(entityState);  // player position update
actions->placeFluid(pos, fluidTypeId, level);
actions->removeFluid(pos);
actions->setWorldTime(ticks);
actions->attackEntity(attackerId, targetId, damageInfoValue);  // combat

// WorldTime — read from any thread (atomic)
int64_t ticks = session->worldTime().totalTicks();  // atomic read
float brightness = session->worldTime().skyBrightness();
bool isDay = session->worldTime().isDaytime();
```

---

## Game Thread Behavior

- Wakes on: command arrival in `Queue<finescript::Value>` OR tick alarm (30 TPS)
- Commands processed immediately on wake (no waiting for tick boundary)
- Tick drives: `worldTime.advance()`, `scheduler.advanceGameTick()`, `entityManager.tick()`, `fluidTickManager.tick()`
- Catch-up capped at 10 ticks max to avoid spiral of death
- Sound events pushed **eagerly on calling thread** for instant audio feedback (not deferred)
- Block mutations deferred to game thread via command queue

---

## EntityState (Cross-Thread Communication)

`EntityState` is the entity snapshot struct for game→graphics communication:

```cpp
struct EntityState {
    glm::dvec3 position;
    glm::dvec3 velocity;
    float yaw, pitch;
    EntityId id;
    std::unique_ptr<DataContainer> extra;  // mod-extensible data (nullptr by default)
    // ... other fields

    static EntityState fromEntity(const Entity& e);  // float→double conversion
    std::vector<uint8_t> toCBOR() const;
    static EntityState fromCBOR(std::span<const uint8_t> data);
};
```

Non-trivially copyable (deep-clones `extra`). Used in:
- `sendPlayerState()` to send player position to game thread
- `GraphicsMessage` for entity rendering snapshots
- Network packets (CBOR serializable)

---

## Gotchas

- `WorldTime::totalTicks_` is `std::atomic<int64_t>` — read from any thread safely
- `WorldTime` has explicit **move constructor** because `std::atomic` is non-movable
- `EntityId` is defined in `entity_state.hpp`, NOT `block_event.hpp` — include the right header
- `LocalGameActions` sound events are pushed **eagerly** on the calling thread; only block mutations go through the command queue
- `tick(float dt)` kept for backwards compat; **asserts** game thread is not running — don't call both

---

## Render Demo Wiring

In `render_demo.cpp`:
- Game thread started alongside lighting thread
- Uses `actions->sendPlayerState()` instead of direct entity access
- Player state updates: render thread reads from `GraphicsEventQueue`, game thread writes
