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
    // Convert yaw and pitch (in radians) to a unit direction vector
    // Matches PlayerController::forwardVector() and MobEntity::lookAt():
    // Yaw: 0 = +Z, +pi/2 = +X, pi = -Z, -pi/2 = -X
    // Pitch: 0 = forward, +pi/2 = up, -pi/2 = down

    float cosPitch = std::cos(pitch_);
    float x = std::sin(yaw_) * cosPitch;
    float y = std::sin(pitch_);
    float z = std::cos(yaw_) * cosPitch;

    return Vec3(x, y, z);
}

}  // namespace finevox
