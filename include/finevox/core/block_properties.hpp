#pragma once

/**
 * @file block_properties.hpp
 * @brief Names for the states of blocks that have properties
 *
 * A block with properties (`state:` in its .model) is one block type per combination of
 * values, each with its own name: `door[half=upper,open=true]`. The default state, every
 * property at its first value, keeps the plain name, `door`, so a block that never changes
 * reads as it always did. Properties come in the order the model declares them.
 *
 * Every state is an ordinary BlockTypeId, so the world, saves, lighting and replication need
 * nothing new. This registry only knows how the names fit together, for scripts that want
 * to read or change one property.
 */

#include "finevox/core/block_model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class BlockPropertyRegistry {
public:
    static BlockPropertyRegistry& global();

    /// Declare a block's properties, by its name without a namespace
    void addFamily(const std::string& family, std::vector<BlockProperty> properties);

    /// The properties of a block, or nullptr for one without any
    [[nodiscard]] const std::vector<BlockProperty>* properties(std::string_view family) const;

    /// The name of a state: `family` itself for the default, `family[a=x,b=y]` otherwise.
    /// `values` are in the family's property order.
    [[nodiscard]] std::string stateName(const std::string& family,
                                        const std::vector<std::string>& values) const;

    /// A state's block name without its properties: `ns:door[open=true]` is `ns:door`
    [[nodiscard]] static std::string familyOf(std::string_view stateName);

    /// One property of a state, or nothing if the block has no such property
    [[nodiscard]] std::optional<std::string> property(std::string_view stateName,
                                                      std::string_view prop) const;

    /// The same state with one property changed, keeping any namespace, or nothing if the
    /// block has no such property or value
    [[nodiscard]] std::optional<std::string> withProperty(std::string_view stateName,
                                                          std::string_view prop,
                                                          std::string_view value) const;

    void clear() { families_.clear(); }

private:
    /// Every property's value for a state name, defaults filled in, or nothing if unknown
    [[nodiscard]] std::optional<std::vector<std::string>> valuesOf(std::string_view stateName,
                                                                   std::string& family,
                                                                   std::string& prefix) const;

    std::unordered_map<std::string, std::vector<BlockProperty>> families_;
};

}  // namespace finevox
