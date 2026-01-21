# 23. Distance-Based Systems and Chunk Loading

[Back to Index](INDEX.md) | [Previous: Phase 6 LOD Design](22-phase6-lod-design.md)

---

## Overview

This document describes the distance-based systems that control what gets loaded, processed, rendered, and unloaded. These systems span both the **engine layer** (generic mechanisms) and the **game layer** (policy decisions).

**Key Principle:** The engine provides the *mechanisms* (callbacks, timers, distance calculations). The game provides the *policies* (when to process, what to keep loaded, how to handle cross-chunk dependencies).

---

## 1. Distance Zones

Multiple concentric distance zones around the player (or other reference points) control different systems:

```
┌─────────────────────────────────────────────────────────────────┐
│                     Beyond simulation range                      │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              Entity processing range                     │   │
│   │   ┌─────────────────────────────────────────────────┐   │   │
│   │   │         Block update processing range            │   │   │
│   │   │   ┌─────────────────────────────────────────┐   │   │   │
│   │   │   │          Entity render range             │   │   │   │
│   │   │   │   ┌─────────────────────────────────┐   │   │   │   │
│   │   │   │   │      Chunk render distance       │   │   │   │   │
│   │   │   │   │   ┌─────────────────────────┐   │   │   │   │   │
│   │   │   │   │   │   LOD0 (full detail)    │   │   │   │   │   │
│   │   │   │   │   └─────────────────────────┘   │   │   │   │   │
│   │   │   │   │         LOD1-4 zones            │   │   │   │   │
│   │   │   │   └─────────────────────────────────┘   │   │   │   │
│   │   │   └─────────────────────────────────────────┘   │   │   │
│   │   └─────────────────────────────────────────────────┘   │   │
│   └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### 1.1 Zone Definitions

| Zone | Purpose | Typical Range | Hysteresis |
|------|---------|---------------|------------|
| **LOD0** | Full detail rendering | 0-32 blocks | ±4 blocks |
| **LOD1-4** | Reduced detail rendering | 32-512 blocks | Per-level |
| **Render distance** | Maximum chunk rendering | 256 blocks | ±16 blocks |
| **Entity render** | Entity visibility | 128 blocks | ±8 blocks |
| **Block updates** | Redstone, automation | 128 blocks | ±16 blocks |
| **Entity processing** | AI, physics for entities | 192 blocks | ±16 blocks |
| **Chunk loading** | Keep chunks in memory | 384 blocks | ±32 blocks |
| **Simulation** | Maximum processing range | 512 blocks | ±32 blocks |

### 1.2 Hysteresis

All zone boundaries use hysteresis to prevent thrashing:
- Moving **outward**: Cross threshold + hysteresis before downgrade
- Moving **inward**: Cross threshold - hysteresis before upgrade

The existing `LODRequest` with 2x encoding provides this for LOD levels. Similar mechanisms needed for other zones.

---

## 2. Engine Layer (Mechanisms)

### 2.1 Distance Configuration (Engine-Provided)

```cpp
// In config.hpp or new distances.hpp

struct DistanceConfig {
    // Rendering distances
    struct {
        float renderDistance = 256.0f;      // Max chunk render
        float entityRenderDistance = 128.0f; // Entity visibility
        float fogStart = 200.0f;            // Where fog begins
        float fogEnd = 256.0f;              // Where fog is opaque
    } rendering;

    // LOD thresholds (already in LODConfig)
    LODConfig lod;

    // Loading distances
    struct {
        float loadDistance = 384.0f;        // Keep chunks loaded
        float unloadDistance = 416.0f;      // Unload threshold (with hysteresis)
    } loading;

    // Processing distances (game layer sets policy, engine enforces)
    struct {
        float blockUpdateDistance = 128.0f;
        float entityProcessDistance = 192.0f;
        float simulationDistance = 512.0f;
    } processing;

    // Global hysteresis multiplier
    float hysteresisScale = 1.0f;
};
```

### 2.2 Chunk Load/Unload Hooks (Engine-Provided)

Already implemented:
- `SubChunkManager::setChunkLoadCallback()` - notifies when chunks load
- `SubChunkManager::setEvictionCallback()` - notifies when chunks evict

Needed:
- `SubChunkManager::setUnloadVetoCallback()` - game can veto unload
- Activity timer per column (engine tracks, game queries)

```cpp
// Unload veto callback - return true to prevent unload
using UnloadVetoCallback = std::function<bool(ColumnPos pos)>;
void setUnloadVetoCallback(UnloadVetoCallback callback);
```

The veto callback is checked before a column transitions to the unload cache. If it returns true, the column stays active. This is used by:
- `ChunkForceLoader` to keep force-loaded chunks in memory
- Game logic to keep chunks with pending critical updates

### 2.3 Fog System (Engine-Provided)

```cpp
// In WorldRenderer or separate FogSystem

struct FogConfig {
    bool enabled = true;
    float startDistance = 200.0f;   // Where fog begins (0% density)
    float endDistance = 256.0f;     // Where fog is complete (100%)
    glm::vec3 color = {0.7f, 0.8f, 0.9f};  // Sky-like default

    // Dynamic fog (game can modify)
    bool dynamicColor = true;  // Tie to sky color
};
```

### 2.4 Force-Load Mechanism (Engine-Provided)

Force-loading operates at **column granularity** (ColumnPos, not ChunkPos). Loading partial columns causes visual artifacts and breaks game logic that assumes full columns are available.

```cpp
// Columns can have force-load flags that prevent unloading

class ChunkForceLoader {
public:
    explicit ChunkForceLoader(const std::filesystem::path& worldDir);
    ~ChunkForceLoader();

    // Add/remove force-load tickets
    // Multiple tickets can exist per column (ref-counted)
    // Tickets have a reason string for debugging and a persistent flag
    void addTicket(ColumnPos pos, std::string_view reason, bool persistent = false);
    void removeTicket(ColumnPos pos, std::string_view reason);

    // Query
    bool isForceLoaded(ColumnPos pos) const;
    size_t ticketCount(ColumnPos pos) const;

    // Enumeration
    void forEachForceLoaded(std::function<void(ColumnPos, const std::vector<std::string>&)> callback) const;

    // Get all persistently force-loaded columns (for startup loading)
    std::vector<ColumnPos> getPersistentColumns() const;

    // Persistence
    void save();                    // Save to registry file (clean shutdown)
    void load();                    // Load from registry + journal on startup

    // Configuration
    void setMaxJournalSize(size_t bytes);  // Trigger compaction threshold (default 64KB)

private:
    std::filesystem::path registryPath_;   // world/force_loaded.cbor
    std::filesystem::path journalPath_;    // world/force_loaded.journal

    // Background compaction
    void triggerCompaction();
    void compactionThread();
};
```

**Persistence Strategy:**

Force-load state uses a world-level registry with journaling for crash protection:

```
world/
├── force_loaded.cbor      # Main registry (written on clean save)
└── force_loaded.journal   # Append-only journal for crash recovery
```

**Registry Format (CBOR):**
```
{
  "version": 1,
  "columns": [
    {"pos": [x, z], "reasons": ["spawn", "chunk_loader:minecraft:chunk_loader"]},
    {"pos": [x2, z2], "reasons": ["chunk_loader:mymod:advanced_loader"]}
  ]
}
```

**Journal Format:**
Each entry is a CBOR object appended to the file:
```
{"op": "add", "pos": [x, z], "reason": "chunk_loader:..."}
{"op": "remove", "pos": [x, z], "reason": "chunk_loader:..."}
```

**Lifecycle:**

1. **Startup**:
   - Load registry file
   - Replay journal entries to in-memory state
   - Compact immediately (write new registry, atomic rename, delete journal)

2. **Runtime**:
   - All queries use in-memory state (fast)
   - Changes queue to journal writer thread
   - Journal entries flushed periodically or on explicit sync

3. **Background Compaction** (when journal exceeds size threshold):
   - Write new registry to temp file from in-memory state
   - `fsync()` the temp file
   - Atomic rename over old registry (`rename()` is atomic on most filesystems)
   - Drain queued journal entries to new journal file
   - Delete old journal

4. **Shutdown**:
   - Write clean registry
   - Delete journal
   - No journal replay needed on next startup

5. **Crash Recovery**:
   - Registry may be slightly stale
   - Journal contains recent changes
   - Replay reconstructs last known state

**Compaction Details:**

```cpp
// Background compaction runs when journal exceeds threshold
void ChunkForceLoader::compactionThread() {
    // 1. Snapshot current in-memory state
    auto snapshot = getSnapshot();  // Under read lock

    // 2. Write to temp file
    auto tempPath = registryPath_.string() + ".tmp";
    writeRegistry(tempPath, snapshot);
    fsync(tempPath);

    // 3. Atomic replace
    std::filesystem::rename(tempPath, registryPath_);

    // 4. Accept queued journal entries to new journal
    //    (entries that arrived during compaction)
    drainQueueToNewJournal();

    // 5. Delete old journal
    std::filesystem::remove(journalPath_);
}
```

**Size Threshold:** Default 64KB. Configurable via `setMaxJournalSize()`.

Game layer adds tickets for:
- Spawn chunks (persistent)
- Player-placed chunk loaders (persistent)
- Active cross-chunk machinery (transient - rebuilt on load)
- Areas with pending scheduled updates (transient)

**Integration with SubChunkManager:**

```cpp
// ChunkForceLoader hooks into SubChunkManager to prevent unloading
class ChunkForceLoader {
public:
    void bindSubChunkManager(SubChunkManager* manager) {
        manager->setUnloadVetoCallback([this](ColumnPos pos) {
            return isForceLoaded(pos);  // Simple - already per-column
        });
    }
};
```

This requires adding `setUnloadVetoCallback` to SubChunkManager (see §2.2).

### 2.5 Data Storage Layers (Engine-Provided)

The engine provides hierarchical data storage; games decide what to store.

```
┌─────────────────────────────────────────────────────────────────┐
│                     World-Level Data                             │
│  - Force-load registry (§2.4)                                    │
│  - Player data                                                   │
│  - World-wide game state                                         │
├─────────────────────────────────────────────────────────────────┤
│                     Column-Level Data                            │
│  - Pending block updates (when unloading with queued events)     │
│  - Per-column game state                                         │
├─────────────────────────────────────────────────────────────────┤
│                     SubChunk-Level Data                          │
│  - Per-subchunk game state (lighting cache, heightmaps, etc.)    │
│  - Metadata that applies to a 16³ region                         │
├─────────────────────────────────────────────────────────────────┤
│                     Block-Level Data                             │
│  - Tile entity data (chest contents, furnace progress)           │
│  - Per-block game state                                          │
└─────────────────────────────────────────────────────────────────┘
```

**Block Extra Data (Needed):**

Blocks with extra state (chests, signs, furnaces) need per-block `DataContainer` storage. This is sometimes called "tile entity data" or "block entity data".

```cpp
class SubChunk {
public:
    // ... existing methods ...

    // Per-block extra data (sparse - most blocks have none)
    // Key is local index (0-4095)
    DataContainer* getBlockData(int32_t x, int32_t y, int32_t z);
    DataContainer* getBlockData(int32_t index);
    DataContainer& getOrCreateBlockData(int32_t x, int32_t y, int32_t z);
    void removeBlockData(int32_t x, int32_t y, int32_t z);
    bool hasBlockData(int32_t x, int32_t y, int32_t z) const;

    // Iteration for serialization
    void forEachBlockData(std::function<void(int32_t index, const DataContainer&)> callback) const;

private:
    std::unordered_map<int32_t, std::unique_ptr<DataContainer>> blockData_;
};
```

Example usage:
```cpp
// Game code - chest block stores inventory
auto* data = subchunk->getOrCreateBlockData(x, y, z);
data->set("items", inventoryData);
data->set("custom_name", "My Chest");
```

**SubChunk Extra Data (Needed):**

SubChunks need a `DataContainer` for per-subchunk state. Games may prefer this granularity over per-column for data that varies vertically.

```cpp
class SubChunk {
public:
    // ... existing methods ...

    // Per-subchunk extra data (game-defined contents)
    DataContainer& extraData() { return extraData_; }
    const DataContainer& extraData() const { return extraData_; }

private:
    DataContainer extraData_;
};
```

Example use cases:
- Cached lighting data for fast lookup
- Local heightmaps (highest solid block per X/Z within subchunk)
- Biome blend weights
- Custom game state that varies by Y level

```cpp
// Game code - caching lighting state per subchunk
subchunk->extraData().set("light_dirty", false);
subchunk->extraData().set("sky_light_computed", true);
```

**Column Extra Data (Needed):**

ChunkColumn needs a `DataContainer` for per-column state. Primary use case: storing pending block updates when a column unloads.

```cpp
class ChunkColumn {
public:
    // ... existing methods ...

    // Per-column extra data (game-defined contents)
    DataContainer& extraData() { return extraData_; }
    const DataContainer& extraData() const { return extraData_; }

private:
    DataContainer extraData_;
};
```

**Pending Block Updates Storage:**

When a column unloads with pending updates, the game stores them in column extra data:

```cpp
// Game code - storing pending updates on unload
void onColumnUnloading(ChunkColumn& column) {
    auto pendingUpdates = updateScheduler.extractUpdatesForColumn(column.position());
    if (!pendingUpdates.empty()) {
        // Serialize pending updates to column extra data
        auto updateData = serializeUpdates(pendingUpdates);
        column.extraData().set("pending_updates", std::move(updateData));
    }
}

// Game code - restoring pending updates on load
void onColumnLoaded(ChunkColumn& column) {
    if (column.extraData().has("pending_updates")) {
        auto updateData = column.extraData().get<std::vector<uint8_t>>("pending_updates");
        auto updates = deserializeUpdates(updateData);
        updateScheduler.addUpdates(std::move(updates));
        column.extraData().remove("pending_updates");  // Don't re-process
    }
}
```

The engine serializes column extra data automatically when saving. The game controls what goes in it.

---

## 3. Game Layer (Policies)

### 3.1 Block Update System (Game-Defined)

The engine provides the scheduling mechanism; the game defines what updates mean.

```cpp
// Engine provides:
class BlockUpdateScheduler {
public:
    // Schedule an update for a block position
    // Returns a ticket that can be used to cancel
    UpdateTicket scheduleUpdate(BlockPos pos, int tickDelay, int priority = 0);

    // Cancel a scheduled update
    void cancelUpdate(UpdateTicket ticket);

    // Process pending updates (called by game loop)
    // Respects distance limits if configured
    void processPendingUpdates(const glm::dvec3& referencePoint, float maxDistance);

    // Callback for when updates are processed
    using UpdateHandler = std::function<void(BlockPos pos)>;
    void setUpdateHandler(UpdateHandler handler);

    // Persistence
    void serialize(std::vector<uint8_t>& out) const;
    void deserialize(const std::vector<uint8_t>& data);
};
```

The **game** defines:
- What block types schedule updates
- How updates propagate (redstone power levels, water flow, etc.)
- Cross-chunk update handling

### 3.2 Cross-Chunk Update Handling (Game Policy)

**The Problem:** Updates in chunk A may need to propagate to chunk B. If B is unloaded:
1. A's update could load B (cascade loading)
2. A's update could be deferred until B loads
3. A's update could be queued at the chunk boundary

**Recommended Pattern:**

```cpp
// Game implements this policy
class UpdatePropagationPolicy {
public:
    enum class Action {
        LoadTarget,      // Load the target chunk and process
        Defer,           // Queue update, process when target loads
        Drop,            // Discard update (dangerous but fast)
        HaltSource,      // Stop processing in source chunk too
    };

    // Called when an update needs to cross chunk boundaries
    virtual Action onCrossChunkUpdate(
        ChunkPos sourceChunk,
        ChunkPos targetChunk,
        BlockPos targetBlock,
        const BlockUpdate& update
    ) = 0;

    // Called when a chunk with pending deferred updates loads
    virtual void onDeferredChunkLoaded(ChunkPos chunk) = 0;
};
```

### 3.3 Network Quiescence (Game Policy)

The scenario you described: connected blocks across chunks preventing unload.

**Game-Level Solution:**

```cpp
class BlockNetworkManager {
public:
    // Register that blocks are connected (e.g., redstone network)
    void registerConnection(BlockPos a, BlockPos b);
    void unregisterConnection(BlockPos a, BlockPos b);

    // Get all chunks in a network
    std::vector<ChunkPos> getNetworkChunks(BlockPos anyBlock) const;

    // Called by chunk lifecycle to check if safe to unload
    // Returns true if the network can be safely suspended
    bool canSuspendNetwork(ChunkPos chunk) const;

    // Suspend a network (halt processing, save state)
    void suspendNetwork(ChunkPos triggeringChunk);

    // Resume when any chunk in network loads
    void resumeNetwork(ChunkPos loadedChunk);

private:
    // Track which networks are active vs suspended
    // Track pending updates per network
};
```

**Quiescence Protocol:**
1. Chunk A is candidate for unload
2. Check if A is part of a connected network
3. If yes, identify all chunks in network
4. Check if ALL network chunks are beyond processing range
5. If yes, suspend the entire network atomically
6. Serialize pending updates, clear activity timers
7. Allow chunks to unload
8. When any network chunk loads, resume entire network

### 3.4 Activity Timers (Game Policy)

```cpp
// Engine tracks last activity time per chunk
// Game decides what constitutes "activity" and timeout duration

class ChunkActivityTracker {
public:
    // Engine API
    void markActivity(ChunkPos pos);  // Called on any block change
    std::chrono::steady_clock::time_point lastActivity(ChunkPos pos) const;

    // Game configures
    void setInactivityTimeout(std::chrono::seconds timeout);

    // Game queries
    bool isInactive(ChunkPos pos) const;  // Beyond timeout
    std::vector<ChunkPos> getInactiveChunks() const;
};
```

---

## 4. Implementation Priority

### Phase 1 (Now - Engine Foundation)
- [x] LOD distance thresholds with hysteresis (`LODConfig`)
- [x] Chunk load callbacks (`SubChunkManager::setChunkLoadCallback`)
- [ ] Add `DistanceConfig` to `ConfigManager`/`WorldConfig`
- [ ] Add fog configuration to `WorldRenderer`
- [ ] Add render distance configuration to `WorldRenderer`

### Phase 2 (Soon - Core Mechanisms)
- [ ] `ChunkForceLoader` for force-load tickets
- [ ] `ChunkActivityTracker` for activity timing
- [ ] Fog shader implementation
- [ ] Entity render distance (when entity system exists)

### Phase 3 (Later - Block Updates)
- [ ] `BlockUpdateScheduler` for timed block updates
- [ ] Cross-chunk update queueing
- [ ] Update persistence across save/load

### Phase 4 (Game Layer)
- [ ] `BlockNetworkManager` for connected block tracking
- [ ] Network quiescence protocol
- [ ] Game-specific update propagation policies

---

## 5. Configuration Storage

### 5.1 Engine Config (user/config.conf)

```
# Rendering distances
rendering.distance: 256
rendering.entity_distance: 128
rendering.fog.enabled: true
rendering.fog.start: 200
rendering.fog.end: 256

# LOD thresholds (blocks)
lod.0: 32
lod.1: 64
lod.2: 128
lod.3: 256
lod.4: 512
lod.hysteresis: 4

# Loading
loading.distance: 384
loading.unload_hysteresis: 32
```

### 5.2 World Config (world/<name>/world.conf)

```
# Per-world overrides (optional)
rendering.distance: 192  # Smaller for this world
```

### 5.3 Game Module Config

Game modules can register their own config sections:

```cpp
// Game module registers config schema
void MyGameModule::registerContent(ModuleLoader& loader) {
    loader.config().registerSection("mygame.redstone", {
        {"update_distance", ConfigType::Float, 128.0f},
        {"max_updates_per_tick", ConfigType::Int, 1000},
    });
}
```

---

## 6. Interaction Diagram

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│   Game Module    │     │   Engine Core    │     │   Rendering      │
│                  │     │                  │     │                  │
│ Block Updates    │────▶│ SubChunkManager  │◀───▶│ WorldRenderer    │
│ Network Manager  │     │ ActivityTracker  │     │ FogSystem        │
│ Quiescence       │     │ ForceLoader      │     │ LODConfig        │
└──────────────────┘     └──────────────────┘     └──────────────────┘
         │                       │                        │
         │                       │                        │
         ▼                       ▼                        ▼
┌──────────────────────────────────────────────────────────────────┐
│                     DistanceConfig / WorldConfig                  │
│                     (Single source of truth for distances)        │
└──────────────────────────────────────────────────────────────────┘
```

---

## 7. Open Questions

1. ~~**Chunk loader blocks**: Should force-load tickets be serialized with chunks or in a separate registry?~~
   **Resolved:** World-level registry with append-only journal for crash protection. See §2.4.

2. **Spawn chunks**: Are spawn chunks always loaded, or only when players are online?
   - Minecraft: Always loaded in singleplayer, configurable in multiplayer
   - Recommendation: Game-configurable, default to always-loaded

3. **Cross-dimension loading**: Can machinery in one dimension cause loading in another?
   - Complex case: Nether portals, ender chests
   - Recommendation: Game layer decides; engine provides the mechanism

4. **Network discovery**: How expensive is discovering the full extent of a block network? Should it be cached?
   - BFS/DFS from any block can be expensive for large networks
   - Recommendation: Cache network membership, update incrementally on block changes

5. **Update priority**: Within the processing range, should closer updates have higher priority than distant ones?
   - Recommendation: Yes, but game can override

6. ~~**Force-load granularity**: Should force-loading be per-column or per-subchunk?~~
   **Resolved:** Per-column (ColumnPos). Loading partial columns causes visual artifacts and breaks game logic assumptions. See §2.4.

7. ~~**Journal compaction**: When should the force-load journal be compacted?~~
   **Resolved:** On startup (always), and when file size exceeds threshold. Background compaction with queued writes. See §2.4.

---

[Back to Index](INDEX.md)
