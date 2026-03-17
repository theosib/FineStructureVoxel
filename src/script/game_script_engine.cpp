#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/entity_context_proxy.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/ai_goals.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/pathfinder.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include <finescript/map_data.h>
#include <iostream>

namespace finevox::script {

// Pre-interned face symbol IDs for fast lookup in native functions
struct FaceSymbols {
    uint32_t pos_x, neg_x, pos_y, neg_y, pos_z, neg_z;

    static const FaceSymbols& instance() {
        static FaceSymbols syms = [] {
            auto& si = StringInterner::global();
            return FaceSymbols{
                si.intern("pos_x"), si.intern("neg_x"),
                si.intern("pos_y"), si.intern("neg_y"),
                si.intern("pos_z"), si.intern("neg_z"),
            };
        }();
        return syms;
    }

    /// Convert a face symbol ID to Face enum. Returns false if not a face.
    bool toFace(uint32_t sym, Face& out) const {
        if (sym == pos_x)      { out = Face::PosX; return true; }
        if (sym == neg_x)      { out = Face::NegX; return true; }
        if (sym == pos_y)      { out = Face::PosY; return true; }
        if (sym == neg_y)      { out = Face::NegY; return true; }
        if (sym == pos_z)      { out = Face::PosZ; return true; }
        if (sym == neg_z)      { out = Face::NegZ; return true; }
        return false;
    }

    /// Get the position offset for a face direction.
    bool toOffset(uint32_t sym, int& dx, int& dy, int& dz) const {
        dx = dy = dz = 0;
        if (sym == pos_x)      { dx = 1;  return true; }
        if (sym == neg_x)      { dx = -1; return true; }
        if (sym == pos_y)      { dy = 1;  return true; }
        if (sym == neg_y)      { dy = -1; return true; }
        if (sym == pos_z)      { dz = 1;  return true; }
        if (sym == neg_z)      { dz = -1; return true; }
        return false;
    }
};

// Helper: extract numeric value as double from int or float
static double toDouble(const finescript::Value& v) {
    if (v.isFloat()) return v.asFloat();
    if (v.isInt()) return static_cast<double>(v.asInt());
    return 0.0;
}

// Helper: extract numeric value as float from int or float
static float toFloatVal(const finescript::Value& v) {
    return static_cast<float>(toDouble(v));
}

// Helper: extract x,y,z from args (either [x y z] array or three ints)
static bool extractPos(const std::vector<finescript::Value>& args,
                       size_t startIdx, int& x, int& y, int& z) {
    if (startIdx < args.size() && args[startIdx].isArray()) {
        const auto& arr = args[startIdx].asArray();
        if (arr.size() < 3) return false;
        x = static_cast<int>(arr[0].asInt());
        y = static_cast<int>(arr[1].asInt());
        z = static_cast<int>(arr[2].asInt());
        return true;
    }
    if (startIdx + 2 < args.size()) {
        x = static_cast<int>(args[startIdx].asInt());
        y = static_cast<int>(args[startIdx + 1].asInt());
        z = static_cast<int>(args[startIdx + 2].asInt());
        return true;
    }
    return false;
}

// ============================================================================

GameScriptEngine::GameScriptEngine(World& world)
    : engine_(std::make_unique<finescript::ScriptEngine>())
    , cache_(*engine_)
    , world_(world)
{
    engine_->setInterner(&interner_);
    userData_.world = &world_;
    registerNativeFunctions();
    registerMobNativeFunctions();
    registerSpatialNativeFunctions();
}

GameScriptEngine::~GameScriptEngine() = default;

ScriptBlockHandler* GameScriptEngine::loadBlockScript(
    const std::string& scriptPath,
    const std::string& blockName)
{
    auto* script = cache_.load(scriptPath);
    if (!script) {
        std::cerr << "[GameScriptEngine] Failed to load script: "
                  << scriptPath << "\n";
        return nullptr;
    }

    auto ctx = std::make_unique<finescript::ExecutionContext>(*engine_);
    ctx->setUserData(&userData_);

    auto result = engine_->execute(*script, *ctx);
    if (!result.success) {
        std::cerr << "[GameScriptEngine] Script error in '"
                  << scriptPath << "': " << result.error << "\n";
        return nullptr;
    }

    auto handler = std::make_unique<ScriptBlockHandler>(
        blockName, *engine_, std::move(ctx));

    if (!handler->hasHandlers()) {
        return nullptr;
    }

    auto* ptr = handler.get();
    handlers_[blockName] = std::move(handler);
    return ptr;
}

void GameScriptEngine::reloadChangedScripts() {
    cache_.reloadChanged();
}

ScriptEntityHandler* GameScriptEngine::loadEntityScript(
    const std::string& scriptPath,
    const std::string& entityName)
{
    auto* script = cache_.load(scriptPath);
    if (!script) {
        std::cerr << "[GameScriptEngine] Failed to load entity script: "
                  << scriptPath << "\n";
        return nullptr;
    }

    auto ctx = std::make_unique<finescript::ExecutionContext>(*engine_);
    ctx->setUserData(&userData_);

    auto result = engine_->execute(*script, *ctx);
    if (!result.success) {
        std::cerr << "[GameScriptEngine] Entity script error in '"
                  << scriptPath << "': " << result.error << "\n";
        return nullptr;
    }

    auto handler = std::make_unique<ScriptEntityHandler>(
        entityName, *engine_, std::move(ctx));

    if (!handler->hasHandlers()) {
        return nullptr;
    }

    auto* ptr = handler.get();
    entityHandlers_[entityName] = std::move(handler);
    return ptr;
}

ScriptEntityHandler* GameScriptEngine::getEntityHandler(const std::string& entityName) {
    auto it = entityHandlers_.find(entityName);
    return (it != entityHandlers_.end()) ? it->second.get() : nullptr;
}

void GameScriptEngine::setEntityManager(EntityManager* em) {
    userData_.entityManager = em;
}

void GameScriptEngine::registerNativeFunctions() {
    // ========================================================================
    // ctx.* action functions
    // ========================================================================

    engine_->registerFunction("ctx.notify_neighbors",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->blockCtx) {
                ud->blockCtx->notifyNeighbors();
            }
            return finescript::Value::nil();
        });

    engine_->registerFunction("ctx.schedule_tick",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->blockCtx && !args.empty() && args[0].isInt()) {
                ud->blockCtx->scheduleTick(static_cast<int>(args[0].asInt()));
            }
            return finescript::Value::nil();
        });

    engine_->registerFunction("ctx.set_repeat_tick",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->blockCtx && !args.empty() && args[0].isInt()) {
                ud->blockCtx->setRepeatTickInterval(static_cast<int>(args[0].asInt()));
            }
            return finescript::Value::nil();
        });

    engine_->registerFunction("ctx.set_rotation",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->blockCtx && !args.empty() && args[0].isInt()) {
                ud->blockCtx->setRotationIndex(
                    static_cast<uint8_t>(args[0].asInt()));
            }
            return finescript::Value::nil();
        });

    engine_->registerFunction("ctx.set_block",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->blockCtx && !args.empty() && args[0].isSymbol()) {
                ud->blockCtx->setBlock(BlockTypeId(args[0].asSymbol()));
            }
            return finescript::Value::nil();
        });

    engine_->registerFunction("ctx.get_neighbor",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->blockCtx || args.empty() || !args[0].isSymbol()) {
                return finescript::Value::nil();
            }

            Face face;
            if (!FaceSymbols::instance().toFace(args[0].asSymbol(), face)) {
                return finescript::Value::nil();
            }

            BlockTypeId neighbor = ud->blockCtx->getNeighbor(face);
            return finescript::Value::symbol(neighbor.id);
        });

    engine_->registerFunction("ctx.neighbor_pos",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->blockCtx || args.empty() || !args[0].isSymbol()) {
                return finescript::Value::nil();
            }

            int dx, dy, dz;
            if (!FaceSymbols::instance().toOffset(args[0].asSymbol(), dx, dy, dz)) {
                return finescript::Value::nil();
            }

            auto p = ud->blockCtx->pos();
            auto arr = std::vector<finescript::Value>{
                finescript::Value::integer(p.x + dx),
                finescript::Value::integer(p.y + dy),
                finescript::Value::integer(p.z + dz)
            };
            return finescript::Value::array(std::move(arr));
        });

    engine_->registerFunction("ctx.request_rebuild",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->blockCtx) {
                ud->blockCtx->requestMeshRebuild();
            }
            return finescript::Value::nil();
        });

    // ========================================================================
    // world.* functions
    // ========================================================================

    engine_->registerFunction("world.get_block",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::nil();

            int x, y, z;
            if (!extractPos(args, 0, x, y, z)) return finescript::Value::nil();

            BlockTypeId type = ud->world->getBlock(BlockCoord{x, y, z});
            return finescript::Value::symbol(type.id);
        });

    engine_->registerFunction("world.set_block",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::nil();

            int x, y, z;
            uint32_t typeId;

            // [x y z] type  or  x y z type
            if (args.size() == 2 && args[0].isArray() && args[1].isSymbol()) {
                if (!extractPos(args, 0, x, y, z)) return finescript::Value::nil();
                typeId = args[1].asSymbol();
            } else if (args.size() >= 4 && args[3].isSymbol()) {
                if (!extractPos(args, 0, x, y, z)) return finescript::Value::nil();
                typeId = args[3].asSymbol();
            } else {
                return finescript::Value::nil();
            }

            ud->world->setBlock(BlockCoord{x, y, z}, BlockTypeId(typeId));
            return finescript::Value::nil();
        });

    engine_->registerFunction("world.is_air",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::nil();

            int x, y, z;
            if (!extractPos(args, 0, x, y, z)) return finescript::Value::nil();

            BlockTypeId type = ud->world->getBlock(BlockCoord{x, y, z});
            return finescript::Value::boolean(type.isAir());
        });

    // ========================================================================
    // fluid_* functions
    // ========================================================================

    // fluid_at(x, y, z) → symbol (fluid type name) or nil
    engine_->registerFunction("fluid_at",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::nil();

            int x, y, z;
            if (!extractPos(args, 0, x, y, z)) return finescript::Value::nil();

            FluidTypeId fid = ud->world->getFluid(BlockCoord{x, y, z});
            if (fid.isEmpty()) return finescript::Value::nil();
            return finescript::Value::symbol(fid.id);
        });

    // fluid_level(x, y, z) → int (0-15)
    engine_->registerFunction("fluid_level",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::nil();

            int x, y, z;
            if (!extractPos(args, 0, x, y, z)) return finescript::Value::nil();

            uint8_t level = ud->world->getFluidLevel(BlockCoord{x, y, z});
            return finescript::Value::integer(level);
        });

    // fluid_place(x, y, z, type_symbol, level) → bool
    engine_->registerFunction("fluid_place",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::boolean(false);

            int x, y, z;
            uint32_t typeId;
            int level = 15;

            // fluid_place(x, y, z, :type, level)  or  fluid_place([x,y,z], :type, level)
            if (args.size() >= 2 && args[0].isArray()) {
                if (!extractPos(args, 0, x, y, z)) return finescript::Value::boolean(false);
                if (!args[1].isSymbol()) return finescript::Value::boolean(false);
                typeId = args[1].asSymbol();
                if (args.size() >= 3) level = static_cast<int>(args[2].asInt());
            } else if (args.size() >= 4 && args[3].isSymbol()) {
                if (!extractPos(args, 0, x, y, z)) return finescript::Value::boolean(false);
                typeId = args[3].asSymbol();
                if (args.size() >= 5) level = static_cast<int>(args[4].asInt());
            } else {
                return finescript::Value::boolean(false);
            }

            FluidTypeId fid(typeId);
            if (fid.isEmpty()) return finescript::Value::boolean(false);

            uint8_t lvl = static_cast<uint8_t>(std::clamp(level, 1, 15));
            bool ok = ud->world->setFluid(BlockCoord{x, y, z}, fid, lvl);
            return finescript::Value::boolean(ok);
        });

    // fluid_remove(x, y, z) → bool
    engine_->registerFunction("fluid_remove",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::boolean(false);

            int x, y, z;
            if (!extractPos(args, 0, x, y, z)) return finescript::Value::boolean(false);

            bool ok = ud->world->removeFluid(BlockCoord{x, y, z});
            return finescript::Value::boolean(ok);
        });

    // fluid_set_level(x, y, z, level) → bool
    engine_->registerFunction("fluid_set_level",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world) return finescript::Value::boolean(false);

            int x, y, z;
            if (!extractPos(args, 0, x, y, z)) return finescript::Value::boolean(false);

            // Level is the last argument
            int levelArgIdx;
            if (args.size() >= 1 && args[0].isArray()) {
                levelArgIdx = 1;
            } else {
                levelArgIdx = 3;
            }
            if (levelArgIdx >= static_cast<int>(args.size()))
                return finescript::Value::boolean(false);

            int level = static_cast<int>(args[levelArgIdx].asInt());

            BlockCoord pos{x, y, z};
            FluidTypeId fid = ud->world->getFluid(pos);
            if (fid.isEmpty()) return finescript::Value::boolean(false);

            uint8_t lvl = static_cast<uint8_t>(std::clamp(level, 1, 15));
            bool ok = ud->world->setFluid(pos, fid, lvl);
            return finescript::Value::boolean(ok);
        });
}

// ============================================================================
// mob.* native functions
// ============================================================================

void GameScriptEngine::registerMobNativeFunctions() {

    // mob.health(entity_proxy) → float
    engine_->registerFunction("mob_health",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(ud->entityCtx->health());
            }
            return finescript::Value::nil();
        });

    // mob.set_health(hp)
    engine_->registerFunction("mob_set_health",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx && !args.empty()) {
                ud->entityCtx->setHealth(toFloatVal(args[0]));
            }
            return finescript::Value::nil();
        });

    // mob_max_health → float
    engine_->registerFunction("mob_max_health",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(ud->entityCtx->maxHealth());
            }
            return finescript::Value::nil();
        });

    // mob.position() → [x, y, z]
    engine_->registerFunction("mob_position",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                auto pos = ud->entityCtx->position();
                auto arr = std::vector<finescript::Value>{
                    finescript::Value::number(pos.x),
                    finescript::Value::number(pos.y),
                    finescript::Value::number(pos.z)
                };
                return finescript::Value::array(std::move(arr));
            }
            return finescript::Value::nil();
        });

    // mob.velocity() → [vx, vy, vz]
    engine_->registerFunction("mob_velocity",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                auto vel = ud->entityCtx->velocity();
                auto arr = std::vector<finescript::Value>{
                    finescript::Value::number(vel.x),
                    finescript::Value::number(vel.y),
                    finescript::Value::number(vel.z)
                };
                return finescript::Value::array(std::move(arr));
            }
            return finescript::Value::nil();
        });

    // mob.move_to(x, y, z)
    engine_->registerFunction("mob_move_to",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx) return finescript::Value::nil();

            double x, y, z;
            if (args.size() >= 3) {
                x = toDouble(args[0]);
                y = toDouble(args[1]);
                z = toDouble(args[2]);
            } else if (args.size() == 1 && args[0].isArray()) {
                const auto& arr = args[0].asArray();
                if (arr.size() < 3) return finescript::Value::nil();
                x = toDouble(arr[0]);
                y = toDouble(arr[1]);
                z = toDouble(arr[2]);
            } else {
                return finescript::Value::nil();
            }

            ud->entityCtx->moveTo(glm::dvec3(x, y, z));
            return finescript::Value::nil();
        });

    // mob_look_at x y z
    engine_->registerFunction("mob_look_at",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx) return finescript::Value::nil();

            double x, y, z;
            if (args.size() >= 3) {
                x = toDouble(args[0]);
                y = toDouble(args[1]);
                z = toDouble(args[2]);
            } else if (args.size() == 1 && args[0].isArray()) {
                const auto& arr = args[0].asArray();
                if (arr.size() < 3) return finescript::Value::nil();
                x = toDouble(arr[0]);
                y = toDouble(arr[1]);
                z = toDouble(arr[2]);
            } else {
                return finescript::Value::nil();
            }

            ud->entityCtx->lookAt(glm::dvec3(x, y, z));
            return finescript::Value::nil();
        });

    // mob.jump()
    engine_->registerFunction("mob_jump",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                ud->entityCtx->jump();
            }
            return finescript::Value::nil();
        });

    // mob.damage(amount)
    engine_->registerFunction("mob_damage",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx && !args.empty()) {
                float amount = toFloatVal(args[0]);
                EntityId source = INVALID_ENTITY_ID;
                if (args.size() >= 2 && args[1].isInt()) {
                    source = static_cast<EntityId>(args[1].asInt());
                }
                ud->entityCtx->damage(amount, source);
            }
            return finescript::Value::nil();
        });

    // mob.heal(amount)
    engine_->registerFunction("mob_heal",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx && !args.empty()) {
                ud->entityCtx->heal(toFloatVal(args[0]));
            }
            return finescript::Value::nil();
        });

    // mob.set_animation(anim_id)
    engine_->registerFunction("mob_set_animation",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx && !args.empty() && args[0].isInt()) {
                ud->entityCtx->setAnimation(
                    static_cast<uint8_t>(args[0].asInt()));
            }
            return finescript::Value::nil();
        });

    // mob.is_dead() → bool
    engine_->registerFunction("mob_is_dead",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::boolean(ud->entityCtx->isDead());
            }
            return finescript::Value::nil();
        });

    // mob.is_on_ground() → bool
    engine_->registerFunction("mob_is_on_ground",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::boolean(ud->entityCtx->isOnGround());
            }
            return finescript::Value::nil();
        });

    // mob.id() → integer
    engine_->registerFunction("mob_id",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::integer(
                    static_cast<int64_t>(ud->entityCtx->id()));
            }
            return finescript::Value::nil();
        });

    // mob.type() → symbol
    engine_->registerFunction("mob_type",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::symbol(ud->entityCtx->typeId().id);
            }
            return finescript::Value::nil();
        });

    // mob.spawn(type_name, x, y, z) → entity_id or nil
    engine_->registerFunction("mob_spawn",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityManager) return finescript::Value::nil();

            if (args.size() < 4) return finescript::Value::nil();
            if (!args[0].isSymbol() && !args[0].isString()) return finescript::Value::nil();

            std::string typeName;
            if (args[0].isSymbol()) {
                typeName = StringInterner::global().lookup(args[0].asSymbol());
            } else {
                typeName = args[0].asString();
            }

            auto typeId = EntityTypeId::fromName(typeName);
            if (typeId.isEmpty()) return finescript::Value::nil();

            float x = toFloatVal(args[1]);
            float y = toFloatVal(args[2]);
            float z = toFloatVal(args[3]);

            auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
            mob->setPosition(Vec3(x, y, z));
            EntityId id = ud->entityManager->spawnEntity(std::move(mob));
            return finescript::Value::integer(static_cast<int64_t>(id));
        });

    // mob.find_path(x, y, z) → bool (whether path was set)
    engine_->registerFunction("mob_find_path",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx || !ud->world) return finescript::Value::boolean(false);

            double x, y, z;
            if (args.size() >= 3) {
                x = toDouble(args[0]);
                y = toDouble(args[1]);
                z = toDouble(args[2]);
            } else if (args.size() == 1 && args[0].isArray()) {
                const auto& arr = args[0].asArray();
                if (arr.size() < 3) return finescript::Value::boolean(false);
                x = toDouble(arr[0]);
                y = toDouble(arr[1]);
                z = toDouble(arr[2]);
            } else {
                return finescript::Value::boolean(false);
            }

            auto pos = ud->entityCtx->position();
            auto he = ud->entityCtx->halfExtents();
            auto path = Pathfinder::findPath(
                *ud->world,
                glm::dvec3(pos.x, pos.y, pos.z),
                glm::dvec3(x, y, z),
                he.x * 2.0f, he.y * 2.0f);

            if (path && !path->empty()) {
                // Set move target to first waypoint
                auto& wp = path->front();
                ud->entityCtx->moveTo(glm::dvec3(
                    wp.pos.x + 0.5, wp.pos.y, wp.pos.z + 0.5));
                return finescript::Value::boolean(true);
            }
            return finescript::Value::boolean(false);
        });

    // mob.set_speed(multiplier)
    engine_->registerFunction("mob_set_speed",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx && !args.empty()) {
                ud->entityCtx->setSpeedMultiplier(toFloatVal(args[0]));
            }
            return finescript::Value::nil();
        });

    // mob_add_goal(goal_type_string, priority, params_map)
    engine_->registerFunction("mob_add_goal",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx || args.size() < 2) return finescript::Value::boolean(false);

            std::string goalType;
            if (args[0].isString()) {
                goalType = args[0].asString();
            } else if (args[0].isSymbol()) {
                goalType = StringInterner::global().lookup(args[0].asSymbol());
            } else {
                return finescript::Value::boolean(false);
            }

            int prio = static_cast<int>(args[1].asInt());

            // Optional params map (arg 2)
            const finescript::Value* paramsMap = nullptr;
            if (args.size() >= 3 && args[2].isMap()) {
                paramsMap = &args[2];
            }

            auto& si = StringInterner::global();
            auto getFloat = [&](const char* key, float def) -> float {
                if (!paramsMap) return def;
                auto symId = si.intern(key);
                const auto& map = paramsMap->asMap();
                if (map.has(symId)) return toFloatVal(map.get(symId));
                return def;
            };
            auto getInt = [&](const char* key, int def) -> int {
                if (!paramsMap) return def;
                auto symId = si.intern(key);
                const auto& map = paramsMap->asMap();
                if (map.has(symId)) return static_cast<int>(map.get(symId).asInt());
                return def;
            };

            std::unique_ptr<AIGoal> goal;
            if (goalType == "idle") {
                IdleGoalParams p;
                p.minDuration = getFloat("min_duration", p.minDuration);
                p.maxDuration = getFloat("max_duration", p.maxDuration);
                p.animSlot = getInt("anim_slot", p.animSlot);
                goal = std::make_unique<IdleGoal>(prio, p);
            } else if (goalType == "wander") {
                WanderGoalParams p;
                p.range = getFloat("range", p.range);
                p.maxTime = getFloat("max_time", p.maxTime);
                p.startChance = getFloat("start_chance", p.startChance);
                p.animSlot = getInt("anim_slot", p.animSlot);
                goal = std::make_unique<WanderGoal>(prio, p);
            } else if (goalType == "chase") {
                ChaseGoalParams p;
                p.maxRange = getFloat("max_range", p.maxRange);
                p.repathInterval = getFloat("repath_interval", p.repathInterval);
                p.damageMemory = getFloat("damage_memory", p.damageMemory);
                p.animSlot = getInt("anim_slot", p.animSlot);
                goal = std::make_unique<ChaseGoal>(prio, p);
            } else if (goalType == "attack") {
                AttackGoalParams p;
                p.animSlot = getInt("anim_slot", p.animSlot);
                p.rangeHysteresis = getFloat("range_hysteresis", p.rangeHysteresis);
                goal = std::make_unique<AttackGoal>(prio, p);
            } else if (goalType == "flee") {
                FleeGoalParams p;
                p.distance = getFloat("distance", p.distance);
                p.duration = getFloat("duration", p.duration);
                p.speedMult = getFloat("speed_mult", p.speedMult);
                p.damageMemory = getFloat("damage_memory", p.damageMemory);
                p.animSlot = getInt("anim_slot", p.animSlot);
                goal = std::make_unique<FleeGoal>(prio, p);
            } else if (goalType == "look_at_player") {
                LookAtPlayerGoalParams p;
                p.range = getFloat("range", p.range);
                p.duration = getFloat("duration", p.duration);
                goal = std::make_unique<LookAtPlayerGoal>(prio, p);
            } else if (goalType == "panic") {
                PanicGoalParams p;
                p.duration = getFloat("duration", p.duration);
                p.speedMult = getFloat("speed_mult", p.speedMult);
                p.wanderRange = getFloat("wander_range", p.wanderRange);
                p.damageMemory = getFloat("damage_memory", p.damageMemory);
                p.animSlot = getInt("anim_slot", p.animSlot);
                goal = std::make_unique<PanicGoal>(prio, p);
            } else {
                return finescript::Value::boolean(false);
            }

            ud->entityCtx->brain().addGoal(prio, std::move(goal));
            return finescript::Value::boolean(true);
        });

    // mob_clear_goals()
    engine_->registerFunction("mob_clear_goals",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                ud->entityCtx->brain().clear();
            }
            return finescript::Value::nil();
        });

    // mob_apply_impulse(vx, vy, vz)
    engine_->registerFunction("mob_apply_impulse",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx) return finescript::Value::nil();

            float vx = 0, vy = 0, vz = 0;
            if (args.size() >= 3) {
                vx = toFloatVal(args[0]);
                vy = toFloatVal(args[1]);
                vz = toFloatVal(args[2]);
            } else if (args.size() == 1 && args[0].isArray()) {
                const auto& arr = args[0].asArray();
                if (arr.size() >= 3) {
                    vx = toFloatVal(arr[0]);
                    vy = toFloatVal(arr[1]);
                    vz = toFloatVal(arr[2]);
                }
            }

            auto vel = ud->entityCtx->velocity();
            ud->entityCtx->setVelocity(vel + Vec3(vx, vy, vz));
            return finescript::Value::nil();
        });

    // mob_get_data(key) → value or nil
    engine_->registerFunction("mob_get_data",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx || args.empty()) return finescript::Value::nil();

            std::string key;
            if (args[0].isString()) key = args[0].asString();
            else if (args[0].isSymbol()) key = StringInterner::global().lookup(args[0].asSymbol());
            else return finescript::Value::nil();

            auto& extra = ud->entityCtx->getOrCreateEntityData();
            // Try common types
            auto fval = extra.get<float>(key, 0.0f);
            if (fval != 0.0f) return finescript::Value::number(fval);
            auto ival = extra.get<int64_t>(key, 0);
            if (ival != 0) return finescript::Value::integer(ival);
            auto sval = extra.get<std::string>(key, "");
            if (!sval.empty()) return finescript::Value::string(sval);
            return finescript::Value::nil();
        });

    // mob_set_data(key, value)
    engine_->registerFunction("mob_set_data",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx || args.size() < 2) return finescript::Value::nil();

            std::string key;
            if (args[0].isString()) key = args[0].asString();
            else if (args[0].isSymbol()) key = StringInterner::global().lookup(args[0].asSymbol());
            else return finescript::Value::nil();

            auto& extra = ud->entityCtx->getOrCreateEntityData();
            const auto& val = args[1];
            if (val.isFloat()) extra.set<float>(key, static_cast<float>(val.asFloat()));
            else if (val.isInt()) extra.set<int64_t>(key, val.asInt());
            else if (val.isString()) extra.set<std::string>(key, std::string(val.asString()));
            else if (val.isBool()) extra.set<int64_t>(key, val.asBool() ? 1 : 0);

            return finescript::Value::nil();
        });

    // mob_remove() — marks entity for removal
    engine_->registerFunction("mob_remove",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                ud->entityCtx->markForRemoval();
            }
            return finescript::Value::nil();
        });

    // mob_is_player() → bool
    engine_->registerFunction("mob_is_player",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::boolean(ud->entityCtx->isPlayerEntity());
            }
            return finescript::Value::boolean(false);
        });

    // mob_fall_velocity() → float (Y velocity at last landing, for fall damage)
    engine_->registerFunction("mob_fall_velocity",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(
                    ud->entityCtx->preLandingVelocityY());
            }
            return finescript::Value::number(0.0);
        });

    // mob_speed_multiplier() → float
    engine_->registerFunction("mob_speed_multiplier",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(ud->entityCtx->speedMultiplier());
            }
            return finescript::Value::number(1.0);
        });

    // mob_yaw() → float
    engine_->registerFunction("mob_yaw",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(ud->entityCtx->yaw());
            }
            return finescript::Value::number(0.0);
        });

    // mob_pitch() → float
    engine_->registerFunction("mob_pitch",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(ud->entityCtx->pitch());
            }
            return finescript::Value::number(0.0);
        });

    // mob_last_attacker() → entity_id or nil
    engine_->registerFunction("mob_last_attacker",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                EntityId attacker = ud->entityCtx->lastAttacker();
                if (attacker != INVALID_ENTITY_ID) {
                    return finescript::Value::integer(static_cast<int64_t>(attacker));
                }
            }
            return finescript::Value::nil();
        });

    // mob_time_since_damage() → float (seconds since last damage)
    engine_->registerFunction("mob_time_since_damage",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (ud && ud->entityCtx) {
                return finescript::Value::number(ud->entityCtx->timeSinceLastDamage());
            }
            return finescript::Value::number(999.0);
        });
}

// ============================================================================
// Spatial Query & LOS Native Functions
// ============================================================================

void GameScriptEngine::registerSpatialNativeFunctions() {

    // entities_in_radius(x, y, z, radius) → array of entity IDs
    engine_->registerFunction("entities_in_radius",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityManager || args.size() < 4)
                return finescript::Value::array(std::vector<finescript::Value>{});

            float x = toFloatVal(args[0]);
            float y = toFloatVal(args[1]);
            float z = toFloatVal(args[2]);
            float radius = toFloatVal(args[3]);

            auto ids = ud->entityManager->spatialIndex().queryRadius(
                Vec3(x, y, z), radius);

            std::vector<finescript::Value> result;
            result.reserve(ids.size());
            for (EntityId id : ids) {
                result.push_back(finescript::Value::integer(static_cast<int64_t>(id)));
            }
            return finescript::Value::array(std::move(result));
        });

    // entities_in_box(x1, y1, z1, x2, y2, z2) → array of entity IDs
    engine_->registerFunction("entities_in_box",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityManager || args.size() < 6)
                return finescript::Value::array(std::vector<finescript::Value>{});

            Vec3 min(toFloatVal(args[0]), toFloatVal(args[1]), toFloatVal(args[2]));
            Vec3 max(toFloatVal(args[3]), toFloatVal(args[4]), toFloatVal(args[5]));

            auto ids = ud->entityManager->spatialIndex().queryAABB(min, max);

            std::vector<finescript::Value> result;
            result.reserve(ids.size());
            for (EntityId id : ids) {
                result.push_back(finescript::Value::integer(static_cast<int64_t>(id)));
            }
            return finescript::Value::array(std::move(result));
        });

    // nearest_entity(x, y, z, radius) → entity ID or nil
    engine_->registerFunction("nearest_entity",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityManager || args.size() < 4)
                return finescript::Value::nil();

            float x = toFloatVal(args[0]);
            float y = toFloatVal(args[1]);
            float z = toFloatVal(args[2]);
            float radius = toFloatVal(args[3]);

            EntityId nearest = ud->entityManager->spatialIndex().findNearest(
                Vec3(x, y, z), radius);

            if (nearest != INVALID_ENTITY_ID) {
                return finescript::Value::integer(static_cast<int64_t>(nearest));
            }
            return finescript::Value::nil();
        });

    // mob_can_see(target_id) → bool (raycast between eye positions)
    engine_->registerFunction("mob_can_see",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->entityCtx || !ud->entityManager || !ud->world || args.empty())
                return finescript::Value::boolean(false);

            EntityId targetId = static_cast<EntityId>(args[0].asInt());
            Entity* target = ud->entityManager->getEntity(targetId);
            if (!target) return finescript::Value::boolean(false);

            Vec3 origin = ud->entityCtx->eyePosition();
            Vec3 targetPos = target->eyePosition();
            Vec3 dir = targetPos - origin;
            float dist = glm::length(dir);
            if (dist < 0.01f) return finescript::Value::boolean(true);
            dir /= dist;

            auto shapeProvider = createBlockShapeProvider(*ud->world);
            auto hit = raycastBlocks(origin, dir, dist, RaycastMode::Collision, shapeProvider);

            // Can see if no block was hit before reaching the target
            return finescript::Value::boolean(!hit.hit || hit.distance >= dist - 0.5f);
        });

    // raycast_blocks(x, y, z, dx, dy, dz, max_dist) → hit info map or nil
    engine_->registerFunction("raycast_blocks",
        [](finescript::ExecutionContext& ctx, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* ud = static_cast<ScriptUserData*>(ctx.userData());
            if (!ud || !ud->world || args.size() < 7)
                return finescript::Value::nil();

            Vec3 origin(toFloatVal(args[0]), toFloatVal(args[1]), toFloatVal(args[2]));
            Vec3 dir(toFloatVal(args[3]), toFloatVal(args[4]), toFloatVal(args[5]));
            float maxDist = toFloatVal(args[6]);

            float dirLen = glm::length(dir);
            if (dirLen < 0.001f) return finescript::Value::nil();
            dir /= dirLen;

            auto shapeProvider = createBlockShapeProvider(*ud->world);
            auto hit = raycastBlocks(origin, dir, maxDist, RaycastMode::Interaction, shapeProvider);

            if (!hit.hit) return finescript::Value::nil();

            auto& si = StringInterner::global();
            auto map = std::make_shared<finescript::MapData>();
            map->set(si.intern("x"), finescript::Value::integer(hit.blockPos.x));
            map->set(si.intern("y"), finescript::Value::integer(hit.blockPos.y));
            map->set(si.intern("z"), finescript::Value::integer(hit.blockPos.z));
            map->set(si.intern("distance"), finescript::Value::number(hit.distance));
            map->set(si.intern("hit_x"), finescript::Value::number(hit.hitPoint.x));
            map->set(si.intern("hit_y"), finescript::Value::number(hit.hitPoint.y));
            map->set(si.intern("hit_z"), finescript::Value::number(hit.hitPoint.z));
            map->set(si.intern("face"), finescript::Value::integer(static_cast<int64_t>(hit.face)));
            return finescript::Value::map(map);
        });
}

// ============================================================================
// Entity Script Loading & Hooks Provider
// ============================================================================

void GameScriptEngine::loadEntityScriptsFromRegistry() {
    EntityTypeRegistry::global().forEachType(
        [this](EntityTypeId /*id*/, const EntityTypeDef& def) {
            if (def.script.empty()) return;
            if (entityHandlers_.count(def.name)) return;  // Already loaded

            loadEntityScript(def.script, def.name);
        });
}

MobEventHooksProvider GameScriptEngine::createHooksProvider() {
    return [this](const std::string& typeName) -> MobEventHooks* {
        // Check if adapter already exists
        auto it = hooksAdapters_.find(typeName);
        if (it != hooksAdapters_.end()) {
            return it->second.get();
        }

        // Find handler for this type
        auto* handler = getEntityHandler(typeName);
        if (!handler) return nullptr;

        // Create adapter
        auto adapter = std::make_unique<ScriptMobEventHooks>(*handler);
        auto* ptr = adapter.get();
        hooksAdapters_[typeName] = std::move(adapter);
        return ptr;
    };
}

}  // namespace finevox::script
