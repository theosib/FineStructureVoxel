#pragma once

/**
 * @file entity_type_def.hpp
 * @brief Static data for an entity type (loaded from .entity files)
 */

#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/loot_table.hpp"
#include "finevox/core/sound_event.hpp"
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace finevox {

// ============================================================================
// AIType — behavioral category for mob AI
// ============================================================================

enum class AIType : uint8_t {
    Passive,   // Wanders, flees when hit
    Hostile,   // Chases and attacks players on sight
    Neutral,   // Peaceful until attacked, then hostile
    None       // No built-in AI (script-only or special entities)
};

/// Parse AI type from string (e.g., "passive", "hostile", "neutral", "none")
[[nodiscard]] AIType parseAIType(std::string_view str);

/// Get string name for an AI type
[[nodiscard]] std::string_view aiTypeName(AIType type);

// ============================================================================
// EntityTypeDef — static properties for an entity type
// ============================================================================

struct EntityTypeDef {
    // Identity
    std::string name;  // "zombie", "pig", etc.

    // Physics
    glm::vec3 halfExtents{0.3f, 0.9f, 0.3f};
    float maxSpeed = 4.0f;        // blocks/sec
    float jumpStrength = 0.5f;
    bool hasGravity = true;
    bool swims = false;

    // Health
    float maxHealth = 20.0f;
    float armor = 0.0f;

    // Behavior
    AIType aiType = AIType::Passive;
    float followRange = 16.0f;
    float attackDamage = 0.0f;
    float attackRange = 1.5f;
    float attackCooldown = 1.0f;  // seconds

    // Visual
    InternedId model;              // Skeleton/mesh reference (interned)
    InternedId defaultAnimation;   // Default animation name (interned)

    // Spawning
    float spawnWeight = 1.0f;
    int spawnGroupMin = 1;
    int spawnGroupMax = 4;

    // Loot
    LootTableId lootTable;

    // Script (optional — file path)
    std::string script;

    // Sound
    SoundSetId soundSet;
};

}  // namespace finevox
