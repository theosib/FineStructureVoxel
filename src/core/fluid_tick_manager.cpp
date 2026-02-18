#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/world.hpp"

namespace finevox {

FluidTickManager::FluidTickManager(World& world)
    : simulator_(world) {}

void FluidTickManager::tick() {
    simulator_.simulateTick();
}

void FluidTickManager::markActive(ChunkPos pos) {
    activeSubChunks_.insert(pos);
}

bool FluidTickManager::isActive(ChunkPos pos) const {
    return activeSubChunks_.count(pos) > 0;
}

void FluidTickManager::setEnabled(bool enabled) {
    auto config = simulator_.config();
    config.enabled = enabled;
    simulator_.setConfig(config);
}

bool FluidTickManager::isEnabled() const {
    return simulator_.config().enabled;
}

}  // namespace finevox
