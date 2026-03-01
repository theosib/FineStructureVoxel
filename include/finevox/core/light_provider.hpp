#pragma once

/**
 * @file light_provider.hpp
 * @brief Pluggable light provider system for the LightEngine
 *
 * LightProviders abstract how the LightEngine queries emission and
 * attenuation values. Instead of hardcoded BlockRegistry/FluidRegistry
 * lookups, the LightEngine iterates over registered providers.
 *
 * Combination rules:
 *   - Emission: MAX across all providers
 *   - Attenuation: SUM across all providers (clamped to 15)
 *   - Log attenuation: accumulated multiplicatively (0 = not applicable)
 *   - Sky light blocking: OR across all providers
 */

#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace finevox {

/**
 * @brief Base class for pluggable light providers
 *
 * Implementations provide emission and attenuation values from
 * specific sources (blocks, fluids, entities, etc.)
 *
 * Lifecycle:
 *   1. Provider is created externally
 *   2. Added to LightEngine via addLightProvider()
 *   3. LightEngine calls query methods during propagation
 *   4. Removed via removeLightProvider() or when LightEngine is destroyed
 */
class LightProvider : public std::enable_shared_from_this<LightProvider> {
public:
    virtual ~LightProvider() = default;

    /// Human-readable name for debugging
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Priority for query ordering. Lower values are queried first.
    [[nodiscard]] virtual int32_t priority() const { return 100; }

    // === Light emission ===

    /// Get the light emission at a position.
    /// Return 0 if this provider has no emission at this position.
    /// LightEngine takes MAX across all providers.
    [[nodiscard]] virtual uint8_t getEmission(BlockTypeId blockType) const = 0;

    // === Attenuation ===

    /// Get the fixed (additive) light attenuation for a block type.
    /// Return 0 if this provider has no attenuation effect.
    /// LightEngine takes SUM (clamped to 15) across all providers.
    [[nodiscard]] virtual uint8_t getAttenuation(BlockTypeId blockType) const = 0;

    /// Get the logarithmic (multiplicative) attenuation at a position.
    /// Return 0.0f if not applicable (most providers).
    /// Used for fluid light attenuation (e.g., water reduces light multiplicatively).
    /// LightEngine accumulates multiplicatively.
    [[nodiscard]] virtual float getLogAttenuation(const BlockCoord& /*pos*/) const {
        return 0.0f;
    }

    // === Sky light ===

    /// Check if the block type blocks sky light from passing through.
    /// LightEngine uses OR: any provider returning true = sky light blocked.
    [[nodiscard]] virtual bool blocksSkyLight(BlockTypeId blockType) const = 0;
};

// ============================================================================
// Built-in provider factories
// ============================================================================

class World;

/// Create a provider that queries BlockRegistry for block light properties
std::shared_ptr<LightProvider> createBlockLightProvider();

/// Create a provider that queries FluidRegistry for fluid light properties
std::shared_ptr<LightProvider> createFluidLightProvider(World& world);

}  // namespace finevox
