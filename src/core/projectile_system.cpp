#include "finevox/core/projectile_system.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>

namespace finevox {

namespace {

/// Flight is subdivided so a fast projectile can't tunnel through a one-block wall
constexpr double MAX_STEP = 0.4;

}  // namespace

std::optional<glm::dvec3> solveBallistic(const glm::dvec3& from, const glm::dvec3& to, float speed,
                                         float gravity, bool lobbed) {
    glm::dvec3 delta = to - from;
    double horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    double v2 = double(speed) * speed;
    double g = gravity;

    if (horizontal < 1e-6) {
        double up = delta.y >= 0.0 ? 1.0 : -1.0;
        return glm::dvec3(0.0, up * speed, 0.0);
    }

    double disc = v2 * v2 - g * (g * horizontal * horizontal + 2.0 * delta.y * v2);
    if (disc < 0.0) return std::nullopt;

    double root = std::sqrt(disc);
    double tanTheta = (v2 + (lobbed ? root : -root)) / (g * horizontal);
    double cosTheta = 1.0 / std::sqrt(1.0 + tanTheta * tanTheta);
    double sinTheta = tanTheta * cosTheta;

    return glm::dvec3(delta.x / horizontal * speed * cosTheta, speed * sinTheta,
                      delta.z / horizontal * speed * cosTheta);
}

ProjectileSystem::ProjectileSystem(World& world, EntityManager& entities)
    : world_(world), entities_(entities) {}

void ProjectileSystem::fire(const ProjectileSpec& spec) {
    Projectile p;
    p.spec = spec;
    p.position = spec.origin;
    p.velocity = spec.velocity;
    active_.push_back(p);
}

bool ProjectileSystem::step(Projectile& p, float dt) {
    p.age += dt;
    if (p.age >= p.spec.lifetime) return true;

    double speed = glm::length(p.velocity);
    int steps = std::max(1, static_cast<int>(std::ceil(speed * dt / MAX_STEP)));
    double slice = double(dt) / steps;

    for (int i = 0; i < steps; ++i) {
        p.velocity.y -= double(p.spec.gravity) * slice;
        glm::dvec3 next = p.position + p.velocity * slice;

        BlockCoord cell(static_cast<int32_t>(std::floor(next.x)),
                        static_cast<int32_t>(std::floor(next.y)),
                        static_cast<int32_t>(std::floor(next.z)));
        BlockTypeId block = world_.getBlock(cell);
        if (!block.isAir() && BlockRegistry::global().getType(block).hasCollision()) {
            ProjectileHit hit;
            hit.position = next;
            hit.block = cell;
            hit.owner = p.spec.owner;
            hit.faction = p.spec.faction;
            hit.damage = p.spec.damage;
            hits_.push_back(hit);
            return true;
        }

        Vec3 center(static_cast<float>(next.x), static_cast<float>(next.y), static_cast<float>(next.z));
        for (EntityId id : entities_.spatialIndex().queryRadius(center, 2.5f)) {
            MobEntity* mob = entities_.getMob(id);
            if (!mob || !mob->isAlive() || mob->isDead()) continue;
            if (id == p.spec.owner) continue;
            if (p.spec.faction != 0 && mob->faction() == p.spec.faction) continue;

            Vec3 pos = mob->position();
            Vec3 half = mob->halfExtents();
            float r = p.spec.hitRadius;
            if (std::abs(center.x - pos.x) > half.x + r) continue;
            if (std::abs(center.z - pos.z) > half.z + r) continue;
            if (center.y < pos.y - r || center.y > pos.y + half.y * 2.0f + r) continue;

            mob->damage(p.spec.damage, p.spec.owner);
            ProjectileHit hit;
            hit.position = next;
            hit.entity = id;
            hit.owner = p.spec.owner;
            hit.faction = p.spec.faction;
            hit.damage = p.spec.damage;
            hits_.push_back(hit);
            return true;
        }

        p.position = next;
    }
    return false;
}

void ProjectileSystem::tick(float dt) {
    auto lock = entities_.lock();
    active_.erase(std::remove_if(active_.begin(), active_.end(),
                                 [this, dt](Projectile& p) { return step(p, dt); }),
                  active_.end());

    std::vector<ProjectileView> view;
    view.reserve(active_.size());
    for (const Projectile& p : active_) {
        view.push_back({p.position, glm::vec3(p.velocity), p.spec.faction});
    }
    std::lock_guard<std::mutex> guard(viewMutex_);
    view_ = std::move(view);
}

std::vector<ProjectileHit> ProjectileSystem::drainHits() {
    std::vector<ProjectileHit> hits;
    hits.swap(hits_);
    return hits;
}

std::vector<ProjectileView> ProjectileSystem::snapshot() const {
    std::lock_guard<std::mutex> guard(viewMutex_);
    return view_;
}

}  // namespace finevox
