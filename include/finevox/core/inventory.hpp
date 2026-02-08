#pragma once

/**
 * @file inventory.hpp
 * @brief InventoryView — ephemeral adapter over DataContainer for inventory access
 *
 * Design: Phase 13 Inventory & Items
 *
 * InventoryView provides a convenient API on top of a DataContainer that stores
 * inventory data. It reads and writes slots directly to the DC — no bulk copy.
 * Create an InventoryView when needed; it's lightweight (just references).
 *
 * Works identically for block inventories (via SubChunk::blockData) and entity
 * inventories (via Entity::entityData). The DataContainer IS the source of truth.
 *
 * Storage format within the DataContainer:
 *   "size"  → int64_t (slot count)
 *   "0", "1", ... → nested DataContainer per non-empty slot (sparse)
 *     "t" → int64_t (NameRegistry PersistentId for item type)
 *     "c" → int64_t (count)
 *     "d" → int64_t (durability, only if non-zero)
 *     "m" → nested DataContainer (metadata, only if present)
 */

#include "finevox/core/item_stack.hpp"
#include "finevox/core/name_registry.hpp"

namespace finevox {

class InventoryView {
public:
    /// Create an inventory view over a DataContainer.
    /// The DC must outlive this view.
    /// @param dc The DataContainer storing inventory data
    /// @param registry NameRegistry for ItemTypeId ↔ PersistentId translation
    InventoryView(DataContainer& dc, NameRegistry& registry);

    /// Get the number of slots
    [[nodiscard]] int32_t slotCount() const;

    /// Set the number of slots (initializes "size" in DC)
    void setSlotCount(int32_t count);

    /// Read a single slot from the DC
    [[nodiscard]] ItemStack getSlot(int32_t index) const;

    /// Write a single slot to the DC.
    /// If the stack is empty, removes the slot entry (sparse).
    void setSlot(int32_t index, const ItemStack& stack);

    /// Clear a slot (remove its entry from DC)
    void clearSlot(int32_t index);

    // ========================================================================
    // Higher-level operations (built on getSlot/setSlot)
    // ========================================================================

    /// Add items, filling existing stacks first, then empty slots.
    /// @return Count of items that couldn't fit (0 = all added)
    int32_t addItem(ItemTypeId type, int32_t count);

    /// Take items from a specific slot.
    /// @return The taken items (count may be less than requested)
    ItemStack takeItem(int32_t slotIndex, int32_t count);

    /// Swap two slots
    void swapSlots(int32_t a, int32_t b);

    /// Count total items of a type across all slots
    [[nodiscard]] int32_t countItem(ItemTypeId type) const;

    /// Check if inventory contains at least `count` of an item type
    [[nodiscard]] bool hasItem(ItemTypeId type, int32_t count = 1) const;

    /// Remove items of a type from anywhere in the inventory.
    /// @return Count actually removed
    int32_t removeItem(ItemTypeId type, int32_t count);

    /// Check if all slots are empty
    [[nodiscard]] bool isEmpty() const;

    /// Check if no empty slots remain
    [[nodiscard]] bool isFull() const;

private:
    DataContainer& dc_;
    NameRegistry& registry_;

    /// Convert slot index to DC key string (e.g., 0 → "0", 1 → "1")
    static std::string slotKey(int32_t index);

    /// Get maxStackSize for an item type from ItemRegistry (defaults to 64)
    static int32_t getMaxStackSize(ItemTypeId type);
};

}  // namespace finevox
