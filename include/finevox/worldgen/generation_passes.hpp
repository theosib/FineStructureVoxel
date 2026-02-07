/**
 * @file generation_passes.hpp
 * @brief Standard generation passes: terrain, surface, caves, ores, structures, decoration
 *
 * Design: [27-world-generation.md] Section 27.4.4
 *
 * Each pass reads from and writes to GenerationContext. Games can replace
 * any standard pass or insert custom passes at any priority level.
 */

#pragma once

#include "finevox/worldgen/world_generator.hpp"
#include "finevox/worldgen/noise.hpp"
#include "finevox/worldgen/feature_registry.hpp"

#include <memory>

namespace finevox::worldgen {

// ============================================================================
// TerrainPass — fills stone below surface height from noise
// ============================================================================

class TerrainPass : public GenerationPass {
public:
    explicit TerrainPass(uint64_t worldSeed);

    [[nodiscard]] std::string_view name() const override { return "core:terrain"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::TerrainShape);
    }
    void generate(GenerationContext& ctx) override;

private:
    std::unique_ptr<Noise2D> continentNoise_;   ///< Large-scale height
    std::unique_ptr<Noise2D> detailNoise_;      ///< Small-scale detail
    std::unique_ptr<Noise3D> densityNoise_;     ///< 3D density for overhangs
};

// ============================================================================
// SurfacePass — replaces top layers with biome-specific blocks
// ============================================================================

class SurfacePass : public GenerationPass {
public:
    [[nodiscard]] std::string_view name() const override { return "core:surface"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Surface);
    }
    void generate(GenerationContext& ctx) override;
};

// ============================================================================
// CavePass — carves caves using 3D noise
// ============================================================================

class CavePass : public GenerationPass {
public:
    explicit CavePass(uint64_t worldSeed);

    [[nodiscard]] std::string_view name() const override { return "core:caves"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Carving);
    }
    void generate(GenerationContext& ctx) override;

private:
    std::unique_ptr<Noise3D> cheeseNoise_;      ///< Large caverns
    std::unique_ptr<Noise3D> spaghettiNoise_;   ///< Tunnel-like caves
};

// ============================================================================
// OrePass — places ore veins from FeatureRegistry
// ============================================================================

class OrePass : public GenerationPass {
public:
    [[nodiscard]] std::string_view name() const override { return "core:ores"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Ores);
    }
    void generate(GenerationContext& ctx) override;
};

// ============================================================================
// StructurePass — places multi-block features (trees, buildings)
// ============================================================================

class StructurePass : public GenerationPass {
public:
    [[nodiscard]] std::string_view name() const override { return "core:structures"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Structures);
    }
    void generate(GenerationContext& ctx) override;
    [[nodiscard]] bool needsNeighbors() const override { return true; }
};

// ============================================================================
// DecorationPass — places single-block decorations on surface
// ============================================================================

class DecorationPass : public GenerationPass {
public:
    [[nodiscard]] std::string_view name() const override { return "core:decoration"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Decoration);
    }
    void generate(GenerationContext& ctx) override;
};

}  // namespace finevox::worldgen
