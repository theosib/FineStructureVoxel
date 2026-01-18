#include "finevox/block_type.hpp"
#include "finevox/world.hpp"

namespace finevox {

// ============================================================================
// BlockType Implementation
// ============================================================================

BlockType& BlockType::setCollisionShape(const CollisionShape& shape) {
    collisionShapes_ = CollisionShape::computeRotations(shape);
    hasCollision_ = !shape.isEmpty();
    return *this;
}

BlockType& BlockType::setHitShape(const CollisionShape& shape) {
    hitShapes_ = CollisionShape::computeRotations(shape);
    hasExplicitHit_ = true;
    return *this;
}

BlockType& BlockType::setShape(const CollisionShape& shape) {
    setCollisionShape(shape);
    setHitShape(shape);
    return *this;
}

BlockType& BlockType::setNoCollision() {
    for (auto& s : collisionShapes_) {
        s = CollisionShape::NONE;
    }
    hasCollision_ = false;
    return *this;
}

BlockType& BlockType::setNoHit() {
    for (auto& s : hitShapes_) {
        s = CollisionShape::NONE;
    }
    hasExplicitHit_ = true;
    return *this;
}

BlockType& BlockType::setOpaque(bool opaque) {
    opaque_ = opaque;
    return *this;
}

BlockType& BlockType::setTransparent(bool transparent) {
    transparent_ = transparent;
    return *this;
}

BlockType& BlockType::setLightEmission(uint8_t level) {
    lightEmission_ = level;
    return *this;
}

BlockType& BlockType::setHardness(float hardness) {
    hardness_ = hardness;
    return *this;
}

const CollisionShape& BlockType::collisionShape(const Rotation& rotation) const {
    return collisionShapes_[rotation.index()];
}

const CollisionShape& BlockType::hitShape(const Rotation& rotation) const {
    if (hasExplicitHit_) {
        return hitShapes_[rotation.index()];
    }
    // Fall back to collision shape
    return collisionShapes_[rotation.index()];
}

bool BlockType::hasCollision() const {
    return hasCollision_;
}

bool BlockType::hasHitShape() const {
    if (hasExplicitHit_) {
        return !hitShapes_[0].isEmpty();
    }
    return hasCollision_;
}

// ============================================================================
// BlockRegistry Implementation
// ============================================================================

BlockRegistry& BlockRegistry::global() {
    static BlockRegistry instance;
    return instance;
}

BlockRegistry::BlockRegistry() {
    // Register air type at ID 0
    BlockType air;
    air.setNoCollision()
       .setNoHit()
       .setOpaque(false)
       .setTransparent(true)
       .setHardness(0.0f);

    types_[AIR_BLOCK_TYPE] = std::move(air);
}

bool BlockRegistry::registerType(BlockTypeId id, BlockType type) {
    std::unique_lock lock(mutex_);

    // Don't allow overwriting
    if (types_.find(id) != types_.end()) {
        return false;
    }

    types_[id] = std::move(type);
    return true;
}

bool BlockRegistry::registerType(std::string_view name, BlockType type) {
    return registerType(BlockTypeId::fromName(name), std::move(type));
}

const BlockType& BlockRegistry::getType(BlockTypeId id) const {
    std::shared_lock lock(mutex_);

    auto it = types_.find(id);
    if (it != types_.end()) {
        return it->second;
    }

    // Return default type for unregistered blocks
    return defaultType();
}

const BlockType& BlockRegistry::getType(std::string_view name) const {
    auto id = StringInterner::global().find(name);
    if (!id.has_value()) {
        return defaultType();
    }
    return getType(BlockTypeId(*id));
}

bool BlockRegistry::hasType(BlockTypeId id) const {
    std::shared_lock lock(mutex_);
    return types_.find(id) != types_.end();
}

size_t BlockRegistry::size() const {
    std::shared_lock lock(mutex_);
    return types_.size();
}

const BlockType& BlockRegistry::defaultType() {
    static BlockType defaultBlock = []() {
        BlockType b;
        b.setShape(CollisionShape::FULL_BLOCK);
        return b;
    }();
    return defaultBlock;
}

const BlockType& BlockRegistry::airType() {
    static BlockType airBlock = []() {
        BlockType b;
        b.setNoCollision()
         .setNoHit()
         .setOpaque(false)
         .setTransparent(true)
         .setHardness(0.0f);
        return b;
    }();
    return airBlock;
}

// ============================================================================
// Block Shape Provider
// ============================================================================

BlockShapeProvider createBlockShapeProvider(World& world) {
    return [&world](const BlockPos& pos, RaycastMode mode) -> const CollisionShape* {
        // Get block type at position
        BlockTypeId blockType = world.getBlock(pos);

        // Air has no collision
        if (blockType.isAir()) {
            return nullptr;
        }

        // Look up block type in registry
        const BlockType& type = BlockRegistry::global().getType(blockType);

        // For now, assume no rotation (rotation storage to be added later)
        Rotation rotation = Rotation::IDENTITY;

        switch (mode) {
            case RaycastMode::Collision:
                if (!type.hasCollision()) {
                    return nullptr;
                }
                return &type.collisionShape(rotation);

            case RaycastMode::Interaction:
                if (!type.hasHitShape()) {
                    return nullptr;
                }
                return &type.hitShape(rotation);

            case RaycastMode::Both:
                // Return collision if it exists, else hit
                if (type.hasCollision()) {
                    return &type.collisionShape(rotation);
                }
                if (type.hasHitShape()) {
                    return &type.hitShape(rotation);
                }
                return nullptr;
        }

        return nullptr;
    };
}

}  // namespace finevox
