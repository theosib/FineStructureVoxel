#include "finevox/core/light_provider.hpp"
#include "finevox/core/block_type.hpp"

namespace finevox {

/**
 * @brief LightProvider that queries BlockRegistry for light properties
 *
 * Extracts the emission, attenuation, and sky light blocking queries
 * that were previously hardcoded in LightEngine.
 */
class BlockLightProvider : public LightProvider {
public:
    std::string_view name() const override { return "BlockLight"; }
    int32_t priority() const override { return 0; }

    uint8_t getEmission(BlockTypeId blockType) const override {
        if (blockType.isAir()) return 0;
        return BlockRegistry::global().getType(blockType).lightEmission();
    }

    uint8_t getAttenuation(BlockTypeId blockType) const override {
        if (blockType.isAir()) return 1;  // Air has minimal attenuation
        return BlockRegistry::global().getType(blockType).lightAttenuation();
    }

    bool blocksSkyLight(BlockTypeId blockType) const override {
        if (blockType.isAir()) return false;
        return BlockRegistry::global().getType(blockType).blocksSkyLight();
    }
};

// Factory function (defined in header for LightEngine to use)
std::shared_ptr<LightProvider> createBlockLightProvider() {
    return std::make_shared<BlockLightProvider>();
}

}  // namespace finevox
