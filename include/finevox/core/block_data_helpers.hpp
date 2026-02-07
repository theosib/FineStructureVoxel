#pragma once

/**
 * @file block_data_helpers.hpp
 * @brief Helper functions for BlockTypeId storage in DataContainer
 *
 * Design: [17-implementation-phases.md] §9.1 Extra Data
 */

#include "finevox/core/data_container.hpp"
#include "finevox/core/string_interner.hpp"  // For BlockTypeId

namespace finevox {

// ============================================================================
// Block Type Storage Helpers
// ============================================================================
// These functions ensure BlockTypeId values are serialized correctly as their
// string names (e.g., "minecraft:stone") rather than numeric IDs.
//
// Use these instead of set/get<int64_t> when storing block type references
// in extra data, so the data remains valid across game sessions.
//
// Example:
//   DataContainer& data = ctx.getOrCreateData();
//   setBlockType(data, "material", BlockTypeId::fromName("minecraft:stone"));
//   ...
//   BlockTypeId mat = getBlockType(data, "material", AIR_BLOCK_TYPE);

/// Store a BlockTypeId as an interned string value
/// The block's name is serialized, not the numeric ID
inline void setBlockType(DataContainer& data, DataKey key, BlockTypeId type) {
    // BlockTypeId and InternedString both use StringInterner::global(),
    // so the intern IDs are compatible. Just wrap the ID.
    data.set<InternedString>(key, InternedString(type.id));
}

/// Store a BlockTypeId as an interned string value (string key version)
inline void setBlockType(DataContainer& data, std::string_view key, BlockTypeId type) {
    data.set<InternedString>(key, InternedString(type.id));
}

/// Retrieve a BlockTypeId from an interned string value
/// Returns defaultValue if key doesn't exist or type doesn't match
inline BlockTypeId getBlockType(const DataContainer& data, DataKey key,
                                 BlockTypeId defaultValue = AIR_BLOCK_TYPE) {
    InternedString is = data.get<InternedString>(key);
    if (is.id == 0) {
        return defaultValue;
    }
    return BlockTypeId(is.id);
}

/// Retrieve a BlockTypeId from an interned string value (string key version)
inline BlockTypeId getBlockType(const DataContainer& data, std::string_view key,
                                 BlockTypeId defaultValue = AIR_BLOCK_TYPE) {
    return getBlockType(data, internKey(key), defaultValue);
}

/// Check if a key contains a BlockTypeId value
inline bool hasBlockType(const DataContainer& data, DataKey key) {
    const DataValue* raw = data.getRaw(key);
    return raw && std::holds_alternative<InternedString>(*raw);
}

/// Check if a key contains a BlockTypeId value (string key version)
inline bool hasBlockType(const DataContainer& data, std::string_view key) {
    return hasBlockType(data, internKey(key));
}

}  // namespace finevox
