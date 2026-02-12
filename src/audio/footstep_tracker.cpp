#include "finevox/audio/footstep_tracker.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/player_controller.hpp"
#include "finevox/core/block_type.hpp"

#include <cmath>

namespace finevox::audio {

FootstepTracker::FootstepTracker(SoundEventQueue& eventQueue, const World& world)
    : eventQueue_(eventQueue), world_(world) {}

void FootstepTracker::update(float /*dt*/, const PlayerController& player, glm::dvec3 eyePos) {
    // Only emit footsteps when on ground and moving (not flying)
    if (player.flyMode() || !player.isOnGround()) {
        firstUpdate_ = true;
        distanceAccumulator_ = 0.0f;
        return;
    }

    auto moveDir = player.getMoveDirection();
    bool moving = (moveDir.x != 0.0f || moveDir.z != 0.0f);
    if (!moving) {
        firstUpdate_ = true;
        distanceAccumulator_ = 0.0f;
        return;
    }

    if (firstUpdate_) {
        lastPosition_ = eyePos;
        firstUpdate_ = false;
        return;
    }

    // Compute horizontal distance moved
    double dx = eyePos.x - lastPosition_.x;
    double dz = eyePos.z - lastPosition_.z;
    float dist = static_cast<float>(std::sqrt(dx * dx + dz * dz));

    lastPosition_ = eyePos;
    distanceAccumulator_ += dist;

    // Emit a footstep when we've traveled enough
    if (distanceAccumulator_ >= stepInterval_) {
        distanceAccumulator_ -= stepInterval_;

        // Get the block below the player's feet
        glm::dvec3 feetPos = eyePos;
        feetPos.y -= 1.7;  // Eye height offset (approximate)

        auto soundSet = getSurfaceSoundSet(feetPos);
        if (soundSet.isValid()) {
            eventQueue_.push(SoundEvent::footstep(soundSet, glm::vec3(feetPos)));
        }
    }
}

SoundSetId FootstepTracker::getSurfaceSoundSet(glm::dvec3 feetPos) const {
    // Look at the block directly below the feet
    BlockPos blockBelow(
        static_cast<int32_t>(std::floor(feetPos.x)),
        static_cast<int32_t>(std::floor(feetPos.y)) - 1,
        static_cast<int32_t>(std::floor(feetPos.z))
    );

    BlockTypeId blockType = world_.getBlock(blockBelow);
    if (blockType.isAir()) {
        return SoundSetId{};
    }

    const auto& type = BlockRegistry::global().getType(blockType);
    return type.soundSet();
}

}  // namespace finevox::audio
