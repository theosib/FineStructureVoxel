#pragma once

/**
 * @file particle_bridge.hpp
 * @brief Registers finescript native functions for particle spawning/queries.
 *
 * Scripts running on the game thread can spawn/despawn particle emitters and
 * fire one-shot bursts.
 *
 * Natives provided:
 *   - spawn_particle_emitter(preset, x, y, z, [duration])    → emitter_id
 *   - spawn_particle_emitter_on_entity(preset, entity_id, [duration]) → emitter_id
 *   - despawn_particle_emitter(emitter_id) → nil
 *   - particle_emitter_active(emitter_id) → bool
 *   - particle_burst(preset, x, y, z) → particle_count_spawned
 *   - active_particle_count() → int
 */

#include <finescript/script_engine.h>

namespace finevox {

class ParticleSystem;

namespace script {

class ParticleBridge {
public:
    explicit ParticleBridge(ParticleSystem& system) : system_(system) {}

    /// Register all particle natives on the given engine.
    void registerNativeFunctions(finescript::ScriptEngine& engine);

private:
    ParticleSystem& system_;
};

} // namespace script
} // namespace finevox
