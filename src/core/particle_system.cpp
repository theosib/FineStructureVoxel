#include "finevox/core/particle_system.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity.hpp"
#include "finevox/core/physics.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace finevox {

ParticleSystem::ParticleSystem(World& world)
    : world_(world),
      rng_(std::random_device{}()) {}

// ============================================================================
// Emitter control
// ============================================================================

EmitterId ParticleSystem::spawnEmitter(std::string_view presetName,
                                        glm::dvec3 position,
                                        float durationOverride)
{
    const EmitterPresetDef* preset = EmitterPresetRegistry::global().get(presetName);
    if (!preset) return INVALID_EMITTER_ID;

    std::unique_lock lock(mutex_);
    ParticleEmitter emitter;
    emitter.id = nextEmitterId_++;
    if (emitter.id == INVALID_EMITTER_ID) emitter.id = nextEmitterId_++;
    emitter.presetId = EmitterPresetId::fromName(presetName);
    emitter.position = position;
    emitter.duration = (durationOverride > -1.5f) ? durationOverride : preset->duration;
    emitter.active = true;
    emitters_[emitter.id] = emitter;
    return emitter.id;
}

EmitterId ParticleSystem::spawnEmitterAttached(std::string_view presetName,
                                                EntityId entityId,
                                                float durationOverride)
{
    const EmitterPresetDef* preset = EmitterPresetRegistry::global().get(presetName);
    if (!preset) return INVALID_EMITTER_ID;

    std::unique_lock lock(mutex_);
    ParticleEmitter emitter;
    emitter.id = nextEmitterId_++;
    if (emitter.id == INVALID_EMITTER_ID) emitter.id = nextEmitterId_++;
    emitter.presetId = EmitterPresetId::fromName(presetName);
    emitter.attachedEntity = entityId;
    emitter.duration = (durationOverride > -1.5f) ? durationOverride : preset->duration;
    emitter.active = true;
    emitters_[emitter.id] = emitter;
    return emitter.id;
}

bool ParticleSystem::despawnEmitter(EmitterId id) {
    std::unique_lock lock(mutex_);
    auto it = emitters_.find(id);
    if (it == emitters_.end()) return false;
    emitters_.erase(it);
    return true;
}

bool ParticleSystem::isEmitterActive(EmitterId id) const {
    std::shared_lock lock(mutex_);
    auto it = emitters_.find(id);
    return it != emitters_.end() && it->second.active && !it->second.isExpired();
}

size_t ParticleSystem::burst(std::string_view presetName, glm::dvec3 position) {
    const EmitterPresetDef* preset = EmitterPresetRegistry::global().get(presetName);
    if (!preset) return 0;
    const ParticleTypeDef* type = ParticleTypeRegistry::global().get(preset->particleType);
    if (!type) return 0;

    std::unique_lock lock(mutex_);

    // Synthetic one-shot emitter (not retained)
    ParticleEmitter scratch;
    scratch.presetId = EmitterPresetId::fromName(presetName);
    scratch.position = position;

    size_t count = preset->burstCount > 0 ? preset->burstCount : 1;
    size_t spawned = 0;
    for (size_t i = 0; i < count; ++i) {
        if (particles_.size() >= maxParticles_) {
            enforceCapUnlocked();
            if (particles_.size() >= maxParticles_) break;
        }
        spawnParticleUnlocked(*type, scratch, *preset);
        ++spawned;
    }
    return spawned;
}

// ============================================================================
// Tick
// ============================================================================

void ParticleSystem::tick(float dt, const EntityManager* entities) {
    if (dt <= 0.0f) return;

    std::unique_lock lock(mutex_);

    // 1. Advance emitters (spawn new particles)
    std::vector<EmitterId> toRemove;
    toRemove.reserve(8);

    for (auto& [id, emitter] : emitters_) {
        if (!emitter.active) {
            toRemove.push_back(id);
            continue;
        }

        const EmitterPresetDef* preset = EmitterPresetRegistry::global().get(emitter.presetId);
        if (!preset) {
            toRemove.push_back(id);
            continue;
        }
        const ParticleTypeDef* type = ParticleTypeRegistry::global().get(preset->particleType);
        if (!type) {
            toRemove.push_back(id);
            continue;
        }

        // Follow attached entity
        if (emitter.attachedEntity != INVALID_ENTITY_ID && entities) {
            const Entity* ent = entities->getEntity(emitter.attachedEntity);
            if (ent) {
                Vec3 p = ent->position();
                emitter.position = glm::dvec3(p.x, p.y, p.z);
            } else {
                // entity gone — despawn emitter
                toRemove.push_back(id);
                continue;
            }
        }

        // Initial burst
        if (!emitter.bursted && preset->burstCount > 0) {
            for (uint32_t i = 0; i < preset->burstCount; ++i) {
                if (particles_.size() >= maxParticles_) {
                    enforceCapUnlocked();
                    if (particles_.size() >= maxParticles_) break;
                }
                spawnParticleUnlocked(*type, emitter, *preset);
            }
            emitter.bursted = true;
        }

        // Continuous spawn rate
        if (preset->spawnRate > 0.0f) {
            emitter.spawnAccumulator += preset->spawnRate * dt;
            while (emitter.spawnAccumulator >= 1.0f) {
                if (particles_.size() >= maxParticles_) {
                    enforceCapUnlocked();
                    if (particles_.size() >= maxParticles_) break;
                }
                spawnParticleUnlocked(*type, emitter, *preset);
                emitter.spawnAccumulator -= 1.0f;
            }
        }

        emitter.age += dt;
        if (emitter.isExpired()) {
            toRemove.push_back(id);
        }
    }

    for (auto id : toRemove) emitters_.erase(id);

    // 2. Advance particles.
    //
    // When a particle enables terrain collision, we sub-step so the displacement
    // per sub-step never exceeds ~0.25 blocks. Without this, a fast particle
    // can jump over a thin-floor / single-block obstacle between ticks and the
    // endpoint-only test misses the contact.
    for (auto& p : particles_) {
        p.age += dt;

        // Gravity (positive gravity = pulled down).
        if (p.gravity != 0.0f) {
            p.velocity.y -= p.gravity * dt;
        }

        if (p.drag > 0.0f) {
            float k = std::max(0.0f, 1.0f - p.drag * dt);
            p.velocity *= k;
        }

        int subSteps = 1;
        if (p.flags & ParticleFlag::Collides) {
            float speed = glm::length(p.velocity);
            float displacement = speed * dt;
            // 0.25 block max per sub-step; clamp to 8 to keep cost bounded.
            if (displacement > 0.25f) {
                subSteps = std::min(8, static_cast<int>(std::ceil(displacement / 0.25f)));
            }
        }
        float subDt = dt / static_cast<float>(subSteps);

        bool killed = false;
        for (int s = 0; s < subSteps && !killed; ++s) {
            glm::dvec3 nextPos = p.position + glm::dvec3(p.velocity) * static_cast<double>(subDt);

            if ((p.flags & ParticleFlag::Collides) && isSolidAt(nextPos)) {
                if (p.flags & ParticleFlag::KillOnCollision) {
                    p.age = p.maxAge;
                    killed = true;
                    break;
                }

                // Axial reflection: walk one axis at a time from the current
                // position toward the next.  Any axis that would enter a
                // solid block is reflected with restitution; the others
                // advance normally.
                glm::dvec3 prev = p.position;
                glm::dvec3 test = prev;
                bool bounced = false;

                test.x = nextPos.x;
                if (isSolidAt(test)) {
                    p.velocity.x = -p.velocity.x * p.restitution;
                    bounced = true;
                } else {
                    prev.x = nextPos.x;
                }

                test = prev;
                test.y = nextPos.y;
                if (isSolidAt(test)) {
                    p.velocity.y = -p.velocity.y * p.restitution;
                    bounced = true;
                } else {
                    prev.y = nextPos.y;
                }

                test = prev;
                test.z = nextPos.z;
                if (isSolidAt(test)) {
                    p.velocity.z = -p.velocity.z * p.restitution;
                    bounced = true;
                } else {
                    prev.z = nextPos.z;
                }

                p.position = prev;
                if (!bounced && isSolidAt(p.position)) {
                    p.age = p.maxAge;
                    killed = true;
                }
            } else {
                p.position = nextPos;
            }
        }
    }

    // 3. Remove dead particles
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p) { return p.isDead(); }),
        particles_.end());

    // 4. Enforce cap (should usually be a no-op here)
    enforceCapUnlocked();
}

// ============================================================================
// Snapshot / stats
// ============================================================================

ParticleSystem::Snapshot ParticleSystem::snapshot() const {
    std::shared_lock lock(mutex_);
    Snapshot s;
    s.particles = particles_;
    return s;
}

size_t ParticleSystem::activeParticleCount() const {
    std::shared_lock lock(mutex_);
    return particles_.size();
}

size_t ParticleSystem::activeEmitterCount() const {
    std::shared_lock lock(mutex_);
    return emitters_.size();
}

void ParticleSystem::setMaxParticles(size_t cap) {
    std::unique_lock lock(mutex_);
    maxParticles_ = cap;
    enforceCapUnlocked();
}

size_t ParticleSystem::maxParticles() const {
    std::shared_lock lock(mutex_);
    return maxParticles_;
}

void ParticleSystem::setSeed(uint64_t seed) {
    std::unique_lock lock(mutex_);
    rng_.seed(seed ? seed : std::random_device{}());
}

void ParticleSystem::clear() {
    std::unique_lock lock(mutex_);
    particles_.clear();
    emitters_.clear();
}

// ============================================================================
// Internals
// ============================================================================

void ParticleSystem::enforceCapUnlocked() {
    if (particles_.size() <= maxParticles_) return;
    // Drop oldest (by age) first. Simplest: partial_sort by age desc and truncate.
    // But we don't want to disturb order much; use nth_element to find the threshold.
    size_t over = particles_.size() - maxParticles_;
    // Oldest = largest age. Move 'over' largest-age particles to the end, then erase.
    std::nth_element(
        particles_.begin(), particles_.end() - over, particles_.end(),
        [](const Particle& a, const Particle& b) { return a.age < b.age; });
    particles_.resize(maxParticles_);
}

glm::vec3 ParticleSystem::sampleUnitSphereUnlocked() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    // Rejection sample inside unit sphere to avoid biasing corners.
    for (int i = 0; i < 8; ++i) {
        glm::vec3 v(dist(rng_), dist(rng_), dist(rng_));
        float l2 = glm::dot(v, v);
        if (l2 > 0.0001f && l2 <= 1.0f) {
            return v / std::sqrt(l2);
        }
    }
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::dvec3 ParticleSystem::sampleEmitterPositionUnlocked(const EmitterPresetDef& preset,
                                                           const glm::dvec3& origin)
{
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_real_distribution<float> um11(-1.0f, 1.0f);

    switch (preset.shape) {
        case EmitterShape::Point:
            return origin;

        case EmitterShape::Sphere: {
            float r = preset.shapeSize.x;
            glm::vec3 d = sampleUnitSphereUnlocked();
            float frac = std::cbrt(u01(rng_));
            return origin + glm::dvec3(d) * static_cast<double>(r * frac);
        }

        case EmitterShape::Box: {
            return origin + glm::dvec3(
                um11(rng_) * preset.shapeSize.x,
                um11(rng_) * preset.shapeSize.y,
                um11(rng_) * preset.shapeSize.z);
        }

        case EmitterShape::Cone: {
            // Cone axis = +Y, radius = shapeSize.x, height = shapeSize.y
            float h = u01(rng_) * preset.shapeSize.y;
            float radiusAtH = preset.shapeSize.x * (h / std::max(0.0001f, preset.shapeSize.y));
            float theta = u01(rng_) * glm::two_pi<float>();
            float r = radiusAtH * std::sqrt(u01(rng_));
            return origin + glm::dvec3(r * std::cos(theta), h, r * std::sin(theta));
        }

        case EmitterShape::Line: {
            float t = u01(rng_);
            return origin + glm::dvec3(preset.shapeSize.x * t, 0.0, 0.0);
        }
    }
    return origin;
}

void ParticleSystem::spawnParticleUnlocked(const ParticleTypeDef& type,
                                            const ParticleEmitter& emitter,
                                            const EmitterPresetDef& preset)
{
    Particle p;
    p.typeIdRaw = ParticleTypeId::fromName(type.name.empty() ? preset.particleType : type.name).id;
    p.position = sampleEmitterPositionUnlocked(preset, emitter.position);

    glm::vec3 dir = sampleUnitSphereUnlocked();
    float speed = type.initialSpeed.sample(rng_);
    p.velocity = dir * speed + type.velocityBias;

    p.age = 0.0f;
    p.maxAge = std::max(0.01f, type.lifetime.sample(rng_));

    p.sizeStart = std::max(0.0f, type.size.sample(rng_));
    p.sizeEnd = p.sizeStart * type.sizeEndScale;

    p.startTint = type.startTint;
    p.endTint = type.endTint;
    if (preset.hasTint) {
        p.startTint *= preset.tint;
        p.endTint *= preset.tint;
    }

    // Atlas UV — copied from the type def, which ParticleAtlas patches at
    // startup (see render::ParticleAtlas::applyToRegistry).  When no atlas
    // is loaded the default (0,0,1,1) leaves an untextured-but-tinted quad.
    p.atlasUV = type.atlasUV;

    p.gravity = type.gravity;
    p.drag = type.drag;
    p.restitution = type.restitution;
    p.billboard = type.billboard;

    uint8_t flags = 0;
    if (type.collidesWithTerrain) flags |= ParticleFlag::Collides;
    if (type.killOnCollision)     flags |= ParticleFlag::KillOnCollision;
    if (type.gravity != 0.0f)     flags |= ParticleFlag::AffectedByGravity;
    p.flags = flags;

    particles_.push_back(p);
}

bool ParticleSystem::isSolidAt(const glm::dvec3& pos) const {
    BlockCoord bc = toBlockCoord(Vec3(
        static_cast<float>(pos.x),
        static_cast<float>(pos.y),
        static_cast<float>(pos.z)));
    BlockTypeId blockId = world_.getBlock(bc);
    if (blockId.isAir()) return false;
    const BlockType& bt = BlockRegistry::global().getType(blockId);
    return bt.hasCollision();
}

} // namespace finevox
