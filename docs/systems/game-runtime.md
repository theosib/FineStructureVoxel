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
| `menu_script` | none | The client's first screen. With none, a client goes straight into a game |
| `lobby_script` | none | Declares the pre-game. See [multiplayer.md](multiplayer.md) |
| `client_content`, `net_port` | | What a client needs, and the game port. See [multiplayer.md](multiplayer.md) |
| `world_seed`, `world_radius`, `sea_level`, `caves` | | World; radius is in columns |
| `world_biomes` | all | Only these biomes, by file name, so that a lobby setting can pick a kind of land |
| `fog_of_war`, `viewer_faction` | `true`, `1` | Whose eyes the client sees through |
| `view_distance`, `default_locale`, `tick_rate`, `gravity` | | |
| `lod` | `true` | Merge distant blocks. Turn off for an overhead camera, which keeps the map in the transition zone where meshes visibly swap |
| `detail_pixels` | `0` | Draw a block in its `far-geometry` once it's under this many pixels tall on screen. 0 draws everything in full |
| `saves` | `false` | Keep the game in `saves/autosave`, resume it on launch, and save on exit |
| `autosave_minutes` | `0` | Also save during play this often |
| `nav_max_fall` | `3` | Largest drop path-following units will take. Past 3 blocks each one costs extra, so it's a way down and not a shortcut. Raise it where falling doesn't hurt, or a unit on anything tall has no way back |
| `step_height` | `0.6` | Highest ledge the player walks up without jumping. Just over 1 walks up whole blocks, as path-following units do. The eye rises after the body, so it reads as a stair |
| `player_entity` | `finevox:player` | Entity type of the player's body, as seen when they aren't in it. With no such type, or one without a model, it draws as a box in the viewer's faction colour |
| `random_ticks` | `4` | Per subchunk per tick; `0` if no block wants them |
| `sky_sun`, `sky_moon` | none | Textures (under `textures/`) drawn in the sky: the sun where the light comes from, the moon opposite, `sky_moon` a list taken a day at a time. Where they're clear is cut out, not blended; `sky_body_size` is their half-width as a fraction of their distance |

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

This is the multiplayer boundary, and it's real: even in single player, a client knows only what
arrives over a connection. See [multiplayer.md](multiplayer.md).

### Game script events

| Event | In scope |
|---|---|
| `on :start` | `WORLD_SEED`, `WORLD_RADIUS`, `SEA_LEVEL`, `SETTINGS`, `PLAYERS` (always) |
| `on :tick` | `dt` |
| `on :paused_tick` | `dt`. In place of `on :tick` while the game is paused |
| `on :command` | `cmd`, and `sender`, the player's id, or 0 if it's nobody in particular |
| `on :player_join`, `on :player_leave` | `player`: `{=id =name =local}` |

A handler that throws is logged once per distinct message and the game continues.
`finevox_headless` exits nonzero if any occurred.

### UI script events

| Event | In scope |
|---|---|
| `on :frame` | |
| `on :mouse` | `mouse_event`, `mouse_button`, `mouse_x`, `mouse_y`, `mouse_shift` |
| `on :action` | `action`, `action_arg` (from `input/default.bindings`) |
| `on :state` | `msg` |
| `on :net` | `msg`: `{=phase}`, and `=failure` if it failed |
| `on :lobby` | `msg`: LobbyState from the protocol |
| `on :chat` | `msg`: `{=from =name =text}` |

Camera actions (`begin_pan_*`, `begin_orbit_*`, `toggle_view`) are consumed by the client, and so
is `begin_jump` for a player on foot. Everything else reaches `on :action`, `begin_jump` overhead
included, so the same key can do something else from there.

---

## Pausing

```
game_pause [paused]     game_paused -> bool     (game thread)
game_paused -> bool                             (UI script, as the server last said)
```

A pause stops the simulation and nothing else. Ticks keep their rate: subsystems get `tickPaused`
in place of `tick`, which does nothing unless they say so, and only replication, the MCP hooks,
column housekeeping and the game script do. `game_tick` stands still.

So commands are still carried out, and a game can take orders while stopped. `on :command` runs as
ever, which means it's the game that has to refuse what shouldn't happen with the clock stopped,
and `on :paused_tick` that has to keep telling clients what's changed. Who may pause is also the
game's business: the engine has no opinion, and a pause isn't kept in a save.

`Time` carries the flag, and goes at once when it changes. Without it a client would go on playing
walk cycles and effects by itself, and its body's physics are its own.

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
cell_occupied x y z [=except id] -> an entity whose body is in that cell, or nil
```

A solid block set where a body stands would trap it, so the engine doesn't leave it there: next
tick it's lifted onto the block, if there's room within a block and a quarter, or else put in the
middle of a free cell beside it. Policy that can wait should still check `cell_occupied` first, as a
builder placing a wall would, since being shoved out of the way isn't pretty.

### Navigation

```
ent_goto id x y z [=range r] [=place true] [=dig true] [=partial true] [=max_nodes n]
ent_nav id -> :idle | :pending | :moving | :blocked | :arrived | :failed
ent_nav_action id -> {=kind :place|:dig =pos [x y z] =stand [x y z]}
ent_nav_resume id     ent_nav_ack id     ent_stop id
```

With `=place` or `=dig`, a path may route through cells needing a block changed. The agent
stops at each and reports `:blocked`. Policy decides what to do, then calls
`ent_nav_resume`. The search knows only blocks: the cell it wants filled can have someone in it. `:arrived` and `:failed` persist until `ent_nav_ack`.

The search works in whole cells, but climbs by real floor heights: a block's collision top (0.5
for a half slab) is what's underfoot, and no step may rise more than the agents' 1.05. Standing
on a slab, a one-cell step is a step and a half, so a route from there goes down and builds up
from the ground instead. Anything the route places counts as a full block.

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
biome_at x z [y] -> the biome's name as a symbol, or nil
player_body id -> the entity a player walks in     time_of_day -> 0 dawn .. 0.5 dusk .. 1
player_move id x y z    put a player's body there, server and client both (a respawn): the client
                        owns its body's position, so ent_set_pos on it doesn't reach them
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
site_remaining id [=now true] -> {=block n}, with =now only for the layer being built
site_remove id
light_at x y z -> {=sky =block}
site_show_ghosts [id ...]                 what's left of these sites, drawn for the viewer
hold_time_of_day fraction                 stop the clock here, and it stays stopped
```

Ghosts are drawn through `SiteGhostView`. The viewer's copy of the world lags the server's,
so a ghost the server has finished with stays up until its block has arrived in that copy,
plus a moment for the mesh, or for three seconds if it never does.

Sites compare against the live world, so a destroyed block becomes work again. Cells come
bottom-up. A position has one target; the first added wins, which lets foundations be
added after the structure without undoing it.

### Work assignment

```
board_create -> id         board_remove id        board_alive id -> bool
board_options id [=stick blocks] [=vertical weight]
board_resources id {=block resource ...}      board_sources id resource [[x y z] ...]
board_sites id [[site priority [resource ...]] ...]
board_post id key x y z [=needs r] [=priority p]     board_unpost id key
board_agent id entity [=holds r] [=fetches b] [=only_holds b] [=committed b] [=speed s]
board_drop id entity       board_release id entity
board_solve id -> {=agents =tasks =assigned =micros}
board_task id entity -> task or nil          board_take id entity -> task or nil
```

A task board gives each agent the job that makes the whole team's travel least, solved exactly,
with construction sites' layers being built as tasks. See [task-board.md](task-board.md).

### Vision and projectiles

```
vision_add_faction f      vision_source f x y z radius -> handle     vision_remove_source h
vision_state f x z -> :unknown|:explored|:visible    vision_can_see f x z
vision_can_see_ent f id   line_of_sight x y z tx ty tz      reveal_all [bool]
projectile_fire x y z tx ty tz [=speed s] [=damage d] [=owner id] [=faction f] [=lob b]
projectile_hits -> [{=x =y =z =entity =owner =faction =damage}...]
```

For a fair AI, `vision_can_see_ent` is the gate: filter everything through it.

`faction_team f team` puts factions on a team: allies don't hit each other with projectiles, aren't
each other's `=enemy_of` in `ent_find`, and each sees what the others' units and sources see. 0 puts
a faction back on its own, and `faction_hostile a b` asks. Teams aren't saved: set them again on
`:resume`.

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
window_size   mouse_pos  gui_has_mouse   key_held "left_shift"   fps   sym_name :x -> "x"
pick_block mx my     pick_ground mx my     pick_view     pick_entities x0 y0 x1 y1 -> ids
avatar_overlaps x y z -> bool     set_ghost_style r g b faint solid
do_action name     gui_click x y   gui_hover x y   input_text "abc"   (for scripted UI tests)
ui_texture name "ui/x.png"   loads an image for ui.image to show by name
entity_pos id   entity_faction id  entity_health id -> 0..1   set_faction_color id r g b
entity_type id -> :militia, or nil if not known
marker_ring x y z radius r g b [a]     marker_box x y z sx sy sz r g b [a]
marker_bar x y z width fraction        ui_rect x0 y0 x1 y1 r g b a   (an outline)
ui_fill x0 y0 x1 y1 r g b a            ui_text x y text [=color [r g b a]] [=size px]
mouse_free [b]       on foot, the cursor out for a screen over the view (looking stops)
ui_image name x0 y0 x1 y1 [u0 v0 u1 v1] [=tint [r g b a]]   a ui_texture, for a HUD
marker_xray id r g b [a]    where blocks hide that entity, its shape shows through in that colour
pref name [default]    set_pref name value   the player's own settings, in prefs.txt
cam_focus x z   cam_set distance yaw pitch   blueprint_size name -> [x y z]
cam_pivot -> [x y z]
pick_block mx my, pick_view -> {=x =y =z =nx =ny =nz =distance =block name} or nil
pick_ground mx my -> the same, looking through what a body can walk through. Orders use it,
so that foliage with a `hit:` shape can be inspected without catching clicks on the ground.
request_screenshot path    quit_game    to_menu
```

Markers last one frame, so a script that wants one asks again every frame. `marker_xray` copies the
body into a pass drawn only where something nearer has already been drawn, front faces only, and
only for a body a ray from the camera to its feet, middle or head finds a block in front of.

`pref` and `set_pref` keep bools, numbers, symbols and strings in `prefs.txt` in the player's
folder (the game root, with `--game-root`), written at once. `set_pref name nil` forgets one.
Nothing in a game should depend on them: they're the player's, not the game's.

`ui_skin`, `ui_texture` and `source` take paths inside the content root and refuse anything else,
and `request_screenshot` does too when the content came from a server. A UI script can be a
server's, so natives must stay this way.

### Networking (UI script)

`menu_script` starts with these. `ui_script` takes over when the world has arrived, in a new
context, and the windows the menu showed are gone.

```
game_start [=new true]                     a game for one, with no pre-game
net_host [=listen true] [=title t] [=port p] [=password pw] [=new true] -> nil, or what went wrong
net_join address [=password p]             as people type them, or a list of them for one server
net_leave      net_phase -> :idle...       net_failure -> text
net_browse on                              start or stop looking
net_servers -> [listing...]                each with =addresses =port =rtt added
net_blocked -> the OS is refusing this process the local network
net_recent     net_addresses               remembered servers, and this machine's addresses
net_open [port] -> nil, or what went wrong  a running game, on the network from now on
net_port -> port, or nil                   net_is_host    net_me -> player id
lobby_info -> declaration                  lobby_set key value    lobby_ready b
lobby_launch [b]                           lobby_kick id
chat_send text [=team true]
has_save       player_name    set_player_name name
```

`my_player` and `my_faction` are set when `ui_script` starts.

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
only_in: chop quarry      # drawn only while one of these animations plays: a tool in hand
hidden_in: shoot          # never while one of these does: the same tool put away
```

A model can wear a skin instead of colours: `texture: entity/beast` (under `textures/`) and
`texture_size: 64 32` before the parts, then per part `uv: u v`, where its box unwrap starts in
skin pixels. The unwrap is the usual one: top and bottom side by side at (u+d, v), then right,
front, left and back in a strip below them, a face as many pixels as the box is at 16 a block
(`uv_size: w h d` to say otherwise). `mirror: true` flips it, for a left limb drawn from a right
one's pixels, and `turn: x y z` turns a part about its own centre (degrees, X then Y then Z), so a
beast's body can lie along it and keep a standing box's unwrap. The client packs every skin into
one atlas, sampled nearest so pixel art stays sharp.

Keys must not be indented: `ConfigParser` reads indented lines as continuation data. The
entity needs `model: name` and `anim:name: slot`. `only_in` and `hidden_in` go by those names.
An entity can also have `personal_space: 0.4`: one standing still closer to another than the sum
of theirs is eased away at up to 1.2 blocks a second. One that's walking isn't pushed at all, so
units never jam each other, and those standing step out of its way. Units of a type without it
neither push nor get pushed. Naming `walk` gets server-side idle/walk
switching for free.

```
death_seconds: 1.2                  # how long the body stays on view; 0 or none, gone at once
death_emitter: game:unit_lost       # burst as it shrinks away, tinted with the faction's colour
```

A killed entity keeps drawing on the client: it topples, pales, then shrinks into the burst.
It's gone from the server already, so it can't be selected or targeted. A death the viewer
couldn't see shows nothing, and `despawnEntity` on a living one removes it at once.

### .model additions

`texture-top`, `texture-bottom`, `texture-side`, `walk-speed`, `connects`, `opaque`,
`blocks-sky-light`, `far-geometry`. With `opaque: false` a block lets light through, its `light-attenuation`
defaulting to 1, and sky light falls past it. `blocks-sky-light: true` stops the sky light while
letting the rest through, for leaves that shade the ground under them.

Each cube face can have its own texture: `texture-north`, `-south`, `-east`, `-west`, `-up`,
`-down`, over `texture-top`/`-bottom`/`-side`, over `texture`. A texture name ending `@90`, `@180`
or `@270` is that texture turned clockwise. A `.geom` face names its own texture after the colon,
`face:front: blocks/oven_front`, or `#name` for one the model fills in with `texture:name: path`;
a face with none draws the block's texture for the side it's on. Words after the texture describe
the face further and are ignored by a loader that doesn't know them. A geometry that is exactly a
cube's six whole faces draws as a cube, with merged faces and ambient occlusion.

**Properties.** A block can have properties, each combination its own block type:

```
state:facing: north east south west     # the first value is the default
state:open: false true
variant:facing=east: looks/door y=90     # the first rule that matches gives the look
variant:facing=south|west: looks/door y=180 lock-uv
part:open=true: looks/door_leaf          # or: every matching part, drawn together
```

The default state keeps the block's plain name; the others are named `door[facing=east,open=true]`,
properties in declared order. Each is an ordinary block type, so nothing else needs to know.
Behaviour can differ by state too: `set:lit=true: light-emission=13` (also `light-attenuation`,
`hardness`, `opaque`, `walk-speed`, `collision=none|full`); every matching line applies, in order.
A look is another `.model`, by path from `blocks/` (keep looks in a subfolder, which isn't
loaded as blocks), turned `x=` degrees about X then `y=` about the vertical, each clockwise seen
from the positive end. The turn is baked in at load: geometry, collision and hit shapes, and the
faces each is culled by. `lock-uv` keeps textures aligned with the world as the faces turn. The
block's own keys (hardness, light, script…) apply to every state; the look gives shape and textures.
A shared look can leave `#name` textures for each block to fill. Scripts move between states:

```
block_prop name :open -> :true       block_with name :open :false -> name
block_family name -> the plain name
```

**Tint.** A face can be coloured by where it is, as grass and leaves are by their biome.
`tint: grass` names a colour each biome gives (`tint:grass: r g b` in its `.biome`), blended over
the five columns either way so that colours shade into each other at a border; `tint-color: r g b`
is used where a biome doesn't say, or always for a block with no `tint:`. `tinted: up` (or face
names, or `all`) tints a cube's faces, and a `.geom` face is tinted by `tint=0` after its texture.
Colours are as they look: the multiply happens as an artist would do it, not in linear light.

`connects: <group>` makes blocks of a group join up. A geometry face named
`connect_<dir>_<anything>`, with dir one of `n ne e se s sw w nw`, shows only towards a
neighbour of the same group at the same height. A diagonal joins only where neither straight
neighbour beside it does, so three blocks in an L make a corner, not a triangle. `up` and `down`
are directions too, and `open_<dir>_<anything>` shows only where nothing of the group is: a rock
under another can have a flat top to carry it, and a rounded one with nothing on it.

`far-geometry: <shape>` is a cheaper shape for where a block's detail is too small to see, as set
by `detail_pixels`. A subchunk switches as a whole, once all of it is past the distance where a
block is that many pixels tall, and switches back only once it's well inside it again
(`DetailRange`). A taller or sharper window reaches further. A UI script can change it while a
game runs: `detail_pixels n` sets it and gives back the distance, in blocks.

### .feature additions

Any feature with `density` registers its own placement: `density`, `surface`,
`place_min_height`, `place_max_height`. A tree can give its crown layer by layer, bottom to top, `crown: 2~ 2~ 1 1+`: radii, `~` dropping
corners at random, `+` cutting them; `crown_top: 1` puts the top layer that far above the trunk.
`type: cluster` scatters a block: `block`, `on`,
`attempts`, `radius`, `min_stack`, `max_stack`, and `top` for a different block at the top of a
stack. `biomes:` limits any placed feature to those biomes. An ore (`.ore`) can name more rock it
runs through with `host:<rock>: <ore>`, and `height_shape: peaked` makes its veins likeliest in the
middle of their heights; a density under one vein a column is a chance of one.

---

## Saving

With `saves: true`, every clean exit saves and the next launch resumes, firing `on :resume`
instead of `on :start`. `--new` starts over, and `--no-save` or `--exit-after` leave saves alone.
`finevox_headless` only saves when told: `--save DIR` at the end of a run, `--resume DIR` to load.

`trace category a b c` writes a line to the game's trace file, with the tick and the time:
`logs/game-<date>-<time>-<pid>.log` beside `saves/`, the newest 20 kept. It's flushed a line at a
time, so a crash keeps everything up to it. `--trace FILE` sends it somewhere else, and a run that
doesn't save (`--no-save`, any headless run without `--trace`) writes none. With MCP on, each line
also goes to the `logs` tool. It's for the questions a game can't answer by looking at it now:
why nothing was trained, where a unit went and why.

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

`new_game` from the UI script saves and restarts the process with `--new`, and `to_menu` restarts
it without. Only a fresh process is sure of leaving nothing of the old game in the global registries.

Each faction with a player has its own memory in the save, and seats go by name, so a host resuming
a game gets everyone back on their own side. A hosted game keeps to `saves/hosted`, and a server
that says `saves: true` keeps to `saves/server`, so neither costs you the game you were playing.

## MCP server

`--mcp` (or `--mcp-port N`) serves the running game over Model Context Protocol, for debugging
and for LLMs. `finevox_headless --mcp` plays in real time until `game_exit`, and ignores
`--ticks`.

```text
client  --stdio-->  tools/mcp_bridge.py  --HTTP-->  game, on 127.0.0.1:7801/mcp
```

The bridge exists because a client doesn't retry a server that failed when it connected, so a
server only inside the game would be missing from any session that began first. The bridge
always connects, and with no game a tool call is an ordinary tool error. It remembers the last
tool list in `~/.cache/finevox`, and sends `tools/list_changed` when a game offers a different
one. It uses the standard library alone, so there's no venv.

**One endpoint, tools by side.** Client and server share a process, so they share a port, and
each registers its own tools: `server_*` in `addServerMcpTools`, `client_*` in
`GameClient::Impl::registerMcpTools`. A headless process has server tools alone, so if the two
split into separate processes each will offer its own without changing a tool.

| Tool | Runs on | Player | |
|------|---------|--------|---|
| `status` | HTTP thread | yes | Tick and frame counts and ages, so a hang shows as a growing age |
| `logs` | HTTP thread | | stdout and stderr, the last 5000 lines |
| `wait` | HTTP thread | yes | Until N more ticks or frames |
| `server_state`, `server_eval`, `server_entities`, `server_blocks`, `server_command` | Game thread, last in the tick | | The real simulation |
| `client_state`, `client_entities`, `client_blocks`, `client_command` | Render thread, after UI scripts | yes | What the player knows: fog of war applies |
| `client_eval` | The same | | UI script globals and natives. Input goes through this: `input_key`, `gui_click`, `do_action` |
| `client_screenshot` | Render thread, after the frame | yes | A JPEG, 800 pixels unless asked |

`--mcp-player` offers only the tools marked Player, for an LLM to play with. The others are
refused as well as unlisted.

**Threads.** The HTTP thread queues a call for the thread that owns its data, and that thread runs
it at a safe point, so a tool reads what its thread may read, with no locks. A thread that doesn't
arrive in 10 seconds gives a tool error with `status` in it. `McpSide::Any` tools answer even when
the game has hung, so they may touch only atomics. A running tool can't be interrupted: a script
in `*_eval` that never returns holds that thread. A game behind time runs up to 10 ticks in a row
before it reads commands, so a command can land several ticks after it's sent.

**Adding a tool.** Fill in an `McpTool` and pick its `side`. Arguments are checked against
`inputSchema` first, and an unknown one is refused, so a misspelt option can't quietly answer a
different question. `player = true` promises it shows nothing a player couldn't see.

**Runtime, not compile time.** Off, the game thread has nothing extra to do: the tick hooks are
subsystems that only exist when there's a server. A frame has two pointer checks. There's no
socket and no thread, and output capture only starts with the server. Measured on 2400 Keystone
ticks, five interleaved runs each:

| | Median ms/tick | Range |
|---|---|---|
| Off | 1.38 | 1.33-1.98 |
| On, idle | 1.43 | 1.30-1.60 |
| On, about 100 calls a second | 1.37 | 1.32-1.64 |

That's noise, and a compile-time switch would buy nothing for its `#ifdef`s. A tool costs the
wait for its safe point: 28ms for the game thread and 12ms for the render thread. Screenshots,
the only tool with real work in it, took 16ms at 800 pixels and 29ms at full size, including that
wait, and five a second held 60 fps.

Only 127.0.0.1 is served, and a request with a non-local `Origin` is refused, because a web page
could otherwise post scripts to it. Nothing else guards it: anything already running as you can
use `*_eval`, so it stays off unless asked for.

## Testing a Game

```
finevox_headless --game-root DIR --ticks N --script scenario.fsc
```

A scenario is a second game script, loaded after the main one with full access to its
state. It ends the run with `game_exit code`.

## Gotchas

- Lifetime in `GameClient::Impl` is order-sensitive: atlases must outlive `WorldRenderer`,
  whose mesh workers read them until it stops.
- `WorldRenderer` renders `ClientSession::world()`, not the session's world, and light comes from
  there too: the client never touches the server's `LightEngine`.
- `LightEngine::processUpdates` doesn't drain the update queue. Only the lighting thread
  applies queued updates, so a test must run the thread to exercise them.
- Per-frame geometry must not go through `RawMesh::update`, which blocks on a submit.
  `SceneRenderer` writes mapped per-frame buffers instead.
- Replacing a chunk mesh goes through `RenderSurface::deferDelete`: frames in flight may
  still draw it. Validation flags the mistake; a plain run just gets corruption.
- A default `BlockType` reports `hasCollision()` but has no shape, so physics walks
  through what pathfinding avoids. Set a collision shape in test fixtures.
