# FineStructureVoxel

A voxel game engine in C++20, on Vulkan, for macOS, Linux and Windows. It aims at the kind of game
that's hard to build on a general-purpose engine: a fully mutable world, simulated with authority on
a server, where **a new game is a directory of content and scripts, with no C++**.

[Keystone](https://github.com/theosib/Keystone-RTS), an RTS, is the game that drives it. It's
written entirely in data and script, and when it needs a mechanism, that mechanism goes in the
engine in a form that knows nothing about Keystone.

## A game is a directory

```
mygame/
  game.conf       Name, scripts, world settings
  blocks/         Blocks, with shapes and textures
  entities/       Units and creatures
  biomes/         What the world looks like
  scripts/        The rules: runs on the server
  ui/             The interface: runs on the client
```

```sh
finevox_run --game-root mygame            # Play
finevox_run --game-root mygame --server   # Host, with no window
finevox_headless --game-root mygame       # Run with no GPU at all, for tests
```

Packaged, the same program finds its game beside itself and keeps saves in a per-user folder. See
[game-runtime.md](docs/systems/game-runtime.md).

## What makes it interesting

- **There's only one way to play, and it's multiplayer.** A client speaks only the protocol, and
  holds no reference to the server's world, even in the same process. Single player is a server
  with nobody else on it, so there's no second code path to go wrong, and any game can open itself
  to the network while it's running.
  See [multiplayer.md](docs/systems/multiplayer.md).
- **Fog of war in the protocol.** Vision is true 3D line of sight per faction, and the server keeps
  a replica of the world as each faction remembers it. Clients only ever receive that, so what a
  player can't see isn't on their machine to be revealed. Tests assert on the bytes the client holds,
  not on what it draws.
- **Replication is state, not a stream.** Changes go in a dirty set and are sent nearest first, when
  there's room, from current state. A slow client gets fewer updates, not older ones.
- **Clients get their content from the server.** On joining, a client compares a manifest of hashes
  with its own files and cache, and downloads only what it's missing.
- **Two script worlds that share nothing.** Game logic runs on the game thread, and UI on the render
  thread in a separate engine. They talk through commands and published state, which is the same
  boundary the network is. See [scripting.md](docs/systems/scripting.md).
- **A running game can be driven by an AI.** With `--mcp`, a game serves Model Context Protocol, with
  tools for what the server knows, tools for what the player can see, fog included, and evaluation
  of script on either side. `--mcp-player` limits it to what's fair for an LLM to use as a player.

## What's in it

**World.** Chunk columns of subchunks with palette storage, custom block shapes, region files with a
journal-style table of contents, and CBOR throughout.
[world.md](docs/systems/world.md), [persistence.md](docs/systems/persistence.md)

**Rendering.** Greedy meshing on worker threads with push-based rebuilds, LOD levels 0 to 4, fog,
smooth lighting and ambient occlusion, translucent fluids, skeletal animation, particles, and
golden-image tests that hold across different GPUs.
[rendering.md](docs/systems/rendering.md), [lighting.md](docs/systems/lighting.md),
[particles.md](docs/systems/particles.md)

**Simulation.** A 30-tick game thread, sky and block light that propagate, flowing fluids, AABB
physics with step climbing, raycasting, and scheduled block updates.
[game-session.md](docs/systems/game-session.md), [fluids.md](docs/systems/fluids.md),
[physics.md](docs/systems/physics.md), [events.md](docs/systems/events.md)

**Entities.** AI brains built from goals, pathfinding and navigation, senses, skeletons and
animation, and script hooks. [entities.md](docs/systems/entities.md)

**World generation.** Noise, biomes from data files, and a pipeline of passes and placed features.
[worldgen.md](docs/systems/worldgen.md)

**Scripting and UI.** [finescript](https://github.com/theosib/FineStructureScript) with proxies onto
blocks and entities, and a UI where [finegui](https://github.com/theosib/finegui) widgets are live
script data. [scripting.md](docs/systems/scripting.md), [ui.md](docs/systems/ui.md)

**Audio.** miniaudio with 3D spatialization and footsteps. [audio.md](docs/systems/audio.md)

**Input.** Bindings are data, with contexts, and scripts can inject real input for testing.

## How it's put together

```
┌──────────────────────────────────────────────────────────────┐
│  A game: content, scripts, config                            │
├──────────────────────────────────────────────────────────────┤
│  finevox_run  finevox_headless  finevox_bot                  │
├──────────────────────────────────────────────────────────────┤
│  finevox_game      Runtime, host, saves, MCP server          │
│  finevox_net       Protocol, client session                  │
│  finevox_script    finescript integration                    │
│  finevox_render    Meshes, LOD, fluids, particles            │
│  finevox_worldgen  Noise, biomes, generation                 │
│  finevox_audio     Sound                                     │
│  finevox           World, physics, entities, persistence     │
├──────────────────────────────────────────────────────────────┤
│  finegui  finescript  finenet  FineStructureVK               │
├──────────────────────────────────────────────────────────────┤
│  Vulkan  GLFW  GLM                                           │
└──────────────────────────────────────────────────────────────┘
```

The core has no GPU dependency, so servers, bots and most tests run on a machine with no display.

| Library | What it is |
|---|---|
| [finescript](https://github.com/theosib/FineStructureScript) | The embedded scripting language |
| [finegui](https://github.com/theosib/finegui) | GUI, on Dear ImGui |
| [finenet](https://github.com/theosib/finenet) | Messages and channels, in-process or over a network, and LAN discovery |
| [FineStructureVK](https://github.com/theosib/FineStructureVulkan) | Vulkan, with the boilerplate gone |

## Building

The libraries above are sibling directories, each with a Release build in its own `build/`.
[PLATFORMS.md](https://github.com/theosib/Keystone-RTS/blob/main/docs/PLATFORMS.md) in Keystone has
the whole recipe for each operating system.

```sh
cmake -S . -B ../fv-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DFINEVOX_BUILD_RENDER=ON -DFINEVOX_BUILD_AUDIO=ON
cmake --build ../fv-build
../fv-build/finevox_tests         # Run test binaries directly, not ctest -j
```

## Documentation

**[docs/INDEX.md](docs/INDEX.md)** is the map of everything. To begin with:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): layers, libraries and technical decisions
- [docs/systems/game-runtime.md](docs/systems/game-runtime.md): how a game is put together
- [docs/systems/multiplayer.md](docs/systems/multiplayer.md): how the client and server work
- [docs/protocol/PROTOCOL.md](docs/protocol/PROTOCOL.md): the wire format
- [docs/PATTERNS.md](docs/PATTERNS.md): conventions and gotchas
- [docs/STATUS.md](docs/STATUS.md): the current state, and [docs/ROADMAP.md](docs/ROADMAP.md): what's next

## License

Apache License 2.0: see [LICENSE](LICENSE) and [NOTICE](NOTICE).
