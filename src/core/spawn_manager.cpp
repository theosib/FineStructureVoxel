#include "finevox/core/spawn_manager.hpp"
#include "finevox/core/spawn_predicate.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/light_engine.hpp"
#include <algorithm>

namespace finevox {

SpawnManager::SpawnManager() = default;

void SpawnManager::addRule(SpawnRule rule) {
    rules_.push_back(std::move(rule));
}

void SpawnManager::clearRules() {
    rules_.clear();
}

void SpawnManager::tick(float dt, World& world, EntityManager& em,
                         const std::vector<glm::dvec3>& playerPositions)
{
    if (rules_.empty() || playerPositions.empty()) return;

    spawnTimer_ += dt;
    if (spawnTimer_ < spawnInterval_) return;
    spawnTimer_ = 0.0f;

    // Check global mob cap
    if (static_cast<int>(em.entityCount()) >= globalMobCap_) return;

    // Try to spawn for each player
    for (const auto& playerPos : playerPositions) {
        const SpawnRule* rule = selectRule();
        if (!rule) continue;

        // Check per-type mob cap
        if (countEntitiesOfType(em, rule->entityType) >= rule->mobCap) continue;

        trySpawnGroup(*rule, world, em, playerPos);
    }
}

bool SpawnManager::trySpawnGroup(const SpawnRule& rule, World& world,
                                  EntityManager& em, const glm::dvec3& nearPlayer)
{
    // Pick a random offset from the player within spawn range
    std::uniform_real_distribution<float> angleDist(0.0f, 6.2832f);
    std::uniform_real_distribution<float> distDist(rule.minPlayerDistance,
                                                    rule.maxPlayerDistance);

    float angle = angleDist(rng_);
    float dist = distDist(rng_);

    BlockCoord center(
        static_cast<int>(nearPlayer.x + std::cos(angle) * dist),
        static_cast<int>(nearPlayer.y),
        static_cast<int>(nearPlayer.z + std::sin(angle) * dist)
    );

    auto surface = findSpawnSurface(world, center, rule);
    if (!surface) return false;

    if (!checkLightLevel(world, *surface, rule)) return false;

    // Evaluate custom predicates
    if (!rule.customPredicates.empty()) {
        SpawnContext ctx{world, *surface, 0.0f, dist};
        if (!SpawnPredicateRegistry::global().evaluateAll(rule, ctx)) {
            return false;
        }
    }

    // Determine group size
    std::uniform_int_distribution<int> groupDist(rule.groupMin, rule.groupMax);
    int groupSize = groupDist(rng_);

    int spawned = 0;
    for (int i = 0; i < groupSize; ++i) {
        // Slight offset within the group
        std::uniform_real_distribution<float> spreadDist(-2.0f, 2.0f);
        float ox = spreadDist(rng_);
        float oz = spreadDist(rng_);

        BlockCoord spawnPos(surface->x + static_cast<int>(ox),
                          surface->y,
                          surface->z + static_cast<int>(oz));

        // Verify spawn surface at this offset too
        auto offsetSurface = findSpawnSurface(world, spawnPos, rule);
        if (!offsetSurface) continue;

        // Create MobEntity
        auto mob = std::make_unique<MobEntity>(0, rule.entityType);  // ID assigned by EntityManager
        mob->setPosition(Vec3(
            static_cast<float>(offsetSurface->x) + 0.5f,
            static_cast<float>(offsetSurface->y + 1),
            static_cast<float>(offsetSurface->z) + 0.5f
        ));

        em.spawnEntity(std::move(mob));
        ++spawned;
    }

    return spawned > 0;
}

std::optional<BlockCoord> SpawnManager::findSpawnSurface(
    const World& world, const BlockCoord& near, const SpawnRule& rule) const
{
    // Search downward from a height for a solid block with air above
    int startY = std::min(near.y + 16, 255);
    int endY = std::max(near.y - 16, 0);

    for (int y = startY; y >= endY; --y) {
        BlockCoord checkPos(near.x, y, near.z);
        auto surfaceType = world.getBlock(checkPos);

        // Need solid block below (non-transparent)
        const auto& bt = BlockRegistry::global().getType(surfaceType);
        if (bt.isTransparent()) continue;

        // Check valid surfaces list
        if (!rule.validSurfaces.empty()) {
            bool found = false;
            for (auto s : rule.validSurfaces) {
                if (s == surfaceType) { found = true; break; }
            }
            if (!found) continue;
        }

        // Need air above (2 blocks for entity clearance)
        BlockCoord above1(near.x, y + 1, near.z);
        BlockCoord above2(near.x, y + 2, near.z);
        auto block1 = world.getBlock(above1);
        auto block2 = world.getBlock(above2);

        const auto& bt1 = BlockRegistry::global().getType(block1);
        const auto& bt2 = BlockRegistry::global().getType(block2);

        if (bt1.isTransparent() && bt2.isTransparent()) {
            return BlockCoord(near.x, y, near.z);
        }
    }

    return std::nullopt;
}

bool SpawnManager::checkLightLevel(const World& world, const BlockCoord& pos,
                                    const SpawnRule& rule) const
{
    // No light constraints? Always pass.
    if (rule.maxLightLevel < 0 && rule.minLightLevel < 0) return true;

    // Check the block above the surface via light engine
    BlockCoord checkPos(pos.x, pos.y + 1, pos.z);
    uint8_t light = 0;
    if (auto* engine = world.lightEngine()) {
        light = engine->getSkyLight(checkPos);
    }

    if (rule.maxLightLevel >= 0 && light > rule.maxLightLevel) return false;
    if (rule.minLightLevel >= 0 && light < rule.minLightLevel) return false;
    return true;
}

int SpawnManager::countEntitiesOfType(const EntityManager& em,
                                       EntityTypeId typeId) const
{
    int count = 0;
    for (const auto& [id, entity] : em.entities()) {
        auto* mob = dynamic_cast<const MobEntity*>(entity.get());
        if (mob && mob->typeId() == typeId) ++count;
    }
    return count;
}

const SpawnRule* SpawnManager::selectRule() const {
    if (rules_.empty()) return nullptr;

    float totalWeight = 0.0f;
    for (const auto& rule : rules_) {
        totalWeight += rule.weight;
    }

    if (totalWeight <= 0.0f) return nullptr;

    std::uniform_real_distribution<float> dist(0.0f, totalWeight);
    float roll = dist(rng_);

    float cumulative = 0.0f;
    for (const auto& rule : rules_) {
        cumulative += rule.weight;
        if (roll <= cumulative) return &rule;
    }

    return &rules_.back();
}

}  // namespace finevox
