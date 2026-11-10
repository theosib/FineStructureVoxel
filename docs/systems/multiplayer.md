# System: Multiplayer

**Libraries:** `finenet` (transport, discovery), `finevox_net` (protocol, client session),
`finevox_game` (host), `src/client/game_client.cpp` (window, built into `finevox_run`)
**Wire format:** [../protocol/PROTOCOL.md](../protocol/PROTOCOL.md) is the normative spec. This file
is how the engine implements it and why.

---

## The Rule

The client speaks only the protocol. It holds no reference to the server's world, entities, vision
or scripts, even when both run in one process. There is one client code path, and "single player"
is a host with no listener and a client on `Connection::createLocalPair()`.

Everything else follows from that:

| Want | How it falls out |
|---|---|
| Open a running game to the LAN | Start a listener and a beacon. The local client doesn't notice. |
| Server-only | The same host with no local client. |
| Standalone client or server | Link less: a client needs `finevox_net`, not `finevox_game`. |
| Generic client | Don't ship a game root. The content phase fetches everything. |
| Web client | Implement the spec. Nothing in it is C++-shaped. |
| Hub | A hub is a source of listings. It hands out the same address a LAN browse does. |

---

## Shape

```
 finevox_run / finevox_headless
 ┌───────────────────────────────────────┐        ┌────────────────────────┐
 │ GameHost            host thread       │        │ ClientSession          │
 │  listener, beacon, lobby, chat,       │◄──────►│  sync thread           │
 │  content server, players              │ local  │  ClientWorld           │
 │                                       │  or    │  (blocks, light, fluid │
 │ ReplicationServer   game thread       │  TCP   │   entities, fog, time) │
 │  per-client views of per-faction      │        │                        │
 │  WorldReplicas                        │        │ GameClient  render     │
 │                                       │        │  shell → world         │
 │ GameRuntime         game thread       │        └────────────────────────┘
 └───────────────────────────────────────┘
```

**Control plane on the host thread.** Connections, lobby, chat, pings and content run there, so a
long tick doesn't stall them and they work before a world exists. It hands commands to the game
through `GameActions`, which is a thread-safe queue.

**Data plane on the game thread.** `ReplicationServer` is a `GameSubsystem` at `LateTick`, so it
reads the world with nobody writing. `ui_publish` lands here too. Before the game thread starts,
the host thread pumps it instead, which is how the first sync happens with the clock stopped.

**Client threads.** The sync thread applies messages to `ClientWorld`, as the game thread used to
write the replica: world storage is "writers lock, readers don't", so mesh workers are unaffected.
The render thread reads, and drains queues of what needs script: UI messages, chat, lobby state.

---

## Fog of War

A client must never receive what its faction can't see. `WorldReplica` already gives that per
faction: a second `World` holding only what was seen, stale where nothing's looking. That stays on
the server, one per faction with a player, and it's what a client view syncs from. So a late joiner
or a reconnecting player gets the faction's memory, not the true world, and saves keep working.

Each `ClientView` sends subchunks from its faction's replica, entities its faction can see now, its
faction's vision cells, and only its faction's ghosts. Tests assert on the bytes the client holds,
not on what it draws.

---

## Replication Is State, Not a Stream

Over TCP or WebSocket a slow client can't drop anything, so a naive sender queues stale positions
behind stale positions. So nothing that's latest-state is ever queued:

- A view holds a **dirty set**. A subchunk changing twice before it's sent goes once.
- A view sends only while its connection's backlog is under a limit, nearest the player first.
- Entity updates are built when there's room for them, from current state. A frame with no room
  is skipped, not delayed.

Which means the design doesn't need an unreliable channel. UDP would still help with packet loss on
the internet, and can come later behind `finenet`'s transport seam with no protocol change.

Messages that can't be recomputed (commands, chat, UI, entity spawn and despawn) are reliable and
ordered. Ordering holds only within a channel, so anything that depends on something else travels
on the same channel: a dictionary entry always precedes its first use on that channel.

---

## Lobby

`GameHost` phases: `Lobby → Loading → Running → Ended`.

The game declares what it wants in `lobby.fsc`, a finescript file that returns a map (see
[Declaring a Game](#declaring-a-game)). The engine knows the mechanism: players, ready flags,
settings with a schema, who may launch. The game supplies the policy and all of the UI.

**Settings.** The schema goes to clients in `Welcome`, so a client that has never seen the game can
draw the form. The host validates every change against it. A server's config file can set values
and lock them.

**Player settings.** `player_settings` is a second schema, for what each player chooses for
themselves, such as a team. A player sets their own with `lobby_set key value =player id`, and the
host can set anyone's. Each player's values are in their `settings` in the lobby state, and in
`PLAYERS` when the game starts.

**Launching.** `launch` picks who can start it: `host` (the first player, or anyone listed in the
server's `admins`), `vote` (everyone connected must press it), or `auto` (as soon as `min_players`
are ready). `start: immediate` has no lobby at all: the world exists from boot and players walk in.

**Simultaneous start.** On launch the host generates the world, sends `GameStarting`, and sends
each client its first sync. The game thread doesn't start until every client has said `Loaded`, or
`load_timeout` runs out and the stragglers are dropped. Then `GameStart` goes to everyone at once.

**Reconnecting.** `Welcome` hands out a secret token. A player who drops stays in the roster as
disconnected. Presenting the token gets their seat back, faction and all, for as long as the game
lasts. `late_join` decides about strangers: `never`, or `open` for a sandbox.

---

## Players and Scripts

A player is a seat: id, name, token, faction. Game scripts bind seats to factions; the engine has
no opinion on how.

```
on :start               PLAYERS: [{=id =name =local =settings}...]   SETTINGS: {=key value}
on :player_join         player: {=id =name}
on :player_leave        player
on :command             cmd, and sender: the player's id
player_bind id faction [=at [x y z]]   this player sees and acts as this faction, and their body
                        starts here. A body sees for its faction, so where it stands matters.
                        Replicas appear on demand.
player_faction id -> faction
ui_publish value [=to player_id] [=faction f]    nothing named means everyone
site_show_ghosts [ids] [=faction f]
```

The scripts are authoritative. `sender` is set by the host from the connection, so a client can't
act as someone else, but what a command may touch is the game's to check. Keystone checks a unit's
faction before ordering it.

The player's body belongs to the seat, spawned on binding at the faction's home, moved by the
client (it's the client's physics) and reported in `PlayerState`.

---

## Content

The client's starting point is its own game root: menus and skin come from there, so the first
screen needs no server. On joining, the host sends a manifest of the client-side content, by path,
size and SHA-256. The client resolves each file against, in order, its own game root and its
content cache (`~/.cache/finevox/content/<sha256>`), and asks for what's left.

If nothing was missing, it runs from its own game root. If anything was, it builds a session
directory of links into the cache and runs from that. Either way, by the time it loads, it has
exactly the server's files.

The game names what's client-side with `client_content` in `game.conf`. Anything else stays on the
server: `scripts/` holds the rules, and a client has no use for them.

A file from a server may be a UI script, which is code. It runs in the UI engine, where natives
resolve paths inside the content root and no native can reach beyond it. See
[Trust](#trust).

---

## Discovery

`finenet::DiscoveryBeacon` and `DiscoveryBrowser`. "Find it on the LAN" fails in a lot of quiet
ways, so several independent mechanisms run together and any one of them is enough:

| Failure | Answer |
|---|---|
| A multicast-filtering switch or AP | Directed broadcast on each interface as well |
| Broadcast leaves by only one interface | Sent per interface, from a socket bound to it |
| VPN or Docker bridge is the default route | Every interface tried, every cycle, none assumed |
| Wi-Fi joined after the browser opened | Interfaces are re-read each cycle |
| Both beacon and browser miss a beat | Each side both announces and queries |
| Lost datagrams | Fast at first (0, 0.25, 1, 2, 4s), then steady |
| Two servers on one machine | `SO_REUSEPORT`, and replies are unicast to an ephemeral port |
| Everything multicast and broadcast blocked | With nothing found after 3s, a slow unicast sweep of the local /24s |
| Known server, flaky network | Remembered servers are probed directly |
| Server has several addresses | Each one seen is kept, and connecting tries them all |
| macOS Local Network permission denied | `EHOSTUNREACH` from every send is reported as `blocked`, so the UI can say so |

The address of a found server is the source of the datagram that arrived, never what the datagram
claims, because that's the one known to route.

A listing is an opaque CBOR map as far as `finenet` knows. The engine's listing is the same record
a hub will serve: title, game, players, phase, settings summary. The browser UI draws listings; it
doesn't care where they came from.

---

## Addresses

An address is a URL: `finevox://host:port`. The scheme is optional, as is the port, so `192.168.1.5`,
`host:7810`, `[fe80::1]:7810` all parse. Reserved for later: `wss://` for WebSocket, `hub://hub/game`
for a hub-brokered game. The UI passes strings through and doesn't parse them.

---

## Declaring a Game

`game.conf`:

| Key | Default | Meaning |
|---|---|---|
| `menu_script` | none | Client's first screen. With none, boot straight into a single-player game |
| `lobby_script` | none | Returns the lobby declaration. With none, a default lobby with no settings |
| `client_content` | see content-roots | Directories and files a client needs |
| `net_port` | `7810` | Game port |

`lobby.fsc` returns:

```
{=start :lobby          # or :immediate
 =launch :host          # or :vote, :auto
 =late_join :never      # or :open
 =min_players 1
 =max_players 8
 =settings [
    {=key :map =type :choice =label "lobby.map" =default :woodland
     =choices [{=value :woodland =label "lobby.map.woodland"}...]}
    {=key :rivals =type :int =label "lobby.rivals" =default 1 =min 0 =max 7}
    {=key :title =type :text =label "lobby.title" =default ""}
    {=key :fog =type :bool =label "lobby.fog" =default true}
 ]}
```

A setting with `=manifest "world_radius"` overrides that `game.conf` key before the world is made.
A choice's value can carry its own `=manifest {=world_biomes "..."}`, so a map type can change
generation without any engine knowledge of map types.

`server.conf`, for `--server --config FILE`:

```
title: Friday night
port: 7810
password: swordfish
max_players: 4
admins: Alice Bob
setting.map: highland
setting.rivals: 2
lock: map rivals         # Clients can't change these
```

---

## Running It

```
finevox_run --game-root DIR                       menu_script, or straight into a game
finevox_run --game-root DIR --server [--config F] no window, no GPU, serves until killed
finevox_headless --game-root DIR --serve          the same, in the GPU-free binary
finevox_run --game-root DIR --join ADDR [--name N]
finevox_run --game-root DIR --new | --continue    skip the menu, single player
```

---

## Trust

There's no encryption yet, so a password crosses the wire in the clear, and anyone on the path can
read a game. That's fine on a LAN and not fine on the internet: it goes before the hub does, and
the handshake already carries capabilities to negotiate it.

A malicious server can run UI script on a client that accepts its content. The UI engine's natives
must stay confined to the content root. A malicious client can send any command: the host bounds
sizes and rates, and game scripts check ownership. The avatar's position is client-authoritative.

---

## Testing

`finevox_bot` is the client session with no window: it joins, plays by script, and asserts. It links
`finevox_net` and nothing of the game, so it's also the first standalone client. Assert on what the
client holds, including what it must not hold.

| Run | Covers |
|---|---|
| `finevox_tests`, `finevox_script_tests` | Codecs, fuzzed input, prediction, content safety, what each view is sent |
| Keystone `tests/run_net.sh` | A server and bots over real sockets, and through a proxy that cuts, delays and throttles |
| Keystone `tests/run_windows.sh` | Real windows driven over MCP: typing, opening a game live, saving |
| `../fv-tsan` | The above again. It should report nothing |

Things that took time to learn:

- Nothing is ordered between channels, and that includes tests. A UI that waits for a chat line and
  a lobby state has to look at both, whichever came last.
- A cut on a timer is a race on a slow build. Cut when something has happened instead.
- A body sees for its faction. Two bodies standing at the origin saw each other through the fog.
- `EntityState`'s copies were written by hand, so a new field needs adding in two more places.
- `auto_focus` in `MapRenderer` has to call `SetKeyboardFocusHere` before the item. Without it a
  text field waits for a click.
- `on_change` on `ui.input` fires when the field loses focus, not per key.

---

## Not Done Yet

- **UDP.** TCP only, so packet loss on the internet stalls all channels for a moment.
- **WebSocket, hub, generic client.** The seams are there: `Connection` over a transport, listings
  from any source, content by hash. The windowed client is compiled into `finevox_run` and still
  needs `finevox_game` for `mcp_tools`, so a lighter one starts by pulling those apart. A generic
  client with a window hasn't been tried: only `finevox_bot` has joined with no game of its own.
- **Encryption.** See [Trust](#trust).
- **A server's second game.** A server plays one match, then needs restarting.
- **Reconnecting from a lobby.** A dropped connection there just loses the seat. It's only held once
  the game's begun.
- **Team chat by default.** The protocol has it and nothing in Keystone's UI asks for it.
- **Block rotation and particles from the server.** The wire has room for both; nothing uses them.
- **Deltas.** One changed block resends its subchunk, about a kilobyte. That's fine for what
  Keystone does and wouldn't be for a game that changes thousands a second.
- **Late tokens.** A reconnect token lives in memory, so a client that crashes or restarts has lost
  its seat, even though the server is holding it.
- **Resuming by name.** A resumed game gives seats back by name, which is anyone's to claim, and it
  has only ever been run for the local player: a resumed game with remote players is untested.
- **Content changing under a server.** The manifest is built once, for the first client to join.
- **Limits are silent.** Commands past the rate limit are dropped, with nothing said to the client.
- **A body can be anywhere.** Its position is the client's word, checked only for being finite.

## Not Tested

- **Windows.** See Keystone's `docs/PLATFORMS.md`, which has what's been run on two machines and Linux.
- **Hours of play, and long games.** Runs so far are about a minute. Nothing has measured a client
  that's been connected for an hour, or a game with hundreds of units per client.
- **Eight sides.** Up to four, and the large map not at all with players on it.
- **The `vote` and `auto` launch policies, `lobby_kick`, and `immediate` start.** Keystone uses `host`
  and has no button for kicking, so no client has exercised these.
