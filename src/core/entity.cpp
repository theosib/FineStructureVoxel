#include "finevox/core/entity.hpp"
#include <cmath>

namespace finevox {

Entity::Entity(EntityId id, EntityType type)
    : id_(id)
    , type_(type)
{
}

std::string Entity::typeName() const {
    switch (type_) {
        case EntityType::Player: return "Player";
        case EntityType::Pig: return "Pig";
        case EntityType::Cow: return "Cow";
        case EntityType::Sheep: return "Sheep";
        case EntityType::Chicken: return "Chicken";
        case EntityType::Zombie: return "Zombie";
        case EntityType::Skeleton: return "Skeleton";
        case EntityType::Creeper: return "Creeper";
        case EntityType::Spider: return "Spider";
        case EntityType::ItemDrop: return "ItemDrop";
        case EntityType::Arrow: return "Arrow";
        case EntityType::Fireball: return "Fireball";
        case EntityType::Minecart: return "Minecart";
        case EntityType::Boat: return "Boat";
        default:
            if (static_cast<uint16_t>(type_) >= static_cast<uint16_t>(EntityType::Custom)) {
                return "Custom:" + std::to_string(static_cast<uint16_t>(type_));
            }
            return "Unknown";
    }
}

Vec3 Entity::lookDirection() const {
    // Convert yaw and pitch (in degrees) to a unit direction vector
    // Yaw: 0 = +Z, 90 = -X, 180 = -Z, 270 = +X (Minecraft-style)
    // Pitch: 0 = forward, -90 = up, +90 = down

    float yawRad = glm::radians(yaw_);
    float pitchRad = glm::radians(pitch_);

    float cosPitch = std::cos(pitchRad);
    float x = -std::sin(yawRad) * cosPitch;
    float y = -std::sin(pitchRad);
    float z = std::cos(yawRad) * cosPitch;

    return Vec3(x, y, z);
}

}  // namespace finevox
