#pragma once

/**
 * @file particle_type.hpp
 * @brief Particle and emitter type definitions (immutable, registry-loaded).
 *
 * Data-driven policy: ParticleTypeDef and EmitterPresetDef are loaded from
 * .particle and .emitter files in resources/. The simulation/rendering
 * mechanism lives in C++; the visual policy lives in data.
 */

#include "finevox/core/string_interner.hpp"
#include "finevox/core/position.hpp"
#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace finevox {

// ============================================================================
// Type IDs (interned, same pattern as LootTableId / FluidTypeId)
// ============================================================================

struct ParticleTypeId {
    InternedId id = 0;

    constexpr ParticleTypeId() = default;
    constexpr explicit ParticleTypeId(InternedId id_) : id(id_) {}

    [[nodiscard]] static ParticleTypeId fromName(std::string_view name) {
        return ParticleTypeId{StringInterner::global().intern(name)};
    }

    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const ParticleTypeId&) const = default;
    constexpr auto operator<=>(const ParticleTypeId&) const = default;
};

struct EmitterPresetId {
    InternedId id = 0;

    constexpr EmitterPresetId() = default;
    constexpr explicit EmitterPresetId(InternedId id_) : id(id_) {}

    [[nodiscard]] static EmitterPresetId fromName(std::string_view name) {
        return EmitterPresetId{StringInterner::global().intern(name)};
    }

    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const EmitterPresetId&) const = default;
    constexpr auto operator<=>(const EmitterPresetId&) const = default;
};

// ============================================================================
// Enums
// ============================================================================

enum class BillboardMode : uint8_t {
    CameraFacing = 0,      ///< Quad always perpendicular to view direction
    VelocityStretched = 1, ///< Quad elongated along particle velocity (sparks, rain)
    AxisLocked = 2,        ///< Free rotation around world Y axis (columns of smoke)
};

[[nodiscard]] BillboardMode parseBillboardMode(std::string_view s);
[[nodiscard]] std::string_view billboardModeName(BillboardMode m);

enum class EmitterShape : uint8_t {
    Point = 0,   ///< zero-volume point
    Sphere = 1,  ///< uniform volume inside radius
    Box = 2,     ///< axis-aligned half-extents
    Cone = 3,    ///< apex at origin, axis along +Y by default (shapeSize.x = radius, .y = height)
    Line = 4,    ///< segment along +X of length shapeSize.x
};

[[nodiscard]] EmitterShape parseEmitterShape(std::string_view s);
[[nodiscard]] std::string_view emitterShapeName(EmitterShape s);

// ============================================================================
// Range — inclusive [min, max] uniform sample
// ============================================================================

struct Range {
    float min = 0.0f;
    float max = 0.0f;

    constexpr Range() = default;
    constexpr Range(float v) : min(v), max(v) {}
    constexpr Range(float lo, float hi) : min(lo), max(hi) {}

    [[nodiscard]] float sample(std::mt19937& rng) const {
        if (min >= max) return min;
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }

    /// Parse "a" or "a-b" — same syntax as loot-table ranges.
    [[nodiscard]] static Range parse(std::string_view s);
};

// ============================================================================
// ParticleTypeDef — immutable per-type configuration
// ============================================================================

struct ParticleTypeDef {
    std::string name;                          ///< "finevox:block_fragment"

    Range lifetime{1.0f, 1.0f};                ///< seconds before particle dies

    Range initialSpeed{0.0f, 0.0f};            ///< magnitude of launch velocity
    glm::vec3 velocityBias{0.0f};              ///< constant offset added to velocity

    float gravity = 0.0f;                      ///< m/s^2 downward acceleration (+y = up so we subtract)
    float drag = 0.0f;                         ///< per-second drag coefficient (0 = none)

    Range size{0.1f, 0.1f};                    ///< initial billboard size (meters, side length)
    float sizeEndScale = 1.0f;                 ///< end-of-life size multiplier (0 = shrinks to zero)

    glm::vec4 startTint{1.0f, 1.0f, 1.0f, 1.0f}; ///< RGBA at age 0
    glm::vec4 endTint{1.0f, 1.0f, 1.0f, 1.0f};   ///< RGBA at age = maxAge

    std::string atlasRegion;                   ///< key into ParticleAtlas (empty = untextured)

    /// Atlas UV rect (minU, minV, maxU, maxV) resolved from `atlasRegion` once
    /// a ParticleAtlas is loaded (see render::ParticleAtlas::applyToRegistry).
    /// Defaults to the full [0,1]² rect so sims without a loaded atlas still
    /// produce valid vertices (just untextured).
    glm::vec4 atlasUV{0.0f, 0.0f, 1.0f, 1.0f};

    BillboardMode billboard = BillboardMode::CameraFacing;

    bool collidesWithTerrain = false;          ///< test voxel at next step
    bool killOnCollision = true;               ///< true = die on contact; false = reflect
    float restitution = 0.3f;                  ///< speed retained after reflection (0-1)
};

// ============================================================================
// EmitterPresetDef — immutable emitter configuration
// ============================================================================

struct EmitterPresetDef {
    std::string name;                       ///< "finevox:block_break"

    EmitterShape shape = EmitterShape::Point;
    glm::vec3 shapeSize{0.0f};              ///< radius for sphere; half-extents for box; (radius,height) for cone

    /// Particles per second for continuous emission (0 = burst-only).
    float spawnRate = 0.0f;

    /// Instantaneous spawn count fired once when the emitter starts.
    uint32_t burstCount = 0;

    /// Total lifetime in seconds. Negative = continuous (despawn manually).
    float duration = -1.0f;

    /// Name of ParticleTypeDef this emitter spawns.
    std::string particleType;

    /// Optional per-emitter tint override (multiplied with particle tint).
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    bool hasTint = false;
};

} // namespace finevox

// Hash specializations so IDs can live in unordered_map
template<>
struct std::hash<finevox::ParticleTypeId> {
    size_t operator()(const finevox::ParticleTypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
template<>
struct std::hash<finevox::EmitterPresetId> {
    size_t operator()(const finevox::EmitterPresetId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
