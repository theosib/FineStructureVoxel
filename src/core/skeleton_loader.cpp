#include "finevox/core/skeleton_loader.hpp"
#include "finevox/core/config_parser.hpp"
#include <fstream>
#include <sstream>

namespace finevox {

// ============================================================================
// Helpers
// ============================================================================

static glm::vec3 parseVec3FromString(std::string_view str, glm::vec3 def = glm::vec3{0}) {
    std::string tmp{str};
    std::istringstream iss{tmp};
    glm::vec3 v = def;
    iss >> v.x >> v.y >> v.z;
    return v;
}

// ============================================================================
// loadSkeleton
// ============================================================================

std::optional<Skeleton> SkeletonLoader::loadSkeletonFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);
    if (doc.empty()) return std::nullopt;

    Skeleton skeleton;
    std::string currentBoneName;
    Bone currentBone;
    bool haveBone = false;

    auto finalizeBone = [&]() {
        if (haveBone && !currentBoneName.empty()) {
            currentBone.name = currentBoneName;
            skeleton.addBone(std::move(currentBone));
            currentBone = Bone{};
        }
    };

    for (const auto& entry : doc.entries()) {
        if (entry.key == "bone") {
            finalizeBone();
            currentBoneName = entry.value.asStringOwned();
            currentBone = Bone{};
            haveBone = true;
        } else if (haveBone) {
            if (entry.key == "parent") {
                auto parentName = entry.value.asString();
                if (parentName == "-" || parentName.empty()) {
                    currentBone.parentIndex = -1;
                } else {
                    currentBone.parentIndex = skeleton.findBone(parentName);
                }
            } else if (entry.key == "position") {
                currentBone.restPosition = parseVec3FromString(entry.value.asString());
            } else if (entry.key == "rotation") {
                auto str = entry.value.asString();
                std::string tmp{str};
                std::istringstream iss{tmp};
                float x = 0, y = 0, z = 0, w = 1;
                iss >> x >> y >> z >> w;
                currentBone.restRotation = glm::quat(w, x, y, z);
            } else if (entry.key == "scale") {
                currentBone.restScale = parseVec3FromString(entry.value.asString(), glm::vec3{1});
            }
        }
    }

    finalizeBone();

    if (skeleton.isEmpty()) return std::nullopt;
    return skeleton;
}

std::optional<Skeleton> SkeletonLoader::loadSkeleton(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadSkeletonFromString(content);
}

// ============================================================================
// loadAnimation
// ============================================================================

std::optional<AnimationClip> SkeletonLoader::loadAnimationFromString(
    std::string_view content, const Skeleton& skeleton)
{
    ConfigParser parser;
    auto doc = parser.parseString(content);
    if (doc.empty()) return std::nullopt;

    AnimationClip clip;

    // Top-level properties
    auto nameStr = doc.getString("name");
    if (!nameStr.empty()) clip.name = std::string(nameStr);
    clip.duration = doc.getFloat("duration", 1.0f);
    clip.looping = doc.getBool("loop", true);

    // Parse bone tracks
    BoneTrack currentTrack;
    bool haveTrack = false;

    auto finalizeTrack = [&]() {
        if (haveTrack && currentTrack.boneIndex >= 0 && !currentTrack.isEmpty()) {
            clip.tracks.push_back(std::move(currentTrack));
            currentTrack = BoneTrack{};
        }
    };

    for (const auto& entry : doc.entries()) {
        if (entry.key == "track") {
            finalizeTrack();
            auto boneName = entry.value.asString();
            currentTrack = BoneTrack{};
            currentTrack.boneIndex = skeleton.findBone(boneName);
            haveTrack = true;
        } else if (haveTrack && entry.key == "keyframe") {
            // keyframe: time
            Keyframe kf;
            kf.time = entry.value.asFloat();
            // Use default pos/rot/scale, will be overridden by data lines
            currentTrack.keyframes.push_back(kf);
        } else if (haveTrack && entry.key == "pos" && !currentTrack.keyframes.empty()) {
            auto& kf = currentTrack.keyframes.back();
            kf.position = parseVec3FromString(entry.value.asString());
        } else if (haveTrack && entry.key == "rot" && !currentTrack.keyframes.empty()) {
            auto& kf = currentTrack.keyframes.back();
            std::string tmp{entry.value.asString()};
            std::istringstream iss{tmp};
            float x = 0, y = 0, z = 0, w = 1;
            iss >> x >> y >> z >> w;
            kf.rotation = glm::quat(w, x, y, z);
        } else if (haveTrack && entry.key == "scale" && !currentTrack.keyframes.empty()) {
            auto& kf = currentTrack.keyframes.back();
            kf.scale = parseVec3FromString(entry.value.asString(), glm::vec3{1});
        }
    }

    finalizeTrack();

    if (clip.isEmpty()) return std::nullopt;
    return clip;
}

std::optional<AnimationClip> SkeletonLoader::loadAnimation(
    const std::string& path, const Skeleton& skeleton)
{
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadAnimationFromString(content, skeleton);
}

}  // namespace finevox
