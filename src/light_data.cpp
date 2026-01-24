#include "finevox/light_data.hpp"

#include <algorithm>
#include <cstring>

namespace finevox {

LightData::LightData() {
    // Initialize all light to zero (complete darkness)
    std::memset(light_.data(), 0, VOLUME);
}

// ============================================================================
// Light Access - Sky Light
// ============================================================================

uint8_t LightData::getSkyLight(int32_t x, int32_t y, int32_t z) const {
    return getSkyLight(toIndex(x, y, z));
}

uint8_t LightData::getSkyLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) {
        return NO_LIGHT;
    }
    return unpackSkyLight(light_[index]);
}

void LightData::setSkyLight(int32_t x, int32_t y, int32_t z, uint8_t level) {
    setSkyLight(toIndex(x, y, z), level);
}

void LightData::setSkyLight(int32_t index, uint8_t level) {
    if (index < 0 || index >= VOLUME) {
        return;
    }

    uint8_t block = unpackBlockLight(light_[index]);
    uint8_t newPacked = packLight(level, block);

    if (light_[index] != newPacked) {
        light_[index] = newPacked;
        bumpVersion();
    }
}

// ============================================================================
// Light Access - Block Light
// ============================================================================

uint8_t LightData::getBlockLight(int32_t x, int32_t y, int32_t z) const {
    return getBlockLight(toIndex(x, y, z));
}

uint8_t LightData::getBlockLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) {
        return NO_LIGHT;
    }
    return unpackBlockLight(light_[index]);
}

void LightData::setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t level) {
    setBlockLight(toIndex(x, y, z), level);
}

void LightData::setBlockLight(int32_t index, uint8_t level) {
    if (index < 0 || index >= VOLUME) {
        return;
    }

    uint8_t sky = unpackSkyLight(light_[index]);
    uint8_t newPacked = packLight(sky, level);

    if (light_[index] != newPacked) {
        light_[index] = newPacked;
        bumpVersion();
    }
}

// ============================================================================
// Light Access - Combined
// ============================================================================

uint8_t LightData::getCombinedLight(int32_t x, int32_t y, int32_t z) const {
    return getCombinedLight(toIndex(x, y, z));
}

uint8_t LightData::getCombinedLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) {
        return NO_LIGHT;
    }
    uint8_t packed = light_[index];
    uint8_t sky = unpackSkyLight(packed);
    uint8_t block = unpackBlockLight(packed);
    return std::max(sky, block);
}

uint8_t LightData::getPackedLight(int32_t x, int32_t y, int32_t z) const {
    return getPackedLight(toIndex(x, y, z));
}

uint8_t LightData::getPackedLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) {
        return 0;
    }
    return light_[index];
}

void LightData::setLight(int32_t x, int32_t y, int32_t z, uint8_t skyLight, uint8_t blockLight) {
    setLight(toIndex(x, y, z), skyLight, blockLight);
}

void LightData::setLight(int32_t index, uint8_t skyLight, uint8_t blockLight) {
    if (index < 0 || index >= VOLUME) {
        return;
    }

    uint8_t newPacked = packLight(skyLight, blockLight);
    if (light_[index] != newPacked) {
        light_[index] = newPacked;
        bumpVersion();
    }
}

void LightData::setPackedLight(int32_t x, int32_t y, int32_t z, uint8_t packed) {
    setPackedLight(toIndex(x, y, z), packed);
}

void LightData::setPackedLight(int32_t index, uint8_t packed) {
    if (index < 0 || index >= VOLUME) {
        return;
    }

    if (light_[index] != packed) {
        light_[index] = packed;
        bumpVersion();
    }
}

// ============================================================================
// Bulk Operations
// ============================================================================

void LightData::clear() {
    bool wasNonZero = false;
    for (const auto& val : light_) {
        if (val != 0) {
            wasNonZero = true;
            break;
        }
    }

    if (wasNonZero) {
        std::memset(light_.data(), 0, VOLUME);
        bumpVersion();
    }
}

void LightData::fillSkyLight(uint8_t level) {
    level = std::min(level, MAX_LIGHT);
    bool changed = false;

    for (int32_t i = 0; i < VOLUME; ++i) {
        uint8_t block = unpackBlockLight(light_[i]);
        uint8_t newPacked = packLight(level, block);
        if (light_[i] != newPacked) {
            light_[i] = newPacked;
            changed = true;
        }
    }

    if (changed) {
        bumpVersion();
    }
}

void LightData::fillBlockLight(uint8_t level) {
    level = std::min(level, MAX_LIGHT);
    bool changed = false;

    for (int32_t i = 0; i < VOLUME; ++i) {
        uint8_t sky = unpackSkyLight(light_[i]);
        uint8_t newPacked = packLight(sky, level);
        if (light_[i] != newPacked) {
            light_[i] = newPacked;
            changed = true;
        }
    }

    if (changed) {
        bumpVersion();
    }
}

bool LightData::isDark() const {
    for (const auto& val : light_) {
        if (val != 0) {
            return false;
        }
    }
    return true;
}

bool LightData::isFullSkyLight() const {
    for (const auto& val : light_) {
        if (unpackSkyLight(val) != MAX_LIGHT) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Serialization
// ============================================================================

void LightData::setRawData(const std::array<uint8_t, VOLUME>& data) {
    if (light_ != data) {
        light_ = data;
        bumpVersion();
    }
}

}  // namespace finevox
