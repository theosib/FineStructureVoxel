#pragma once

/**
 * @file animation_controller.hpp
 * @brief Animation playback with crossfade blending
 *
 * Runs on the graphics thread. Manages current clip playback
 * and smooth crossfade transitions between clips.
 */

#include <glm/glm.hpp>
#include <vector>

namespace finevox {

class AnimationClip;

class AnimationController {
public:
    AnimationController() = default;

    /// Play a new clip, optionally crossfading from current
    void play(const AnimationClip* clip, float crossfadeDuration = 0.2f);

    /// Advance time and update blend weights
    void update(float dt);

    /// Sample current animation state into local poses
    /// localPoses must be pre-sized to bone count
    void sample(std::vector<glm::mat4>& localPoses) const;

    /// Check if anything is playing
    [[nodiscard]] bool isPlaying() const;

    /// Get current playback time
    [[nodiscard]] float currentTime() const;

    /// Get current clip
    [[nodiscard]] const AnimationClip* currentClip() const;

    /// Stop all playback
    void stop();

private:
    struct PlayState {
        const AnimationClip* clip = nullptr;
        float time = 0.0f;
        float weight = 1.0f;
    };

    PlayState current_;
    PlayState blendFrom_;      // Previous clip for crossfade
    float blendTimer_ = 0.0f;
    float blendDuration_ = 0.0f;
    bool blending_ = false;
};

}  // namespace finevox
