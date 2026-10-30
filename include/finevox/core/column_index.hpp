#pragma once

/**
 * @file column_index.hpp
 * @brief Column lookup that never blocks a reader
 */

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace finevox {

class ChunkColumn;

/// Open-addressed map from a packed column position to its column. find() is a few atomic
/// loads from any thread. set() and erase() take turns under the caller's own lock. A table
/// that outgrows itself stays allocated for readers still on it, which at twice the size
/// each time comes to less than the final table again.
class ColumnIndex {
public:
    ColumnIndex() { tables_.push_back(std::make_unique<Table>(INITIAL_CAPACITY)); current_ = tables_.back().get(); }

    [[nodiscard]] ChunkColumn* find(uint64_t key) const {
        const Table* table = current_.load(std::memory_order_acquire);
        for (size_t i = table->home(key);; i = (i + 1) & table->mask) {
            uint64_t found = table->slots[i].key.load(std::memory_order_acquire);
            if (found == key) return table->slots[i].column.load(std::memory_order_acquire);
            if (found == EMPTY) return nullptr;
        }
    }

    void set(uint64_t key, ChunkColumn* column) {
        Table* table = current_.load(std::memory_order_relaxed);
        if ((used_ + 1) * 2 > table->capacity()) table = grow(table);
        if (place(*table, key, column)) ++used_;
    }

    /// Readers holding the column keep it: freeing it is the caller's concern
    void erase(uint64_t key) {
        Table* table = current_.load(std::memory_order_relaxed);
        for (size_t i = table->home(key);; i = (i + 1) & table->mask) {
            uint64_t found = table->slots[i].key.load(std::memory_order_relaxed);
            if (found == EMPTY) return;
            if (found == key) {
                table->slots[i].column.store(nullptr, std::memory_order_release);
                return;
            }
        }
    }

private:
    /// Not all ones, which is column (-1, -1). No column sits this far out: its blocks
    /// would be past what an int32 can address.
    static constexpr uint64_t EMPTY = 0x8000000080000000ull;
    static constexpr size_t INITIAL_CAPACITY = 1024;

    struct Slot {
        std::atomic<uint64_t> key{EMPTY};
        std::atomic<ChunkColumn*> column{nullptr};
    };

    struct Table {
        explicit Table(size_t size)
            : slots(std::make_unique<Slot[]>(size)), mask(size - 1), shift(64 - std::countr_zero(size)) {}
        [[nodiscard]] size_t capacity() const { return mask + 1; }
        /// The top of the product, where every bit of the key has had its say. A column's
        /// X and Z sit in opposite halves of the key, so lower bits would ignore one of them.
        [[nodiscard]] size_t home(uint64_t key) const {
            return static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> shift);
        }
        std::unique_ptr<Slot[]> slots;
        size_t mask;
        int shift;
    };

    /// True if this took a new slot. A key's slot is never given up, only its column,
    /// so a reader partway along a probe can't miss what lies beyond an erased entry.
    static bool place(Table& table, uint64_t key, ChunkColumn* column) {
        for (size_t i = table.home(key);; i = (i + 1) & table.mask) {
            uint64_t found = table.slots[i].key.load(std::memory_order_relaxed);
            if (found == key) {
                table.slots[i].column.store(column, std::memory_order_release);
                return false;
            }
            if (found == EMPTY) {
                table.slots[i].column.store(column, std::memory_order_release);
                table.slots[i].key.store(key, std::memory_order_release);
                return true;
            }
        }
    }

    Table* grow(Table* old) {
        auto bigger = std::make_unique<Table>(old->capacity() * 2);
        size_t live = 0;
        for (size_t i = 0; i < old->capacity(); ++i) {
            ChunkColumn* column = old->slots[i].column.load(std::memory_order_relaxed);
            if (column) {
                place(*bigger, old->slots[i].key.load(std::memory_order_relaxed), column);
                ++live;
            }
        }
        used_ = live;
        Table* table = bigger.get();
        tables_.push_back(std::move(bigger));
        current_.store(table, std::memory_order_release);
        return table;
    }

    std::atomic<Table*> current_{nullptr};
    std::vector<std::unique_ptr<Table>> tables_;
    size_t used_ = 0;     ///< Slots holding a key, live or erased
};

}  // namespace finevox
