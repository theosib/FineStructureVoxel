#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/entity.hpp"
#include <chrono>

namespace finevox {

EntitySnapshot EntitySnapshot::fromEntity(const Entity& entity, uint64_t tick) {
    EntitySnapshot snap;
    snap.entity = EntityState::fromEntity(entity);
    snap.tickNumber = tick;
    snap.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return snap;
}

}  // namespace finevox
