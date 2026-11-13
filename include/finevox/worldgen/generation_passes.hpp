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

#include "finevox/worldgen/density_graph.hpp"
#include "finevox/worldgen/world_generator.hpp"
#include "finevox/worldgen/noise.hpp"
#include "finevox/worldgen/feature_registry.hpp"

#include <memory>
#include <optional>

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
    SurfacePass() = default;
    /// With a sea level, ground under water gets the biome's `underwater` block instead
    explicit SurfacePass(int32_t seaLevel) : seaLevel_(seaLevel) {}

    [[nodiscard]] std::string_view name() const override { return "core:surface"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Surface);
    }
    void generate(GenerationContext& ctx) override;

private:
    std::optional<int32_t> seaLevel_;
};

// ============================================================================
// DensityTerrainPass — terrain from a graph a game describes
// ============================================================================

/// Fills the graph's `fill` block where its terrain is above zero, between its bottom and top
/// (see density_graph.hpp). Sets the heightmap to each column's highest solid block, and each
/// column's biome: the one whose `range:` lines the graph's fields fall within, or failing
/// that the nearest; with no biome giving ranges, the BiomeMap's choice. Replaces TerrainPass.
class DensityTerrainPass : public GenerationPass {
public:
    explicit DensityTerrainPass(std::shared_ptr<const DensityGraph> graph);

    [[nodiscard]] std::string_view name() const override { return "core:terrain"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::TerrainShape);
    }
    void generate(GenerationContext& ctx) override;

    /// The biome for a column's field values, by the rule above (BiomeId{} if none can say)
    [[nodiscard]] BiomeId chooseBiome(const std::vector<float>& values) const;

private:
    struct Candidate {
        BiomeId id;
        std::vector<std::pair<int, std::pair<float, float>>> ranges;  // graph node, lo..hi
    };
    std::shared_ptr<const DensityGraph> graph_;
    std::vector<Candidate> candidates_;
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

// ============================================================================
// FluidPass — fills water below sea level, optional underground lava
// ============================================================================

class FluidPass : public GenerationPass {
public:
    explicit FluidPass(int32_t seaLevel = 62);

    [[nodiscard]] std::string_view name() const override { return "core:fluids"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Fluids);
    }
    void generate(GenerationContext& ctx) override;

    void setSeaLevel(int32_t level) { seaLevel_ = level; }
    [[nodiscard]] int32_t seaLevel() const { return seaLevel_; }

private:
    int32_t seaLevel_;
};

}  // namespace finevox::worldgen
