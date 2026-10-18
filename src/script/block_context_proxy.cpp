#include "finevox/script/block_context_proxy.hpp"
#include "finevox/script/data_container_proxy.hpp"
#include "finevox/core/string_interner.hpp"
#include <finescript/map_data.h>
#include <finescript/native_function.h>

namespace finevox::script {

// Pre-interned field IDs (initialized once, never change)
struct BlockContextFields {
    uint32_t pos;
    uint32_t block_type;
    uint32_t rotation;
    uint32_t is_air;
    uint32_t is_opaque;
    uint32_t is_transparent;
    uint32_t previous_type;
    uint32_t sky_light;
    uint32_t block_light;
    uint32_t combined_light;
    uint32_t data;

    // Action methods callable via ctx.method_name
    uint32_t notify_neighbors;
    uint32_t schedule_tick;
    uint32_t set_repeat_tick;
    uint32_t set_rotation;
    uint32_t set_block;
    uint32_t get_neighbor;
    uint32_t neighbor_pos;
    uint32_t request_rebuild;

    // Face symbols for get_neighbor / neighbor_pos
    uint32_t pos_x, neg_x, pos_y, neg_y, pos_z, neg_z;

    static const BlockContextFields& instance() {
        static BlockContextFields fields = [] {
            auto& si = StringInterner::global();
            return BlockContextFields{
                si.intern("pos"),
                si.intern("block_type"),
                si.intern("rotation"),
                si.intern("is_air"),
                si.intern("is_opaque"),
                si.intern("is_transparent"),
                si.intern("previous_type"),
                si.intern("sky_light"),
                si.intern("block_light"),
                si.intern("combined_light"),
                si.intern("data"),
                si.intern("notify_neighbors"),
                si.intern("schedule_tick"),
                si.intern("set_repeat_tick"),
                si.intern("set_rotation"),
                si.intern("set_block"),
                si.intern("get_neighbor"),
                si.intern("neighbor_pos"),
                si.intern("request_rebuild"),
                si.intern("pos_x"),
                si.intern("neg_x"),
                si.intern("pos_y"),
                si.intern("neg_y"),
                si.intern("pos_z"),
                si.intern("neg_z"),
            };
        }();
        return fields;
    }

    bool toFace(uint32_t sym, Face& out) const {
        if (sym == pos_x)      { out = Face::PosX; return true; }
        if (sym == neg_x)      { out = Face::NegX; return true; }
        if (sym == pos_y)      { out = Face::PosY; return true; }
        if (sym == neg_y)      { out = Face::NegY; return true; }
        if (sym == pos_z)      { out = Face::PosZ; return true; }
        if (sym == neg_z)      { out = Face::NegZ; return true; }
        return false;
    }

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

    std::vector<uint32_t> allKeys() const {
        return {pos, block_type, rotation, is_air, is_opaque,
                is_transparent, previous_type, sky_light, block_light,
                combined_light, data};
    }
};

BlockContextProxy::BlockContextProxy(BlockContext& ctx)
    : ctx_(ctx) {}

finescript::Value BlockContextProxy::get(uint32_t key) const {
    const auto& f = BlockContextFields::instance();

    if (key == f.pos) {
        auto p = ctx_.pos();
        auto arr = std::vector<finescript::Value>{
            finescript::Value::integer(p.x),
            finescript::Value::integer(p.y),
            finescript::Value::integer(p.z)
        };
        return finescript::Value::array(std::move(arr));
    }
    if (key == f.block_type) {
        return finescript::Value::symbol(ctx_.blockType().id);
    }
    if (key == f.rotation) {
        return finescript::Value::integer(ctx_.rotationIndex());
    }
    if (key == f.is_air) {
        return finescript::Value::boolean(ctx_.isAir());
    }
    if (key == f.is_opaque) {
        return finescript::Value::boolean(ctx_.isOpaque());
    }
    if (key == f.is_transparent) {
        return finescript::Value::boolean(ctx_.isTransparent());
    }
    if (key == f.previous_type) {
        return finescript::Value::symbol(ctx_.previousType().id);
    }
    if (key == f.sky_light) {
        return finescript::Value::integer(ctx_.skyLight());
    }
    if (key == f.block_light) {
        return finescript::Value::integer(ctx_.blockLight());
    }
    if (key == f.combined_light) {
        return finescript::Value::integer(ctx_.combinedLight());
    }
    if (key == f.data) {
        return finescript::Value::proxyMap(std::make_shared<DataContainerProxy>(ctx_));
    }

    // Action methods on ctx (invoked via ctx.set_rotation, ctx.request_rebuild, etc.)
    BlockContext* ctxPtr = &ctx_;
    if (key == f.notify_neighbors) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>&) {
                ctxPtr->notifyNeighbors();
                return finescript::Value::nil();
            }));
    }
    if (key == f.schedule_tick) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
                if (!args.empty() && args[0].isInt()) {
                    ctxPtr->scheduleTick(static_cast<int>(args[0].asInt()));
                }
                return finescript::Value::nil();
            }));
    }
    if (key == f.set_repeat_tick) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
                if (!args.empty() && args[0].isInt()) {
                    ctxPtr->setRepeatTickInterval(static_cast<int>(args[0].asInt()));
                }
                return finescript::Value::nil();
            }));
    }
    if (key == f.set_rotation) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
                if (!args.empty() && args[0].isInt()) {
                    ctxPtr->setRotationIndex(static_cast<uint8_t>(args[0].asInt()));
                }
                return finescript::Value::nil();
            }));
    }
    if (key == f.set_block) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
                if (!args.empty() && args[0].isSymbol()) {
                    ctxPtr->setBlock(BlockTypeId(args[0].asSymbol()));
                }
                return finescript::Value::nil();
            }));
    }
    if (key == f.get_neighbor) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
                if (args.empty() || !args[0].isSymbol()) return finescript::Value::nil();
                Face face;
                if (!BlockContextFields::instance().toFace(args[0].asSymbol(), face)) {
                    return finescript::Value::nil();
                }
                BlockTypeId neighbor = ctxPtr->getNeighbor(face);
                return finescript::Value::symbol(neighbor.id);
            }));
    }
    if (key == f.neighbor_pos) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
                if (args.empty() || !args[0].isSymbol()) return finescript::Value::nil();
                int dx, dy, dz;
                if (!BlockContextFields::instance().toOffset(args[0].asSymbol(), dx, dy, dz)) {
                    return finescript::Value::nil();
                }
                auto p = ctxPtr->pos();
                return finescript::Value::array({
                    finescript::Value::integer(p.x + dx),
                    finescript::Value::integer(p.y + dy),
                    finescript::Value::integer(p.z + dz)
                });
            }));
    }
    if (key == f.request_rebuild) {
        return finescript::Value::nativeFunction(std::make_shared<finescript::SimpleLambdaFunction>(
            [ctxPtr](finescript::ExecutionContext&, const std::vector<finescript::Value>&) {
                ctxPtr->requestMeshRebuild();
                return finescript::Value::nil();
            }));
    }

    return finescript::Value::nil();
}

void BlockContextProxy::set(uint32_t key, finescript::Value value) {
    const auto& f = BlockContextFields::instance();

    if (key == f.rotation && value.isInt()) {
        ctx_.setRotationIndex(static_cast<uint8_t>(value.asInt()));
    } else if (key == f.data && value.isMap()) {
        DataContainer& dc = ctx_.getOrCreateData();
        dc.clear();
        DataContainerProxy proxy(dc);
        const auto& map = value.asMap();
        for (uint32_t k : map.keys()) {
            proxy.set(k, map.get(k));
        }
    }
}

bool BlockContextProxy::has(uint32_t key) const {
    const auto& f = BlockContextFields::instance();

    return key == f.pos || key == f.block_type || key == f.rotation ||
           key == f.is_air || key == f.is_opaque || key == f.is_transparent ||
           key == f.previous_type || key == f.sky_light ||
           key == f.block_light || key == f.combined_light || key == f.data ||
           key == f.notify_neighbors || key == f.schedule_tick ||
           key == f.set_repeat_tick || key == f.set_rotation ||
           key == f.set_block || key == f.get_neighbor ||
           key == f.neighbor_pos || key == f.request_rebuild;
}

bool BlockContextProxy::remove(uint32_t /*key*/) {
    return false;
}

std::vector<uint32_t> BlockContextProxy::keys() const {
    return BlockContextFields::instance().allKeys();
}

}  // namespace finevox::script
