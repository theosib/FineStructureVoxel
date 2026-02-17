#pragma once

/**
 * @file entity_serializer.hpp
 * @brief Serialization/deserialization for entities to CBOR bytes
 *
 * Entities are serialized as an array of DataContainers, each containing
 * the entity's state. The serialized data can be stored in a ChunkColumn's
 * DataContainer under the "entities" key.
 *
 * EntityIds are NOT preserved across save/load — they are regenerated
 * when entities are respawned.
 */

#include "finevox/core/entity.hpp"
#include "finevox/core/data_container.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace finevox {

class MobEntity;

class EntitySerializer {
public:
    /// Serialize a list of entities to CBOR bytes
    [[nodiscard]] static std::vector<uint8_t> serialize(
        const std::vector<const Entity*>& entities);

    /// Deserialize entities from CBOR bytes
    /// Returns reconstructed entities (with INVALID_ENTITY_ID — caller assigns IDs)
    [[nodiscard]] static std::vector<std::unique_ptr<Entity>> deserialize(
        std::span<const uint8_t> data);

    /// Serialize a single entity to a DataContainer
    [[nodiscard]] static std::unique_ptr<DataContainer> entityToData(const Entity& entity);

    /// Deserialize a single entity from a DataContainer
    /// Returns nullptr if the data is invalid
    [[nodiscard]] static std::unique_ptr<Entity> entityFromData(const DataContainer& data);
};

}  // namespace finevox
