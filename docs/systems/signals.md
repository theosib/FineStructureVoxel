# System: Signals

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/signal_solver.hpp`, `signal_block.hpp`, `signal_binding.hpp`
**Tests:** `tests/test_signal_solver.cpp`, `tests/test_signal_binding.cpp`

---

## Overview

Blocks that carry a signal (wires, switches, gates, lamps, machines that react to power) are a
circuit, and the engine solves circuits as circuits. It doesn't ripple block-update events from
block to block. `SignalSolver` holds the circuit as a graph and advances it one step at a time,
one step per game tick. Blocks give it configuration (what they are and how they connect) and get
results back (which levels changed).

The solver knows nothing about blocks, positions or the world. `SignalBinding` turns blocks into
nodes and results into block states: [Binding](#binding-blocks-as-a-circuit).

## The model

Every node has a level from 0 to the solver's maximum, which the game chooses (there is no
default).

| Kind | Level |
|------|-------|
| **Source** | Set from outside: `setSource(id, level)`. A switch, a sensor, a script. |
| **Conductor** | Instant: the strongest level that reaches it, each link subtracting its own loss. |
| **Component** | Computed from the levels on its ports, and output `delay` steps later (at least 1). |

**Links** feed conductors: `link(from, conductor, loss)`. The `from` end can be any node. Links
are one-way, so a wire is linked both ways between neighbours and a one-way conductor is linked
once. Conductors joined by links form a **region**, and a region settles as a whole.

`isolate(id)` drops a node's links and keeps it with its state, for relinking it;
`presetLevel(id, level)` starts a source or component at a level without reporting a change, for
bringing back a circuit the world still shows.

**Ports** are what a component reads: `connect(from, component, port)`. A port can take any
number of nodes of any kind, and most ops use the strongest of them.

| Op | Output |
|----|--------|
| `Buffer` | A |
| `Invert` | maximum if A is 0, else 0 |
| `Compare` | A if A ≥ B, else 0 |
| `Subtract` | A − B, not below 0 |
| `Max` / `Min` | strongest / weakest input on port A |
| `Parity` | maximum if an odd number of port A's inputs are on |
| `Latch` | on once A is on, off once B is on (B wins) |
| `Toggle` | flips each time A turns on |

Per component, `SignalComponentSpec` sets the op and three options:

- `delay`: steps from reading inputs to outputting the result. It's a transport delay, so a
  pulse comes out exactly as it went in, just later.
- `minPulse`: once the output turns on, it stays on for at least this many steps, so a pulse
  shorter than this comes out longer.
- `fullOutput`: any result above 0 is output at the maximum.

While a component's **Hold** port is on, its output stays as it is, it computes nothing, and
anything already on its way out is dropped.

## A step

```
1. Regions whose shape changed (conductors added, removed, linked, unlinked) are rebuilt
2. Components output the results that are now due   (Hold read from levels as they stand)
3. Sources take the levels set since the last step
4. Regions with a changed driver settle             (levels L_n)
5. Components whose inputs changed read L_n and send their result on its way
6. step() returns every node whose level changed, in id order
```

The design rests on one rule: **only conductors are instant; every component has a delay of at
least one step.** That makes a step synchronous logic. Conductors are the combinational part and
components are the registers. So:

- **Nothing depends on order.** Conductors settle from component outputs fixed for this step, and
  components read settled levels and only schedule. A conductor's level is its strongest path
  from a driver, less the losses along it, which is the same however it's computed. A loop with
  no loss doesn't keep its own level once its driver goes.
- **No instant loops.** Any loop of conductors is a region and settles. A loop through a
  component has a delay in it, so it runs as a clock and gives the same trace every run.
- **Deterministic.** The state after step n depends only on the graph, the state after step n−1,
  and the edits and source levels given before step n.

### Doing only what changed

A circuit that isn't changing costs nothing. Only regions with a changed driver settle, only
components with a changed input evaluate, and only components with something due fire, found
from a timetable keyed by step. A region settles strongest-first with a bucket per level, so each
conductor is final the first time its bucket is reached. A change ripples through a wire in one
pass, each conductor written once, and only levels that end up different are reported. There are
no per-block update events and no repeated updates, which is what makes wires expensive when they
are built from neighbour notifications.

A region is rebuilt by flooding outward from the conductors an edit touched. Removing a conductor
floods from each of its neighbours, so a cut wire becomes two regions, and linking two regions
floods them into one.

## Threads

Today the solver runs on the game thread. In a release build on an Apple M2 Max, a game on a
pre-generated world of 64 × 64 columns, at 30 ticks a second, uses about 3.1 ms of each 33 ms
tick (the `tick_stats` MCP tool shows it). 2.1 ms of that is entities, mostly physics waiting on
`BlockRegistry::getType`'s lock (see ARCHITECTURE.md).

Against that, a 100 × 100 sheet of conductors that resettles completely every step takes 93 µs
(about 9 ns a conductor; `SignalSolver.LargeGridCost` prints it).

The solver is built so it can move to its own thread if it ever needs to. It owns all of its
state and calls nothing outside itself. Its input between steps (edits and source levels) and its
output (the change list) are batches. A thread wrapper would take tick n's batch from the game
thread, step on its own thread while the game thread runs the rest of the tick, and hand back the
changes. To keep that move invisible to a game, the binding applies step n's changes at the start
of tick n+1 even when it steps inline. Node ids are handed out at once and reused
last-freed-first, so a wrapper can allocate them on the game thread.

## Binding: blocks as a circuit

`SignalBinding` (`signal_binding.hpp`, a game-thread subsystem) keeps a node for every block with a
signal role, and for every relay block next to one, links them by the rules below, and steps the
solver once a tick. A game turns it on with `signal_max_level: N` in `game.conf` (N is the
strongest level; without it signal keys do nothing). It learns of block changes from `World`
through a `BlockObserver`, so whoever changes a block needn't know about it: `World::setBlock`,
`setBlocks`, `BlockContext::setBlock`, columns joining and leaving.

Each tick: show the levels the last step made (as block states), take in the block changes
since, step. A change between two states of the same shape (the binding showing a level, a
player changing a delay) changes the node's level or spec and keeps its links and state. Any
other change rebuilds the nodes in the block's neighbourhood (its faces and edges).

### Declaring a block

In `.model`, applying to every state; a `set:` line overrides any of them per state, with
lists comma-separated (`set:powered=true: signal-level=15`).

| Key | Meaning |
|-----|---------|
| `signal:` | `source`, `conductor`, `component` or `sink` |
| `signal-group:` | conductor: joins conductors of the same group beside it |
| `signal-loss:` | conductor: level lost to each neighbour (1) |
| `signal-climb: true` | conductor: also joins its group one block up or down beside it, unless an opaque cube cuts the step (over the lower one going up, beside it going down) |
| `signal-level:` | source: the level it puts out (0) |
| `signal-op:` `signal-delay:` `signal-min-pulse:` `signal-full:` | component: as `SignalComponentSpec` |
| `signal-in:` `signal-side:` `signal-hold:` | component: faces feeding ports A, B and Hold. A sink reads `signal-in`. |
| `signal-in-from:` (and `-side-`, `-hold-`) | which neighbours a port reads: `conductor source component relay` (all) |
| `signal-out:` | faces it drives. A conductor drives only sinks this way. |
| `signal-power:<class>:` | faces whose relay block it powers, as that class |
| `signal-reads:` | relay classes it reads (all) |
| `signal-show:` | property that shows its level: a number property gets the level (or the highest value under it), any other gets `true` or `false` |
| `signal-joins:` `signal-join-values:` | conductor: four properties (north east south west) showing where it joins, and their three values: not joined, joined, joined one block up |
| `signal-facing:` | property that gives `front` (`facing`) |
| `signal-join-faces:` | sink: faces on which it joins sinks of its group, so the halves of a two-block thing act as one |
| `signal-show-edges: true` | only a change of level changes the shown property, so it can also be set by hand, and a block found with it set keeps it (a door) |
| `signal-show-hold:` | component: property showing whether its Hold port is on (`true`/`false`) |
| `signal-notify: true` | report its level changes: `SignalBinding::setChangeCallback`, which a game runtime turns into `on :signal` (`signal.x y z level`). Blocks the handler changes are taken in that same tick. |

Faces are `north south east west up down`, `sides` (the four around), `all`, `front back left
right` (relative to the facing; left and right as seen looking out of the front, from above),
and for conductors `points`.

### How neighbours connect

- **Conductor to conductor**, same group, beside each other: linked both ways with the loss.
- **A driver** (source or component) to the conductor, sink or component port its `signal-out`
  faces: linked or connected at its full level.
- **A conductor to a component port** facing it: always. **To a sink**: only through its
  `signal-out` faces.
- **Relays.** An opaque full cube with no role, next to any signal block, gets a node per relay
  class (a game turns this off with `signal_relays: none`). A block powers it through
  `signal-power:<class>` faces; it feeds every neighbour that reads that class (`signal-reads`)
  and has a port or sink input facing it. Two classes are enough for "a signal into a block
  passes on to wires" against "it passes on only to components".
- **Joins** (what a conductor shows): conductors of its group beside it or a step up or down,
  and drivers whose output or ports face it. **`points`**: a conductor joined on nothing points
  all four ways; joined along one line, both ways along it; otherwise nowhere.

### Scripts

`signal_level x y z` is a block's level (0 out of circuit); `signal_set x y z level` sets a
source's level until its block changes. `on :signal` hears `signal-notify` blocks change.
`world_move_blocks x y z dx dy dz n` moves a line of n blocks one cell (`World::moveBlocks`, with
rotation and block data), for things a signal pushes or pulls.

### Not yet

- **Saving.** Conductor levels come back from the blocks. A component the world shows as on
  (its `signal-show` property) starts on; what was on its way out of it, and latch and toggle
  state, is lost.
- **Unloaded columns.** A column leaving the world drops its nodes; links into it from its
  neighbours go with them, and come back when it returns. Circuits aren't frozen as a whole.
- **Columns from a `ColumnManager`** don't reach the binding: it sees the columns `World` holds.
- **Showing levels** costs a block change (and a remesh) per shown change, so a fast clock
  remeshes its subchunks every step.

## Gotchas

- Edits take effect at the next `step()`, never in the middle of one. Don't call into the solver
  from inside a step.
- `link()` only feeds conductors. A component reads through `connect()`.
- The change list holds levels, not edges. A component that turns on and off within one step
  can't happen, because its output changes only when something it scheduled comes due.
