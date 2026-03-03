# FineStructureVoxel — Roadmap

> Future phases and deferred design work. All phases 0–21 are complete.
> See [STATUS.md](STATUS.md) for current state and deferred items from completed phases.

---

## Deferred From Completed Phases

These items were designed and partially scoped but deferred during implementation:

### From Phase 9 (Block Updates)
- **Scheduled tick persistence** — Pending scheduled ticks lost on world save/reload. Ticks should be serialized in column DataContainer under a `"pending_ticks"` key (priority queue serialization).
- **UpdatePropagationPolicy interface** — Mechanism for game modules to decide: when a block update crosses a chunk boundary, load the neighbor or queue the event for when it loads? Currently undefined; each handler does ad-hoc behavior.
- **Network quiescence protocol** — For connected blocks (wires, pipes), a way to detect when an update wave has settled before snapshotting for network sync.

### From Phase 2 (Persistence)
- **LZ4 compression** — Infrastructure in place (ChunkFlags compression bit in region file header). Just needs actual LZ4 encode/decode wired in.
- **Region file corruption recovery** — If a region file's TOC is corrupted (truncated write), no recovery path. Needs checksum validation and fallback to previous TOC backup.

### From Phase 10 (World Generation)
- **Schematic file I/O** — CBOR format designed (see old_docs/21-clipboard-schematic.md). Clipboard/copy-paste in-memory works; disk save/load not wired.

### Design Issue (Phase 19 / Script)
- **Cross-context native functions** — Functions like `show_main_menu()` crossing finescript execution contexts is architecturally awkward. The right fix is a message-passing or state-machine approach where UI state transitions are events rather than direct calls.

---

## Planned Future Work

### finenet Migration
**Priority: Medium** | See [old_docs/PLAN-network-layer.md](../old_docs/PLAN-network-layer.md) and [old_docs/26-network-protocol.md](../old_docs/26-network-protocol.md) for full design.

`Queue<T>`, `KeyedQueue<K,D>`, and `WakeSignal` currently live in `finevox` core but logically belong in a standalone network transport library `finenet`. Migration plan:
1. Extract these primitives into `finenet` library
2. `finenet` becomes a mandatory dependency of `finevox`
3. Local single-player transport uses `finenet`'s in-process MPSC queues behind the same `Connection` API as multiplayer — identical code path for both

Benefit: single-player and multiplayer use the exact same game logic paths; no special-casing.

### Multiplayer / Network Layer
**Priority: Low (requires finenet)** | See [old_docs/26-network-protocol.md](../old_docs/26-network-protocol.md)

Key design decisions from prior discussion:
- **Thin client architecture** — Server is authoritative; client predicts and corrects
- **Semantic quantization** — Block types, entity types, item types transmitted by interned name (stable across versions) not numeric ID
- **Asset streaming** — Textures/models streamed from server if client doesn't have them
- **UI protocol** — Server sends UI definitions (finescript-driven); client renders with local finegui

### Crafting + Inventory UI
**Priority: Medium** | Data model complete (ItemStack, InventoryView, ItemMatch predicate)

Remaining work:
- Recipe registry (shaped 3×3, shapeless 2×2, etc.)
- Crafting screen UI (finegui-based)
- Drag-and-drop inventory management
- Furnace/smelting progress

### Player Health / Survival Stats
**Priority: Medium**
- Hunger/food system (food items consumed, hunger bar)
- Fall damage (velocity-based threshold)
- Death / respawn with configurable spawn points
- Drowning (fluid damage already implemented; needs oxygen bar UI)

### Redstone / Wiring (Game Module, Not Engine)
**Priority: Low** | Would be the first non-trivial game module demonstration

Engine already provides everything needed:
- Scheduled ticks for signal propagation
- Neighbor update events
- Block extra data for signal state
- Cross-chunk update (needs UpdatePropagationPolicy from Phase 9 deferred items first)

### LOD Boundary Stitching
**Priority: Low** | See [old_docs/22-phase6-lod-design.md](../old_docs/22-phase6-lod-design.md)

Current LOD transitions have visible seams at boundaries. Full fix requires geometry shader or per-boundary vertex stitching. Deferred as the transition distance is set far enough that seams are not conspicuous in normal play.

### Cloud Layer / Weather
**Priority: Low**
- Scrolling cloud texture layer above world max height
- Rain/snow particle effects with biome-specific precipitation types
- Weather affects mob spawning rules and sound ambiance

---

## Architecture Decisions Pending

### Block Displacement
Partial design from Phase 0 (data model exists). Full impl requires:
- Face elision rules: only skip faces between two blocks with identical displacement AND matching type
- Mesh sorting for transparent displaced blocks
- See `old_docs/04-core-data-structures.md` section on BlockDisplacement

### Smart Block Placement (Rotation Preview)
Designed in old AI notes (see `old_docs/AI-NOTES.md` "Non-Cube Block System"):
- Player facing + target surface → suggested rotation
- R-key cycling through valid rotations with ghost block preview
- Side placement for vertical slabs
- Needs: ghost block renderer, rotation cycling input binding

---

## Not Planned (Out of Scope)

- Dimension portals / multiple worlds (engine supports it via ResourceLocator dimension registration; no game content planned)
- Water physics beyond current BFS flow (e.g., surface tension, erosion)
- Voxel destruction / explosion radius (game module concern, not engine)
