#pragma once

/**
 * @file animation_clip.hpp
 * @brief Keyframe animation data for skeletal animation
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace finevox {

// ============================================================================
// Keyframe — single sample of bone state at a point in time
// ============================================================================

struct Keyframe {
    float time = 0.0f;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// ============================================================================
// BoneTrack — animation data for one bone
// ============================================================================

struct BoneTrack {
    int32_t boneIndex = -1;
    std::vector<Keyframe> keyframes;  // Sorted by time

    /// Sample the track at a given time (linear interpolation)
    [[nodiscard]] Keyframe sample(float time) const;

    /// Check if track has keyframes
    [[nodiscard]] bool isEmpty() const { return keyframes.empty(); }

    /// Get duration (time of last keyframe)
    [[nodiscard]] float duration() const;
};

// ============================================================================
// AnimationClip — complete animation (all bone tracks)
// ============================================================================

class AnimationClip {
public:
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<BoneTrack> tracks;

    /// Sample all tracks at a given time, writing to localPoses
    /// localPoses must be pre-sized to bone count
    void sample(float time, std::vector<glm::mat4>& localPoses) const;

    /// Check if clip has any tracks
    [[nodiscard]] bool isEmpty() const { return tracks.empty(); }
};

}  // namespace finevox
