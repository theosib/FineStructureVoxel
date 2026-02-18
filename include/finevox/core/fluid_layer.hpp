#pragma once

/**
 * @file fluid_layer.hpp
 * @brief Per-SubChunk fluid storage with palette-based type mapping
 *
 * Storage layout:
 *   data_[4096]: packed uint8_t per block
 *     - Upper 4 bits: local palette index (0=empty, 1-15=fluid type)
 *     - Lower 4 bits: level (1-14=flowing, 15=source)
 *   palette_: maps local indices 1-15 → FluidTypeId
 *
 * Lazy allocation: SubChunk has unique_ptr<FluidLayer>, null until first fluid.
 * When allocated: ~4.1KB (4096 bytes data + palette + counters).
 *
 * Design: [fluid-system.md]
 */

#include "finevox/core/fluid_type_id.hpp"
#include <array>
#include <atomic>
#include <cstdint>

namespace finevox {

/// Represents a single fluid cell: type (palette index) + level
struct FluidCell {
    uint8_t paletteIndex = 0;  // 0 = empty, 1-15 = fluid type
    uint8_t level = 0;         // 0 = empty, 1-14 = flowing, 15 = source

    [[nodiscard]] constexpr bool isEmpty() const { return paletteIndex == 0; }
    [[nodiscard]] constexpr bool isSource() const { return level == 15; }
    [[nodiscard]] constexpr bool isFlowing() const { return level > 0 && level < 15; }

    /// Pack into a single byte: upper 4 bits = palette index, lower 4 bits = level
    [[nodiscard]] constexpr uint8_t pack() const {
        return static_cast<uint8_t>((paletteIndex << 4) | (level & 0x0F));
    }

    /// Unpack from a single byte
    [[nodiscard]] static constexpr FluidCell unpack(uint8_t packed) {
        return FluidCell{
            static_cast<uint8_t>((packed >> 4) & 0x0F),
            static_cast<uint8_t>(packed & 0x0F)
        };
    }

    constexpr bool operator==(const FluidCell&) const = default;
};

/// Source level constant
constexpr uint8_t FLUID_SOURCE_LEVEL = 15;

/// Maximum flowing level
constexpr uint8_t FLUID_MAX_FLOWING_LEVEL = 14;

/// Maps local palette indices (1-15) to global FluidTypeId.
/// Index 0 is always "empty" (no fluid).
/// Max 15 distinct fluid types per subchunk (practical: usually 1-2).
class FluidPalette {
public:
    static constexpr uint8_t MAX_ENTRIES = 15;

    FluidPalette() = default;

    /// Add a fluid type to the palette. Returns local index (1-15), or 0 if full.
    /// If already in palette, returns existing index.
    [[nodiscard]] uint8_t addType(FluidTypeId type);

    /// Remove a fluid type from the palette (frees the slot).
    void removeType(FluidTypeId type);

    /// Get the FluidTypeId for a local index (0 returns EMPTY_FLUID_TYPE)
    [[nodiscard]] FluidTypeId getType(uint8_t localIndex) const;

    /// Get the local index for a FluidTypeId (returns 0 if not in palette)
    [[nodiscard]] uint8_t getIndex(FluidTypeId type) const;

    /// Check if a fluid type is in the palette
    [[nodiscard]] bool contains(FluidTypeId type) const;

    /// Number of active entries
    [[nodiscard]] uint8_t size() const { return activeCount_; }

    /// Check if palette is empty
    [[nodiscard]] bool empty() const { return activeCount_ == 0; }

    /// Check if palette is full
    [[nodiscard]] bool full() const { return activeCount_ >= MAX_ENTRIES; }

    /// Clear the palette
    void clear();

private:
    // entries_[0] is unused (index 0 = empty)
    // entries_[1..15] map to FluidTypeId
    std::array<FluidTypeId, 16> entries_{};
    uint8_t activeCount_ = 0;
};

/// Per-SubChunk fluid storage layer.
/// Stores fluid type + level for each of 4096 blocks.
class FluidLayer {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE;  // 4096

    FluidLayer() = default;

    // ========================================================================
    // Cell Access
    // ========================================================================

    /// Get fluid cell at local coordinates
    [[nodiscard]] FluidCell getCell(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] FluidCell getCell(int32_t index) const;

    /// Get fluid type at local coordinates
    [[nodiscard]] FluidTypeId getFluidType(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] FluidTypeId getFluidType(int32_t index) const;

    /// Get fluid level at local coordinates (0 = empty)
    [[nodiscard]] uint8_t getLevel(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getLevel(int32_t index) const;

    /// Check if a position has fluid
    [[nodiscard]] bool hasFluid(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] bool hasFluid(int32_t index) const;

    // ========================================================================
    // Cell Modification
    // ========================================================================

    /// Set fluid at local coordinates.
    /// Returns true if the cell changed, false if it was already the same.
    bool setFluid(int32_t x, int32_t y, int32_t z, FluidTypeId type, uint8_t level);
    bool setFluid(int32_t index, FluidTypeId type, uint8_t level);

    /// Remove fluid at local coordinates (set to empty).
    /// Returns true if there was fluid to remove.
    bool removeFluid(int32_t x, int32_t y, int32_t z);
    bool removeFluid(int32_t index);

    /// Clear all fluid
    void clear();

    // ========================================================================
    // Queries
    // ========================================================================

    /// Count of non-empty fluid cells
    [[nodiscard]] uint16_t nonEmptyCount() const { return nonEmptyCount_; }

    /// Check if the layer has no fluid at all
    [[nodiscard]] bool isEmpty() const { return nonEmptyCount_ == 0; }

    /// Access palette
    [[nodiscard]] const FluidPalette& palette() const { return palette_; }

    /// Get raw data for serialization (4096 bytes)
    [[nodiscard]] const std::array<uint8_t, VOLUME>& rawData() const { return data_; }

    /// Set raw data from serialization
    void setRawData(const std::array<uint8_t, VOLUME>& data);

    // ========================================================================
    // Version Tracking (for mesh invalidation)
    // ========================================================================

    /// Get current fluid version (incremented on every change)
    [[nodiscard]] uint64_t version() const {
        return version_.load(std::memory_order_acquire);
    }

private:
    // Packed data: upper 4 bits = palette index, lower 4 bits = level
    std::array<uint8_t, VOLUME> data_{};  // Zero-initialized (empty)
    FluidPalette palette_;
    uint16_t nonEmptyCount_ = 0;
    std::atomic<uint64_t> version_{1};

    /// Convert local coordinates to array index
    [[nodiscard]] static constexpr int32_t toIndex(int32_t x, int32_t y, int32_t z) {
        return y * 256 + z * 16 + x;
    }

    /// Bump version (called on any change)
    void bumpVersion() {
        version_.fetch_add(1, std::memory_order_release);
    }

    /// Decrement reference for a palette entry. If count reaches 0, removes from palette.
    void decrementPaletteRef(uint8_t paletteIndex);

    // Reference counts for palette entries (index 0 unused)
    std::array<uint16_t, 16> refCounts_{};
};

}  // namespace finevox
