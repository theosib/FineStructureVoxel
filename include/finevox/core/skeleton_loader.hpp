#pragma once

/**
 * @file skeleton_loader.hpp
 * @brief Loads Skeleton and AnimationClip from config files
 */

#include "finevox/core/skeleton.hpp"
#include "finevox/core/animation_clip.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace finevox {

class SkeletonLoader {
public:
    /// Load a skeleton from a .skeleton file
    [[nodiscard]] static std::optional<Skeleton> loadSkeleton(const std::string& path);

    /// Load a skeleton from a string
    [[nodiscard]] static std::optional<Skeleton> loadSkeletonFromString(std::string_view content);

    /// Load an animation clip from a .anim file
    [[nodiscard]] static std::optional<AnimationClip> loadAnimation(
        const std::string& path, const Skeleton& skeleton);

    /// Load an animation clip from a string
    [[nodiscard]] static std::optional<AnimationClip> loadAnimationFromString(
        std::string_view content, const Skeleton& skeleton);
};

}  // namespace finevox
