#pragma once

/**
 * @file particle_system.hpp
 * @brief CPU-side particle simulation — runs on game thread at fixed rate.
 *
 * ParticleSystem owns authoritative particle state. The renderer reads
 * particles via snapshot() each frame (internally thread-safe via shared_mutex).
 *
 * Design:
 *   - Emitters spawn particles at configured rate/burst
 *   - Particles age, move, optionally collide with terrain, die
 *   - Global cap evicts oldest particle first when over budget
 *   - Subsystem wrapper ticks this at TickPhase::LateTick (after entity physics)
 */

#include "finevox/core/particle.hpp"
#include "finevox/core/particle_type.hpp"

#include <random>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class World;
class EntityManager;

class ParticleSystem {
public:
    explicit ParticleSystem(World& world);

    // ========================================================================
    // Emitter control
    // ========================================================================

    /// Spawn an emitter at the given world position.
    /// Returns emitter ID, or INVALID_EMITTER_ID if the preset is unknown.
    EmitterId spawnEmitter(std::string_view presetName, glm::dvec3 position,
                           float durationOverride = -2.0f);

    /// Spawn an emitter attached to an entity (position follows entity).
    EmitterId spawnEmitterAttached(std::string_view presetName, EntityId entityId,
                                   float durationOverride = -2.0f);

    /// Despawn an emitter by ID. Returns true if found.
    bool despawnEmitter(EmitterId id);

    /// Check if an emitter is still active.
    [[nodiscard]] bool isEmitterActive(EmitterId id) const;

    /// Fire-and-forget single burst at the given position.
    /// Returns number of particles actually spawned.
    size_t burst(std::string_view presetName, glm::dvec3 position);

    // ========================================================================
    // Tick + snapshot
    // ========================================================================

    /// Advance simulation one step. entities is optional — when supplied, emitters
    /// attached to an EntityId follow that entity's position.
    void tick(float dt, const EntityManager* entities = nullptr);

    /// Snapshot structure for rendering / testing.
    struct Snapshot {
        std::vector<Particle> particles;
    };

    /// Copy out all current particles. Thread-safe.
    [[nodiscard]] Snapshot snapshot() const;

    // ========================================================================
    // Stats / control
    // ========================================================================

    [[nodiscard]] size_t activeParticleCount() const;
    [[nodiscard]] size_t activeEmitterCount() const;

    /// Maximum number of live particles (default 8192).
    void setMaxParticles(size_t cap);
    [[nodiscard]] size_t maxParticles() const;

    /// Deterministic RNG seed (for tests). 0 = random_device.
    void setSeed(uint64_t seed);

    /// Remove all particles and emitters.
    void clear();

private:
    World& world_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<EmitterId, ParticleEmitter> emitters_;
    std::vector<Particle> particles_;

    EmitterId nextEmitterId_ = 1;
    size_t maxParticles_ = 8192;
    std::mt19937 rng_;

    // Internal helpers (caller holds unique_lock)
    void spawnParticleUnlocked(const ParticleTypeDef& type,
                                const ParticleEmitter& emitter,
                                const EmitterPresetDef& preset);
    glm::dvec3 sampleEmitterPositionUnlocked(const EmitterPresetDef& preset,
                                              const glm::dvec3& origin);
    glm::vec3 sampleUnitSphereUnlocked();

    /// Returns true if there is a solid block at the given world position.
    [[nodiscard]] bool isSolidAt(const glm::dvec3& pos) const;

    /// Drop oldest particles until we're below cap (caller holds unique_lock).
    void enforceCapUnlocked();
};

} // namespace finevox
