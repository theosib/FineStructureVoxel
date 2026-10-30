#pragma once

/**
 * @file entity_model.hpp
 * @brief Data-driven box models for entities
 *
 * `.emodel` files bind a skeleton and animation clips to coloured boxes:
 *
 *     skeleton: humanoid          # models/humanoid.skeleton
 *     anim:idle: humanoid_idle    # models/humanoid_idle.anim
 *     anim:walk: humanoid_walk
 *     part: torso                 # name is documentation only
 *     bone: body
 *     offset: 0 0.1 0             # centre of the box, relative to the bone
 *     size: 0.5 0.6 0.3
 *     color: 0.5 0.4 0.3
 *     team: 1.0                   # 0..1 blend toward the entity's faction colour
 *
 * Keys following `part:` apply to that part. They must not be indented: the
 * config parser reads indented lines as continuation data.
 *
 * Skeletons and clips are shared by name, so many models can use one rig.
 */

#include "finevox/core/animation_clip.hpp"
#include "finevox/core/entity_mesh.hpp"
#include "finevox/core/skeleton.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

struct EntityModel {
    std::string name;
    std::shared_ptr<const Skeleton> skeleton;
    EntityMesh mesh;
    /// Animation state name -> clip, as named by the entity's `anim:` keys
    std::unordered_map<std::string, std::shared_ptr<const AnimationClip>> clips;
};

class EntityModelRegistry {
public:
    /// Load every .emodel in a directory. Returns the number loaded.
    size_t loadDirectory(const std::filesystem::path& dir);

    /// Parse a model. `resolveFile` reads a sibling file's text by name, so
    /// tests needn't touch the filesystem.
    using FileReader = std::function<std::optional<std::string>(const std::string& fileName)>;
    std::optional<EntityModel> parse(std::string_view name, std::string_view content,
                                     const FileReader& readFile);

    void add(EntityModel model);

    [[nodiscard]] const EntityModel* find(std::string_view name) const;
    [[nodiscard]] size_t size() const { return models_.size(); }

private:
    std::shared_ptr<const Skeleton> skeletonFor(const std::string& name, const FileReader& readFile);

    std::unordered_map<std::string, EntityModel> models_;
    std::unordered_map<std::string, std::shared_ptr<const Skeleton>> skeletons_;
};

}  // namespace finevox
