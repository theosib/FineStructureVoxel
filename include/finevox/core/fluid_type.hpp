#pragma once

/**
 * @file fluid_type.hpp
 * @brief Data struct for fluid type properties
 *
 * All properties are data-driven via .fluid config files.
 * Games define custom fluid types with per-type flow, visual,
 * physics, light, and interaction properties.
 */

#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/sound_event.hpp"  // SoundSetId
#include <glm/vec4.hpp>
#include <string>
#include <cstdint>

namespace finevox {

struct FluidType {
    // ========================================================================
    // Identity
    // ========================================================================
    std::string name;        // e.g., "water", "lava"
    FluidTypeId id;          // Interned ID (set on registration)

    // ========================================================================
    // Flow Properties
    // ========================================================================
    int32_t spreadDecay = 1;        // Level decrease per horizontal step
    int32_t flowSpeed = 5;          // Ticks between flow updates (higher = slower)
    bool sourceFormation = true;    // Can flowing cells become sources?
    int32_t sourceFormationCount = 2;  // Adjacent sources needed to form new source
    float slopePreference = 1.0f;   // Weight for flowing toward drops (1.0 = normal)
    int32_t maxLevel = 14;          // Maximum flowing level (source = 15)

    // ========================================================================
    // Infiltration Rules
    // ========================================================================
    bool infiltratesNonFull = true;   // Can enter non-full blocks (stairs, slabs)?
    bool infiltratesBelow = true;     // Can seep into non-full blocks below?

    // ========================================================================
    // Physics Properties
    // ========================================================================
    float density = 1000.0f;         // kg/m^3 (water=1000, lava=3100)
    float viscosity = 1.0f;          // Drag multiplier (water=1.0, lava=4.0)
    float buoyancyFactor = 1.0f;     // Buoyancy strength multiplier
    float flowForce = 0.5f;          // Force applied by flowing fluid on entities
    bool canDisplace = true;         // Can push items/entities?

    // ========================================================================
    // Visual Properties
    // ========================================================================
    bool opaque = false;             // Fully opaque (lava, milk)?
    glm::vec4 tintColor{0.2f, 0.4f, 0.9f, 0.6f};  // RGBA tint
    std::string texture;             // Texture resource path
    float surfaceTexSpeed = 1.0f;    // Surface texture animation speed

    // ========================================================================
    // Light Properties
    // ========================================================================
    uint8_t lightEmission = 0;       // Block light emitted (0-15; lava=15)
    uint8_t lightAttenuation = 2;    // Standard per-block attenuation (opaque fluids)
    bool customAttenuation = false;  // Use logarithmic attenuation model?
    float attenuationBase = 0.85f;   // Base for logarithmic model: light * pow(base, depth)

    // ========================================================================
    // Sound Properties
    // ========================================================================
    SoundSetId soundSet;             // Sound set for splash, flow, ambient

    // ========================================================================
    // Damage Properties
    // ========================================================================
    float contactDamage = 0.0f;      // Damage per second on contact
    float submersionDamage = 0.0f;   // Damage per second when submerged

    // ========================================================================
    // Fog Properties (underwater camera)
    // ========================================================================
    glm::vec4 underwaterFogColor{0.1f, 0.2f, 0.4f, 1.0f};  // Fog color when submerged
    float underwaterFogDensity = 0.04f;  // Fog density when submerged

    // ========================================================================
    // Container Properties
    // ========================================================================
    int32_t unitsPerSource = 1000;   // Fractional units per source block
};

}  // namespace finevox
