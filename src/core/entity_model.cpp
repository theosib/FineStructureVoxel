#include "finevox/core/entity_model.hpp"
#include "finevox/core/config_parser.hpp"
#include "finevox/core/skeleton_loader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace finevox {

namespace {

glm::vec3 parseVec3(std::string_view text, glm::vec3 fallback) {
    std::istringstream stream{std::string(text)};
    glm::vec3 v = fallback;
    stream >> v.x >> v.y >> v.z;
    return v;
}

std::vector<std::string> words(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::vector<std::string> out;
    for (std::string word; stream >> word;) out.push_back(word);
    return out;
}

}  // namespace

std::shared_ptr<const Skeleton> EntityModelRegistry::skeletonFor(const std::string& name,
                                                                 const FileReader& readFile) {
    auto it = skeletons_.find(name);
    if (it != skeletons_.end()) return it->second;

    auto text = readFile(name + ".skeleton");
    if (!text) return nullptr;
    auto skeleton = SkeletonLoader::loadSkeletonFromString(*text);
    if (!skeleton) return nullptr;

    auto shared = std::make_shared<const Skeleton>(std::move(*skeleton));
    skeletons_[name] = shared;
    return shared;
}

std::optional<EntityModel> EntityModelRegistry::parse(std::string_view name, std::string_view content,
                                                      const FileReader& readFile) {
    ConfigParser parser;
    auto doc = parser.parseString(content);

    EntityModel model;
    model.name = std::string(name);

    std::string skeletonName(doc.getString("skeleton"));
    if (skeletonName.empty()) return std::nullopt;
    model.skeleton = skeletonFor(skeletonName, readFile);
    if (!model.skeleton) return std::nullopt;

    EntityMeshPart* part = nullptr;
    for (const auto& entry : doc.entries()) {
        if (entry.key == "anim" && entry.hasSuffix()) {
            auto text = readFile(entry.value.asStringOwned() + ".anim");
            if (!text) continue;
            auto clip = SkeletonLoader::loadAnimationFromString(*text, *model.skeleton);
            if (clip) {
                model.clips[entry.suffix] = std::make_shared<const AnimationClip>(std::move(*clip));
            }
        } else if (entry.key == "texture" && !part) {
            model.texture = entry.value.asStringOwned();
        } else if (entry.key == "texture_size" && !part) {
            glm::vec3 v = parseVec3(std::string(entry.value.asString()) + " 0", glm::vec3(64.0f, 64.0f, 0.0f));
            model.textureSize = glm::vec2(v.x, v.y);
        } else if (entry.key == "part") {
            model.mesh.parts.emplace_back();
            part = &model.mesh.parts.back();
            part->color = glm::vec3(0.7f);
        } else if (part) {
            if (entry.key == "bone") {
                part->boneIndex = model.skeleton->findBone(entry.value.asString());
            } else if (entry.key == "offset") {
                part->offset = parseVec3(entry.value.asString(), part->offset);
            } else if (entry.key == "size") {
                part->size = parseVec3(entry.value.asString(), part->size);
            } else if (entry.key == "color") {
                part->color = parseVec3(entry.value.asString(), part->color);
            } else if (entry.key == "team") {
                part->teamTint = entry.value.asFloat(0.0f);
            } else if (entry.key == "uv") {
                glm::vec3 v = parseVec3(std::string(entry.value.asString()) + " 0", glm::vec3(0.0f));
                part->uvPixels = glm::vec2(v.x, v.y);
                part->textured = true;
            } else if (entry.key == "uv_size") {
                part->uvBox = parseVec3(entry.value.asString(), part->uvBox);
            } else if (entry.key == "mirror") {
                part->mirror = entry.value.asBool(false);
            } else if (entry.key == "turn") {
                part->turnDegrees = parseVec3(entry.value.asString(), part->turnDegrees);
            } else if (entry.key == "only_in") {
                part->onlyIn = words(entry.value.asString());
            } else if (entry.key == "hidden_in") {
                part->hiddenIn = words(entry.value.asString());
            }
        }
    }

    if (model.mesh.isEmpty()) return std::nullopt;
    // A textured part's unwrap is its size at 16 pixels a block unless it says otherwise
    for (auto& p : model.mesh.parts) {
        if (p.textured && p.uvBox == glm::vec3(0.0f)) p.uvBox = p.size * 16.0f;
    }
    return model;
}

void EntityModelRegistry::add(EntityModel model) {
    std::string key = model.name;
    models_[key] = std::move(model);
}

const EntityModel* EntityModelRegistry::find(std::string_view name) const {
    auto it = models_.find(std::string(name));
    return it == models_.end() ? nullptr : &it->second;
}

size_t EntityModelRegistry::loadDirectory(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(dir)) return 0;

    FileReader readFile = [&dir](const std::string& fileName) -> std::optional<std::string> {
        std::ifstream file(dir / fileName);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    };

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".emodel") continue;
        std::string name = entry.path().stem().string();
        auto text = readFile(entry.path().filename().string());
        auto model = text ? parse(name, *text, readFile) : std::nullopt;
        if (!model) {
            std::cerr << "[EntityModel] failed to load " << entry.path() << "\n";
            continue;
        }
        add(std::move(*model));
        ++count;
    }
    return count;
}

}  // namespace finevox
