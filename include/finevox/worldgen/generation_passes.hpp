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
#include <string>

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

class Waterways;

// ============================================================================
// SurfacePass — replaces top layers with biome-specific blocks
// ============================================================================

class SurfacePass : public GenerationPass {
public:
    SurfacePass() = default;
    /// With a sea level, ground under water gets the biome's `underwater` block instead, and
    /// ground less than shoreHeight blocks above it gets the biome's `shore` block
    explicit SurfacePass(int32_t seaLevel, int32_t shoreHeight = 0)
        : seaLevel_(seaLevel), shoreHeight_(shoreHeight) {}

    /// Marshes and fords take their own ground
    void setWaterways(std::shared_ptr<const Waterways> waterways) { waterways_ = std::move(waterways); }

    [[nodiscard]] std::string_view name() const override { return "core:surface"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::Surface);
    }
    void generate(GenerationContext& ctx) override;

private:
    std::optional<int32_t> seaLevel_;
    int32_t shoreHeight_ = 0;
    std::shared_ptr<const Waterways> waterways_;
};

// ============================================================================
// Waterways — rivers and lakes, and WaterwayPass, which cuts them
// ============================================================================

/// Where rivers and lakes go, and their shape. All at sea level, so FluidPass fills them and
/// they meet where they cross. A scale of 0 leaves that kind out.
struct WaterwaySettings {
    float riverScale = 0.0f;   ///< Noise frequency: lower is fewer, longer rivers
    /// One river, on a line through the origin, in place of wherever the noise puts them: across
    /// the map at riverHeading degrees (0 runs along z), wandering up to about riverMeander
    /// blocks either side of the line, at riverScale.
    bool riverLine = false;
    float riverHeading = 0.0f;
    float riverMeander = 40.0f;
    float riverWidth = 6.0f;   ///< Blocks across at the surface
    int32_t riverDepth = 3;    ///< Blocks of water at the middle
    /// Rivers spread into a marsh where a noise is below this (-1..1): wider, a block deep,
    /// with tussocks standing out of it. -1 never, 0 about half their length.
    float riverMarshes = -0.5f;
    float marshWidth = 3.0f;   ///< How many times wider than the river a marsh is
    std::string marshBlock;    ///< Ground in a marsh, under water or not. Empty: as elsewhere.
    /// Rivers run a block deep, at their usual width, where the same noise is above this:
    /// 1 never. Marshes and fords are at opposite ends of it, so never in the same place.
    float riverFords = 1.0f;
    std::string fordBlock;     ///< A ford's bed. Empty: as elsewhere.
    float lakeScale = 0.0f;    ///< Noise frequency: lower is fewer, bigger lakes
    /// A lake where its noise is above this (-1..1): higher is fewer and smaller
    float lakeThreshold = 0.45f;
    int32_t lakeDepth = 5;     ///< Deepest a lake gets, in blocks
    /// How fast the land rises from the water, in blocks per block across. Land is only ever
    /// lowered, so this sets the width of the valley a river cuts through high ground.
    float bankSlope = 0.5f;

    [[nodiscard]] bool any() const { return riverScale > 0.0f || lakeScale > 0.0f; }
};

/// The shape of the water, anywhere, worked out from noise alone: distance from water is
/// estimated from each noise's gradient, so a column costs a few samples and needs no
/// neighbours. Shared by the passes that cut it and that surface it.
class Waterways {
public:
    Waterways(uint64_t worldSeed, int32_t seaLevel, WaterwaySettings settings);

    /// The highest the ground may be at a point, or nothing where water doesn't reach it
    [[nodiscard]] std::optional<int32_t> ceilingAt(float x, float z) const;

    /// Inside a marsh, tussocks and all
    [[nodiscard]] bool marshAt(float x, float z) const;
    /// In a ford's bed
    [[nodiscard]] bool fordAt(float x, float z) const;

    [[nodiscard]] const WaterwaySettings& settings() const { return settings_; }
    [[nodiscard]] int32_t seaLevel() const { return seaLevel_; }

private:
    struct River {
        float fromMiddle;   ///< Blocks from the middle of the channel
        float half;         ///< Half its width here
        float marsh;        ///< 0 a river, 1 a marsh, between them in the change
        bool ford;
    };
    [[nodiscard]] River riverAt(float x, float z) const;

    int32_t seaLevel_;
    WaterwaySettings settings_;
    std::unique_ptr<Noise2D> river_;
    std::unique_ptr<Noise2D> marsh_;
    std::unique_ptr<Noise2D> tussocks_;
    std::unique_ptr<Noise2D> lake_;
};

/// After the terrain and before its surface: cuts each column down to the river or lake bed
/// it lies in, or to the bank rising from one.
class WaterwayPass : public GenerationPass {
public:
    explicit WaterwayPass(std::shared_ptr<const Waterways> waterways) : waterways_(std::move(waterways)) {}

    [[nodiscard]] std::string_view name() const override { return "core:waterways"; }
    [[nodiscard]] int32_t priority() const override {
        return static_cast<int32_t>(GenerationPriority::TerrainShape) + 500;
    }
    void generate(GenerationContext& ctx) override;

private:
    std::shared_ptr<const Waterways> waterways_;
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
