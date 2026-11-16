#pragma once

/**
 * @file data_container_proxy.hpp
 * @brief ProxyMap wrapping DataContainer for per-block persistent data
 *
 * Since DataContainer uses the same StringInterner as finescript (via
 * FineVoxInterner), the uint32_t keys map directly — zero overhead.
 *
 * Scripts access per-block data as: data.power_level, data.facing, etc.
 *
 * Over a block's data, the proxy holds no pointer into the world: it finds the cell's data (and
 * the nested map it stands for) every time it's used. A script that keeps one past the block, or
 * past the event it was given in, reads whatever that cell holds then, or nil. A nested map read
 * from it is live in the same way; a list read from it is a copy.
 */

#include "finevox/core/data_container.hpp"
#include "finevox/core/position.hpp"
#include <finescript/proxy_map.h>
#include <finescript/value.h>
#include <optional>
#include <vector>

namespace finevox {
class BlockContext;
class World;
}

namespace finevox::script {

class DataContainerProxy : public finescript::ProxyMap {
public:
    /// Over a container its owner keeps alive for as long as the proxy is used
    explicit DataContainerProxy(DataContainer& container);

    /// Over the data of the block at `cell` (or the map at `path` inside it). Writing creates
    /// the block's data if it has none, and marks its column for saving.
    DataContainerProxy(World& world, BlockCoord cell, std::vector<DataKey> path = {});

    /// The data of the block a handler is running for
    explicit DataContainerProxy(BlockContext& blockCtx);

    finescript::Value get(uint32_t key) const override;
    void set(uint32_t key, finescript::Value value) override;
    bool has(uint32_t key) const override;
    bool remove(uint32_t key) override;
    std::vector<uint32_t> keys() const override;

    /// A stored value as a script gets it, copied all the way down: maps as plain maps
    [[nodiscard]] static finescript::Value copyOut(const DataValue& value);
    [[nodiscard]] static finescript::Value copyOut(const DataContainer& container);

    /// A script value as it's stored, or nothing for one that can't be (a function). Nil is
    /// monostate. An array of only integers, only numbers or only strings is a typed vector;
    /// any other (nil, bools, symbols, mixed, nested) is a DataList.
    [[nodiscard]] static std::optional<DataValue> toData(const finescript::Value& value);

private:
    [[nodiscard]] const DataContainer* find() const;
    /// The container to write to, made at the top level if it isn't there. Null if a nested map
    /// this proxy stood for has gone: writing to it then does nothing.
    [[nodiscard]] DataContainer* findForWrite();
    void changed();

    DataContainer* container_ = nullptr;  // Container mode
    World* world_ = nullptr;              // Cell mode
    BlockCoord cell_;
    std::vector<DataKey> path_;
};

}  // namespace finevox::script
