#pragma once

/**
 * @file skeleton.hpp
 * @brief Bone hierarchy and Skeleton for skeletal animation
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace finevox {

// ============================================================================
// Bone — single joint in the skeleton hierarchy
// ============================================================================

struct Bone {
    std::string name;
    int32_t parentIndex = -1;  // -1 = root bone
    glm::vec3 restPosition{0.0f};
    glm::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};  // w,x,y,z
    glm::vec3 restScale{1.0f};
};

// ============================================================================
// Skeleton — collection of bones with hierarchy
// ============================================================================

class Skeleton {
public:
    Skeleton() = default;

    /// Add a bone to the skeleton (parent must already exist)
    void addBone(Bone bone);

    /// Find bone index by name (-1 if not found)
    [[nodiscard]] int32_t findBone(std::string_view name) const;

    /// Get a bone by index
    [[nodiscard]] const Bone& bone(int32_t index) const;

    /// Get total bone count
    [[nodiscard]] int32_t boneCount() const { return static_cast<int32_t>(bones_.size()); }

    /// Get all bones
    [[nodiscard]] const std::vector<Bone>& bones() const { return bones_; }

    /// Check if skeleton has any bones
    [[nodiscard]] bool isEmpty() const { return bones_.empty(); }

    /// Compute rest-pose transforms (bone-local)
    void computeRestPose(std::vector<glm::mat4>& localPoses) const;

    /// Compute world-space transforms from local poses
    /// Walks the parent chain to accumulate transforms
    void computeWorldTransforms(
        const std::vector<glm::mat4>& localPoses,
        std::vector<glm::mat4>& worldOut) const;

private:
    std::vector<Bone> bones_;
};

}  // namespace finevox
