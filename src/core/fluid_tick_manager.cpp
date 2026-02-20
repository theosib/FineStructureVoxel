#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"

namespace finevox {

FluidTickManager::FluidTickManager(World& world)
    : simulator_(world), world_(world) {}

void FluidTickManager::tick() {
    simulator_.simulateTick();

    // Drain dirty subchunks and push mesh rebuild requests
    const auto& dirty = simulator_.dirtySubChunks();
    if (!dirty.empty()) {
        MeshRebuildQueue* queue = world_.meshRebuildQueue();
        if (queue) {
            for (const ChunkPos& pos : dirty) {
                queue->push(pos, MeshRebuildRequest::normal());
            }
        }
        simulator_.clearDirtySubChunks();
    }
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
