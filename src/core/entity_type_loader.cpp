#include "finevox/core/entity_type_loader.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/config_parser.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace finevox {

// ============================================================================
// Helper: parse space-separated floats (e.g., "0.3 0.9 0.3")
// ============================================================================

static glm::vec3 parseVec3(std::string_view str, glm::vec3 defaultVal = glm::vec3{0}) {
    std::string tmp{str};
    std::istringstream iss{tmp};
    glm::vec3 v = defaultVal;
    iss >> v.x >> v.y >> v.z;
    return v;
}

// ============================================================================
// loadFromString
// ============================================================================

std::optional<EntityTypeDef> EntityTypeLoader::loadFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);

    if (doc.empty()) return std::nullopt;

    EntityTypeDef def;

    for (const auto& entry : doc.entries()) {
        if (entry.key == "name") {
            def.name = entry.value.asStringOwned();
        } else if (entry.key == "half_extents") {
            def.halfExtents = parseVec3(entry.value.asString(), def.halfExtents);
        } else if (entry.key == "max_speed") {
            def.maxSpeed = entry.value.asFloat(def.maxSpeed);
        } else if (entry.key == "jump_strength") {
            def.jumpStrength = entry.value.asFloat(def.jumpStrength);
        } else if (entry.key == "gravity") {
            def.hasGravity = entry.value.asBool(def.hasGravity);
        } else if (entry.key == "swims") {
            def.swims = entry.value.asBool(def.swims);
        } else if (entry.key == "max_health") {
            def.maxHealth = entry.value.asFloat(def.maxHealth);
        } else if (entry.key == "armor") {
            def.armor = entry.value.asFloat(def.armor);
        } else if (entry.key == "ai") {
            def.aiType = parseAIType(entry.value.asString());
        } else if (entry.key == "follow_range") {
            def.followRange = entry.value.asFloat(def.followRange);
        } else if (entry.key == "attack_damage") {
            def.attackDamage = entry.value.asFloat(def.attackDamage);
        } else if (entry.key == "attack_range") {
            def.attackRange = entry.value.asFloat(def.attackRange);
        } else if (entry.key == "attack_cooldown") {
            def.attackCooldown = entry.value.asFloat(def.attackCooldown);
        } else if (entry.key == "model") {
            def.model = StringInterner::global().intern(entry.value.asString());
        } else if (entry.key == "default_animation") {
            def.defaultAnimation = StringInterner::global().intern(entry.value.asString());
        } else if (entry.key == "spawn_weight") {
            def.spawnWeight = entry.value.asFloat(def.spawnWeight);
        } else if (entry.key == "spawn_group_min") {
            def.spawnGroupMin = entry.value.asInt(def.spawnGroupMin);
        } else if (entry.key == "spawn_group_max") {
            def.spawnGroupMax = entry.value.asInt(def.spawnGroupMax);
        } else if (entry.key == "loot") {
            def.lootTable = LootTableId::fromName(entry.value.asString());
        } else if (entry.key == "script") {
            def.script = entry.value.asStringOwned();
        } else if (entry.key == "sounds") {
            def.soundSet = SoundSetId::fromName(entry.value.asString());
        }
    }

    if (def.name.empty()) return std::nullopt;
    return def;
}

// ============================================================================
// loadFromFile
// ============================================================================

std::optional<EntityTypeDef> EntityTypeLoader::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromString(content);
}

// ============================================================================
// loadDirectory
// ============================================================================

size_t EntityTypeLoader::loadDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".entity") continue;

        auto def = loadFromFile(entry.path().string());
        if (def) {
            auto name = def->name;
            if (EntityTypeRegistry::global().registerType(name, std::move(*def))) {
                ++count;
            }
        }
    }

    return count;
}

}  // namespace finevox
