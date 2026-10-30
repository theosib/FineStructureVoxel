#pragma once

/**
 * @file value_graph.hpp
 * @brief Script values to bytes and back, keeping which things are the same thing
 *
 * cborEncode writes a value out as a tree, which suits a message. Saved game state is a graph:
 * two lists holding the same record must hold one record again after loading, or an update
 * through one will never be seen through the other. So containers are written out once each,
 * in a table, and referred to by position. Cycles work the same way.
 */

#include <finescript/interner.h>
#include <finescript/value.h>

#include <cstdint>
#include <vector>

namespace finevox::script {

/// @throws std::runtime_error for what can't be saved, such as a function
[[nodiscard]] std::vector<uint8_t> encodeValueGraph(const finescript::Value& value,
                                                    finescript::Interner& interner);

/// @throws std::runtime_error if the bytes aren't a graph
[[nodiscard]] finescript::Value decodeValueGraph(const std::vector<uint8_t>& bytes,
                                                 finescript::Interner& interner);

}  // namespace finevox::script
