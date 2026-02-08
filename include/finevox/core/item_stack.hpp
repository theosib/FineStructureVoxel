#pragma once

/**
 * @file item_stack.hpp
 * @brief ItemStack — a quantity of items with optional metadata
 *
 * Design: Phase 13 Inventory & Items
 */

#include "finevox/core/item_type.hpp"
#include "finevox/core/data_container.hpp"
#include <memory>

namespace finevox {

/// A stack of items: type + count + optional durability and metadata
struct ItemStack {
    ItemTypeId type;                                ///< Item type (empty if id=0)
    int32_t count = 0;                              ///< Number of items in stack
    int32_t durability = 0;                         ///< Remaining durability (0 = full/N/A)
    std::unique_ptr<DataContainer> metadata;        ///< Custom data (enchantments, etc.)

    /// Check if this stack is empty (no item or zero count)
    [[nodiscard]] bool isEmpty() const {
        return type.isEmpty() || count <= 0;
    }

    /// Clear the stack to empty
    void clear() {
        type = EMPTY_ITEM_TYPE;
        count = 0;
        durability = 0;
        metadata.reset();
    }

    /// Check if this stack can merge with another (same type, no custom data)
    [[nodiscard]] bool canStackWith(const ItemStack& other) const {
        if (type != other.type) return false;
        if (type.isEmpty()) return true;  // Both empty
        if (durability != 0 || other.durability != 0) return false;
        if (metadata || other.metadata) return false;
        return true;
    }

    /// Split off `amount` items from this stack into a new stack.
    /// If amount >= count, takes all items (this stack becomes empty).
    /// Returns the split-off stack.
    ItemStack split(int32_t amount) {
        ItemStack result;
        if (amount <= 0 || isEmpty()) return result;

        int32_t taken = (amount >= count) ? count : amount;
        result.type = type;
        result.count = taken;
        // Durability/metadata stay with the original stack (can't split tools)
        count -= taken;
        if (count <= 0) {
            clear();
        }
        return result;
    }

    /// Deep copy (DataContainer is move-only, so we clone it)
    [[nodiscard]] ItemStack clone() const {
        ItemStack copy;
        copy.type = type;
        copy.count = count;
        copy.durability = durability;
        if (metadata) {
            copy.metadata = metadata->clone();
        }
        return copy;
    }
};

}  // namespace finevox
