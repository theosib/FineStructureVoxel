#include "finevox/core/animation_controller.hpp"
#include "finevox/core/animation_clip.hpp"
#include <algorithm>

namespace finevox {

void AnimationController::play(const AnimationClip* clip, float crossfadeDuration) {
    if (!clip) {
        stop();
        return;
    }

    if (current_.clip && crossfadeDuration > 0.0f) {
        // Start crossfade from current to new
        blendFrom_ = current_;
        blendTimer_ = 0.0f;
        blendDuration_ = crossfadeDuration;
        blending_ = true;
    }

    current_.clip = clip;
    current_.time = 0.0f;
    current_.weight = 1.0f;
}

void AnimationController::update(float dt) {
    // Advance current clip
    if (current_.clip) {
        current_.time += dt;
    }

    // Advance blend
    if (blending_) {
        blendTimer_ += dt;
        if (blendFrom_.clip) {
            blendFrom_.time += dt;
        }

        if (blendTimer_ >= blendDuration_) {
            blending_ = false;
            blendFrom_ = {};
        }
    }
}

void AnimationController::sample(std::vector<glm::mat4>& localPoses) const {
    if (!current_.clip) return;

    // What the caller filled in (the rest pose) stands for any bone a clip has no track for, in
    // both clips. Identity there would pull such a bone to its parent's origin mid-fade.
    bool fading = blending_ && blendFrom_.clip && blendDuration_ > 0.0f;
    std::vector<glm::mat4> prevPoses;
    if (fading) prevPoses = localPoses;

    // Sample current clip
    current_.clip->sample(current_.time, localPoses);

    // Blend with previous clip if crossfading
    if (fading) {
        float blendFactor = std::min(blendTimer_ / blendDuration_, 1.0f);

        // Sample previous clip over the same starting pose
        blendFrom_.clip->sample(blendFrom_.time, prevPoses);

        // Interpolate between prev and current
        for (size_t i = 0; i < localPoses.size(); ++i) {
            // Simple matrix lerp (works for small differences)
            localPoses[i] = prevPoses[i] * (1.0f - blendFactor) +
                            localPoses[i] * blendFactor;
        }
    }
}

bool AnimationController::isPlaying() const {
    return current_.clip != nullptr;
}

float AnimationController::currentTime() const {
    return current_.time;
}

const AnimationClip* AnimationController::currentClip() const {
    return current_.clip;
}

void AnimationController::stop() {
    current_ = {};
    blendFrom_ = {};
    blending_ = false;
}

}  // namespace finevox
