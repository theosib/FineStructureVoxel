#include "finevox/core/work_claims.hpp"

namespace finevox {

bool WorkClaimRegistry::claim(uint16_t scope, BlockCoord pos, EntityId owner, uint32_t ttlTicks) {
    Claim next{owner, ttlTicks == 0 ? 0 : tick_ + ttlTicks};
    auto [it, inserted] = claims_.try_emplace(Key{scope, pos}, next);
    if (inserted) return true;
    if (it->second.owner != owner && isLive(it->second)) return false;
    it->second = next;
    return true;
}

bool WorkClaimRegistry::release(uint16_t scope, BlockCoord pos, EntityId owner) {
    auto it = claims_.find(Key{scope, pos});
    if (it == claims_.end() || it->second.owner != owner) return false;
    claims_.erase(it);
    return true;
}

size_t WorkClaimRegistry::releaseAll(EntityId owner) {
    size_t released = 0;
    for (auto it = claims_.begin(); it != claims_.end();) {
        if (it->second.owner == owner) {
            it = claims_.erase(it);
            ++released;
        } else {
            ++it;
        }
    }
    return released;
}

EntityId WorkClaimRegistry::ownerOf(uint16_t scope, BlockCoord pos) const {
    auto it = claims_.find(Key{scope, pos});
    if (it == claims_.end() || !isLive(it->second)) return INVALID_ENTITY_ID;
    return it->second.owner;
}

}  // namespace finevox
