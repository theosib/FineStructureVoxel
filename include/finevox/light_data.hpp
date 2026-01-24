#pragma once

#include "finevox/position.hpp"
#include <array>
#include <cstdint>
#include <atomic>

namespace finevox {

/**
 * @brief Light data storage for a 16x16x16 subchunk
 *
 * Stores two types of light per block:
 * - Sky light (0-15): Light from the sky, propagates down through air
 * - Block light (0-15): Light from emitting blocks (torches, lava, etc.)
 *
 * Storage format: Each block uses 1 byte (4 bits sky + 4 bits block)
 * Total: 4096 bytes per subchunk
 *
 * Light values:
 * - 15: Maximum brightness (direct sunlight or adjacent to light source)
 * - 0: Complete darkness
 * - Values decrease as light travels through transparent blocks
 */
class LightData {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE;  // 4096

    static constexpr uint8_t MAX_LIGHT = 15;
    static constexpr uint8_t NO_LIGHT = 0;

    LightData();

    // ========================================================================
    // Light Access
    // ========================================================================

    /// Get sky light level at local coordinates (0-15)
    [[nodiscard]] uint8_t getSkyLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getSkyLight(int32_t index) const;

    /// Get block light level at local coordinates (0-15)
    [[nodiscard]] uint8_t getBlockLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getBlockLight(int32_t index) const;

    /// Get combined light (max of sky and block light)
    [[nodiscard]] uint8_t getCombinedLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getCombinedLight(int32_t index) const;

    /// Get raw packed light value (sky in high nibble, block in low nibble)
    [[nodiscard]] uint8_t getPackedLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getPackedLight(int32_t index) const;

    /// Set sky light level at local coordinates
    void setSkyLight(int32_t x, int32_t y, int32_t z, uint8_t level);
    void setSkyLight(int32_t index, uint8_t level);

    /// Set block light level at local coordinates
    void setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t level);
    void setBlockLight(int32_t index, uint8_t level);

    /// Set both sky and block light at once
    void setLight(int32_t x, int32_t y, int32_t z, uint8_t skyLight, uint8_t blockLight);
    void setLight(int32_t index, uint8_t skyLight, uint8_t blockLight);

    /// Set raw packed light value
    void setPackedLight(int32_t x, int32_t y, int32_t z, uint8_t packed);
    void setPackedLight(int32_t index, uint8_t packed);

    // ========================================================================
    // Bulk Operations
    // ========================================================================

    /// Clear all light to zero
    void clear();

    /// Fill all sky light to a value (e.g., MAX_LIGHT for above-ground exposed chunks)
    void fillSkyLight(uint8_t level);

    /// Fill all block light to a value
    void fillBlockLight(uint8_t level);

    /// Check if all light values are zero (completely dark)
    [[nodiscard]] bool isDark() const;

    /// Check if all sky light values are maximum (fully exposed to sky)
    [[nodiscard]] bool isFullSkyLight() const;

    // ========================================================================
    // Serialization
    // ========================================================================

    /// Get raw light data for serialization (4096 bytes)
    [[nodiscard]] const std::array<uint8_t, VOLUME>& rawData() const { return light_; }

    /// Set raw light data from serialization
    void setRawData(const std::array<uint8_t, VOLUME>& data);

    // ========================================================================
    // Version Tracking
    // ========================================================================

    /// Get current light version (incremented on any change)
    /// Used to detect when mesh needs rebuilding for smooth lighting
    [[nodiscard]] uint64_t version() const {
        return version_.load(std::memory_order_acquire);
    }

private:
    // Packed light data: high nibble = sky light, low nibble = block light
    std::array<uint8_t, VOLUME> light_;

    // Version counter for change detection
    std::atomic<uint64_t> version_{1};

    // Convert local coordinates to array index
    [[nodiscard]] static constexpr int32_t toIndex(int32_t x, int32_t y, int32_t z) {
        return y * 256 + z * 16 + x;
    }

    // Pack/unpack light values
    [[nodiscard]] static constexpr uint8_t packLight(uint8_t sky, uint8_t block) {
        return ((sky & 0x0F) << 4) | (block & 0x0F);
    }

    [[nodiscard]] static constexpr uint8_t unpackSkyLight(uint8_t packed) {
        return (packed >> 4) & 0x0F;
    }

    [[nodiscard]] static constexpr uint8_t unpackBlockLight(uint8_t packed) {
        return packed & 0x0F;
    }

    // Increment version
    void bumpVersion() {
        version_.fetch_add(1, std::memory_order_release);
    }
};

// ============================================================================
// Utility functions
// ============================================================================

/// Pack sky and block light into a single byte
[[nodiscard]] inline constexpr uint8_t packLightValue(uint8_t sky, uint8_t block) {
    return ((sky & 0x0F) << 4) | (block & 0x0F);
}

/// Unpack sky light from packed value
[[nodiscard]] inline constexpr uint8_t unpackSkyLightValue(uint8_t packed) {
    return (packed >> 4) & 0x0F;
}

/// Unpack block light from packed value
[[nodiscard]] inline constexpr uint8_t unpackBlockLightValue(uint8_t packed) {
    return packed & 0x0F;
}

/// Get combined light (max of sky and block)
[[nodiscard]] inline constexpr uint8_t combinedLightValue(uint8_t packed) {
    uint8_t sky = unpackSkyLightValue(packed);
    uint8_t block = unpackBlockLightValue(packed);
    return sky > block ? sky : block;
}

}  // namespace finevox
