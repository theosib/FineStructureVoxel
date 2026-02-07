/**
 * @file biome.cpp
 * @brief BiomeId and BiomeRegistry implementation
 *
 * Design: [27-world-generation.md] Sections 27.3.1-27.3.3
 */

#include "finevox/biome.hpp"

#include <cmath>
#include <limits>

namespace finevox {

// ============================================================================
// BiomeId
// ============================================================================

BiomeId BiomeId::fromName(std::string_view name) {
    return {StringInterner::global().intern(name)};
}

std::string_view BiomeId::name() const {
    return StringInterner::global().lookup(id);
}

// ============================================================================
// BiomeRegistry
// ============================================================================

BiomeRegistry& BiomeRegistry::global() {
    static BiomeRegistry instance;
    return instance;
}

void BiomeRegistry::registerBiome(std::string_view name, BiomeProperties properties) {
    std::unique_lock lock(mutex_);
    BiomeId biomeId = BiomeId::fromName(name);
    properties.id = biomeId;
    biomes_[biomeId] = std::move(properties);
}

const BiomeProperties* BiomeRegistry::getBiome(BiomeId id) const {
    std::shared_lock lock(mutex_);
    auto it = biomes_.find(id);
    return (it != biomes_.end()) ? &it->second : nullptr;
}

const BiomeProperties* BiomeRegistry::getBiome(std::string_view name) const {
    auto id = StringInterner::global().find(name);
    if (!id) return nullptr;
    return getBiome(BiomeId{*id});
}

std::vector<BiomeId> BiomeRegistry::allBiomes() const {
    std::shared_lock lock(mutex_);
    std::vector<BiomeId> result;
    result.reserve(biomes_.size());
    for (const auto& [id, props] : biomes_) {
        result.push_back(id);
    }
    return result;
}

size_t BiomeRegistry::size() const {
    std::shared_lock lock(mutex_);
    return biomes_.size();
}

void BiomeRegistry::clear() {
    std::unique_lock lock(mutex_);
    biomes_.clear();
}

BiomeId BiomeRegistry::selectBiome(float temperature, float humidity) const {
    std::shared_lock lock(mutex_);

    BiomeId bestId{};
    float bestScore = std::numeric_limits<float>::max();

    for (const auto& [id, props] : biomes_) {
        // Score = distance from climate point to biome's climate range center
        float tempCenter = (props.temperatureMin + props.temperatureMax) * 0.5f;
        float humCenter = (props.humidityMin + props.humidityMax) * 0.5f;
        float tempRange = (props.temperatureMax - props.temperatureMin) * 0.5f;
        float humRange = (props.humidityMax - props.humidityMin) * 0.5f;

        // Penalty for being outside the range, no penalty inside
        float tempDist = std::max(0.0f, std::abs(temperature - tempCenter) - tempRange);
        float humDist = std::max(0.0f, std::abs(humidity - humCenter) - humRange);
        float score = tempDist * tempDist + humDist * humDist;

        if (score < bestScore) {
            bestScore = score;
            bestId = id;
        }
    }

    return bestId;
}

}  // namespace finevox
