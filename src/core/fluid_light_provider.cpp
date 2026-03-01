#include "finevox/core/light_provider.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/world.hpp"

namespace finevox {

/**
 * @brief LightProvider that queries FluidRegistry for light properties
 *
 * Handles fluid-specific light behavior:
 * - Fluid emission (e.g., lava glows)
 * - Standard fluid attenuation (additive)
 * - Logarithmic fluid attenuation (multiplicative, e.g., water)
 *
 * Note: This provider needs world access to query fluid positions,
 * since fluid attenuation is position-dependent (unlike blocks where
 * attenuation is solely type-dependent).
 */
class FluidLightProvider : public LightProvider {
public:
    explicit FluidLightProvider(World& world) : world_(world) {}

    std::string_view name() const override { return "FluidLight"; }
    int32_t priority() const override { return 50; }

    uint8_t getEmission(BlockTypeId /*blockType*/) const override {
        // Fluid emission is handled separately by onFluidPlaced/Removed
        // in LightEngine. This provider doesn't contribute emission
        // via the block-type path.
        return 0;
    }

    uint8_t getAttenuation(BlockTypeId /*blockType*/) const override {
        // Fluid attenuation is position-dependent, not block-type-dependent.
        // The LightEngine handles this through getAttenuationWithFluid().
        // This fixed path returns 0 (no contribution from fluids per block-type).
        return 0;
    }

    float getLogAttenuation(const BlockCoord& pos) const override {
        FluidTypeId fluidId = world_.getFluid(pos);
        if (fluidId.isEmpty()) return 0.0f;

        const FluidType* ft = FluidRegistry::global().getType(fluidId);
        if (!ft) return 0.0f;

        if (ft->customAttenuation) {
            return ft->attenuationBase;
        }
        return 0.0f;
    }

    bool blocksSkyLight(BlockTypeId /*blockType*/) const override {
        // Fluids don't block sky light (they attenuate it instead)
        return false;
    }

private:
    World& world_;
};

std::shared_ptr<LightProvider> createFluidLightProvider(World& world) {
    return std::make_shared<FluidLightProvider>(world);
}

}  // namespace finevox
