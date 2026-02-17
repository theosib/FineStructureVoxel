#include "finevox/core/skeleton.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace finevox {

void Skeleton::addBone(Bone bone) {
    bones_.push_back(std::move(bone));
}

int32_t Skeleton::findBone(std::string_view name) const {
    for (int32_t i = 0; i < static_cast<int32_t>(bones_.size()); ++i) {
        if (bones_[i].name == name) return i;
    }
    return -1;
}

const Bone& Skeleton::bone(int32_t index) const {
    return bones_[index];
}

void Skeleton::computeRestPose(std::vector<glm::mat4>& localPoses) const {
    localPoses.resize(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        const auto& b = bones_[i];
        glm::mat4 t = glm::translate(glm::mat4(1.0f), b.restPosition);
        glm::mat4 r = glm::toMat4(b.restRotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), b.restScale);
        localPoses[i] = t * r * s;
    }
}

void Skeleton::computeWorldTransforms(
    const std::vector<glm::mat4>& localPoses,
    std::vector<glm::mat4>& worldOut) const
{
    worldOut.resize(bones_.size());
    for (size_t i = 0; i < bones_.size(); ++i) {
        int32_t parent = bones_[i].parentIndex;
        if (parent < 0) {
            worldOut[i] = localPoses[i];
        } else {
            worldOut[i] = worldOut[parent] * localPoses[i];
        }
    }
}

}  // namespace finevox
