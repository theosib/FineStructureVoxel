#pragma once

/**
 * @file data_container_proxy.hpp
 * @brief ProxyMap wrapping DataContainer for per-block persistent data
 *
 * Since DataContainer uses the same StringInterner as finescript (via
 * FineVoxInterner), the uint32_t keys map directly — zero overhead.
 *
 * Scripts access per-block data as: data.power_level, data.facing, etc.
 */

#include "finevox/core/data_container.hpp"
#include <finescript/proxy_map.h>
#include <finescript/value.h>

namespace finevox::script {

class DataContainerProxy : public finescript::ProxyMap {
public:
    explicit DataContainerProxy(DataContainer& container);

    finescript::Value get(uint32_t key) const override;
    void set(uint32_t key, finescript::Value value) override;
    bool has(uint32_t key) const override;
    bool remove(uint32_t key) override;
    std::vector<uint32_t> keys() const override;

private:
    DataContainer& container_;
};

}  // namespace finevox::script
