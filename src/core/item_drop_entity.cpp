#include "finevox/core/item_drop_entity.hpp"

namespace finevox {

ItemDropEntity::ItemDropEntity(EntityId id, ItemStack item)
    : Entity(id, EntityType::ItemDrop)
    , item_(std::move(item))
{
    // Small bounding box for item drops
    halfExtents_ = Vec3(0.125f, 0.125f, 0.125f);
    eyeHeight_ = 0.25f;
    hasGravity_ = true;
    maxStepHeight_ = 0.0f;
}

ItemStack ItemDropEntity::takeItem() {
    ItemStack taken = std::move(item_);
    item_.clear();
    return taken;
}

void ItemDropEntity::tick(float dt, World& /*world*/) {
    age_ += dt;
    if (age_ >= maxAge_) {
        markForRemoval();
    }
}

std::string ItemDropEntity::typeName() const {
    return "ItemDrop";
}

}  // namespace finevox
