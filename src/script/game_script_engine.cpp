#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/entity_context_proxy.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/pathfinder.hpp"
#include "finevox/core/fluid_type_id.hpp"
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
}

}  // namespace finevox::script
