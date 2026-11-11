# System: Task Board

**Library:** `finevox` core (`finevox::TaskBoard`), script natives in `finevox_game`
**Headers:** `include/finevox/core/task_board.hpp`
**Natives:** `src/game/task_board_natives.cpp`
**Tests:** `tests/test_task_board.cpp`, and `ABoardOffersTheLayerBeingBuiltToWhoeverIsNearest` in
`tests/test_game_runtime.cpp`

---

## Why

A game with workers has to decide who does which job. The easy way is for each worker to take
the nearest free job when it asks, and keep it. That's greedy, and it goes wrong in ways that
players notice:

- A worker far off claims a job, and one standing beside it with what the job needs waits.
- Two workers each take the job nearest them, when swapping would save both a long walk.
- A job is held by a worker who's on a long errand to fetch what it needs, while another worker
  already has it in hand.
- A worker waits on one site while another site has work it could do now.

A task board sees all the workers and all the jobs at once, and gives each worker the job that
makes the total cost least. It re-solves as things change, so a job goes to whoever is best placed
now, not whoever asked first.

The board knows nothing about any game. It deals in tasks, agents, resources and sources. Which
tasks exist, which are ready, and what matters more is the game's to say.

---

## The Model

**Tasks** have a key, a position, a resource they need (or none), and a priority. The caller can
also attach a group and a tag of its own.

**Agents** are entities. Each has a position, read from the entity, and:

- `holds`: a resource in hand, or none
- `fetches`: may go to a source for what a task needs
- `onlyHolds`: holding something, it takes only tasks that use it. A carrier delivers, but it
  doesn't do other jobs while its load waits.
- `committed`: already at work on its task, which it keeps
- `speed`: costs are divided by it

**Sources** are where each resource can be fetched from: a list of positions per resource.

### Cost

Cost is travel in blocks: horizontal distance, plus vertical distance times a weight (2 by
default), divided by the agent's speed. Straight lines, not paths: a lower bound that's cheap
enough to work out for every pair.

| Agent | Task needs nothing, or what it holds | Task needs something else |
|---|---|---|
| holds nothing, fetches | walk to the task | walk to the best source, then to the task |
| holds nothing, doesn't fetch | walk to the task | can't |
| holds something | walk to the task (not with `onlyHolds`, for a task needing nothing) | can't |

Priority is in the same units, subtracted from the cost: a task with priority 10 is worth walking
10 blocks further for. Ordering sites in a queue, a game gives each a priority by its place in it.

### Keeping it steady

Re-solving every half second would move agents back and forth over small differences, and each
move throws away a walk already begun. Two things stop that:

- A **committed** agent keeps its task, and is left out of the solve along with that task. A
  worker in the middle of the job, or nearly there, is committed.
- **Stick**: the task an agent already has costs it `stick` blocks less (3 by default). A new solve
  moves it only for a real gain.

### The solve

Agents against tasks is the assignment problem, which the Hungarian method solves exactly in
O(n²m) for n agents and m tasks. Each agent also gets a column of its own for "nothing", so an agent
can be left idle, and a task it can't do costs more than idling. 100 agents on 300 tasks solve
in about 0.2 ms on an M2 Max. `NoAssignmentCostsLessThanTheOneItFinds` checks 400 random boards
against every assignment there is.

Between solves, `take` gives a free agent the cheapest task nobody has. That's greedy, but it
means an agent that has just finished needn't wait for the next solve, and the next solve fixes
anything greedy got wrong.

---

## Construction Sites

A board can be given construction sites (see `BlueprintSite` in [worldgen.md](worldgen.md)) as
well as tasks of the script's own. Each unfinished cell of a site's layer being built (the only
layer `claimNext` would hand out) becomes a task. At every solve the cells are read again, so a
built cell goes and the next layer appears without the script copying anything. A cell's need is
the resource its block costs, from the map given to `board_resources`. Air (a cell to clear), and
any block not in that map, needs nothing.

Each site has a priority. It can also have a list of the resources it may have for now: a cell
costing anything else isn't offered. A game uses this to keep something scarce for the sites at
the front of the queue.

A board doesn't use the site's own claims (`site_claim`). It holds the assignment itself.

---

## Script API

```
board_create -> id         board_remove id        board_alive id -> bool
board_options id [=stick blocks] [=vertical weight]
board_resources id {=block resource ...}      what each block costs to place
board_sites id [[site priority [resource ...]] ...]
                           these sites' layers being built are tasks, replacing the last list
board_post id key x y z [=needs resource] [=priority p]     a task of the script's own
board_unpost id key
board_sources id resource [[x y z] ...]       where it can be fetched, replacing the last list
board_agent id entity [=holds r] [=fetches b] [=only_holds b] [=committed b] [=speed s]
board_drop id entity       off the board, and its task freed
board_release id entity    it has no task now, and stays on the board
board_solve id -> {=agents =tasks =assigned =micros}
board_task id entity -> its task as the last solve left it, or nil
board_take id entity -> its task, or the best one nobody has, or nil
```

A task comes back as `{=key =x =y =z =site =block}` for a site cell (`:air` to clear it), or
`{=key =x =y =z =needs}` for one the script posted. `board_task` and `board_take` check a site
cell against the world, and drop it if it's already built.

`board_take` with nothing to give reads the sites again once before giving up, so a layer finished
since the last solve doesn't leave agents idle until the next one.

Boards aren't saved. A game that resumes makes new ones, as it does its claims. Board ids start
again from 1 in a new process, so a saved id can name another board: forget them on resume.

---

## How Keystone Uses It

Keystone's Hands build through one board per side (`scripts/jobs.fsc`, "The task board"):

- Its tasks are the layers being built of the yard's next floor, the scaffold to take down, and
  each queued building, in that order of priority. A later building is offered a material only
  while there's more of it, in the yard and in hand, than the buildings ahead need for their layers
  being built.
- Builders fetch, from the yard slot nearest the Hall for each material. Gatherers carrying a load
  are carriers (`only_holds`).
- A Hand placing its block, climbing scaffolding to it, or within 3 blocks of it is committed.
- It solves every 15 ticks. A Hand checks its task as it walks. If a solve gave the cell to someone
  better placed, it drops what it was doing and takes whatever the board has for it now. A Hand
  taking a load to the yard, or waiting with one, goes to any cell the board gives it.

With four Shelters queued at once, that finished all four in 390 to 400 seconds, in three runs out
of three. Claiming the nearest cell finished three of the four in 470 seconds.

---

## What It Doesn't Do Yet

- **One task at a time.** An agent with three blocks in hand is assigned one cell, and asks again
  after placing it. Letting an agent take as many tasks as it holds would make it a capacitated
  assignment, which is still polynomial, as a min-cost flow.
- **Shared one-off jobs.** Scaffolding up to a high cell is work every agent going there does for
  itself. A task that one agent does and the others then use is a precedence between tasks, and
  isn't modelled.
- **Path costs.** Straight-line distance doesn't know about walls, water or cliffs. A cached
  distance field from each site would give real walking distances cheaply.
- **Supply as a flow.** Which load goes to which site, and what goes to the yard, is decided by the
  per-site resource lists and the agents' own rules. Solving it as a transportation problem, with
  sources and sinks, would put that in the board too.

Precedence and multi-step plans in general (fetch, then carry, then place, in the best order for a
whole team) are vehicle routing and scheduling, which are NP-hard. The board's answer is to solve
only each agent's next step, exactly, and solve it again often.
