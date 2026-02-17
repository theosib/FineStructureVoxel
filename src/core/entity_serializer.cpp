#include "finevox/core/entity_serializer.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/cbor.hpp"
#include "finevox/core/string_interner.hpp"
#include <iostream>

namespace finevox {

// ============================================================================
// entityToData - Serialize single entity to DataContainer
// ============================================================================

std::unique_ptr<DataContainer> EntitySerializer::entityToData(const Entity& entity) {
    auto dc = std::make_unique<DataContainer>();

    // Entity type - stored as the EntityType enum value
    dc->set<int64_t>("entity_type", static_cast<int64_t>(entity.type()));

    // Position
    dc->set<double>("x", entity.position().x);
    dc->set<double>("y", entity.position().y);
    dc->set<double>("z", entity.position().z);

    // Velocity
    dc->set<double>("vx", entity.velocity().x);
    dc->set<double>("vy", entity.velocity().y);
    dc->set<double>("vz", entity.velocity().z);

    // Half extents
    dc->set<double>("hx", entity.halfExtents().x);
    dc->set<double>("hy", entity.halfExtents().y);
    dc->set<double>("hz", entity.halfExtents().z);

    // Look direction
    dc->set<double>("yaw", entity.yaw());
    dc->set<double>("pitch", entity.pitch());

    // Physics
    dc->set<int64_t>("on_ground", entity.isOnGround() ? 1 : 0);
    dc->set<int64_t>("has_gravity", entity.hasGravity() ? 1 : 0);

    // Animation
    dc->set<int64_t>("anim_id", entity.animationId());
    dc->set<double>("anim_time", entity.animationTime());

    // MobEntity-specific fields
    const auto* mob = dynamic_cast<const MobEntity*>(&entity);
    if (mob) {
        // Store the type ID name for reconstruction
        auto typeName = mob->typeId().name();
        if (!typeName.empty()) {
            dc->set<std::string>("type_name", std::string(typeName));
        }

        dc->set<double>("health", mob->health());
        dc->set<double>("max_health", mob->maxHealth());
        dc->set<double>("speed_mult", mob->speedMultiplier());
        dc->set<int64_t>("is_mob", 1);
    }

    // Per-entity extra data (DataContainer)
    if (entity.entityData()) {
        dc->set("extra_data", entity.entityData()->clone());
    }

    return dc;
}

// ============================================================================
// entityFromData - Deserialize single entity from DataContainer
// ============================================================================

std::unique_ptr<Entity> EntitySerializer::entityFromData(const DataContainer& data) {
    bool isMob = data.get<int64_t>("is_mob", 0) != 0;

    std::unique_ptr<Entity> entity;

    if (isMob) {
        auto typeName = data.get<std::string>("type_name");
        auto typeId = EntityTypeId::fromName(typeName);

        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
        mob->setMaxHealth(static_cast<float>(data.get<double>("max_health", 20.0)));
        mob->setHealth(static_cast<float>(data.get<double>("health", 20.0)));
        mob->setSpeedMultiplier(static_cast<float>(data.get<double>("speed_mult", 1.0)));
        entity = std::move(mob);
    } else {
        auto entityType = static_cast<EntityType>(
            static_cast<uint16_t>(data.get<int64_t>("entity_type", 0)));
        entity = std::make_unique<Entity>(INVALID_ENTITY_ID, entityType);
    }

    // Position
    entity->setPosition(Vec3(
        static_cast<float>(data.get<double>("x", 0.0)),
        static_cast<float>(data.get<double>("y", 0.0)),
        static_cast<float>(data.get<double>("z", 0.0))
    ));

    // Velocity
    entity->setVelocity(Vec3(
        static_cast<float>(data.get<double>("vx", 0.0)),
        static_cast<float>(data.get<double>("vy", 0.0)),
        static_cast<float>(data.get<double>("vz", 0.0))
    ));

    // Half extents
    entity->setHalfExtents(Vec3(
        static_cast<float>(data.get<double>("hx", 0.3)),
        static_cast<float>(data.get<double>("hy", 0.9)),
        static_cast<float>(data.get<double>("hz", 0.3))
    ));

    // Look direction
    entity->setYaw(static_cast<float>(data.get<double>("yaw", 0.0)));
    entity->setPitch(static_cast<float>(data.get<double>("pitch", 0.0)));

    // Physics
    entity->setOnGround(data.get<int64_t>("on_ground", 0) != 0);
    entity->setHasGravity(data.get<int64_t>("has_gravity", 1) != 0);

    // Animation
    entity->setAnimation(static_cast<uint8_t>(data.get<int64_t>("anim_id", 0)));

    // Per-entity extra data
    auto* extra = data.getChild("extra_data");
    if (extra) {
        auto& entityData = entity->getOrCreateEntityData();
        extra->forEach([&](DataKey key, const DataValue& val) {
            entityData.set(key, DataContainer::cloneValue(val));
        });
    }

    return entity;
}

// ============================================================================
// serialize - Serialize multiple entities to CBOR bytes
// ============================================================================

std::vector<uint8_t> EntitySerializer::serialize(
    const std::vector<const Entity*>& entities)
{
    // Wrap in a parent DataContainer with indexed children
    DataContainer parent;
    parent.set<int64_t>("count", static_cast<int64_t>(entities.size()));

    for (size_t i = 0; i < entities.size(); ++i) {
        auto dc = entityToData(*entities[i]);
        parent.set(std::to_string(i), std::move(dc));
    }

    return parent.toCBOR();
}

// ============================================================================
// deserialize - Deserialize multiple entities from CBOR bytes
// ============================================================================

std::vector<std::unique_ptr<Entity>> EntitySerializer::deserialize(
    std::span<const uint8_t> data)
{
    std::vector<std::unique_ptr<Entity>> result;

    auto parent = DataContainer::fromCBOR(data);
    if (!parent) return result;

    int64_t count = parent->get<int64_t>("count", 0);
    result.reserve(static_cast<size_t>(count));

    for (int64_t i = 0; i < count; ++i) {
        auto* child = parent->getChild(std::to_string(i));
        if (!child) continue;

        auto entity = entityFromData(*child);
        if (entity) {
            result.push_back(std::move(entity));
        }
    }

    return result;
}

}  // namespace finevox
