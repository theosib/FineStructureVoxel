#pragma once

/**
 * @file footstep_tracker.hpp
 * @brief Tracks player movement and emits footstep sound events
 *
 * Monitors distance traveled on the ground and pushes footstep
 * SoundEvents to the queue at regular intervals.
 */

#include "finevox/core/sound_event.hpp"

#include <glm/glm.hpp>

namespace finevox {
class World;
class PlayerController;
}

namespace finevox::audio {

class FootstepTracker {
public:
    FootstepTracker(SoundEventQueue& eventQueue, const World& world);

    /// Call each frame with player state
    void update(float dt, const PlayerController& player, glm::dvec3 eyePos);

    /// Set distance (in blocks) between footstep sounds
    void setStepInterval(float blocksPerStep) { stepInterval_ = blocksPerStep; }

private:
    /// Look up the sound set for the block below the player's feet
    SoundSetId getSurfaceSoundSet(glm::dvec3 feetPos) const;

    SoundEventQueue& eventQueue_;
    const World& world_;

    float distanceAccumulator_ = 0.0f;
    float stepInterval_ = 2.5f;
    glm::dvec3 lastPosition_{0.0};
    bool firstUpdate_ = true;
};

}  // namespace finevox::audio
