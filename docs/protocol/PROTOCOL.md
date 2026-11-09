# FineVox Network Protocol

**Protocol version: 3.** This is the normative description of what a client and a server say to
each other. A client written from this document, in any language, should be able to join a game
without reading the engine. Where the engine and this document disagree, one of them has a bug.

The key words MUST, SHOULD and MAY are used as in RFC 2119.

---

## 1. Layers

| Layer | Defines |
|---|---|
| Transport (§2) | A connection carrying numbered channels of whole messages |
| Encoding (§3) | How a message's body is written |
| Session (§4) | Hello, lobby, content, start |
| World (§5–§7) | Blocks, entities, everything else |
| Content (§8) | The files a client needs and what's in them |

A connection is reliable and ordered per channel. Nothing is ordered between channels.

---

## 2. Transport

### 2.1 TCP

A connection opens with an 8-byte preamble, sent by each side at once without waiting:

```
"FNET"  u8 version=1  u8[3] reserved=0
```

A peer that reads anything else MUST close. Frames follow:

```
u32le length      bytes following this field, at least 2, at most 65536
u8    channel     0-254 application, 255 transport
u8    flags       bit 0 FIN: the last frame of this message
u8[]  data
```

A message's bytes are split across one or more frames of its channel, with FIN on the last. Frames
of different channels MAY interleave. Frames of one channel MUST NOT carry parts of two messages at
once. A sender SHOULD split messages at 16 KiB, so that a big one doesn't hold up a small one.

A message's bytes are its **type** then its **body**. The type is prefix-coded:

| First byte | Bytes | Range |
|---|---|---|
| `0xxxxxxx` | 1 | 0 to 0x7F |
| `10xxxxxx` | 2 | to 0x3FFF |
| `110xxxxx` | 3 | to 0x1FFFFF |
| `111xxxxx` | 4 | above |

Remaining bits are big-endian.

### 2.2 Transport channel

Channel 255 frames are single frames with FIN set. The first data byte says what they are:

| Byte | Name | Rest |
|---|---|---|
| 1 | Ping | `u64le` opaque |
| 2 | Pong | The same `u64le` |
| 3 | Close | UTF-8 reason, to the end of the frame |

A peer MUST answer a Ping. A peer SHOULD send one when it has sent nothing for 1 second, and SHOULD
consider the connection dead after 10 seconds of hearing nothing.

### 2.3 WebSocket

Reserved. One binary WebSocket message carries one frame from `channel` onward, with no length
field and no preamble.

### 2.4 Channels

| Id | Name | Carries |
|---|---|---|
| 0 | Control | Hello, phase changes, and anything else that sets the stage |
| 1 | Input | `PlayerState` |
| 2 | Actions | `Command`, `ExpectChange` |
| 3 | Entities | Everything about entities, and `EntityTypeName` |
| 5 | World | Blocks, fog, time, and block names |
| 6 | UI | `UiMessage`, lobby state |
| 7 | Assets | Content transfer |
| 8 | Chat | `Chat` |

A sender SHOULD favour lower-numbered channels when the link is busy, with Assets last.

---

## 3. Encoding

Every message body is one **CBOR** map (RFC 8949). Unknown message types MUST be ignored, and a
decoder MUST ignore keys it doesn't know.

**Control messages** (session, lobby, content lists) have text keys, in this document written
`{key: type}`. An optional key is marked `?`. A finescript symbol is a text string under CBOR tag 39
(`:name` means the tagged string `name`). Decoders that have no symbols MAY read it as a string.

**World messages** (§5 to §8, the ones with a table of keys) have small unsigned integer keys,
which cost a byte each, as this document lists them per message: `0 id: uint` is key 0, holding an
unsigned integer. Types are CBOR's:

| Written | CBOR |
|---|---|
| `uint`, `int` | Major type 0, or 0 and 1 |
| `float` | Any number: a reader MUST take a half, single or double float, or an integer |
| `bool` | Simple values 20 and 21 |
| `text`, `bytes` | Major types 3 and 2 |
| `[T]` | An array of T |

A key marked `?` has a default, given beside it, and a sender SHOULD leave it out when that's its
value. A key that isn't marked is required, and a message or array entry without one MUST be
dropped. Within a message, a reader MAY drop an array entry that's malformed and keep the rest.
Indefinite lengths aren't used.

A reader is reading bytes off the network, so it MUST check every length against what's left, MAY
refuse nesting deeper than 16, and MUST NOT allocate for a count that the bytes left can't hold.

---

## 4. Session

```
Client                                      Server
  │── Hello ───────────────────────────────►│
  │◄──────────────────── Welcome or Reject ─│
  │◄───────────────────── ContentManifest ──│   Unless Welcome says there's no need
  │── ContentRequest ──────────────────────►│
  │◄──────────────────────── ContentChunk ──│
  │── ContentReady ────────────────────────►│
  │◄───────────────────────── LobbyState ───│   Phase: lobby. Repeats on any change.
  │── SetSetting, SetReady, Launch, Chat ──►│
  │◄──────────────────────── GameStarting ──│   Phase: loading
  │◄──────────────── World data, SyncDone ──│
  │── Loaded ──────────────────────────────►│
  │◄─────────────────────────── GameStart ──│   Phase: running. Everyone at once.
  │◄────────────────────────► play ◄───────►│
```

Content comes first so that a client which has never seen this game can draw its lobby. A client
joining a running game gets `Welcome` with `phase: running`, content as above, then the same from
`GameStarting` on, and its `GameStart` when it's ready.

A player who loses their connection keeps their seat. Sending `Hello` with the `token` from `Welcome`
takes it back, whatever phase the game is in, and everything from `GameStarting` on is sent again.
A client MUST forget every entity it knows of before that happens.

All of these are on channel 0 unless the table says otherwise.

| Type | Name | Direction | Body |
|---|---|---|---|
| 0x4001 | Hello | C→S | `{protocol: int, revision: int, game: text, name: text, token?: text, password?: text, caps?: [text]}` |
| 0x4002 | Welcome | S→C | `{player: int, revision: int, token: text, game: text, title: text, phase: :lobby/:loading/:running/:ended, lobby: Lobby, host: bool, content: bool}`. `content` false means no manifest is coming. |
| 0x4003 | Reject | S→C | `{reason: :protocol/:game/:password/:full/:started/:banned/:other, text?: text}` |
| 0x4004 | SetSetting | C→S | `{key: text, value: any}` |
| 0x4005 | SetReady | C→S | `{ready: bool}` |
| 0x4006 | Launch | C→S | `{launch: bool}`. With `vote`, `false` takes a vote back. |
| 0x4007 | LobbyState | S→C, ch 6 | `{phase, title: text, players: [Player], settings: {text: any}, locked: [text], votes: int, can_launch: bool, open: bool, port: int}`. `open` says it's on the network. |
| 0x4008 | Chat | both, ch 8 | C→S `{text: text, to?: :team}`. S→C adds `from: int, name: text`; `from` 0 is the server. A `:team` line goes only to the sender's faction. |
| 0x4009 | Kick | C→S | `{player: int}`, from a player the server treats as host |
| 0x400A | Leave | C→S | `{}`: going for good, so don't hold the seat |
| 0x4010 | GameStarting | S→C | `{world: WorldInfo}` |
| 0x4011 | Loaded | C→S | `{}` |
| 0x4012 | GameStart | S→C | `{tick: int}` |
| 0x4013 | GameEnded | S→C | `{text?: text}`. The server is closing the game. |
| 0x4014 | SyncDone | S→C, ch 5 | `{}`: everything the client needs for a first frame has been sent, on channel 5 |
| 0x4015 | Bound | S→C, ch 5 | `{faction: int, body?: int, spawn?: [f64,f64,f64]}`: who this player is, in the world |

```
Player    = {id: int, name: text, connected: bool, ready: bool, voted: bool, host: bool, local: bool,
             content: bool, faction?: int, ping?: int}
Lobby     = {start: :lobby/:immediate, launch: :host/:vote/:auto, late_join: :never/:open,
             min_players: int, max_players: int, settings: [Setting]}
Setting   = {key: text, type: :choice/:int/:bool/:text, label: text, default: any,
             choices?: [{value: any, label: text}], min?: int, max?: int}
WorldInfo = {radius: int, sea_level: int, tick_rate: int, entity_rate: int, fog: bool,
             view_distance: f64, lod: bool, step_height: f64, player_entity: text, vision_cell: int,
             vision_min: [int,int], vision_cells: [int,int], resumed: bool, client_state?: any}
```

`label`s are localization keys, resolved with the game's `lang/` files. `entity_rate` is how often
a second `EntityBatch` can be expected, for interpolating between them. `client_state` is what the
client last sent in `ClientState`, if it's resuming a saved game.

---

## 5. World

### 5.1 Names

Block and fluid types have string names, and the wire uses small numbers that stand for them. The
numbers are the server's and mean nothing else. A server MUST send a name before the first use of
its number on the same channel.

| Type | Name | Channel | Body |
|---|---|---|---|
| 0x20 | BlockName | 5 | `0 id: uint`, `1 name: text` |
| 0x21 | FluidName | 5 | The same |
| 0x22 | EntityTypeName | 3 | The same |

Id 0 is always air, and the empty fluid or type.

### 5.2 Subchunks

| Type | Name | Channel |
|---|---|---|
| 0x23 | Subchunk | 5 |

The world is cells of 16×16×16 blocks. A cell's index is `x + z*16 + y*256`. This message replaces
everything the client holds for that subchunk, and describes it whole.

```
0 cx: int, 1 cy: int, 2 cz: int   Subchunk coordinates: block coordinates >> 4
3 cells: bytes                    The Cells map below, as CBOR, LZ4 block-compressed if raw_size is there
4 raw_size?: uint                 How long cells is once decompressed. Absent: it isn't compressed.

Cells:
0 palette: [uint]                 BlockName ids. At least one.
1 index_width?: uint = 1          1 or 2 bytes an index
2 indices?: bytes                 4096 indices into the palette, little-endian. Required with
                                  more than one palette entry.
3 light?: bytes                   4096 bytes, each sky << 4 | block, 0 to 15 each
4 light_all?: uint                Or one value for every cell. One of the two is required.
5 fluids?: [uint]                 Three to a cell: index, FluidName id, level
6 rotations?: bytes               Reserved: 4096, one a cell
```

A client MUST refuse a subchunk whose indices aren't all inside its palette, or whose cells come to
more than 64 KiB.

A client MUST ignore a server's change to a cell it's predicting (§7.3) until that prediction ends.
A subchunk a client hasn't been sent is unknown, not empty. A client SHOULD treat its light as full
sky, so that terrain at the edge of what's known isn't drawn black.

### 5.3 Fog, time, sites

| Type | Name | Channel | Body |
|---|---|---|---|
| 0x24 | Vision | 5 | `0 cells: bytes`, LZ4-compressed: one byte per vision cell, row by row, 0 unknown, 1 explored, 2 visible. `1 raw_size: uint`, its size undone. |
| 0x25 | Time | 5 | `0 ticks: uint`, `1 held?: bool = false` the clock is held, `2 paused?: bool = false` the game is paused. Sent about once a second, and at once when a pause starts or ends. |
| 0x26 | Ghosts | 5 | `0 groups: [Group]`. Group: `0 built?: float = 0`, `1 cells?: [int] = []`, four to a cell: x, y, z, BlockName id |

Vision cells are `vision_cell` blocks square. `vision_min` is the corner of cell 0.

While paused nothing changes on the server, and no updates mean nothing moves. A client MUST also
stop what it runs by itself: animation, effects, and a body whose physics it owns. It stays free to
move its camera, and a server still carries out commands.

---

## 6. Entities

All on channel 3, in the order the server made them.

| Type | Name | Body |
|---|---|---|
| 0x30 | EntitySpawn | `0 id: uint`, `1 type: uint` (EntityTypeName id), `2 faction?: uint = 0`, `3 x, 4 y, 5 z: float` (double, for precision far out), `6 yaw?, 7 pitch?: float = 0`, `8 health?: uint = 255` |
| 0x31 | EntityGone | `0 id: uint`, `1 died?: bool = false` |
| 0x32 | EntityBatch | `0 tick: uint`, `1 entities: [Entity]` |
| 0x33 | Projectiles | `0 shots: [Shot]`. Shot: `0 x, 1 y, 2 z: float`, `3 vx?, 4 vy?, 5 vz?: float = 0` |
| 0x34 | Burst | Reserved: `0 emitter: text`, `1 x, 2 y, 3 z: float`, `4 color: [float]` |

```
Entity:
0 id: uint
1 x, 2 y, 3 z: float            Position
4 vx?, 5 vy?, 6 vz?: float = 0   Velocity, for extrapolation
7 yaw?, 8 pitch?: float = 0      As the server's entity holds them
9 animation?: uint = 0          Slot, as the entity's `.entity` file lists them
10 on_ground?: bool = false
11 health?: uint = 255          In 255ths of its most. 255 only when nothing's missing, so a scratch shows
```

A change of health alone is enough to send an entity again. Anything that isn't a mob reads 255.

An entity the client has been told about stays exactly where it is until told otherwise. Silence
means it hasn't moved, and an entity that leaves the player's sight is `EntityGone` with `died` 0. A
`died` of 1 is only sent for a death the player could see.

---

## 7. Playing

| Type | Name | Channel | Direction | Body |
|---|---|---|---|---|
| 0x40 | PlayerState | 1 | C→S | `0 x, 1 y, 2 z: float`, `3 vx?, 4 vy?, 5 vz?, 6 yaw?, 7 pitch?: float = 0`, `8 on_ground?: bool = false`. A server MUST drop one whose position isn't finite. |
| 0x4020 | Command | 2 | C→S | CBOR. Any value: the game defines it. |
| 0x41 | ExpectChange | 2 | C→S | `0 x, 1 y, 2 z: int`: look at this column soon |
| 0x42 | ViewHint | 1 | C→S | `0 x, 1 z: float`: send what's near here first |
| 0x4021 | UiMessage | 6 | S→C | CBOR. Any value: the game defines it. |
| 0x4022 | ClientState | 2 | C→S | CBOR. Any value: handed back in `WorldInfo` on resuming. Only a server's own local client's is kept. |

### 7.1 Commands

A Command's value is opaque to the engine. A server MUST attribute it to the connection it arrived
on, and MUST NOT take any claim of identity from inside it.

### 7.2 Limits

A server MAY close a connection that exceeds these. A client MUST stay inside them.

| Limit | Value |
|---|---|
| A Command's body | 64 KiB |
| Commands | 120 a second |
| A Chat's text | 512 bytes |
| Chats | 5 a second |
| A name | 32 bytes |

### 7.3 Prediction

A client MAY show its own action at once. Sending `ExpectChange` asks the server to look at that
column soon, instead of waiting for its sweep to come round. A client that predicts SHOULD hold a
prediction for at least a round trip, and then take the server's word.

---

## 8. Content

| Type | Name | Channel | Direction | Body |
|---|---|---|---|---|
| 0x4030 | ContentManifest | 7 | S→C | `{files: [{path: text, size: int, sha256: text}]}`, hashes in lower-case hex |
| 0x4031 | ContentRequest | 7 | C→S | `{files: [text]}`, by hash. Send it only if something's missing. |
| 0x4032 | ContentChunk | 7 | S→C | `0 sha256: bytes` (32 raw bytes), `1 offset: uint`, `2 total: uint`, `3 data: bytes` |
| 0x4033 | ContentReady | 7 | C→S | `{}`: every file on the list is here. Sent even if none were requested. |

Paths are relative, separated by `/`, and a client MUST refuse any with an empty, `.` or `..`
component, or a leading `/`. A client MUST check each file's hash, and MUST NOT use one that fails.
A `ContentChunk` is at most 256 KiB, and chunks of one file arrive in order.

A client MAY keep what it fetched, keyed by hash, for next time.

The files are a game directory, and their formats are what a client must implement:

| Path | Format |
|---|---|
| `game.conf` | Key-value |
| `blocks/*.model`, `shapes/*` | Block appearance and collision |
| `entities/*.entity`, `models/*` | Entity definitions, models, skeletons, animations |
| `textures/*` | PNG |
| `lang/*.lang` | Localized strings |
| `input/*.bindings` | Default input bindings |
| `ui/*` | finescript UI, skins, images |
| `particles/*`, `emitters/*`, `sky.conf` | Effects |

Formats are described in the engine's `docs/systems/`. UI scripts are finescript, so a generic client
needs a finescript interpreter and the UI natives in `game-runtime.md`.

---

## 9. Discovery

UDP, port 7811. A datagram:

```
"FNDS"    u8 version=1    u8 kind
str app                   Game namespace; empty in a Query asks for any game
u64le instance            Random, fixed for the life of a server
u16le gamePort
bytes listing             CBOR
```

| Kind | Name | Sent |
|---|---|---|
| 0 | Query | By a client looking for servers |
| 1 | Announce | By a server, in answer to a Query (unicast) or every 2 seconds (group) |
| 2 | Bye | By a server closing |

Group sends go to IPv4 multicast `239.255.70.86`, to each interface's directed broadcast, to
`255.255.255.255`, and to IPv6 `ff02::4656:5854`, all on port 7811. A client takes the address to
connect to from the datagram's source, because that's the one known to route. A client SHOULD also
query hosts it remembers, directly, and MAY query its local /24 one host at a time if it has found
nothing after a few seconds.

```
Listing = {title: text, game: text, game_name: text, players: int, max_players: int,
           phase: :lobby/:loading/:running/:ended, joinable: bool, password: bool,
           settings?: {text: any}}
```

---

## 10. Versioning

`protocol` in `Hello` is this document's version, and a server rejects any other. `revision`, in
`Hello` and `Welcome` both, counts the changes since that a peer can safely not know about, so the
two can differ, and each side knows which the other has. `caps` in `Hello` lists optional features
the client has.

The engine's code is `PROTOCOL_VERSION` and `PROTOCOL_REVISION` in `net/protocol.hpp`.

### Changing the protocol

While nothing's been released, any change can just bump `protocol`. After a release, a patch to it
should still talk to it, so prefer a change that only bumps `revision`:

- **A key**: add one to any map, at the next unused number, with a default that means what an older
  peer meant by leaving it out (health: unhurt). Readers ignore keys they don't know. A key can also
  stop being sent, if a reader's default for it is right. Never reuse a key's number for something
  else, or change what it holds, and never make an optional key required.
- **A message type**: add one freely, since unknown types are ignored. If the sender has to know
  the other side will act on it, look at the peer's `revision` first, or at `caps`.
- In the engine, every world message is written and read in `net/messages.cpp` (Subchunk in
  `net/subchunk_codec.cpp`), on the CBOR of `net/wire.hpp`, and `tests/test_wire.cpp` checks
  messages from an older sender and a newer one, built with a different encoder from the one under
  test.
- **A new revision** each time one of those reaches a release, with a line in the table below.

Bump `protocol` for anything an older peer would get wrong without knowing: changing what a key or
a message type means, its units, or a required key. Inside a `bytes` field (indices, light, vision
cells, file data) nothing is self-describing, so changing a layout there means a new key for it.

A game's own data needs none of this. Commands and `ui_publish` are maps, and the client runs the
server's own UI scripts and content (§8), so a game's scripts and the UI that reads them always
match. Saved games keep their own format: see `docs/systems/persistence.md`.

| Protocol | Revision | Added |
|---|---|---|
| 3 | 0 | Every message a CBOR map; world messages with integer keys and defaults left out |
