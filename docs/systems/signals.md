# System: Signals

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/signal_solver.hpp`
**Tests:** `tests/test_signal_solver.cpp`

---

## Overview

Blocks that carry a signal (wires, switches, gates, lamps, machines that react to power) are a
circuit, and the engine solves circuits as circuits. It doesn't ripple block-update events from
block to block. `SignalSolver` holds the circuit as a graph and advances it one step at a time,
one step per game tick. Blocks give it configuration (what they are and how they connect) and get
results back (which levels changed).

The solver knows nothing about blocks, positions or the world. A binding layer turns blocks into
nodes and results into block states. It's the next piece of work: [Binding (planned)](#binding-planned).

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
pre-generated world of 64 × 64 columns, at 30 ticks a second, used about 7.7 ms of each 33 ms
tick. Most of that is two costs that can be fixed:

- 5.3 ms in `UpdateScheduler::generateGameTickEvents`, which lists every subchunk in the world
  each tick to find the blocks that asked for game ticks.
- 2.1 ms in entities, mostly physics waiting on `BlockRegistry::getType`'s lock (see
  ARCHITECTURE.md).

Against that, a 100 × 100 sheet of conductors that resettles completely every step takes 93 µs
(about 9 ns a conductor; `SignalSolver.LargeGridCost` prints it).

The solver is built so it can move to its own thread if it ever needs to. It owns all of its
state and calls nothing outside itself. Its input between steps (edits and source levels) and its
output (the change list) are batches. A thread wrapper would take tick n's batch from the game
thread, step on its own thread while the game thread runs the rest of the tick, and hand back the
changes. To keep that move invisible to a game, the binding applies step n's changes at the start
of tick n+1 even when it steps inline. Node ids are handed out at once and reused
last-freed-first, so a wrapper can allocate them on the game thread.

## Binding (planned)

The binding turns blocks into nodes and results into block states. Proposed shape:

- **Declared in `.model`.** A block says what it is to the solver: source, conductor (with a
  group, a loss per block, and the faces it joins on), component (op, delay, which faces feed
  ports A, B and Hold, which face it drives), or a block that only shows a level. Values can
  depend on state through `set:` lines, so a block with a `delay` property can have a delay per
  state.
- **Relays.** A block can pass a signal driven into it on to its neighbours without being a wire.
  A game can declare more than one kind of relay, each feeding a different set of neighbours.
- **Results as state.** A node's level can set a property (`powered=true`, or the level itself),
  coalesced to one block change per node per step. Changing the shown state doesn't change what
  the block is to the solver, so it causes no graph edit.
- **Sources from scripts** (`signal_set pos level`) and **consumers to scripts** (an `on :signal`
  hook), for anything the declarations can't express.
- **Saving.** Conductor levels are recomputed on load. What's on its way out of a component, and
  latch and toggle state, are saved per position, as `TickJournal` saves pending ticks.
- **Unloaded columns.** A circuit that reaches into a column that isn't loaded is frozen as a
  whole until it is ("network quiescence" in the ROADMAP).
- **Showing levels.** Every distinct shown level is a block state, so a fast clock remeshes its
  subchunks every step. A per-state `tint-color` is needed so a conductor can show its level.

## Gotchas

- Edits take effect at the next `step()`, never in the middle of one. Don't call into the solver
  from inside a step.
- `link()` only feeds conductors. A component reads through `connect()`.
- The change list holds levels, not edges. A component that turns on and off within one step
  can't happen, because its output changes only when something it scheduled comes due.
