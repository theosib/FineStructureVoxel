# Network Transport Layer Specification

A standalone, reusable network library for game client/server communication. Designed to be developed independently and integrated into FineStructureVoxel (and potentially other projects).

---

## 1. Design Goals

1. **Builder/encoder pattern** for message construction — type-safe, chainable, validates on `build()`
2. **Decoder/reader** that mirrors the builder — field-by-field extraction with error handling
3. **Generalized queue transport** — any `Queue<T>` can be wired to a network channel
4. **Multiple logical channels** with independent reliability, ordering, and priority guarantees
5. **Transparent compression** — per-message or per-batch, offloadable to thread pool
6. **Large message fragmentation** — automatic split/reassemble for messages exceeding MTU
7. **Priority scheduling** — urgent packets (input, corrections) skip ahead of bulk data (chunks, assets)
8. **Receiver-side dependency resolution** — out-of-order arrival handled by the consumer, not the transport
9. **UDP-based with selective reliability** — no head-of-line blocking; TCP fallback for environments that block UDP

---

## 2. Architecture Overview

```
Subsystem Queues          Network Layer              Wire            Network Layer          Subsystem Queues
(sender side)                                                                              (receiver side)

┌──────────┐
│ InputQ   │──┐
├──────────┤  │   ┌─────────────┐                                ┌─────────────┐
│ ActionQ  │──┼──►│  Mux +      │   ┌──────────┐  UDP/TCP       │  Demux +    │──►  Per-channel
├──────────┤  │   │  Priority   │──►│  Wire     │───────────────►│  Reassemble │     output queues
│ ChunkQ   │──┼──►│  Scheduler  │   │  Encoder  │                │  + Decomp   │
├──────────┤  │   │  + Compress │   └──────────┘                └─────────────┘
│ AssetQ   │──┘   │  + Fragment │
└──────────┘      └─────────────┘
```

### Key Components

| Component | Role |
|-----------|------|
| **MessageBuilder** | Construct messages field-by-field with builder pattern |
| **MessageReader** | Decode messages field-by-field, mirrors builder |
| **Channel** | Logical stream with defined reliability/ordering semantics |
| **Muxer** | Collects messages from channels, schedules by priority |
| **Fragmenter** | Splits large messages into MTU-sized datagrams |
| **Compressor** | Optional per-message or per-batch compression (zstd) |
| **WireEncoder** | Frames datagrams for UDP/TCP transport |
| **Demuxer** | Routes incoming datagrams to channels, reassembles fragments |
| **AckTracker** | Selective acknowledgment for reliable channels |
| **Connection** | Manages handshake, keepalive, disconnect, congestion |

---

## 3. Message Format

### 3.1 MessageBuilder — Constructing Messages

Follows finevk's builder pattern: chainable methods accumulate fields into a byte buffer, `build()` finalizes and validates.

```cpp
namespace finenet {

class MessageBuilder {
public:
    explicit MessageBuilder(MessageType type);

    // Primitive fields (all return *this for chaining)
    MessageBuilder& writeU8(uint8_t v);
    MessageBuilder& writeU16(uint16_t v);
    MessageBuilder& writeU32(uint32_t v);
    MessageBuilder& writeU64(uint64_t v);
    MessageBuilder& writeI8(int8_t v);
    MessageBuilder& writeI16(int16_t v);
    MessageBuilder& writeI32(int32_t v);
    MessageBuilder& writeI64(int64_t v);
    MessageBuilder& writeF32(float v);
    MessageBuilder& writeF64(double v);
    MessageBuilder& writeBool(bool v);

    // Compound fields
    MessageBuilder& writeString(std::string_view s);    // Length-prefixed
    MessageBuilder& writeBytes(std::span<const uint8_t> data);  // Length-prefixed
    MessageBuilder& writeVec3(float x, float y, float z);
    MessageBuilder& writeBlockPos(int32_t x, int32_t y, int32_t z);

    // Quantized fields (compact wire representation)
    MessageBuilder& writeQuantizedPos(double x, double y, double z);  // 20.12 fixed point
    MessageBuilder& writeQuantizedAngle(float radians);               // 16-bit normalized
    MessageBuilder& writeVarInt(uint64_t v);                          // Variable-length integer

    // CBOR embedding (for complex nested data)
    MessageBuilder& writeCBOR(std::span<const uint8_t> cbor);

    // Array/map support (writes count, caller writes elements)
    MessageBuilder& writeArrayHeader(uint32_t count);
    MessageBuilder& writeMapHeader(uint32_t count);

    // Terminal: finalize and produce the message
    Message build();

    // Query
    size_t currentSize() const;

private:
    MessageType type_;
    std::vector<uint8_t> buffer_;
};

}  // namespace finenet
```

### 3.2 MessageReader — Decoding Messages

Mirrors the builder. Reads fields in the same order they were written. Throws or returns error on format mismatch.

```cpp
class MessageReader {
public:
    explicit MessageReader(const Message& msg);
    explicit MessageReader(std::span<const uint8_t> data);

    // Type identification
    MessageType type() const;

    // Primitive reads (advance cursor)
    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    int32_t readI32();
    float readF32();
    double readF64();
    bool readBool();

    // Compound reads
    std::string_view readString();           // Zero-copy view into buffer
    std::span<const uint8_t> readBytes();    // Zero-copy view
    void readVec3(float& x, float& y, float& z);
    void readBlockPos(int32_t& x, int32_t& y, int32_t& z);

    // Quantized reads
    void readQuantizedPos(double& x, double& y, double& z);
    float readQuantizedAngle();
    uint64_t readVarInt();

    // CBOR
    std::span<const uint8_t> readCBOR();

    // Array/map
    uint32_t readArrayHeader();
    uint32_t readMapHeader();

    // Cursor control
    size_t remaining() const;
    bool hasMore() const;
    void skip(size_t bytes);

    // Error state
    bool valid() const;   // false if any read failed
    std::string_view error() const;
};
```

### 3.3 Message — The Built Product

```cpp
class Message {
public:
    MessageType type() const;
    ChannelId channel() const;           // Set by channel when enqueued
    Priority priority() const;           // Set by channel or overridden
    uint32_t sequenceNumber() const;     // Assigned by channel

    std::span<const uint8_t> payload() const;  // Raw encoded fields
    size_t size() const;                       // Total size including header

    // Metadata (set by transport layer, not user)
    bool isReliable() const;
    bool isOrdered() const;
    bool isCompressed() const;
    bool isFragmented() const;

private:
    friend class MessageBuilder;
    friend class Channel;
    // Internal storage
};
```

### 3.4 MessageType — Variable-Length Type Encoding

Same prefix coding from doc 26 for bandwidth efficiency:

```
0x00-0x7F:    1 byte   — types 0-127    (high-frequency: input, positions, block changes)
0x80-0xBF:    2 bytes  — types 128-16K  (medium: actions, UI, chunk data)
0xC0-0xDF:    3 bytes  — types 16K-2M   (rare: handshake, assets)
0xE0-0xFF:    Reserved
```

```cpp
enum class MessageType : uint32_t {
    // === 1-byte types (high frequency) ===
    PlayerInput        = 0x01,
    InputAck           = 0x02,
    EntityPosition     = 0x03,
    EntityBatch        = 0x04,
    BlockChange        = 0x05,
    LightUpdate        = 0x06,
    KeepAlive          = 0x07,
    SoundEvent         = 0x08,
    EffectTrigger      = 0x09,
    ParticleSpawn      = 0x0A,
    WorldTimeSync      = 0x0B,
    DamageEvent        = 0x0C,

    // === 2-byte types (medium frequency) ===
    ChunkData          = 0x8001,
    ChunkUnload        = 0x8002,
    PlayerAction       = 0x8003,
    ActionResult       = 0x8004,
    EntitySpawn        = 0x8005,
    EntityDespawn      = 0x8006,
    EntityAnimation    = 0x8007,
    PlayerCorrection   = 0x8008,
    BlockCorrection    = 0x8009,
    OpenUI             = 0x8010,
    CloseUI            = 0x8011,
    UIUpdate           = 0x8012,
    UIAction           = 0x8013,
    ChatMessage        = 0x8014,
    InventoryUpdate    = 0x8015,

    // === 3-byte types (rare) ===
    ClientHello        = 0xC00001,
    ServerHello        = 0xC00002,
    AssetRequest       = 0xC00003,
    AssetResponse      = 0xC00004,
    BlockAppearanceList= 0xC00005,
    ItemAppearanceList = 0xC00006,
    EntityModelDef     = 0xC00007,
    TextureAtlas       = 0xC00008,
    AudioSample        = 0xC00009,
    UIDefinition       = 0xC0000A,
    ClientRuleDef      = 0xC0000B,
    NameRegistrySync   = 0xC0000C,
    ServerConfig       = 0xC0000D,
    Disconnect         = 0xC0000E,
};
```

---

## 4. Channel Architecture

Channels are logical streams multiplexed over a single connection. Each channel defines reliability, ordering, and priority guarantees.

### 4.1 Channel Properties

```cpp
struct ChannelConfig {
    ChannelId id;
    std::string name;                  // For debugging

    // Delivery guarantees
    Reliability reliability;           // Unreliable, Reliable, ReliableOrdered
    bool ordered;                      // Preserve send order within this channel

    // Priority
    Priority defaultPriority;          // Default for messages on this channel
    bool allowPriorityOverride;        // Can individual messages override?

    // Compression
    CompressionPolicy compression;     // Never, Always, SizeThreshold
    uint32_t compressionThreshold;     // Bytes; compress if payload exceeds this

    // Fragmentation
    bool allowFragmentation;           // Can messages exceed MTU?
    uint32_t maxMessageSize;           // Hard limit (0 = unlimited)

    // Flow control
    uint32_t maxUnackedBytes;          // Backpressure threshold
    uint32_t maxQueuedMessages;        // Drop oldest if exceeded (unreliable only)
};

enum class Reliability : uint8_t {
    Unreliable,         // Fire-and-forget (sounds, particles)
    Reliable,           // Retransmit until acked, no ordering guarantee
    ReliableOrdered,    // Retransmit + deliver in order (actions, chunks)
};

enum class Priority : uint8_t {
    Critical  = 0,      // Player input, corrections — never delayed
    High      = 1,      // Block changes, action results — minimal delay
    Normal    = 2,      // Entity updates, sounds — standard scheduling
    Low       = 3,      // Chunk data, light updates — background streaming
    Bulk      = 4,      // Asset downloads — fill remaining bandwidth
};

enum class CompressionPolicy : uint8_t {
    Never,              // Already compressed data (textures, audio)
    Always,             // Always compress (chat, definitions)
    SizeThreshold,      // Compress if payload > threshold bytes
};
```

### 4.2 Standard Channel Definitions

```cpp
namespace Channels {

// Player input: must arrive, must be in order, highest priority
constexpr ChannelConfig Input {
    .id = 1, .name = "input",
    .reliability = Reliability::ReliableOrdered,
    .ordered = true,
    .defaultPriority = Priority::Critical,
    .compression = CompressionPolicy::Never,  // Too small, too urgent
    .allowFragmentation = false,
    .maxMessageSize = 256,
};

// Game actions (break/place/use): reliable + ordered
constexpr ChannelConfig Actions {
    .id = 2, .name = "actions",
    .reliability = Reliability::ReliableOrdered,
    .ordered = true,
    .defaultPriority = Priority::High,
    .compression = CompressionPolicy::Never,
    .allowFragmentation = false,
    .maxMessageSize = 512,
};

// Entity state: reliable but unordered (latest wins)
constexpr ChannelConfig Entities {
    .id = 3, .name = "entities",
    .reliability = Reliability::Reliable,
    .ordered = false,
    .defaultPriority = Priority::Normal,
    .compression = CompressionPolicy::SizeThreshold,
    .compressionThreshold = 128,
    .allowFragmentation = false,
};

// Effects (sounds, particles): unreliable, normal priority
constexpr ChannelConfig Effects {
    .id = 4, .name = "effects",
    .reliability = Reliability::Unreliable,
    .ordered = false,
    .defaultPriority = Priority::Normal,
    .compression = CompressionPolicy::Never,
    .allowFragmentation = false,
    .maxQueuedMessages = 64,  // Drop oldest if overwhelmed
};

// World data (chunks, lighting): reliable ordered, low priority, compressed
constexpr ChannelConfig World {
    .id = 5, .name = "world",
    .reliability = Reliability::ReliableOrdered,
    .ordered = true,
    .defaultPriority = Priority::Low,
    .compression = CompressionPolicy::Always,
    .allowFragmentation = true,
    .maxMessageSize = 0,  // Unlimited (chunks can be large)
};

// UI: reliable ordered, high priority
constexpr ChannelConfig UI {
    .id = 6, .name = "ui",
    .reliability = Reliability::ReliableOrdered,
    .ordered = true,
    .defaultPriority = Priority::High,
    .compression = CompressionPolicy::SizeThreshold,
    .compressionThreshold = 256,
    .allowFragmentation = false,
};

// Asset streaming: reliable, bulk priority, compressed, fragmented
constexpr ChannelConfig Assets {
    .id = 7, .name = "assets",
    .reliability = Reliability::Reliable,
    .ordered = false,  // Assets can arrive in any order
    .defaultPriority = Priority::Bulk,
    .compression = CompressionPolicy::SizeThreshold,
    .compressionThreshold = 1024,
    .allowFragmentation = true,
    .maxMessageSize = 0,
};

// Chat: reliable ordered, normal priority
constexpr ChannelConfig Chat {
    .id = 8, .name = "chat",
    .reliability = Reliability::ReliableOrdered,
    .ordered = true,
    .defaultPriority = Priority::Normal,
    .compression = CompressionPolicy::Never,
    .allowFragmentation = false,
    .maxMessageSize = 4096,
};

// Control (handshake, keepalive, disconnect): reliable ordered, critical
constexpr ChannelConfig Control {
    .id = 0, .name = "control",
    .reliability = Reliability::ReliableOrdered,
    .ordered = true,
    .defaultPriority = Priority::Critical,
    .compression = CompressionPolicy::Never,
    .allowFragmentation = false,
};

}  // namespace Channels
```

### 4.3 Message-to-Channel Routing

```
Channel 0 (Control):   ClientHello, ServerHello, KeepAlive, Disconnect, ServerConfig
Channel 1 (Input):     PlayerInput
Channel 2 (Actions):   PlayerAction, ActionResult, PlayerCorrection, BlockCorrection
Channel 3 (Entities):  EntityPosition, EntityBatch, EntitySpawn, EntityDespawn,
                        EntityAnimation, InputAck, DamageEvent
Channel 4 (Effects):   SoundEvent, EffectTrigger, ParticleSpawn
Channel 5 (World):     ChunkData, ChunkUnload, BlockChange, LightUpdate, WorldTimeSync
Channel 6 (UI):        OpenUI, CloseUI, UIUpdate, UIAction, InventoryUpdate
Channel 7 (Assets):    AssetRequest, AssetResponse, BlockAppearanceList,
                        ItemAppearanceList, EntityModelDef, TextureAtlas,
                        AudioSample, UIDefinition, ClientRuleDef, NameRegistrySync
Channel 8 (Chat):      ChatMessage
```

---

## 5. Wire Format

### 5.1 Datagram Structure (UDP)

Every UDP datagram has this header:

```
┌─────────────────────────────────────────────────────────────┐
│ Connection ID (4 bytes)                                      │
│ Datagram Sequence (4 bytes)                                  │
│ Ack Bitfield (4 bytes) — selective ack of recent datagrams   │
│ Last Ack Sequence (4 bytes) — most recent received datagram  │
│ Timestamp (4 bytes) — sender's clock, for RTT estimation     │
├─────────────────────────────────────────────────────────────┤
│ Packet 1: [Channel(1) | Flags(1) | SeqNum(2) | Len(2) | Payload...]  │
│ Packet 2: [Channel(1) | Flags(1) | SeqNum(2) | Len(2) | Payload...]  │
│ ...                                                          │
└─────────────────────────────────────────────────────────────┘
```

**Datagram header** (20 bytes):
- `connectionId`: Identifies the session (prevents stale packet misrouting)
- `datagramSeq`: Monotonic per-connection, for ack tracking
- `ackBitfield`: Bits represent receipt of `lastAckSeq - 1` through `lastAckSeq - 32`
- `lastAckSeq`: Most recent datagram sequence received from peer
- `timestamp`: Sender's monotonic clock (milliseconds), for RTT calculation

**Packet header** (6 bytes per packet within datagram):
- `channel`: Channel ID (0-255)
- `flags`: Bitfield:
  - Bit 0: Reliable (expects ack)
  - Bit 1: Ordered (has sequence number)
  - Bit 2: Fragment (part of larger message)
  - Bit 3: Compressed (payload is zstd-compressed)
  - Bit 4: Last fragment (final piece of fragmented message)
- `seqNum`: Per-channel sequence number (for ordering/reliability)
- `length`: Payload length in bytes

Multiple packets can be packed into one datagram up to MTU (typically 1200 bytes for safe UDP).

### 5.2 TCP Framing (Fallback)

For environments that block UDP, TCP framing uses length-prefixed messages:

```
┌──────────────────────────────────────┐
│ Length (4 bytes, big-endian)          │
│ Channel (1 byte)                     │
│ Flags (1 byte)                       │
│ SeqNum (2 bytes)                     │
│ Payload (Length - 4 bytes)           │
└──────────────────────────────────────┘
```

TCP inherently provides reliability and ordering, so the AckTracker is bypassed. Multiple TCP connections can be used for parallel channels (avoids head-of-line blocking between channels).

### 5.3 Hybrid Strategy

```
Default: Try UDP first
  ├── Success → Use UDP for all channels
  └── Fail (timeout) → Fall back to TCP
        ├── Single TCP connection (simple)
        └── Multiple TCP connections (one per priority tier, optional)
```

---

## 6. Fragmentation & Reassembly

Messages larger than ~1100 bytes (MTU minus headers) are fragmented automatically.

### 6.1 Fragment Header (appended to packet flags)

When `flags.Fragment` is set:

```
│ FragmentId (2 bytes)    — unique ID for this message's fragment group
│ FragmentIndex (1 byte)  — 0-based index within the group
│ FragmentCount (1 byte)  — total fragments in the group (on first fragment)
│ TotalSize (4 bytes)     — total uncompressed message size (on first fragment only)
```

### 6.2 Reassembly

```cpp
class FragmentAssembler {
    // Called for each incoming fragment
    // Returns complete message when all fragments received, nullopt otherwise
    std::optional<Message> addFragment(ChannelId channel, uint16_t fragmentId,
                                        uint8_t index, uint8_t count,
                                        std::span<const uint8_t> data);

    // Timeout stale incomplete messages (GC)
    void expireStale(std::chrono::milliseconds maxAge);
};
```

**Design decisions:**
- Compression happens BEFORE fragmentation (compress full message, then split)
- Decompression happens AFTER reassembly (reassemble, then decompress)
- Each fragment is individually reliable (retransmitted if lost)
- Fragment groups use a 16-bit ID scoped per-channel, wrapping

---

## 7. Compression

### 7.1 Strategy

```
Message arrives at Muxer
  │
  ├── Size < channel.compressionThreshold → send uncompressed
  ├── channel.compression == Never → send uncompressed
  └── Compress:
        ├── Size < 4KB → compress synchronously on send thread
        └── Size >= 4KB → dispatch to compression thread pool
              │
              └── Compressed result enqueued with original priority
                  (other small/high-priority packets continue flowing)
```

### 7.2 Compression Thread Pool

```cpp
class CompressionPool {
public:
    explicit CompressionPool(uint32_t threadCount = 2);

    // Async compress. Callback fires on pool thread with result.
    void compressAsync(std::vector<uint8_t> data, Priority priority,
                       std::function<void(std::vector<uint8_t> compressed)> callback);

    // Async decompress.
    void decompressAsync(std::vector<uint8_t> compressed, uint32_t originalSize,
                         std::function<void(std::vector<uint8_t> decompressed)> callback);

    // Sync versions for small payloads
    std::vector<uint8_t> compress(std::span<const uint8_t> data);
    std::vector<uint8_t> decompress(std::span<const uint8_t> data, uint32_t originalSize);
};
```

### 7.3 Algorithm

**zstd** (level 3 for real-time, level 9 for bulk assets). Dictionary training possible for voxel data patterns.

---

## 8. Priority Scheduling (Muxer)

The Muxer decides what goes into each outbound datagram.

### 8.1 Scheduling Algorithm

```
Each tick (typically tied to send rate, e.g. 60 Hz):
  1. Collect pending packets from all channels
  2. Sort by: (priority ASC, then channel weight, then age DESC)
  3. Pack packets into datagrams up to MTU:
     a. Always include ack header
     b. Fill with highest-priority packets first
     c. If a reliable packet hasn't been acked within RTT*2, retransmit it
  4. Send datagrams

Bandwidth limiting:
  - Track bytes sent per second
  - If approaching limit, only send Critical/High priority
  - Low/Bulk channels get remaining bandwidth
```

### 8.2 Per-Channel Bandwidth Allocation

```cpp
struct BandwidthBudget {
    uint32_t maxBytesPerSecond;           // Total bandwidth cap
    float criticalReserve;                // Fraction always reserved (e.g. 0.2)
    float bulkMaxFraction;                // Max fraction for Bulk priority (e.g. 0.5)
};
```

**Example allocation at 1 Mbps:**
- Critical (input, corrections): 200 Kbps reserved, always available
- High (actions, UI): Up to 300 Kbps
- Normal (entities, effects): Up to 300 Kbps
- Low (chunks, lights): Up to 200 Kbps (shared with remaining)
- Bulk (assets): Up to 500 Kbps but only when others aren't using it

---

## 9. Reliability & Acknowledgment

### 9.1 Selective ACK (SACK)

Every outbound datagram piggybacks acks:
- `lastAckSeq`: Highest datagram sequence received
- `ackBitfield`: 32 bits, each representing receipt of `lastAckSeq - N`

This means up to 33 datagrams can be acked per outbound datagram without dedicated ack packets.

### 9.2 Retransmission

```cpp
class AckTracker {
    // Called when we send a reliable packet
    void trackSent(uint16_t channelSeq, ChannelId channel,
                   std::span<const uint8_t> payload, Priority priority);

    // Called when datagram ack received — marks contained packets as acked
    void processAck(uint32_t datagramSeq, uint32_t lastAckSeq, uint32_t ackBitfield);

    // Returns packets needing retransmission (RTT * 2 timeout, exponential backoff)
    std::vector<PendingPacket> getRetransmissions();

    // RTT tracking
    float smoothedRtt() const;    // EWMA
    float rttVariance() const;
};
```

### 9.3 Ordered Delivery

For `ReliableOrdered` channels, the receiver buffers out-of-order packets:

```cpp
class OrderedReceiver {
    // Accept packet with sequence number. Returns packets ready for delivery
    // (may return multiple if a gap was filled).
    std::vector<Message> receive(uint16_t seq, Message msg);

    // How many packets are buffered waiting for gaps
    size_t bufferedCount() const;
};
```

---

## 10. Receiver-Side Dependency Resolution

Beyond per-channel ordering, some messages have cross-channel dependencies. The network layer doesn't enforce these — the application layer does.

### 10.1 Dependency Patterns

| Dependency | Resolution |
|-----------|-----------|
| Block appearance must arrive before chunk using it | Client requests missing appearances; buffers chunk until resolved |
| Entity model must arrive before entity renders | Client shows placeholder until model arrives |
| NameRegistry must be synced before inventory decodes | Part of handshake; incremental updates have sequence numbers |
| UI definition must exist before OpenUI | Client requests missing definition; shows loading indicator |

### 10.2 Application-Level Request/Response

```cpp
// Client detects missing dependency
if (!hasBlockAppearance(appearanceId)) {
    // Queue an AssetRequest on the Assets channel
    connection.send(Channels::Assets,
        MessageBuilder(MessageType::AssetRequest)
            .writeU8(AssetType::BlockAppearance)
            .writeU16(appearanceId)
            .build());

    // Buffer the chunk data until dependency resolves
    pendingChunks_.emplace(chunkPos, std::move(chunkMessage));
}
```

The network layer provides the transport; the game layer handles the dependency logic.

---

## 11. Queue Integration

### 11.1 QueueBridge — Connecting Game Queues to Network Channels

A generic adapter that drains a local `Queue<T>` and sends messages, or receives messages and pushes to a local queue.

```cpp
template<typename T>
class QueueBridge {
public:
    QueueBridge(Queue<T>& localQueue, Connection& connection,
                ChannelId channel, MessageType messageType);

    // Configure serialization (user provides encode/decode)
    using Encoder = std::function<Message(const T& item)>;
    using Decoder = std::function<T(MessageReader& reader)>;

    void setEncoder(Encoder enc);
    void setDecoder(Decoder dec);

    // Sending: drain local queue → encode → send on channel
    // Called from network send thread
    size_t flushToNetwork();

    // Receiving: decode incoming messages → push to local queue
    // Called from network receive thread
    void onReceive(const Message& msg);

    // Batching: collect multiple items into one message
    void setBatchMode(bool enabled, uint32_t maxBatchSize = 32);
};
```

### 11.2 Example: Wiring SoundEventQueue

```cpp
// Server side: sound events generated locally, sent to client
QueueBridge<SoundEvent> soundBridge(session.soundEvents(), connection,
                                     Channels::Effects, MessageType::SoundEvent);

soundBridge.setEncoder([](const SoundEvent& ev) {
    return MessageBuilder(MessageType::SoundEvent)
        .writeU32(ev.soundSet.id)
        .writeU8(static_cast<uint8_t>(ev.action))
        .writeVec3(ev.posX, ev.posY, ev.posZ)
        .writeF32(ev.volume)
        .writeF32(ev.pitch)
        .writeBool(ev.positional)
        .build();
});

// Client side: receive sound events, push to local queue for audio playback
soundBridge.setDecoder([](MessageReader& r) {
    SoundEvent ev;
    ev.soundSet.id = r.readU32();
    ev.action = static_cast<SoundAction>(r.readU8());
    r.readVec3(ev.posX, ev.posY, ev.posZ);
    ev.volume = r.readF32();
    ev.pitch = r.readF32();
    ev.positional = r.readBool();
    return ev;
});
```

### 11.3 Example: Wiring GraphicsEventQueue (Entity Snapshots)

```cpp
QueueBridge<GraphicsEvent> entityBridge(session.graphicsEvents(), connection,
                                         Channels::Entities, MessageType::EntityBatch);

entityBridge.setBatchMode(true, 64);  // Batch up to 64 entity updates per message

entityBridge.setEncoder([](const GraphicsEvent& ev) {
    return MessageBuilder(MessageType::EntityPosition)
        .writeU32(ev.entityId)
        .writeVec3(ev.posX, ev.posY, ev.posZ)
        .writeVec3(ev.velX, ev.velY, ev.velZ)
        .writeQuantizedAngle(ev.yaw)
        .writeQuantizedAngle(ev.pitch)
        .writeBool(ev.onGround)
        .writeU8(ev.animationId)
        .build();
});
```

---

## 12. Connection Lifecycle

### 12.1 States

```
Disconnected → Connecting → Connected → Disconnecting → Disconnected
                   │                         │
                   └── Timeout ──────────────┘
```

### 12.2 Handshake (over Control channel)

```
Client                              Server
   │                                   │
   │── ClientHello ────────────────►  │
   │   (protocol version, cache hashes)│
   │                                   │
   │◄──────────────── ServerHello ──  │
   │   (session ID, cache validation,  │
   │    name registry, server config)  │
   │                                   │
   │── AckReady ───────────────────►  │
   │   (client confirms ready)         │
   │                                   │
   │◄── Begin streaming chunks ─────  │
   │◄── Begin streaming assets ─────  │
```

### 12.3 Connection API

```cpp
class Connection {
public:
    // Client-side: connect to server
    static std::unique_ptr<Connection> connect(
        const std::string& host, uint16_t port,
        const ConnectionConfig& config = {});

    // Server-side: accept from listener
    // (created by ConnectionListener::accept())

    ~Connection();

    // State
    ConnectionState state() const;
    bool isConnected() const;

    // Send a message on a channel
    void send(ChannelId channel, Message msg);
    void send(ChannelId channel, Message msg, Priority overridePriority);

    // Register message handler (called from receive thread)
    using MessageHandler = std::function<void(const Message&)>;
    void onMessage(MessageType type, MessageHandler handler);

    // Bulk: register handler for all messages on a channel
    void onChannel(ChannelId channel, MessageHandler handler);

    // Connection events
    using StateHandler = std::function<void(ConnectionState)>;
    void onStateChange(StateHandler handler);

    // Stats
    ConnectionStats stats() const;  // RTT, bandwidth, packet loss, etc.

    // Lifecycle
    void disconnect(std::string_view reason = "");
    void update();  // Call each frame: processes incoming, sends outgoing
};

struct ConnectionConfig {
    bool preferUdp = true;             // Try UDP first, fall back to TCP
    uint32_t maxBandwidthBps = 0;      // 0 = unlimited
    uint32_t connectionTimeoutMs = 5000;
    uint32_t keepAliveIntervalMs = 30000;
    uint32_t compressionThreads = 2;
    std::vector<ChannelConfig> channels = {/* defaults from section 4.2 */};
};

struct ConnectionStats {
    float rttMs;                       // Smoothed round-trip time
    float packetLossPercent;           // Recent loss rate
    uint64_t bytesSent, bytesReceived;
    uint64_t packetsSent, packetsReceived;
    uint32_t pendingReliablePackets;   // Awaiting ack
    std::map<ChannelId, ChannelStats> perChannel;
};
```

### 12.4 Server Listener

```cpp
class ConnectionListener {
public:
    static std::unique_ptr<ConnectionListener> listen(
        uint16_t port, const ListenerConfig& config = {});

    // Accept new connections (non-blocking, returns nullptr if none pending)
    std::unique_ptr<Connection> accept();

    // Callback-based alternative
    using AcceptHandler = std::function<void(std::unique_ptr<Connection>)>;
    void onAccept(AcceptHandler handler);

    void stop();
};
```

---

## 13. Complete Networkable Data Catalog

### 13.1 Summary by Channel

| Channel | Direction | Messages | Frequency | Size | Reliability |
|---------|-----------|----------|-----------|------|-------------|
| Control | Both | Hello, KeepAlive, Disconnect, Config | Rare | Small | Reliable+Ordered |
| Input | C→S | PlayerInput | 20-60 Hz | ~35 B | Reliable+Ordered |
| Actions | Both | PlayerAction, ActionResult, Corrections | 1-5/s | ~16-50 B | Reliable+Ordered |
| Entities | S→C | Positions, Spawns, Despawns, Anims, Acks | 20 Hz | ~16-60 B | Reliable |
| Effects | S→C | Sounds, Particles, Triggers | 1-10/s | ~16-29 B | Unreliable |
| World | S→C | Chunks, BlockChanges, Light, Time | 1-10/s | ~16 B - 7 KB | Reliable+Ordered |
| UI | Both | Open/Close/Update/Action, Inventory | 1-10/s | ~5-50 B | Reliable+Ordered |
| Assets | Both | Textures, Models, Audio, Defs | On demand | 1-300 KB | Reliable |
| Chat | Both | ChatMessage | Rare | ~150-600 B | Reliable+Ordered |

### 13.2 Bandwidth Estimates (Active Gameplay)

| Traffic | Direction | Estimated BW |
|---------|-----------|-------------|
| Player input | C→S | ~1 KB/s |
| Entity updates (20 nearby) | S→C | ~6 KB/s |
| Block changes + light | S→C | ~2 KB/s |
| Sound/effect events | S→C | ~1 KB/s |
| Chunk streaming (walking) | S→C | ~20-50 KB/s |
| Input acks | S→C | ~0.5 KB/s |
| **Total steady-state** | | **~30-60 KB/s** |
| Asset burst (on connect) | S→C | ~200 KB/s for 5-10s |

---

## 14. Library Structure

```
finenet/
├── include/finenet/
│   ├── finenet.hpp              // Single include
│   ├── message.hpp              // Message, MessageType
│   ├── message_builder.hpp      // MessageBuilder
│   ├── message_reader.hpp       // MessageReader
│   ├── channel.hpp              // ChannelConfig, ChannelId, standard channels
│   ├── connection.hpp           // Connection, ConnectionConfig, ConnectionStats
│   ├── listener.hpp             // ConnectionListener
│   ├── queue_bridge.hpp         // QueueBridge<T> template
│   ├── priority.hpp             // Priority, Reliability enums
│   └── compression.hpp          // CompressionPool
├── src/
│   ├── connection.cpp           // Connection implementation
│   ├── muxer.cpp                // Priority scheduling, datagram packing
│   ├── demuxer.cpp              // Datagram unpacking, channel routing
│   ├── ack_tracker.cpp          // Selective ACK, retransmission
│   ├── fragment.cpp             // Fragmentation + reassembly
│   ├── compression.cpp          // zstd wrapper + thread pool
│   ├── wire_udp.cpp             // UDP socket layer
│   ├── wire_tcp.cpp             // TCP socket layer (fallback)
│   └── ordered_receiver.cpp     // Per-channel ordered delivery buffer
├── tests/
│   ├── test_message.cpp         // Builder/reader round-trip
│   ├── test_channel.cpp         // Reliability, ordering
│   ├── test_fragment.cpp        // Fragmentation/reassembly
│   ├── test_compression.cpp     // Compress/decompress
│   ├── test_muxer.cpp           // Priority scheduling
│   ├── test_ack.cpp             // Selective ACK, retransmission
│   ├── test_connection.cpp      // End-to-end connect/send/receive
│   └── test_queue_bridge.cpp    // Queue<T> ↔ network round-trip
└── CMakeLists.txt
```

### 14.1 Dependencies

- **zstd** — compression (header-only or linked)
- **Platform sockets** — POSIX `sendto`/`recvfrom` (Unix), Winsock (Windows)
- No dependency on finevox, finevk, or any game code

### 14.2 Integration with FineStructureVoxel

```cpp
// In game code (e.g., server main loop):
#include <finenet/finenet.hpp>
#include <finevox/core/game_session.hpp>

auto listener = finenet::ConnectionListener::listen(25565);
auto session = finevox::GameSession::createLocal();

// Wire game queues to network
finenet::QueueBridge<SoundEvent> soundBridge(
    session->soundEvents(), *connection, finenet::Channels::Effects,
    finenet::MessageType::SoundEvent);

// In update loop:
connection->update();       // Process network I/O
soundBridge.flushToNetwork(); // Drain game events → network
session->tick(dt);          // Advance game state
```

---

## 15. Game Feature → Network Requirements (Shattered Lands)

This section maps game features from the stress-test game concept to concrete
network traffic. Each subsection identifies what data crosses the wire, which
direction, approximate frequency, and which channel it belongs to.

### 15.1 Structural Integrity & Collapse

When a player removes a load-bearing block, the server evaluates structural
integrity via block event propagation. This can cascade into hundreds or
thousands of block changes in a single tick.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **CollapseEvent** | S→C | World | Batch message: list of (blockPos, newBlockId) pairs for all blocks that fell/broke in one cascade. Single message, not individual BlockChange per block — a 500-block collapse should be ~4 KB, not 500 separate messages. |
| **DebrisSpawn** | S→C | Entities | Falling debris are physics entities. Batch spawn for many debris items from one collapse. |
| **CollapseSound** | S→C | Effects | Single sound event at collapse centroid. Clients compute local rumble from magnitude. |
| **CollapseParticles** | S→C | Effects | Dust cloud parameters (center, radius, material type). Client generates particles locally. |

**New MessageType needed:** `CollapseEvent = 0x0D` (1-byte, high frequency during combat/horde events). Contains: centroid position, block count, array of (relativePos, newBlockId) pairs using delta encoding from centroid.

**Bandwidth note:** A major collapse during a horde event could spike to ~10-20 KB in a single tick. The priority scheduler should allow this to temporarily borrow from Low/Bulk budget since it's gameplay-critical.

### 15.2 Fluid Simulation

Water, lava, oil, and steam flow continuously. The server is authoritative over
fluid state; clients receive updates.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **FluidUpdate** | S→C | World | Batch: array of (blockPos, fluidType, fluidLevel 0-15). Sent per-tick for chunks with active fluid sim. |
| **FluidSourceChange** | S→C | World | A spring dries up, a dam breaks, lava source exposed. Single block-level event. |

**New MessageType needed:** `FluidUpdate = 0x0E` (1-byte — fluid updates are frequent when rivers/lava are active near players). Contains: chunk coordinates + array of (localPos, fluidType, level) tuples. Delta-encoded against previous state when possible.

**Design consideration:** Fluid updates are high-volume but loss-tolerant in a sense — if you miss one tick's fluid update, the next tick's update contains the current state, not a delta. Consider using **Reliable** (not ReliableOrdered) for fluid updates so a dropped packet doesn't block subsequent ones. Or even **Unreliable** with periodic full-state snapshots.

**Bandwidth note:** An active waterfall near a player could generate ~500 B/tick of fluid updates. Multiple active fluid sources could push ~2-5 KB/s sustained.

### 15.3 Weather, Seasons & Environment

Weather is server-authoritative. Clients need to render the correct weather
and apply gameplay effects (temperature, crop growth, visibility).

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **WeatherUpdate** | S→C | World | Weather state: precipitation type/intensity (0-1), wind direction (angle) + speed, cloud cover, temperature modifier, visibility range. Sent when weather changes or periodically (~0.1 Hz). |
| **SeasonChange** | S→C | World | New season ID + transition progress. Rare (~4 per game-year). |
| **TemporalStormBegin** | S→C | World | Storm intensity, duration, wave count. Triggers client visual effects. |
| **TemporalStormWave** | S→C | World | Wave number, mob types/counts incoming. Client shows warning UI. |
| **TemporalStormEnd** | S→C | World | Storm results, loot summary. |
| **EnvironmentSync** | S→C | World | Periodic full state: time of day, season, weather, moon phase. Sent on connect and every ~60s as drift correction. |

**New MessageTypes needed:**
- `WeatherUpdate = 0x0F` (1-byte — moderate frequency)
- `EnvironmentEvent = 0x10` (1-byte — storms, seasons, special events)

**Bandwidth:** Negligible. ~50-100 B every few seconds.

### 15.4 Automation: Conveyors, Machines & Power

Factory floors with conveyor belts, processing machines, and power grids
generate continuous state updates. This is the heaviest sustained traffic
source after chunk streaming.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **BeltItemUpdate** | S→C | Entities | Items on belts are visual entities with positions. Batch update for all visible belt items. Position interpolated client-side between updates. |
| **MachineStateUpdate** | S→C | World | Machine on/off, current recipe, progress (0-100%), fuel level. Only sent on state change, not continuously. |
| **PowerGridUpdate** | S→C | World | Per-grid summary: total production, total consumption, storage level, brownout state. Sent on change or ~1 Hz. |
| **ConveyorItemSpawn/Despawn** | S→C | Entities | Item appears on belt (exits machine) or disappears (enters machine). |

**Design consideration:** Belt items are numerous but predictable. Rather than sending position updates for every item on every belt, send **belt segment state**: belt segment ID, item type, entry timestamp. Client computes positions deterministically from belt speed. Only send corrections when something disrupts flow (item removed, belt stopped, jam). This dramatically reduces bandwidth vs. treating each belt item as an entity.

**New MessageTypes needed:**
- `MachineState = 0x8016` (2-byte — moderate frequency)
- `PowerGridState = 0x8017` (2-byte — low frequency)
- `BeltSegmentState = 0x8018` (2-byte — moderate frequency)

**Bandwidth estimate:** A medium factory (~50 machines, ~20 belt segments in view) generates ~1-3 KB/s of state updates. Large factories could push ~5-10 KB/s.

### 15.5 Oligosynthetic Logic / Signal Propagation

When a player flips a switch, signal changes cascade through wires, gates,
latches, and actuators. The server computes the full propagation; clients
need the resulting state changes.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **SignalBatch** | S→C | World | Batch of (blockPos, signalValue) for all blocks whose signal state changed this tick. One message per tick, only when signals are active. |
| **ActuatorEvent** | S→C | World/Effects | Piston extends, door opens, dispenser fires. Block state change + optional sound/particle. |

**Design consideration:** Signal propagation can cascade across hundreds of
wires in one tick (think: a clock circuit). Rather than sending individual
updates, send a **signal batch** with all changed blocks in one message.
Clients apply the batch atomically. Use delta encoding — only blocks whose
state changed since last tick.

**New MessageType needed:** `SignalBatch = 0x11` (1-byte — can be high frequency with active circuits). Contains: block count + array of (blockPos, newSignalValue).

**Bandwidth:** Idle circuits: 0. Active Turing machine: ~500 B-2 KB/tick. Typical clock + door system: ~50-100 B/tick.

### 15.6 Boss Fights

Bosses are high-priority entities with complex state. During a boss fight,
the server sends frequent updates about boss phase, health, AoE zones, and
terrain modification.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **BossState** | S→C | Entities | Boss entity with extended data: phase, health, shield HP, charge-up timer, target player ID, AoE zone positions. ~10-20 Hz during fight. |
| **BossPhaseChange** | S→C | Entities | Phase transition: new phase ID, arena modifications, cutscene trigger. Reliable. |
| **ArenaTerrainChange** | S→C | World | Boss attack reshapes terrain. Sent as a batch of block changes (similar to CollapseEvent). |
| **BossAoEZone** | S→C | Effects | Area-of-effect indicator: center, radius, shape, duration, damage type. Client renders warning zone. |

**Design consideration:** Boss state should have **elevated priority** during a fight. Entity channel normally uses Priority::Normal, but boss updates should be Priority::High. Use the per-message priority override mechanism.

**Bandwidth:** During boss fight: ~2-5 KB/s for boss state + terrain changes. Brief spikes during phase transitions.

### 15.7 Horde Events (Temporal Storms)

Horde events spawn 50-200+ mobs over several minutes, all converging on
player bases. This is the peak stress case for entity networking.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **MassEntitySpawn** | S→C | Entities | Batch spawn: array of (entityId, entityType, position, velocity). 20-50 mobs per wave, 5-10 waves. |
| **HordePathUpdate** | S→C | Entities | Batch position updates for all horde mobs. These mobs move predictably (toward base), so use **dead reckoning** — send target position + speed, client interpolates. Only send corrections when path changes (obstacle, reroute). |
| **EntityDeathBatch** | S→C | Entities | Batch despawn when AoE kills multiple mobs simultaneously. |

**Bandwidth estimate:** Peak horde (100 active mobs near base):
- If treating each mob as a full entity: ~100 × 30 B × 10 Hz = ~30 KB/s (too much)
- With dead reckoning + batch updates: ~5-10 KB/s (manageable)
- Key optimization: horde mobs are predictable. Send target + speed. Only correct on path change.

**New MessageType needed:** `EntityBatchSpawn = 0x8019` (2-byte), `EntityBatchDespawn = 0x801A` (2-byte)

### 15.8 Vehicles & Multi-Block Entities

Vehicles (minecarts, boats, airships) are compound entities that occupy
multiple blocks and carry passengers/cargo.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **VehicleState** | S→C | Entities | Position, rotation, velocity, passenger list, cargo summary. ~10-20 Hz for nearby vehicles. |
| **VehicleMount/Dismount** | Both | Actions | Player mounts or dismounts a vehicle. Changes input routing — mounted player sends vehicle controls, not movement. |
| **AirshipBlockChange** | S→C | World | Player modifies blocks on a moving airship. Block positions are relative to vehicle origin, not world coordinates. |
| **VehicleSpawn/Despawn** | S→C | Entities | Vehicle enters/leaves player's view range. Includes full vehicle definition (block layout, entity model, properties). |

**Design consideration:** Airships are the hardest case — they're moving
structures that players build on. The server must track two coordinate spaces
(world and vehicle-local). Block changes on an airship are sent as vehicle-
relative coordinates. Clients transform to world space for rendering.

**New MessageType needed:** `VehicleState = 0x801B` (2-byte)

### 15.9 Dimension Transitions

When a player enters a dimensional portal, the server must unload the old
dimension's chunks, load the new dimension, and stream the new environment.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **DimensionTransition** | S→C | Control | Server tells client: you're entering dimension X. Client shows loading screen, flushes old chunk cache. |
| **DimensionReady** | S→C | Control | Server has prepared spawn area. Client can begin rendering. Followed immediately by chunk streaming. |
| **Bulk chunk stream** | S→C | World | 20-50 chunks streamed for the new dimension's spawn area. Same as initial connect. |

**Design consideration:** During a dimension transition, the client is
effectively "reconnecting" to a different world without dropping the TCP/UDP
connection. The server should:
1. Send DimensionTransition (client shows loading screen)
2. Stop sending entity/world updates for old dimension
3. Generate new dimension spawn area
4. Send DimensionReady
5. Begin streaming new chunks at elevated priority

**Bandwidth:** Spike of ~100-300 KB over 2-5 seconds (same as initial chunk load).

### 15.10 NPC Interaction, Quests & Trading

NPC dialog, quest state, and player-to-player trading generate UI-heavy
network traffic.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **NPCDialog** | S→C | UI | Dialog text, response options, NPC expression/animation. Triggered by player interaction. |
| **QuestUpdate** | S→C | UI | Quest accepted/progressed/completed. Quest ID, objective progress, rewards. |
| **QuestLog** | S→C | UI | Full quest state sync. Sent on connect and periodically. |
| **TradeProposal** | Both | UI | Player A offers items to Player B. Shows both inventories and proposed exchange. |
| **TradeConfirm/Cancel** | Both | UI | Both players must confirm for trade to execute. |
| **NPCRelationship** | S→C | UI | Relationship value change with an NPC. Affects available trades/quests. |
| **MerchantInventory** | S→C | UI | NPC shop inventory. Sent when player opens shop UI. |

**New MessageTypes needed:**
- `QuestUpdate = 0x8020` (2-byte)
- `TradeAction = 0x8021` (2-byte)
- `NPCInteraction = 0x8022` (2-byte)
- `MerchantInventory = 0x8023` (2-byte)

**Bandwidth:** Negligible. Dialog/trade is low-frequency, small payloads.

### 15.11 Player Vital Stats & Status Effects

The server is authoritative over player health, hunger, thirst, temperature,
stamina, and active buffs/debuffs.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **VitalStatsUpdate** | S→C | UI | All vital stats in one message: health, maxHealth, hunger, thirst, temperature, stamina, comfort. Sent on change or ~1 Hz. |
| **StatusEffectUpdate** | S→C | UI | Active buffs/debuffs: effect ID, remaining duration, intensity. Sent on change. |
| **PlayerDeath** | S→C | Control | Death event: cause, killer (if any), dropped item summary, respawn options. |
| **RespawnRequest** | C→S | Actions | Player chooses respawn point (bed, spawn, etc.). |

**New MessageType needed:** `VitalStats = 0x12` (1-byte — updates frequently during combat, survival pressure). `StatusEffect = 0x8024` (2-byte).

### 15.12 Territory, Claims & PvP

Territory claiming and raid mechanics require synchronization of claim
boundaries and permission states.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **ClaimUpdate** | S→C | World | Claim block placed/removed/upgraded. Includes claim boundaries (AABB) and owner info. |
| **PermissionChange** | S→C | World | Player granted/revoked access to a claim. |
| **RaidBegin/End** | S→C | World | PvP raid window opens/closes on a territory. |
| **ClaimQuery** | C→S | Actions | Client requests claim info for a block position (who owns this?). |

**New MessageType needed:** `ClaimEvent = 0x8025` (2-byte)

### 15.13 Crafting Stations & Minigames

Interactive crafting (knapping, smithing) requires real-time state sync
between client and server.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **CraftingStationOpen** | Both | UI | Player opens a crafting station. Server sends station state: available recipes (filtered by adjacent inventory + player inventory), station tier. |
| **MinigameState** | S→C | UI | Smithing: anvil grid state, target pattern, metal temperature. Knapping: stone grid, remaining material. Sent at ~10 Hz during active minigame. |
| **MinigameAction** | C→S | Actions | Player hammer strike position, knapping chip-off position. Server validates and returns updated state. |
| **CraftingResult** | S→C | UI | Item crafted. Quality/durability based on minigame performance. |

**New MessageTypes needed:**
- `CraftingState = 0x8026` (2-byte)
- `MinigameAction = 0x8027` (2-byte)

### 15.14 Mob Nests & Ecosystem

Nest state affects spawning behavior and is relevant when players are
near dens/nests.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **NestState** | S→C | Entities | Nest block: population count, creature type, activity state (dormant/active/agitated). Sent when player enters nest range or state changes. |
| **NestDestroyed** | S→C | World | Nest block broken. Area permanently cleared of that mob type. Block change + special event for any quest tracking. |
| **CreatureBreed** | S→C | Entities | New creature spawned from nest. Normal EntitySpawn but with nest source ID for tracking. |

No new MessageType needed — these can use existing EntitySpawn, BlockChange, and a general `GameEvent = 0x8028` type.

### 15.15 Farming & Growth

Crop growth happens on server tick. Clients need visual state updates.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **CropStateUpdate** | S→C | World | Block metadata change: crop type, growth stage (0-7), hydration, fertility. Sent as a BlockChange variant with extended metadata. |
| **AnimalState** | S→C | Entities | Penned animal: hunger, age, traits, pregnancy state. Sent when player inspects animal or on periodic ~0.2 Hz for nearby animals. |

**Design consideration:** On chunk load, the server applies all growth that
occurred while the chunk was unloaded (catch-up ticks). The chunk data sent
to the client already contains current growth state. CropStateUpdate is only
needed for real-time changes while the player is watching.

### 15.16 Map, Waypoints & Exploration

Map discovery and shared waypoints need synchronization.

| Event | Direction | Channel | Notes |
|-------|-----------|---------|-------|
| **MapReveal** | S→C | World | Minimap data for newly explored chunks. Low-res (1 pixel per block column, top block type + height). ~16-32 B per chunk. |
| **WaypointSync** | Both | UI | Create/delete/rename waypoint. Position, icon, name, visibility (personal/party/public). |
| **DeathMarker** | S→C | UI | Position of death + timestamp. Auto-created on player death. |
| **PartyMemberPosition** | S→C | UI | Periodic position of party members for minimap display. ~1 Hz per party member. |

**New MessageType needed:** `MapData = 0x801C` (2-byte), `WaypointAction = 0x801D` (2-byte)

---

## 16. Updated MessageType Registry

Incorporating all game feature requirements from Section 15:

```cpp
enum class MessageType : uint32_t {
    // === 1-byte types (high frequency) ===
    PlayerInput        = 0x01,
    InputAck           = 0x02,
    EntityPosition     = 0x03,
    EntityBatch        = 0x04,
    BlockChange        = 0x05,
    LightUpdate        = 0x06,
    KeepAlive          = 0x07,
    SoundEvent         = 0x08,
    EffectTrigger      = 0x09,
    ParticleSpawn      = 0x0A,
    WorldTimeSync      = 0x0B,
    DamageEvent        = 0x0C,
    CollapseEvent      = 0x0D,  // NEW: batch structural collapse
    FluidUpdate        = 0x0E,  // NEW: batch fluid level changes
    WeatherUpdate      = 0x0F,  // NEW: weather state change
    EnvironmentEvent   = 0x10,  // NEW: storms, seasons, special events
    SignalBatch        = 0x11,  // NEW: batch logic signal changes
    VitalStats         = 0x12,  // NEW: player HP/hunger/thirst/temp/stamina

    // === 2-byte types (medium frequency) ===
    ChunkData          = 0x8001,
    ChunkUnload        = 0x8002,
    PlayerAction       = 0x8003,
    ActionResult       = 0x8004,
    EntitySpawn        = 0x8005,
    EntityDespawn      = 0x8006,
    EntityAnimation    = 0x8007,
    PlayerCorrection   = 0x8008,
    BlockCorrection    = 0x8009,
    OpenUI             = 0x8010,
    CloseUI            = 0x8011,
    UIUpdate           = 0x8012,
    UIAction           = 0x8013,
    ChatMessage        = 0x8014,
    InventoryUpdate    = 0x8015,
    MachineState       = 0x8016,  // NEW: automation machine state
    PowerGridState     = 0x8017,  // NEW: power production/consumption
    BeltSegmentState   = 0x8018,  // NEW: conveyor belt item flow
    EntityBatchSpawn   = 0x8019,  // NEW: horde mass spawn
    EntityBatchDespawn = 0x801A,  // NEW: mass despawn (AoE kills)
    VehicleState       = 0x801B,  // NEW: vehicle pos/rot/passengers
    MapData            = 0x801C,  // NEW: minimap chunk reveal
    WaypointAction     = 0x801D,  // NEW: create/delete/update waypoint
    DimensionTransition= 0x801E,  // NEW: entering new dimension
    DimensionReady     = 0x801F,  // NEW: new dimension ready to render
    QuestUpdate        = 0x8020,  // NEW: quest accepted/progressed/done
    TradeAction        = 0x8021,  // NEW: player trade proposal/confirm
    NPCInteraction     = 0x8022,  // NEW: NPC dialog/response
    MerchantInventory  = 0x8023,  // NEW: shop inventory
    StatusEffect       = 0x8024,  // NEW: buff/debuff applied/removed
    ClaimEvent         = 0x8025,  // NEW: territory claim change
    CraftingState      = 0x8026,  // NEW: crafting station state
    MinigameAction     = 0x8027,  // NEW: smithing/knapping input
    GameEvent          = 0x8028,  // NEW: generic game event (nest, etc.)
    BossState          = 0x8029,  // NEW: boss HP/phase/AoE zones
    RespawnRequest     = 0x802A,  // NEW: player respawn choice
    VehicleMount       = 0x802B,  // NEW: mount/dismount vehicle
    PartyUpdate        = 0x802C,  // NEW: party invite/join/leave/kick

    // === 3-byte types (rare) ===
    ClientHello        = 0xC00001,
    ServerHello        = 0xC00002,
    AssetRequest       = 0xC00003,
    AssetResponse      = 0xC00004,
    BlockAppearanceList= 0xC00005,
    ItemAppearanceList = 0xC00006,
    EntityModelDef     = 0xC00007,
    TextureAtlas       = 0xC00008,
    AudioSample        = 0xC00009,
    UIDefinition       = 0xC0000A,
    ClientRuleDef      = 0xC0000B,
    NameRegistrySync   = 0xC0000C,
    ServerConfig       = 0xC0000D,
    Disconnect         = 0xC0000E,
    BiomeDefinitions   = 0xC0000F,  // NEW: biome data for client rendering
    RecipeList         = 0xC00010,  // NEW: all recipes (on connect)
    LootTableDefs      = 0xC00011,  // NEW: loot tables (for client prediction)
    ScriptBundle       = 0xC00012,  // NEW: scripting language code for client
};
```

---

## 17. Updated Channel Routing

```
Channel 0 (Control):   ClientHello, ServerHello, KeepAlive, Disconnect,
                        ServerConfig, DimensionTransition, DimensionReady,
                        PlayerDeath, RespawnRequest

Channel 1 (Input):     PlayerInput

Channel 2 (Actions):   PlayerAction, ActionResult, PlayerCorrection,
                        BlockCorrection, VehicleMount, MinigameAction,
                        ClaimEvent, TradeAction, RespawnRequest

Channel 3 (Entities):  EntityPosition, EntityBatch, EntitySpawn, EntityDespawn,
                        EntityAnimation, InputAck, DamageEvent,
                        EntityBatchSpawn, EntityBatchDespawn, VehicleState,
                        BossState, GameEvent

Channel 4 (Effects):   SoundEvent, EffectTrigger, ParticleSpawn

Channel 5 (World):     ChunkData, ChunkUnload, BlockChange, LightUpdate,
                        WorldTimeSync, CollapseEvent, FluidUpdate,
                        WeatherUpdate, EnvironmentEvent, SignalBatch,
                        MachineState, PowerGridState, BeltSegmentState,
                        MapData

Channel 6 (UI):        OpenUI, CloseUI, UIUpdate, UIAction, InventoryUpdate,
                        VitalStats, StatusEffect, QuestUpdate, NPCInteraction,
                        MerchantInventory, CraftingState, WaypointAction,
                        PartyUpdate

Channel 7 (Assets):    AssetRequest, AssetResponse, BlockAppearanceList,
                        ItemAppearanceList, EntityModelDef, TextureAtlas,
                        AudioSample, UIDefinition, ClientRuleDef,
                        NameRegistrySync, BiomeDefinitions, RecipeList,
                        LootTableDefs, ScriptBundle

Channel 8 (Chat):      ChatMessage
```

---

## 18. Updated Bandwidth Estimates (Full Shattered Lands Gameplay)

### 18.1 Scenario: Solo Exploration (Calm)

| Traffic | Direction | Estimated BW |
|---------|-----------|-------------|
| Player input | C→S | ~1 KB/s |
| Entity updates (10 passive mobs) | S→C | ~3 KB/s |
| Block changes + light | S→C | ~1 KB/s |
| Sound/effect events | S→C | ~0.5 KB/s |
| Chunk streaming (walking) | S→C | ~20-40 KB/s |
| Weather + environment | S→C | ~0.1 KB/s |
| Vital stats | S→C | ~0.1 KB/s |
| Map reveal | S→C | ~1 KB/s |
| **Total** | | **~25-45 KB/s** |

### 18.2 Scenario: Horde Night (Peak Combat)

| Traffic | Direction | Estimated BW |
|---------|-----------|-------------|
| Player input | C→S | ~2 KB/s |
| Entity updates (100+ hostile mobs) | S→C | ~8-12 KB/s (dead reckoning) |
| Mass spawn/despawn | S→C | ~2 KB/s (bursts) |
| Block changes (mob digging, collapse) | S→C | ~5-10 KB/s |
| Collapse events | S→C | ~3-5 KB/s (bursts) |
| Damage events | S→C | ~1 KB/s |
| Sound/effects (combat) | S→C | ~2 KB/s |
| Vital stats (rapid changes) | S→C | ~0.5 KB/s |
| Fluid (if lava traps active) | S→C | ~1-2 KB/s |
| Signal propagation (trap systems) | S→C | ~1-2 KB/s |
| **Total** | | **~25-35 KB/s** |

### 18.3 Scenario: Factory Floor (Automation Heavy)

| Traffic | Direction | Estimated BW |
|---------|-----------|-------------|
| Player input | C→S | ~1 KB/s |
| Machine state updates | S→C | ~2-3 KB/s |
| Belt segment state | S→C | ~3-5 KB/s |
| Power grid updates | S→C | ~0.5 KB/s |
| Signal batch (active circuits) | S→C | ~1-2 KB/s |
| Entity updates (belt items entering/leaving) | S→C | ~2 KB/s |
| Sound (machine ambience) | S→C | ~0.5 KB/s |
| **Total** | | **~10-15 KB/s** |

### 18.4 Scenario: Boss Fight (4-Player Party)

| Traffic | Direction | Estimated BW (per client) |
|---------|-----------|-------------|
| Player input × 4 | C→S | ~2 KB/s each |
| Boss state (high frequency) | S→C | ~3-5 KB/s |
| Player entity updates × 3 others | S→C | ~3 KB/s |
| Arena terrain changes | S→C | ~2-5 KB/s (bursts) |
| Damage events | S→C | ~1 KB/s |
| Effects (boss AoE, particles) | S→C | ~3-5 KB/s |
| Sound | S→C | ~1 KB/s |
| Party member vitals | S→C | ~0.5 KB/s |
| **Total per client** | | **~15-25 KB/s** |

### 18.5 Scenario: 50-Player Server (Worst Case)

| Traffic | Direction | Estimated BW (server total) |
|---------|-----------|-------------|
| Player inputs | C→S | ~50-100 KB/s total |
| Entity updates (all players + mobs) | S→C | ~150-300 KB/s total (interest management critical) |
| Chunk streaming (multiple exploring) | S→C | ~200-500 KB/s total |
| All other game state | S→C | ~100-200 KB/s total |
| **Total server egress** | | **~500 KB/s - 1.1 MB/s** |

**Critical optimization for 50-player:** Interest management / area-of-interest
filtering. Each client only receives entity updates for entities within their
view range. The server maintains per-connection "subscriptions" for which
entities and which chunk regions each client cares about.

---

## 19. Additional Dependency Patterns

Extending Section 10 with game-feature dependencies:

| Dependency | Resolution |
|-----------|-----------|
| Block appearance must arrive before chunk using it | Client requests missing; buffers chunk |
| Entity model must arrive before entity renders | Client shows placeholder |
| RecipeList must arrive before crafting UI works | Sent during handshake |
| BiomeDefinitions must arrive before client can render biome-specific effects | Sent during handshake; client uses default until received |
| ScriptBundle must arrive before scripted blocks work | Sent during handshake or on first encounter; client ignores scripted behavior until loaded |
| DimensionReady must arrive before client renders new dimension | Client shows loading screen until received |
| MerchantInventory must arrive before shop UI populates | Client shows loading spinner in shop UI |
| CraftingState (adjacent inventory contents) must arrive before crafting UI shows available materials | Sent when player opens station; UI shows "scanning..." until received |
| VehicleState with full block layout must arrive before airship renders | Client shows bounding box placeholder until full state received |
| BossState must arrive before boss health bar renders | Boss entity spawned with initial state; health bar appears immediately |

---

## 20. Open Questions

1. **Encryption** — TLS over TCP is straightforward; for UDP, use DTLS or custom symmetric encryption post-handshake?
2. **NAT traversal** — Relay servers for peer-to-peer? Or always client→dedicated server?
3. **Multiple TCP streams** — Instead of UDP channels, use N TCP connections (one per priority tier)? Simpler but more OS overhead.
4. **Dictionary compression** — Pre-train zstd dictionaries for voxel chunk data patterns? Could improve compression ratio significantly.
5. **WebSocket/WebRTC** — Browser client support? Would need a WebRTC data channel layer.
6. **Packet coalescing** — Should the muxer wait a small amount (e.g., 1ms) to batch multiple small messages into one datagram, or send immediately?
7. **Congestion control** — Implement our own (like QUIC's) or keep it simple (fixed rate + backoff on loss)?

### Game-Feature-Driven Open Questions

8. **Fluid update strategy** — Should fluid updates be reliable or unreliable? They're frequent and self-correcting (each update contains current state, not delta). Unreliable saves retransmission bandwidth but risks visual glitches. A hybrid approach (unreliable with periodic reliable snapshots) might work best.
9. **Belt item optimization** — Sending belt segment state (belt ID + item type + entry time) vs. individual item entities is a major bandwidth/complexity tradeoff. The deterministic approach requires all clients to agree on belt simulation timing, which interacts with clock sync.
10. **Structural collapse throttling** — A massive cascade (1000+ blocks) could generate a huge single-tick message. Should the server spread collapse notification over multiple ticks for bandwidth smoothing, even if the physics resolves in one tick? This introduces visual delay but prevents bandwidth spikes.
11. **Horde mob dead reckoning** — How aggressively should the server correct client-side dead reckoning for horde mobs? Too many corrections defeats the purpose. Too few and mobs teleport when they hit walls or reroute. Threshold-based correction (only correct if >2 blocks divergence) seems right.
12. **Dimension transition as reconnect** — Should dimension transitions reuse the same connection (flush and reload state) or effectively disconnect/reconnect with a new session ID? Reusing is faster but more complex. Reconnecting is simpler but adds latency.
13. **Interest management granularity** — Per-entity subscription (client subscribes to specific entities) vs. area-based (client subscribes to chunk regions)? Area-based is simpler and sufficient for most cases, but boss fights and party members may need explicit entity subscriptions regardless of distance.
14. **Crafting minigame latency** — Smithing and knapping require responsive input (player clicks grid positions). Should the client predict the result of each action and roll back on server disagreement? Or should it wait for server confirmation? At 50ms RTT, waiting feels sluggish. Client prediction with rollback is better but more complex.
15. **Vehicle coordinate spaces** — Airship block changes use vehicle-local coordinates. How does this interact with chunk storage? Are airship blocks stored in a separate "vehicle chunk" structure? Or in the world chunks at their current position (requiring chunk updates as the vehicle moves)?
16. **Script bundle versioning** — When server-side scripts change (mod update, admin change), how do connected clients receive the update? Hot-reload mid-session or require reconnect?
17. **World channel overload** — The World channel now carries terrain, fluids, signals, machines, weather, and more. Should it be split into sub-channels (World.Terrain, World.Fluid, World.Automation, World.Environment) for independent flow control? Or keep it unified and rely on per-message priority overrides?

---

*This spec is designed to be implemented as a standalone library (`finenet`) that can be developed and tested independently of the game engine.*
