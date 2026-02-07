/**
 * @file world_generator.hpp
 * @brief Generation pipeline: passes, context, and pipeline orchestration
 *
 * Design: [27-world-generation.md] Sections 27.4.1-27.4.3
 *
 * The generation pipeline runs an ordered sequence of GenerationPasses over
 * a ChunkColumn. Each pass reads/writes to a shared GenerationContext.
 * Games add, replace, or remove passes to customize world generation.
 */

#pragma once

#include "finevox/biome.hpp"
#include "finevox/biome_map.hpp"
#include "finevox/noise.hpp"
#include "finevox/position.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace finevox {

class ChunkColumn;
class World;

// ============================================================================
// GenerationPriority
// ============================================================================

/// Standard priority levels for generation passes
enum class GenerationPriority : int32_t {
    TerrainShape   = 1000,
    Surface        = 2000,
    Carving        = 3000,
    Ores           = 4000,
    Structures     = 5000,
    Decoration     = 6000,
    Finalization   = 9000,
};

// ============================================================================
// GenerationContext
// ============================================================================

/// Shared mutable context passed through all passes for a column
struct GenerationContext {
    ChunkColumn& column;
    ColumnPos pos;
    World& world;
    const BiomeMap& biomeMap;
    uint64_t worldSeed;

    /// Surface Y per (localX * 16 + localZ), populated by TerrainPass
    std::array<int32_t, 256> heightmap{};

    /// Biome per (localX * 16 + localZ), populated by TerrainPass
    std::array<BiomeId, 256> biomes{};

    /// Per-column deterministic seed derived from worldSeed + column position
    [[nodiscard]] uint64_t columnSeed() const;

    /// Heightmap index from local coords
    [[nodiscard]] static constexpr int32_t hmIndex(int32_t localX, int32_t localZ) {
        return localX * 16 + localZ;
    }
};

// ============================================================================
// GenerationPass
// ============================================================================

/// Abstract base for a single generation pass
class GenerationPass {
public:
    virtual ~GenerationPass() = default;

    /// Unique name for this pass (e.g., "core:terrain", "mymod:rivers")
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Priority determines execution order (lower runs first)
    [[nodiscard]] virtual int32_t priority() const = 0;

    /// Execute this pass on the given context
    virtual void generate(GenerationContext& ctx) = 0;

    /// Whether this pass reads blocks from neighboring columns
    [[nodiscard]] virtual bool needsNeighbors() const { return false; }
};

// ============================================================================
// GenerationPipeline
// ============================================================================

/// Orchestrates ordered generation passes over chunk columns
class GenerationPipeline {
public:
    GenerationPipeline() = default;

    /// Add a pass (sorted by priority on insertion)
    void addPass(std::unique_ptr<GenerationPass> pass);

    /// Remove a pass by name (returns true if found)
    bool removePass(std::string_view name);

    /// Replace a pass with the same name (returns true if found and replaced)
    bool replacePass(std::unique_ptr<GenerationPass> pass);

    /// Generate a column by running all passes in priority order
    void generateColumn(ChunkColumn& column, World& world, const BiomeMap& biomeMap);

    /// Set the world seed
    void setWorldSeed(uint64_t seed) { worldSeed_ = seed; }

    /// Get current world seed
    [[nodiscard]] uint64_t worldSeed() const { return worldSeed_; }

    /// Number of registered passes
    [[nodiscard]] size_t passCount() const { return passes_.size(); }

    /// Get pass by name
    [[nodiscard]] GenerationPass* getPass(std::string_view name) const;

private:
    uint64_t worldSeed_ = 0;
    std::vector<std::unique_ptr<GenerationPass>> passes_;

    void sortPasses();
};

}  // namespace finevox
