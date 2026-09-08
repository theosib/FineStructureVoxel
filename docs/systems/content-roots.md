# Content Roots

> How `render_demo` finds the files it loads, and which of those loads follow the
> configurable game-content root. Written for Phase SL-0, which made the content
> root selectable so a separate game project can supply content from outside the
> engine repository.

---

## The `--game-root` flag

```
./render_demo --game-root /path/to/game_content
```

`--game-root <dir>` sets the `game/` scope of `ResourceLocator` to `<dir>`.
The directory must already exist: if it does not,
[render_demo.cpp](../../examples/render_demo.cpp) throws at startup with a
message naming the path, and the process exits non-zero.

```
$ ./render_demo --game-root /no/such/dir
FineVox Render Demo
==================

Error: --game-root is not an existing directory: /no/such/dir
```

There is deliberately **no fallback** to the engine's own `resources/` when the
flag is supplied. Silently booting engine content because a game directory was
mistyped hides the mistake until something turns out to be missing much later.

Without the flag, behaviour is exactly as before the flag existed: the root is
`resources`, falling back to `../resources` when that does not exist and
`../resources` does (the case where the demo is run from the build directory).

---

## Resource load audit

Every place `render_demo` reaches for a file on disk. "Game root" means the load
follows `--game-root`; anything else does not.

### Follows the game root — via the `resourcePath` variable

These build a path by string concatenation on the same variable that is handed
to `ResourceLocator::setGameRoot()`.

| Line | Loads | Loader |
|------|-------|--------|
| 610 | `<root>/recipes` | `RecipeLoader::loadDirectory` |
| 670 | `<root>/input/default.bindings` | `InputActionSystem::loadBindings` |
| 679 | `<root>/lang/en.lang` | `LabelRegistry::loadFile` |
| 788 | `<root>/fluids` | `FluidLoader::loadDirectory` |
| 795 | `<root>/loot` | `LootTableLoader::loadDirectory` |
| 802 | `<root>/particles` | `ParticleLoader::loadTypesFromDirectory` |
| 808 | `<root>/emitters` | `ParticleLoader::loadPresetsFromDirectory` |
| 816 | `<root>/tags/*.tag` | `loadTagFile` per entry |
| 857 → 401 | `<root>/biomes` | `BiomeLoader::loadDirectory` (`--worldgen` only) |
| 857 → 406 | `<root>/features` | `FeatureLoader::loadDirectory` (`--worldgen` only) |
| 965 | `<root>/textures/particles` | `ParticleAtlas::loadFromDirectory` |

### Follows the game root — via `ResourceLocator::resolve("game/…")`

These go through the locator's `game/` scope, which `--game-root` sets.

| Line | Loads | Notes |
|------|-------|-------|
| 208 | `game/blocks/<name>.model` | the 10 demo block definitions |
| 187, 192 | `game/blocks/<path>`, then `game/<path>` | `.model` include resolver — finds `base/solid_cube`, `shapes/*.collision`, `shapes/*.geom` |
| 152, 154, 156 | `game/<name>`, `+ .fs`, `+ .fsc` | `VoxelResourceFinder`, backing `guiEngine.resolveScript` at 621 — resolves every `loadUiScript` call (632–635, 736–738) |
| 1014 | `game/sounds` | `SoundLoader::loadDirectory`, `FINEVOX_HAS_AUDIO` builds only |

### Does NOT follow the game root

| Line | What | Where it actually comes from |
|------|------|------------------------------|
| 884, 885 | `shaders/chunk.vert.spv`, `chunk.frag.spv`, `fluid.frag.spv` | Hardcoded literals, resolved relative to the **current working directory**. CMake compiles SPIR-V into `${CMAKE_CURRENT_BINARY_DIR}/shaders`, so the demo must be run from the engine build directory. |
| 986 | `shaders/particle.vert.spv`, `particle.frag.spv` | Same — build-dir-relative literal. |
| 2520 | `screenshots/` (F12 output) | Hardcoded literal, cwd-relative. This is a write, not a read. |
| 889 | Block texture atlas | No file at all — `createPlaceholderAtlas` generates 16×16 solid-colour tiles procedurally. The demo never reads `resources/textures/` for block faces, so a game root cannot override block textures here. |
| 658 | Key bindings | `loadKeyBindings()` → `ConfigManager`. `render_demo` never initialises `ConfigManager` and never calls `setUserRoot()`, so `isInitialized()` is false and the **hardcoded engine defaults** in `getDefaultKeyBindings()` are used. The `user/` scope is unset. |
| 1372 | `physics.block_placement_mode` | Same uninitialised `ConfigManager` — returns the engine default. |
| 2405 | Sky parameters | `computeSkyParameters(timeOfDay)` uses the one-argument overload, which holds a `static SkyConfig::defaults()` — **hardcoded**. `resources/sky.conf` exists but `render_demo` never loads it, so a game root cannot override the sky. |
| — | finegui's `gui.vert.spv` / `gui.frag.spv` | Absolute paths baked into the finegui build tree by that library. Outside `render_demo` entirely. |

### Content directories the demo never reads

`resources/entities/` and `resources/scripts/` ship content but no
`render_demo` code path loads them, so they are unaffected by the flag.
`resources/sky.conf` is likewise shipped but unread (see line 2405 above).

---

## Verdict: is a separate content root viable with only the flag?

**Yes, for everything the demo actually loads as content.** Verified by running
the demo against an external directory:

```
$ ./render_demo --game-root /tmp/sl0-root
Game content root: /tmp/sl0-root
Loaded 7 recipes from /tmp/sl0-root/recipes
Loaded input bindings from /tmp/sl0-root/input/default.bindings
Loading block 'stone' from: /tmp/sl0-root/blocks/stone.model
Loaded 2 fluid types from /tmp/sl0-root/fluids
Loaded 10 loot tables from /tmp/sl0-root/loot
Loaded 6 particle types from /tmp/sl0-root/particles
Loaded 6 particle textures from /tmp/sl0-root/textures/particles
...
```

Every content category resolved to the external root, and no
`ResourceLocator` fallback between roots was needed: the game root is the only
root the content path consults, and `engine/` and `user/` are never even
configured by the demo.

Three constraints follow from the audit, none of which the flag can address:

1. **The game root must be self-contained.** Because `resolve()` has no
   scope-to-scope fallback and the demo sets only `game/`, a game directory has
   to supply *every* file the demo asks for — including engine-ish shared pieces
   such as `blocks/base/solid_cube.model` and `shapes/*.collision`, which the
   `.model` include resolver looks up under the game root. A game root
   containing only game-specific blocks will fail to resolve those includes.
   Whether to add engine→game fallback resolution is a design decision, not
   something this phase changed.

2. **Shaders stay with the engine build.** The SPIR-V paths are cwd-relative
   literals, so the demo must still be launched from the engine build directory
   even when its content lives elsewhere.

3. **Some behaviour is not content-driven at all.** Key bindings, block
   placement mode, sky configuration, and block textures come from hardcoded
   defaults or procedural generation, so a game root cannot currently override
   them regardless of what files it provides.
