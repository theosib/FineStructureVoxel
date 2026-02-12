#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/string_interner.hpp"
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

            BlockTypeId type = ud->world->getBlock(BlockPos{x, y, z});
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

            ud->world->setBlock(BlockPos{x, y, z}, BlockTypeId(typeId));
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

            BlockTypeId type = ud->world->getBlock(BlockPos{x, y, z});
            return finescript::Value::boolean(type.isAir());
        });
}

}  // namespace finevox::script
