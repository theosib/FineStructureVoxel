# System: Texture Packs

**Library:** `finevox` (`TexturePacks`, `include/finevox/core/texture_packs.hpp`), used by `GameClient`

A texture pack changes how a game looks and nothing else. The player picks one in a running game
and everything is redrawn with it on the next frame. Packs can be drawn at higher resolution than
the game, up to 128×128 a block face, and a pack can also switch on blending at block edges
(rendering.md, "Blending").

---

## What a pack is

A folder holding files at the same paths as the game's, and a `pack.conf`:

```
packs/smooth/
  pack.conf
  textures/blocks/turf_top.png     # replaces the game's textures/blocks/turf_top.png
  ui/icons/timber.png
```

```
name: Smooth ground
description: Grass, earth, sand and stone blend into each other at their edges
blending: true              # faces spread over their neighbours' (BlockBlend)
blend:sand: 30 0.25         # over sand's own `blend:` while this pack is on
blend:flagstone: none       # no blending for this block
```

Everything in `pack.conf` is optional, and so is the file: a pack with none is named after its
folder. The id is always the folder's name. A file the pack hasn't got comes from the game.

**Only pictures can be replaced**: `.png`, and a UI skin (`.fgs`) with its `.ttf`/`.otf` fonts
(`TexturePacks::replaceable`). Scripts, blocks, models and config always come from the game, so a
pack can't change how a game plays. It's the player's own business: the server never hears of it,
and two players in one game can use different packs.

A skin names its atlas and font relative to itself, so a pack that reskins the UI supplies the
`.fgs` and everything it names, together.

## Where packs are found

`GameClient` scans `<game root>/packs/` (packs that come with the game), then
`<user root>/packs/` (the player's own), at startup. A player's pack replaces the game's with
the same id. In a source tree the two roots are the same folder. The user root of an installed
game is `~/Library/Application Support/<game>`, `%APPDATA%\.<game>` or `~/.<game>`
(`ResourceLocator::userRootFor`). Packs come from the client's own game root even when it plays a
server's content from the cache.

## Choosing one

The choice is the `texture_pack` pref, kept in `prefs.txt` by the engine. UI scripts have:

```
texture_packs -> [{=id =name =description} ...]   the packs to choose from, sorted by name
texture_pack [id] -> id                            the pack in use ("" for the game's own); with
                                                   an id, changes to it on the next frame
```

## Changing pack in a running game

`texture_pack` only records the request. A UI script runs while the current frame's command
buffer is being recorded, with the old atlases already bound in it, so nothing can be freed then:
destroying the old block atlas there invalidated the frame (validation:
`InvalidCommandBuffer-VkImageView`). The change happens in `onUpdate`, between frames, as leaving
a game does. `selectTexturePack` then waits for the GPU to go idle and rebuilds:

- **The block atlas**, which `WorldRenderer::setBlockAtlas` rebinds. Tiles are placed by name with
  borders a sixteenth of a tile, so the same names land at exactly the same UVs at any tile size
  (`BlockAtlas::sameLayout`, tested). Meshes and the shapes' face UVs stand as they are: a pack
  that only changes pictures needs no remeshing. If the layout ever did differ, the client logs
  it, hands the workers a new texture provider and remeshes. A provider keeps its own copy of the
  mappings, so a worker mid-build never reads a freed atlas.
- **The particle atlas**, which `ParticleRenderer::setAtlas` rebinds on its next frame.
- **The entity atlas**: skins, sun and moon, held blocks, markers, item pictures, and the clouds.
- **Blending**: if blending was on or is now, the world is remeshed near the camera.
- **UI pictures**: every `ui_texture` and the last `ui_skin` whose file the pack changes is loaded
  again under the lifetime of the script that first loaded it. The old ones stay in memory until
  that lifetime ends (the UI script's game), as replaced skin images always have.

It's a hitch of a frame or two. Switching back and forth was clean under `--validation`.

## Resolution

`BlockAtlas` makes its tiles as big as the largest block picture: a power of two from 32 to 128,
scaling everything to that size (nearest-neighbour). A 64×64 pack doubles every tile, and the
game's own 32×32 pictures in it are doubled up pixel for pixel. `chunk.frag` works out how far to
mip from the tile's size (`maxAtlasMip`), as the borders grow with the tile. Entity skins keep their
own sizes. Particles are scaled into their atlas's fixed cells.

## Not done

- A pack can't add new textures that the game doesn't name, beyond pictures in folders the game
  lists (particles, markers, item pictures).
- There's no picker before a game starts: the title screen has no settings. The pref still
  applies from the start of the next game.
