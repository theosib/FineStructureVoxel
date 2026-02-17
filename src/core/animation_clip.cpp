#include "finevox/core/animation_clip.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace finevox {

// ============================================================================
// BoneTrack
// ============================================================================

Keyframe BoneTrack::sample(float time) const {
    if (keyframes.empty()) {
        return {};
    }

    if (keyframes.size() == 1) {
        return keyframes[0];
    }

    // Clamp to range
    if (time <= keyframes.front().time) return keyframes.front();
    if (time >= keyframes.back().time) return keyframes.back();

    // Find the two keyframes to interpolate between
    size_t next = 0;
    for (size_t i = 1; i < keyframes.size(); ++i) {
        if (keyframes[i].time >= time) {
            next = i;
            break;
        }
    }
    size_t prev = next - 1;

    const auto& k0 = keyframes[prev];
    const auto& k1 = keyframes[next];

    float dt = k1.time - k0.time;
    float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;

    Keyframe result;
    result.time = time;
    result.position = glm::mix(k0.position, k1.position, t);
    result.rotation = glm::slerp(k0.rotation, k1.rotation, t);
    result.scale = glm::mix(k0.scale, k1.scale, t);
    return result;
}

float BoneTrack::duration() const {
    if (keyframes.empty()) return 0.0f;
    return keyframes.back().time;
}

// ============================================================================
// AnimationClip
// ============================================================================

void AnimationClip::sample(float time, std::vector<glm::mat4>& localPoses) const {
    // Handle looping
    float t = time;
    if (looping && duration > 0.0f) {
        t = std::fmod(time, duration);
        if (t < 0.0f) t += duration;
    }

    for (const auto& track : tracks) {
        if (track.boneIndex < 0 ||
            track.boneIndex >= static_cast<int32_t>(localPoses.size())) {
            continue;
        }

        auto kf = track.sample(t);

        glm::mat4 tr = glm::translate(glm::mat4(1.0f), kf.position);
        glm::mat4 rot = glm::toMat4(kf.rotation);
        glm::mat4 sc = glm::scale(glm::mat4(1.0f), kf.scale);

        localPoses[track.boneIndex] = tr * rot * sc;
    }
}

}  // namespace finevox
