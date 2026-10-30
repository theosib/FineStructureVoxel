#pragma once

/**
 * @file work_claims.hpp
 * @brief Reservation of world positions by agents
 *
 * When many agents draw from one pool of work, each needs to know a target is
 * taken before walking to it. A claim is held per scope (typically a team), so
 * rival teams can contest the same block while teammates never double up.
 *
 * Claims expire: an agent can die or be reassigned without releasing, and a
 * target must not stay reserved forever because of it.
 *
 * Game thread only.
 */

#include "finevox/core/position.hpp"
#include "finevox/core/entity_state.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace finevox {

class WorkClaimRegistry {
public:
    /// Set the clock used for expiry. Callers advance it once per game tick.
    void setTick(uint64_t tick) { tick_ = tick; }

    /// Claim a position for owner. Succeeds if free, expired, or already owned by
    /// owner (which refreshes it). ttlTicks of 0 means no expiry.
    bool claim(uint16_t scope, BlockCoord pos, EntityId owner, uint32_t ttlTicks = 0);

    /// Release a position if owner holds it
    bool release(uint16_t scope, BlockCoord pos, EntityId owner);

    /// Release everything owner holds, across all scopes
    size_t releaseAll(EntityId owner);

    /// Current live owner of a position, or INVALID_ENTITY_ID
    [[nodiscard]] EntityId ownerOf(uint16_t scope, BlockCoord pos) const;

    [[nodiscard]] bool isClaimed(uint16_t scope, BlockCoord pos) const {
        return ownerOf(scope, pos) != INVALID_ENTITY_ID;
    }

    [[nodiscard]] size_t size() const { return claims_.size(); }

private:
    struct Key {
        uint16_t scope;
        BlockCoord pos;
        bool operator==(const Key& o) const { return scope == o.scope && pos == o.pos; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            uint64_t h = static_cast<uint64_t>(k.scope);
            h = h * 0x100000001b3ULL ^ static_cast<uint32_t>(k.pos.x);
            h = h * 0x100000001b3ULL ^ static_cast<uint32_t>(k.pos.y);
            h = h * 0x100000001b3ULL ^ static_cast<uint32_t>(k.pos.z);
            return static_cast<size_t>(h);
        }
    };
    struct Claim {
        EntityId owner;
        uint64_t expiresAt;  ///< 0 = never
    };

    [[nodiscard]] bool isLive(const Claim& claim) const {
        return claim.expiresAt == 0 || tick_ < claim.expiresAt;
    }

    std::unordered_map<Key, Claim, KeyHash> claims_;
    uint64_t tick_ = 0;
};

}  // namespace finevox
