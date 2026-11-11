/**
 * @file task_board_natives.cpp
 * @brief Script access to task boards (docs/systems/task-board.md)
 *
 * A board's tasks are the script's own, posted by key, and the unfinished cells of the layer being
 * built on each construction site it's given. Site cells are read again at every solve, so a
 * finished cell goes and the next layer comes without the script copying anything across. Agents'
 * positions are read from their entities.
 *
 * Boards aren't saved: a game on resuming makes new ones, as it does its claims.
 */

#include "finevox/game/game_runtime.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/task_board.hpp"
#include "finevox/core/world.hpp"

#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <memory>
#include <unordered_set>

namespace finevox::game {

namespace {

using finescript::ExecutionContext;
using finescript::Value;
using Args = std::vector<Value>;
using Key = TaskBoard::Key;

double number(const Value& v, double fallback = 0.0) {
    return v.isNumeric() ? v.asNumber() : fallback;
}

Value option(const Args& args, size_t afterPositional, uint32_t key) {
    if (args.size() > afterPositional && args.back().isMap()) {
        return const_cast<Value&>(args.back()).asMap().get(key);
    }
    return Value::nil();
}

uint32_t symbolOf(const Value& v) {
    if (v.isSymbol()) return v.asSymbol();
    if (v.isString()) return StringInterner::global().intern(v.asString());
    return 0;
}

// A site cell's key: site, then position. The top bit marks the script's own keys.
constexpr Key SCRIPT_KEY = Key(1) << 63;

Key cellKey(int64_t site, BlockCoord pos) {
    return (static_cast<Key>(site & 0x7FFFF) << 44) |
           (static_cast<Key>((pos.x + 32768) & 0xFFFF) << 28) |
           (static_cast<Key>((pos.z + 32768) & 0xFFFF) << 12) |
           static_cast<Key>((pos.y + 2048) & 0xFFF);
}

struct SiteEntry {
    int64_t id = 0;
    double priority = 0.0;
    /// With a list, only cells costing one of these (or nothing) are work
    bool restricted = false;
    std::unordered_set<uint32_t> allowed;
};

struct Board {
    TaskBoard board;
    /// Block to the resource it costs. Blocks not here cost nothing.
    std::unordered_map<uint32_t, uint32_t> resourceOf;
    /// Sites whose layer being built is work
    std::vector<SiteEntry> sites;
    std::unordered_set<Key> siteKeys;
};

}  // namespace

void registerTaskBoardNatives(GameRuntime& runtime) {
    auto& e = runtime.scripts().engine();
    auto& si = StringInterner::global();
    GameRuntime* rt = &runtime;

    const uint32_t keyX = si.intern("x");
    const uint32_t keyY = si.intern("y");
    const uint32_t keyZ = si.intern("z");
    const uint32_t keyKey = si.intern("key");
    const uint32_t keySite = si.intern("site");
    const uint32_t keyBlock = si.intern("block");
    const uint32_t keyNeeds = si.intern("needs");
    const uint32_t keyAir = si.intern("air");
    const uint32_t keyHolds = si.intern("holds");
    const uint32_t keyFetches = si.intern("fetches");
    const uint32_t keyCommitted = si.intern("committed");
    const uint32_t keyOnlyHolds = si.intern("only_holds");
    const uint32_t keySpeed = si.intern("speed");
    const uint32_t keyPriority = si.intern("priority");
    const uint32_t keyStick = si.intern("stick");
    const uint32_t keyVertical = si.intern("vertical");
    const uint32_t keyAgents = si.intern("agents");
    const uint32_t keyTasks = si.intern("tasks");
    const uint32_t keyAssigned = si.intern("assigned");
    const uint32_t keyMicros = si.intern("micros");

    auto boards = std::make_shared<std::unordered_map<int64_t, Board>>();
    auto nextId = std::make_shared<int64_t>(1);

    auto boardArg = [boards](const Args& args) -> Board* {
        if (args.empty()) return nullptr;
        auto it = boards->find(static_cast<int64_t>(number(args[0])));
        return it == boards->end() ? nullptr : &it->second;
    };

    // The cells of each site's layer being built become its tasks, and those that went go
    auto refreshSites = [rt](Board& b) {
        const World& world = rt->session().world();
        std::unordered_set<Key> now;
        for (const auto& entry : b.sites) {
            auto it = rt->sites().find(entry.id);
            if (it == rt->sites().end()) continue;
            for (const auto& cell : it->second.remainingNow(world)) {
                TaskBoard::Task task;
                task.key = cellKey(entry.id, cell.pos);
                task.pos = glm::dvec3(cell.pos.x + 0.5, cell.pos.y, cell.pos.z + 0.5);
                auto cost = b.resourceOf.find(cell.block.id);
                task.need = (cell.block.isAir() || cost == b.resourceOf.end()) ? 0 : cost->second;
                if (task.need != 0 && entry.restricted && !entry.allowed.count(task.need)) continue;
                task.priority = entry.priority;
                task.group = entry.id;
                task.what = cell.block.isAir() ? 0 : cell.block.id;
                b.board.setTask(task);
                now.insert(task.key);
            }
        }
        for (Key key : b.siteKeys) {
            if (!now.count(key)) b.board.removeTask(key);
        }
        b.siteKeys = std::move(now);
    };

    auto refreshAgents = [rt](Board& b) {
        std::vector<EntityId> gone;
        for (const auto& [id, agent] : b.board.agents()) {
            const Entity* entity = rt->session().entities().getEntity(id);
            if (!entity || !entity->isAlive()) {
                gone.push_back(id);
                continue;
            }
            TaskBoard::Agent moved = agent;
            Vec3 p = entity->position();
            moved.pos = glm::dvec3(p.x, p.y, p.z);
            b.board.setAgent(moved);
        }
        for (EntityId id : gone) b.board.removeAgent(id);
    };

    // A site cell already built is done, whoever built it
    auto stillToDo = [rt](const TaskBoard::Task& task) {
        if (task.key & SCRIPT_KEY) return true;
        BlockCoord pos(static_cast<int32_t>(std::floor(task.pos.x)), static_cast<int32_t>(task.pos.y),
                       static_cast<int32_t>(std::floor(task.pos.z)));
        auto it = rt->sites().find(task.group);
        if (it == rt->sites().end()) return false;
        BlockTypeId want = task.what == 0 ? BlockTypeId() : BlockTypeId(task.what);
        return rt->session().world().getBlock(pos) != want;
    };

    auto describe = [=](const TaskBoard::Task& task) {
        auto result = Value::map();
        auto& map = result.asMap();
        map.set(keyKey, Value::integer(static_cast<int64_t>(task.key & ~SCRIPT_KEY)));
        if (task.key & SCRIPT_KEY) {
            map.set(keyX, Value::number(task.pos.x));
            map.set(keyY, Value::number(task.pos.y));
            map.set(keyZ, Value::number(task.pos.z));
            if (task.need != 0) map.set(keyNeeds, Value::symbol(task.need));
        } else {
            map.set(keyX, Value::integer(static_cast<int64_t>(std::floor(task.pos.x))));
            map.set(keyY, Value::integer(static_cast<int64_t>(task.pos.y)));
            map.set(keyZ, Value::integer(static_cast<int64_t>(std::floor(task.pos.z))));
            map.set(keySite, Value::integer(task.group));
            map.set(keyBlock, Value::symbol(task.what == 0 ? keyAir : task.what));
        }
        return result;
    };

    // board_create -> id. board_remove id. board_alive id -> bool
    e.registerFunction("board_create", [=](ExecutionContext&, const Args&) -> Value {
        int64_t id = (*nextId)++;
        boards->emplace(id, Board{});
        return Value::integer(id);
    });
    e.registerFunction("board_remove", [=](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty()) boards->erase(static_cast<int64_t>(number(args[0])));
        return Value::nil();
    });
    e.registerFunction("board_alive", [=](ExecutionContext&, const Args& args) -> Value {
        return Value::boolean(boardArg(args) != nullptr);
    });

    // board_options id [=stick blocks] [=vertical weight]
    e.registerFunction("board_options", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b) return Value::nil();
        Value stick = option(args, 1, keyStick);
        Value vertical = option(args, 1, keyVertical);
        if (stick.isNumeric()) b->board.setStick(stick.asNumber());
        if (vertical.isNumeric()) b->board.setVerticalWeight(vertical.asNumber());
        return Value::nil();
    });

    // board_resources id {=block resource ...}: what each block costs to place
    e.registerFunction("board_resources", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 2 || !args[1].isMap()) return Value::nil();
        b->resourceOf.clear();
        auto& map = const_cast<Value&>(args[1]).asMap();
        for (uint32_t block : map.keys()) {
            uint32_t kind = symbolOf(map.get(block));
            if (kind != 0) b->resourceOf[block] = kind;
        }
        return Value::nil();
    });

    // board_sites id [[site priority [resource ...]] ...]: these sites' layers being built are work.
    // With a list of resources, a cell costing anything else isn't, for now.
    e.registerFunction("board_sites", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 2 || !args[1].isArray()) return Value::nil();
        b->sites.clear();
        for (const Value& item : args[1].asArray()) {
            SiteEntry entry;
            if (item.isArray() && !item.asArray().empty()) {
                const auto& parts = item.asArray();
                entry.id = static_cast<int64_t>(number(parts[0]));
                if (parts.size() > 1) entry.priority = number(parts[1]);
                if (parts.size() > 2 && parts[2].isArray()) {
                    entry.restricted = true;
                    for (const Value& kind : parts[2].asArray()) entry.allowed.insert(symbolOf(kind));
                }
            } else if (item.isNumeric()) {
                entry.id = static_cast<int64_t>(item.asNumber());
            } else {
                continue;
            }
            b->sites.push_back(std::move(entry));
        }
        refreshSites(*b);
        return Value::nil();
    });

    // board_post id key x y z [=needs resource] [=priority p]: a task of the script's own
    e.registerFunction("board_post", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 5) return Value::nil();
        TaskBoard::Task task;
        task.key = SCRIPT_KEY | static_cast<Key>(number(args[1]));
        task.pos = glm::dvec3(number(args[2]), number(args[3]), number(args[4]));
        task.need = symbolOf(option(args, 5, keyNeeds));
        task.priority = number(option(args, 5, keyPriority));
        b->board.setTask(task);
        return Value::nil();
    });
    e.registerFunction("board_unpost", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (b && args.size() > 1) b->board.removeTask(SCRIPT_KEY | static_cast<Key>(number(args[1])));
        return Value::nil();
    });

    // board_sources id resource [[x y z] ...]: where it can be fetched from
    e.registerFunction("board_sources", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 3 || !args[2].isArray()) return Value::nil();
        std::vector<glm::dvec3> where;
        for (const Value& p : args[2].asArray()) {
            if (p.isArray() && p.asArray().size() >= 3) {
                const auto& xyz = p.asArray();
                where.emplace_back(number(xyz[0]), number(xyz[1]), number(xyz[2]));
            }
        }
        b->board.setSources(symbolOf(args[1]), std::move(where));
        return Value::nil();
    });

    // board_agent id entity [=holds resource] [=fetches b] [=only_holds b] [=committed b] [=speed s]
    e.registerFunction("board_agent", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 2) return Value::nil();
        auto id = static_cast<EntityId>(number(args[1]));
        const Entity* entity = rt->session().entities().getEntity(id);
        if (!entity || !entity->isAlive()) {
            b->board.removeAgent(id);
            return Value::nil();
        }
        TaskBoard::Agent agent;
        agent.id = id;
        Vec3 p = entity->position();
        agent.pos = glm::dvec3(p.x, p.y, p.z);
        agent.holds = symbolOf(option(args, 2, keyHolds));
        agent.fetches = option(args, 2, keyFetches).truthy();
        agent.committed = option(args, 2, keyCommitted).truthy();
        agent.onlyHolds = option(args, 2, keyOnlyHolds).truthy();
        agent.speed = number(option(args, 2, keySpeed), 1.0);
        b->board.setAgent(agent);
        return Value::nil();
    });

    // board_drop id entity: off the board, and its task free
    e.registerFunction("board_drop", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (b && args.size() > 1) b->board.removeAgent(static_cast<EntityId>(number(args[1])));
        return Value::nil();
    });

    // board_release id entity: it has no task now, and stays on the board
    e.registerFunction("board_release", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (b && args.size() > 1) b->board.release(static_cast<EntityId>(number(args[1])));
        return Value::nil();
    });

    // board_solve id -> {=agents =tasks =assigned =micros}
    e.registerFunction("board_solve", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b) return Value::nil();
        refreshSites(*b);
        refreshAgents(*b);
        auto stats = b->board.solve();
        auto result = Value::map();
        result.asMap().set(keyAgents, Value::integer(static_cast<int64_t>(stats.agents)));
        result.asMap().set(keyTasks, Value::integer(static_cast<int64_t>(stats.tasks)));
        result.asMap().set(keyAssigned, Value::integer(static_cast<int64_t>(stats.assigned)));
        result.asMap().set(keyMicros, Value::number(stats.micros));
        return result;
    });

    // board_task id entity -> its task as the last solve left it, or nil
    e.registerFunction("board_task", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 2) return Value::nil();
        auto id = static_cast<EntityId>(number(args[1]));
        auto key = b->board.assignment(id);
        if (!key) return Value::nil();
        const auto* task = b->board.task(*key);
        if (!task || !stillToDo(*task)) {
            if (task) b->board.removeTask(*key);
            return Value::nil();
        }
        return describe(*task);
    });

    // board_take id entity -> its task, or the best one nobody has, or nil
    e.registerFunction("board_take", [=](ExecutionContext&, const Args& args) -> Value {
        Board* b = boardArg(args);
        if (!b || args.size() < 2) return Value::nil();
        auto id = static_cast<EntityId>(number(args[1]));
        if (const Entity* entity = rt->session().entities().getEntity(id); entity && b->board.agent(id)) {
            TaskBoard::Agent moved = *b->board.agent(id);
            Vec3 p = entity->position();
            moved.pos = glm::dvec3(p.x, p.y, p.z);
            b->board.setAgent(moved);
        }
        // A layer finished since the last solve leaves no tasks till the next is read
        for (int attempt = 0; attempt < 2; ++attempt) {
            while (auto key = b->board.take(id)) {
                const auto* task = b->board.task(*key);
                if (task && stillToDo(*task)) return describe(*task);
                b->board.removeTask(*key);
            }
            if (attempt == 0) refreshSites(*b);
        }
        return Value::nil();
    });
}

}  // namespace finevox::game
