# System: Game Runtime

**Libraries:** `finevox_game` (`finevox::game::`), client host in `finevox_run`
**Headers:** `include/finevox/game/`, `include/finevox/client/game_client.hpp`

A game is a directory, not a program. `GameRuntime` turns a directory into a running game
with no GPU dependency. `GameClient` adds a window. A new game should need no C++ until a
measured bottleneck says otherwise.

---

## A Game Directory

```
game.conf            manifest (below)
scripts/main.fsc     game logic, game thread
ui/main.fs           HUD, render thread
blocks/ shapes/      .model, .geom, .collision
entities/ models/    .entity, .emodel, .skeleton, .anim
biomes/ features/    .biome, .feature, .ore
blueprints/          .blueprint, .vxsc
textures/            block textures named by .model files
lang/                <locale>.lang
input/default.bindings
sky.conf
```

| `game.conf` key | Default | Meaning |
|---|---|---|
| `name`, `namespace`, `window_title` | | Identity |
| `main_script`, `ui_script` | `scripts/main.fsc`, `ui/main.fs` | Entry points |
| `world_seed`, `world_radius`, `sea_level`, `caves` | | World; radius is in columns |
| `fog_of_war`, `viewer_faction` | `true`, `1` | Whose eyes the client sees through |
| `view_distance`, `default_locale`, `tick_rate`, `gravity` | | |
| `lod` | `true` | Merge distant blocks. Turn off for an overhead camera, which keeps the map in the transition zone where meshes visibly swap |
| `saves` | `false` | Keep the game in `saves/autosave`, resume it on launch, and save on exit |
| `autosave_minutes` | `0` | Also save during play this often |
| `nav_max_fall` | `3` | Largest drop path-following units will take. Past 3 blocks each one costs extra, so it's a way down and not a shortcut. Raise it where falling doesn't hurt, or a unit on anything tall has no way back |
| `random_ticks` | `4` | Per subchunk per tick; `0` if no block wants them |

The content root must be self-contained: nothing falls back to the engine's `resources/`,
including `blocks/base/*`. See [content-roots.md](content-roots.md).

---

## Two Script Worlds

Game logic runs on the game thread. UI runs on the render thread in a separate
`ScriptEngine`. They share nothing:

```
UI  --send_command value-->  on :command  (`cmd` in scope)     CBOR over finenet
UI  <----on :state--------  ui_publish value                   CBOR through a queue
```

This is the boundary multiplayer needs, so single-player can't quietly depend on shared
state.

### Game script events

| Event | In scope |
|---|---|
| `on :start` | `WORLD_SEED`, `WORLD_RADIUS`, `SEA_LEVEL` (always) |
| `on :tick` | `dt` |
| `on :command` | `cmd` |

A handler that throws is logged once per distinct message and the game continues.
`finevox_headless` exits nonzero if any occurred.

### UI script events

| Event | In scope |
|---|---|
| `on :frame` | |
| `on :mouse` | `mouse_event`, `mouse_button`, `mouse_x`, `mouse_y`, `mouse_shift` |
| `on :action` | `action`, `action_arg` (from `input/default.bindings`) |
| `on :state` | `msg` |

Camera actions (`begin_pan_*`, `begin_orbit_*`, `toggle_view`, `begin_jump`) are consumed
by the client. Everything else reaches `on :action`.

---

## Natives

### Units (game thread)

`mob_*` act on the mob running the script. Central logic uses `ent_*`, id first:

```
ent_spawn type x y z [=faction n] -> id     ent_remove id     ent_alive id
ent_pos id   ent_set_pos id x y z           ent_type id       ent_faction id
ent_health id   ent_max_health id   ent_set_health id hp
ent_damage id amount [source]               ent_last_attacker id
ent_speed id mult    ent_anim id :state     ent_look_at id x y z
ent_find x y z radius [=faction f] [=enemy_of f] [=type t] [=max n] -> ids, nearest first
```

### Navigation

```
ent_goto id x y z [=range r] [=place true] [=dig true] [=partial true] [=max_nodes n]
ent_nav id -> :idle | :pending | :moving | :blocked | :arrived | :failed
ent_nav_action id -> {=kind :place|:dig =pos [x y z] =stand [x y z]}
ent_nav_resume id     ent_nav_ack id     ent_stop id
```

With `=place` or `=dig`, a path may route through cells needing a block changed. The agent
stops at each and reports `:blocked`. Policy decides what to do, then calls
`ent_nav_resume`. `:arrived` and `:failed` persist until `ent_nav_ack`.

Budget the searches: an unreachable goal costs the full `max_nodes` every attempt, so
agents should back off after `:failed`. Chasing a moving target should re-search only once
the old route has stopped being useful.

For long trips use `=partial true` with a modest `max_nodes`. If the search runs out it
walks to the closest point reached and searches again from there, so a cross-map march
costs a few small searches rather than one huge one. It reports `:failed` if a hop gains
no ground.

### World

```
surface_y x z        is_standable x y z       world_get_block  world_set_block
find_blocks x y z radius block_or_list [=max n] -> [[x y z]...], nearest first
count_blocks x0 y0 z0 x1 y1 z1 [block ...] -> {=name count}
claim_block scope x y z owner [ttl_ticks]     release_block scope x y z owner
release_claims owner                          block_claimed scope x y z
```

`find_blocks` skips subchunks with a palette check, so a wide search is cheap when the
block is rare. Use `:air` for air; the natives normalise it.

### Construction

```
blueprint_info name -> {=size_x =size_y =size_z}    blueprint_cost name -> {=block n}
site_create name x y z [turns] -> id     site_create -> empty site
site_add id x y z block                  site_claim id agent x y z -> {=x =y =z =block}
site_release id agent                    site_progress id -> {=done =total =complete}
site_remaining id -> {=block n}          site_remove id
light_at x y z -> {=sky =block}
site_show_ghosts [id ...]                 what's left of these sites, drawn for the viewer
```

Sites compare against the live world, so a destroyed block becomes work again. Cells come
bottom-up. A position has one target; the first added wins, which lets foundations be
added after the structure without undoing it.

### Vision and projectiles

```
vision_add_faction f      vision_source f x y z radius -> handle     vision_remove_source h
vision_state f x z -> :unknown|:explored|:visible    vision_can_see f x z
vision_can_see_ent f id   line_of_sight x y z tx ty tz      reveal_all [bool]
projectile_fire x y z tx ty tz [=speed s] [=damage d] [=owner id] [=faction f] [=lob b]
projectile_hits -> [{=x =y =z =entity =owner =faction =damage}...]
```

For a fair AI, `vision_can_see_ent` is the gate: filter everything through it.

### Camera bindings

The client handles these actions from `input/default.bindings` itself: `begin_pan_*`/`end_pan_*`
(`left`, `right`, `forward`, `back`), `begin_orbit_*`/`end_orbit_*`, and `begin_grab`/`end_grab`,
which drag the ground along with the cursor for as long as any bound key or button is held.
Mouse buttons take `mousepress_middle` and `mouserelease_middle` style names. Anything else
goes to the UI script as an `:action` event.

### Client (UI script)

`ui_skin "ui/skin/x.fgs"` loads a textured skin and puts it in use, and `ui_skin` alone goes back
to plain ImGui. It can be called at any time, say to change skin with the era. The format is in
finegui's `docs/SKIN_SYSTEM.md`.

```
send_command value       ui_show tree [=immediate true] -> id        ui_hide id
window_size   mouse_pos  gui_has_mouse   fps
pick_block mx my     pick_ground mx my     pick_view     pick_entities x0 y0 x1 y1 -> ids
avatar_overlaps x y z -> bool     set_ghost_style r g b faint solid
do_action name     gui_click x y   gui_hover x y   (for scripted UI tests)
ui_texture name "ui/x.png"   loads an image for ui.image to show by name
entity_pos id   entity_faction id  set_faction_color id r g b
marker_ring x y z radius r g b [a]     marker_box x y z sx sy sz r g b [a]
marker_bar x y z width fraction        ui_rect x0 y0 x1 y1 r g b a
cam_focus x z   cam_set distance yaw pitch   blueprint_size name -> [x y z]
cam_pivot -> [x y z]
pick_block mx my, pick_view -> {=x =y =z =nx =ny =nz =distance =block name} or nil
pick_ground mx my -> the same, looking through what a body can walk through. Orders use it,
so that foliage with a `hit:` shape can be inspected without catching clicks on the ground.
request_screenshot path    quit_game
```

Markers are immediate: call them every `on :frame`. Use `ui_show`, not `ui.show`, which
needs a `ScriptGui` behind the context.

### Localization (both)

```
loc "key" [=count n] [=name value ...]     loc_set_locale "fr"     loc_locale
```

`lang/fr.lang` holds `key: value`. With `=count`, lookup tries `key.one`/`few`/`many`,
then `key.other`, then `key`. `{count}` and `{name}` substitute. A missing string falls
back to `default_locale`, then shows the key.

---

## Formats

### .blueprint

A legend, then one layer per `---`, ground up. Rows run along +Z, columns along +X. Space
leaves the world alone, `_` requires air.

```
p = planks
---
ppp
p p
---
p_p
```

### .emodel

```
skeleton: humanoid        # models/humanoid.skeleton
anim:walk: humanoid_walk  # models/humanoid_walk.anim, for state "walk"
part: torso
bone: hips
offset: 0 0.3 0
size: 0.5 0.6 0.3
color: 0.5 0.4 0.3
team: 1.0                 # 0..1 blend toward faction colour
```

Keys must not be indented: `ConfigParser` reads indented lines as continuation data. The
entity needs `model: name` and `anim:name: slot`. Naming `walk` gets server-side idle/walk
switching for free.

### .model additions

`texture-top`, `texture-bottom`, `texture-side`, `walk-speed`, `opaque`. With `opaque:
false` a block lets sky light through; its `light-attenuation` defaults to 1, so foliage
casts shade rather than a black hole.

### .feature additions

Any feature with `density` registers its own placement: `density`, `surface`,
`place_min_height`, `place_max_height`. `type: cluster` scatters a block: `block`, `on`,
`attempts`, `radius`, `min_stack`, `max_stack`.

---

## Saving

With `saves: true`, every clean exit saves and the next launch resumes, firing `on :resume`
instead of `on :start`. `--new` starts over, and `--no-save` or `--exit-after` leave saves alone.
`finevox_headless` only saves when told: `--save DIR` at the end of a run, `--resume DIR` to load.

The engine keeps the world, what the viewer has explored, entities under their ids, construction
sites, vision, and the clocks. Anything else is the game's, in its handlers:

```
on :save do
    game_state_store {=factions factions =contest contest}
end
on :resume do
    let saved {game_state_load}
    set factions saved.factions
end
```

Plain data only, but shared references and cycles survive: two lists holding one record still
hold one record. Paths, work claims, and shots in flight aren't kept, so `on :resume` must set
units to choose new work. A save goes to one side and is renamed into place, so a failure leaves
the last good one alone, and the save it replaces stays as `autosave.bak`. Saves run between
ticks, on the game thread during play.

`new_game` from the UI script saves and restarts the process with `--new`.

## Testing a Game

```
finevox_headless --game-root DIR --ticks N --script scenario.fsc
```

A scenario is a second game script, loaded after the main one with full access to its
state. It ends the run with `game_exit code`.

## Gotchas

- Lifetime in `GameClient::Impl` is order-sensitive: atlases must outlive `WorldRenderer`,
  whose mesh workers read them until it stops.
- `WorldRenderer` renders `WorldReplica::world()`, not the session's world. Light comes
  from the session's `LightEngine`, which is keyed by position.
- `LightEngine::processUpdates` doesn't drain the update queue. Only the lighting thread
  applies queued updates, so a test must run the thread to exercise them.
- Per-frame geometry must not go through `RawMesh::update`, which blocks on a submit.
  `SceneRenderer` writes mapped per-frame buffers instead.
- Replacing a chunk mesh goes through `RenderSurface::deferDelete`: frames in flight may
  still draw it. Validation flags the mistake; a plain run just gets corruption.
- A default `BlockType` reports `hasCollision()` but has no shape, so physics walks
  through what pathfinding avoids. Set a collision shape in test fixtures.
