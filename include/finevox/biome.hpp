/**
 * @file biome.hpp
 * @brief Biome types, properties, and registry
 *
 * Design: [27-world-generation.md] Sections 27.3.1-27.3.3
 *
 * BiomeId is interned via StringInterner (same pattern as BlockTypeId).
 * BiomeRegistry is a thread-safe global singleton populated during module init.
 */

#pragma once

#include "finevox/string_interner.hpp"

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

// ============================================================================
// BiomeId
// ============================================================================

/// Interned biome identifier (same pattern as BlockTypeId)
struct BiomeId {
    InternedId id = 0;

    [[nodiscard]] static BiomeId fromName(std::string_view name);
    [[nodiscard]] std::string_view name() const;

    [[nodiscard]] constexpr bool operator==(const BiomeId& other) const { return id == other.id; }
    [[nodiscard]] constexpr bool operator!=(const BiomeId& other) const { return id != other.id; }
};

}  // namespace finevox

// Hash for BiomeId
template<>
struct std::hash<finevox::BiomeId> {
    size_t operator()(const finevox::BiomeId& b) const noexcept {
        return std::hash<uint32_t>{}(b.id);
    }
};

namespace finevox {

// ============================================================================
// BiomeProperties
// ============================================================================

/// Complete biome definition with climate, terrain, and feature parameters
struct BiomeProperties {
    BiomeId id;
    std::string displayName;

    // ---- Climate (for biome selection) ----
    float temperatureMin = 0.0f;
    float temperatureMax = 1.0f;
    float humidityMin = 0.0f;
    float humidityMax = 1.0f;

    // ---- Terrain shaping ----
    float baseHeight = 64.0f;
    float heightVariation = 16.0f;
    float heightScale = 1.0f;

    // ---- Surface composition (block type names, resolved at generation time) ----
    std::string surfaceBlock = "grass";
    std::string fillerBlock = "dirt";
    int32_t fillerDepth = 3;
    std::string stoneBlock = "stone";
    std::string underwaterBlock = "sand";

    // ---- Feature density multipliers ----
    float treeDensity = 0.0f;
    float oreDensity = 1.0f;
    float decorationDensity = 1.0f;
};

// ============================================================================
// BiomeRegistry
// ============================================================================

/// Thread-safe global registry of biome definitions
class BiomeRegistry {
public:
    static BiomeRegistry& global();

    /// Register a biome (thread-safe, typically called during module init)
    void registerBiome(std::string_view name, BiomeProperties properties);

    /// Get biome properties by ID (returns nullptr if not found)
    [[nodiscard]] const BiomeProperties* getBiome(BiomeId id) const;

    /// Get biome properties by name
    [[nodiscard]] const BiomeProperties* getBiome(std::string_view name) const;

    /// Get all registered biome IDs
    [[nodiscard]] std::vector<BiomeId> allBiomes() const;

    /// Number of registered biomes
    [[nodiscard]] size_t size() const;

    /// Clear all registrations (for testing)
    void clear();

    /// Find the biome whose climate range best matches given temperature/humidity
    [[nodiscard]] BiomeId selectBiome(float temperature, float humidity) const;

private:
    BiomeRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<BiomeId, BiomeProperties> biomes_;
};

}  // namespace finevox
