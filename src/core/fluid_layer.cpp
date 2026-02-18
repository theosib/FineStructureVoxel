#include "finevox/core/fluid_layer.hpp"
#include <algorithm>
#include <cstring>

namespace finevox {

// ============================================================================
// FluidPalette
// ============================================================================

uint8_t FluidPalette::addType(FluidTypeId type) {
    if (type.isEmpty()) return 0;

    // Check if already in palette
    for (uint8_t i = 1; i <= MAX_ENTRIES; ++i) {
        if (entries_[i] == type) return i;
    }

    // Find first free slot
    if (activeCount_ >= MAX_ENTRIES) return 0;  // Full

    for (uint8_t i = 1; i <= MAX_ENTRIES; ++i) {
        if (entries_[i].isEmpty()) {
            entries_[i] = type;
            ++activeCount_;
            return i;
        }
    }

    return 0;  // Should not reach here
}

void FluidPalette::removeType(FluidTypeId type) {
    if (type.isEmpty()) return;

    for (uint8_t i = 1; i <= MAX_ENTRIES; ++i) {
        if (entries_[i] == type) {
            entries_[i] = EMPTY_FLUID_TYPE;
            --activeCount_;
            return;
        }
    }
}

FluidTypeId FluidPalette::getType(uint8_t localIndex) const {
    if (localIndex == 0 || localIndex > MAX_ENTRIES) return EMPTY_FLUID_TYPE;
    return entries_[localIndex];
}

uint8_t FluidPalette::getIndex(FluidTypeId type) const {
    if (type.isEmpty()) return 0;

    for (uint8_t i = 1; i <= MAX_ENTRIES; ++i) {
        if (entries_[i] == type) return i;
    }
    return 0;
}

bool FluidPalette::contains(FluidTypeId type) const {
    if (type.isEmpty()) return false;
    return getIndex(type) != 0;
}

void FluidPalette::clear() {
    entries_.fill(EMPTY_FLUID_TYPE);
    activeCount_ = 0;
}

// ============================================================================
// FluidLayer
// ============================================================================

FluidCell FluidLayer::getCell(int32_t x, int32_t y, int32_t z) const {
    return getCell(toIndex(x, y, z));
}

FluidCell FluidLayer::getCell(int32_t index) const {
    return FluidCell::unpack(data_[index]);
}

FluidTypeId FluidLayer::getFluidType(int32_t x, int32_t y, int32_t z) const {
    return getFluidType(toIndex(x, y, z));
}

FluidTypeId FluidLayer::getFluidType(int32_t index) const {
    auto cell = FluidCell::unpack(data_[index]);
    return palette_.getType(cell.paletteIndex);
}

uint8_t FluidLayer::getLevel(int32_t x, int32_t y, int32_t z) const {
    return getLevel(toIndex(x, y, z));
}

uint8_t FluidLayer::getLevel(int32_t index) const {
    auto cell = FluidCell::unpack(data_[index]);
    return cell.level;
}

bool FluidLayer::hasFluid(int32_t x, int32_t y, int32_t z) const {
    return hasFluid(toIndex(x, y, z));
}

bool FluidLayer::hasFluid(int32_t index) const {
    return data_[index] != 0;
}

bool FluidLayer::setFluid(int32_t x, int32_t y, int32_t z, FluidTypeId type, uint8_t level) {
    return setFluid(toIndex(x, y, z), type, level);
}

bool FluidLayer::setFluid(int32_t index, FluidTypeId type, uint8_t level) {
    auto oldCell = FluidCell::unpack(data_[index]);

    // If setting to empty, use removeFluid
    if (type.isEmpty() || level == 0) {
        return removeFluid(index);
    }

    // Get or add palette entry
    uint8_t newPaletteIndex = palette_.addType(type);
    if (newPaletteIndex == 0) {
        return false;  // Palette full
    }

    FluidCell newCell{newPaletteIndex, level};

    if (oldCell == newCell) {
        return false;  // No change
    }

    // Update reference counts
    if (!oldCell.isEmpty()) {
        decrementPaletteRef(oldCell.paletteIndex);
    } else {
        ++nonEmptyCount_;
    }

    refCounts_[newPaletteIndex]++;
    data_[index] = newCell.pack();
    bumpVersion();
    return true;
}

bool FluidLayer::removeFluid(int32_t x, int32_t y, int32_t z) {
    return removeFluid(toIndex(x, y, z));
}

bool FluidLayer::removeFluid(int32_t index) {
    auto oldCell = FluidCell::unpack(data_[index]);
    if (oldCell.isEmpty()) return false;

    decrementPaletteRef(oldCell.paletteIndex);
    --nonEmptyCount_;

    data_[index] = 0;
    bumpVersion();
    return true;
}

void FluidLayer::clear() {
    std::memset(data_.data(), 0, data_.size());
    palette_.clear();
    refCounts_.fill(0);
    nonEmptyCount_ = 0;
    bumpVersion();
}

void FluidLayer::setRawData(const std::array<uint8_t, VOLUME>& data) {
    data_ = data;

    // Rebuild ref counts and non-empty count from data
    refCounts_.fill(0);
    nonEmptyCount_ = 0;

    for (int32_t i = 0; i < VOLUME; ++i) {
        auto cell = FluidCell::unpack(data_[i]);
        if (!cell.isEmpty()) {
            ++nonEmptyCount_;
            refCounts_[cell.paletteIndex]++;
        }
    }

    bumpVersion();
}

void FluidLayer::decrementPaletteRef(uint8_t paletteIndex) {
    if (paletteIndex == 0 || paletteIndex > FluidPalette::MAX_ENTRIES) return;

    if (refCounts_[paletteIndex] > 0) {
        --refCounts_[paletteIndex];
    }

    // If no more references, free the palette slot
    if (refCounts_[paletteIndex] == 0) {
        FluidTypeId type = palette_.getType(paletteIndex);
        if (type.isValid()) {
            palette_.removeType(type);
        }
    }
}

}  // namespace finevox
