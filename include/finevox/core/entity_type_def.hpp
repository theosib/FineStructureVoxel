#pragma once

/**
 * @file entity_type_def.hpp
 * @brief Static data for an entity type (loaded from .entity files)
 */

#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/loot_table.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/core/data_container.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
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
    /// How far from others of its kind, or any type that has some, it likes to stand: one standing
    /// closer than the sum of theirs is eased away. One walking isn't, so nothing jams. 0 lets them
    /// stand in one another.
    float personalSpace = 0.0f;

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
    float deathSeconds = 0.0f;     // How long the body stays on view; 0 = gone at once
    std::string deathEmitter;      // Burst as the body goes, in its faction's colour

    /// Named animation states: "idle" → 0, "walk" → 1, "attack" → 2, etc.
    /// Loaded from `animation_states` block in .entity files.
    /// Scripts use names; C++ goals use the resolved slot ID.
    std::unordered_map<std::string, uint8_t> animationStates;

    /// Resolve an animation name to its slot ID. Returns defaultSlot if not found.
    [[nodiscard]] uint8_t resolveAnimation(std::string_view name, uint8_t defaultSlot = 0) const {
        auto it = animationStates.find(std::string(name));
        return it != animationStates.end() ? it->second : defaultSlot;
    }

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

    // Extensible properties (mod/script data from .entity files)
    // Unknown keys from .entity files are stored here.
    std::unique_ptr<DataContainer> properties;

    /// Get a property value with default fallback
    template<typename T>
    [[nodiscard]] T getProperty(std::string_view key, T defaultVal = T{}) const {
        return properties ? properties->get<T>(key, std::move(defaultVal)) : defaultVal;
    }
};

}  // namespace finevox
