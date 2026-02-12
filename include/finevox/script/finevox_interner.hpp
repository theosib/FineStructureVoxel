#pragma once

/**
 * @file finevox_interner.hpp
 * @brief Adapter bridging finevox's StringInterner to finescript's Interner interface
 *
 * Ensures that symbol IDs are shared between the voxel engine and scripts,
 * so :stone in a script resolves to the same uint32_t as
 * StringInterner::global().intern("stone").
 */

#include "finevox/core/string_interner.hpp"
#include <finescript/interner.h>

namespace finevox::script {

class FineVoxInterner : public finescript::Interner {
public:
    uint32_t intern(std::string_view str) override {
        return StringInterner::global().intern(str);
    }

    std::string_view lookup(uint32_t id) const override {
        return StringInterner::global().lookup(id);
    }
};

}  // namespace finevox::script
