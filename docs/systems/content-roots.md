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

### Follows the game root — via `ContentPackLoader` (`contentPack.loadAll(resourcePath)`)

Phase R4 consolidated content loading into `finevox::ContentPackLoader` (`include/finevox/core/content_pack_loader.hpp`), which loads all content categories from `resourcePath`:

| Path | Loader / Registry |
|------|-------------------|
| `<root>/tags/*.tag` | `loadTagFile` → `TagRegistry::global()` + `UnificationRegistry::global()` |
| `<root>/sounds/*.sound` | `finevox::audio::SoundLoader::loadDirectory` → `SoundRegistry::global()` |
| `<root>/particles/*.particle` | `ParticleLoader::loadTypesFromDirectory` → `ParticleTypeRegistry::global()` |
| `<root>/emitters/*.emitter` | `ParticleLoader::loadPresetsFromDirectory` → `EmitterPresetRegistry::global()` |
| `<root>/loot/*.loot` | `LootTableLoader::loadDirectory` → `LootRegistry::global()` |
| `<root>/fluids/*.fluid` | `FluidLoader::loadDirectory` → `FluidRegistry::global()` + `FluidInteractionRegistry::global()` |
| `<root>/blocks/*.model` + `<root>/shapes/*` | `BlockModelLoader` → `BlockRegistry::global()` (both bare `"stone"` and `"finevox:stone"`) + `ContentPackLoader::blockGeometries()` |
| `<root>/scripts/*.fsc` | Bound via `ContentPackLoader::bindScripts(gameScriptEngine)` |
| `<root>/entities/*.entity` | `EntityTypeLoader::loadDirectory` → `EntityTypeRegistry::global()` |
| `<root>/recipes/*.recipe` | `RecipeLoader::loadDirectory` → `RecipeRegistry::global()` |
| `<root>/lang/*.lang` | `LabelRegistry::global().loadFile` |
| `<root>/sky.conf` | `SkyConfig::fromFile` → `contentPack.skyConfig()` |

### Follows the game root — direct loads in `render_demo`

| Loads | Loader |
|-------|--------|
| `<root>/input/default.bindings` | `InputActionSystem::loadBindings` |
| `<root>/biomes` | `BiomeLoader::loadDirectory` (`--worldgen` only) |
| `<root>/features` | `FeatureLoader::loadDirectory` (`--worldgen` only) |
| `<root>/textures/particles` | `ParticleAtlas::loadFromDirectory` |
| `game/<name>`, `+ .fs`, `+ .fsc` | `VoxelResourceFinder`, backing `guiEngine.resolveScript` for UI scripts |

### Does NOT follow the game root

| What | Where it actually comes from |
|------|------------------------------|
| `shaders/chunk.vert.spv`, `chunk.frag.spv`, `fluid.frag.spv`, `particle.vert.spv`, `particle.frag.spv` | Hardcoded literals, resolved relative to the **current working directory**. CMake compiles SPIR-V into `${CMAKE_CURRENT_BINARY_DIR}/shaders`, so the demo must be run from the engine build directory. |
| `screenshots/` (F12 output) | Hardcoded literal, cwd-relative. This is a write, not a read. |
| Block texture atlas | `createPlaceholderAtlas` generates 16×16 solid-colour tiles procedurally. |
| Key bindings (`loadKeyBindings()`) & `physics.block_placement_mode` | `ConfigManager` (uses engine defaults unless initialized). |
| finegui's `gui.vert.spv` / `gui.frag.spv` | Absolute paths baked into the finegui build tree by that library. |

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
