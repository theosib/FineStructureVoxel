#include "finevox/script/block_context_proxy.hpp"
#include "finevox/core/string_interner.hpp"

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
            };
        }();
        return fields;
    }

    std::vector<uint32_t> allKeys() const {
        return {pos, block_type, rotation, is_air, is_opaque,
                is_transparent, previous_type, sky_light, block_light,
                combined_light};
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

    return finescript::Value::nil();
}

void BlockContextProxy::set(uint32_t key, finescript::Value value) {
    const auto& f = BlockContextFields::instance();

    if (key == f.rotation && value.isInt()) {
        ctx_.setRotationIndex(static_cast<uint8_t>(value.asInt()));
    }
}

bool BlockContextProxy::has(uint32_t key) const {
    const auto& f = BlockContextFields::instance();

    return key == f.pos || key == f.block_type || key == f.rotation ||
           key == f.is_air || key == f.is_opaque || key == f.is_transparent ||
           key == f.previous_type || key == f.sky_light ||
           key == f.block_light || key == f.combined_light;
}

bool BlockContextProxy::remove(uint32_t /*key*/) {
    return false;
}

std::vector<uint32_t> BlockContextProxy::keys() const {
    return BlockContextFields::instance().allKeys();
}

}  // namespace finevox::script
