#pragma once

/**
 * @file spawn_rule.hpp
 * @brief SpawnRule — conditions and parameters for entity spawning
 */

#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/string_interner.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace finevox {

struct SpawnRule {
    EntityTypeId entityType;

    // Environment conditions
    int maxLightLevel = 5;       // Max sky light for spawning (-1 = any)
    int minLightLevel = -1;      // Min sky light (-1 = any)
    std::vector<BlockTypeId> validSurfaces;  // Empty = any solid block
    std::vector<InternedId> validBiomes;     // Empty = any biome

    // Spawn parameters
    float weight = 1.0f;         // Relative selection weight
    int groupMin = 1;
    int groupMax = 4;
    int mobCap = 80;             // Max entities of this type allowed

    // Player distance
    float minPlayerDistance = 32.0f;   // Don't spawn too close
    float maxPlayerDistance = 160.0f;  // Don't spawn too far

    // Extensible conditions (for mods/scripts)
    std::unique_ptr<class DataContainer> conditions;
    std::vector<std::string> customPredicates;  // Names of registered predicates

    [[nodiscard]] bool isValid() const {
        return entityType.isValid() && weight > 0.0f && groupMin >= 1;
    }
};

}  // namespace finevox
