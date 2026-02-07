#include "finevox/core/block_type.hpp"
#include "finevox/core/block_handler.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>

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
    lightEmission_ = std::min(level, uint8_t(15));
    return *this;
}

BlockType& BlockType::setLightAttenuation(uint8_t attenuation) {
    // Clamp to 1-15 range (0 would mean infinite propagation)
    lightAttenuation_ = std::clamp(attenuation, uint8_t(1), uint8_t(15));
    return *this;
}

BlockType& BlockType::setBlocksSkyLight(bool blocks) {
    blocksSkyLight_ = blocks;
    return *this;
}

BlockType& BlockType::setHardness(float hardness) {
    hardness_ = hardness;
    return *this;
}

BlockType& BlockType::setWantsGameTicks(bool wants) {
    wantsGameTicks_ = wants;
    return *this;
}

BlockType& BlockType::setHasCustomMesh(bool hasMesh) {
    hasCustomMesh_ = hasMesh;
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
       .setLightAttenuation(1)      // Light passes through with minimal loss
       .setBlocksSkyLight(false)     // Doesn't block sky light
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

bool BlockRegistry::hasType(std::string_view name) const {
    auto id = StringInterner::global().find(name);
    if (!id.has_value()) {
        return false;
    }
    return hasType(BlockTypeId(*id));
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
         .setLightAttenuation(1)      // Light passes through with minimal loss
         .setBlocksSkyLight(false)     // Doesn't block sky light
         .setHardness(0.0f);
        return b;
    }();
    return airBlock;
}

// ============================================================================
// Block Handler Registration
// ============================================================================

bool BlockRegistry::registerHandler(std::string_view name, std::unique_ptr<BlockHandler> handler) {
    if (!handler) {
        return false;
    }

    std::unique_lock lock(mutex_);

    std::string nameStr(name);
    auto it = handlers_.find(nameStr);
    if (it != handlers_.end() && (it->second.hasHandler() || it->second.hasFactory())) {
        return false;  // Already registered
    }

    handlers_[nameStr].handler = std::move(handler);
    return true;
}

bool BlockRegistry::registerHandlerFactory(std::string_view name, HandlerFactory factory) {
    if (!factory) {
        return false;
    }

    std::unique_lock lock(mutex_);

    std::string nameStr(name);
    auto it = handlers_.find(nameStr);
    if (it != handlers_.end() && (it->second.hasHandler() || it->second.hasFactory())) {
        return false;  // Already registered
    }

    handlers_[nameStr].factory = std::move(factory);
    return true;
}

BlockHandler* BlockRegistry::getHandler(BlockTypeId id) {
    return getHandler(id.name());
}

BlockHandler* BlockRegistry::getHandler(std::string_view name) {
    std::unique_lock lock(mutex_);

    std::string nameStr(name);
    auto it = handlers_.find(nameStr);
    if (it == handlers_.end()) {
        return nullptr;
    }

    // If we have a handler, return it
    if (it->second.hasHandler()) {
        return it->second.handler.get();
    }

    // If we have a factory, use it to create the handler (lazy loading)
    if (it->second.hasFactory()) {
        it->second.handler = it->second.factory();
        it->second.factory = nullptr;  // Clear factory after use
        return it->second.handler.get();
    }

    return nullptr;
}

bool BlockRegistry::hasHandler(BlockTypeId id) const {
    return hasHandler(id.name());
}

bool BlockRegistry::hasHandler(std::string_view name) const {
    std::shared_lock lock(mutex_);

    auto it = handlers_.find(std::string(name));
    if (it == handlers_.end()) {
        return false;
    }

    return it->second.hasHandler() || it->second.hasFactory();
}

// ============================================================================
// Namespace Utilities
// ============================================================================

bool BlockRegistry::isValidNamespacedName(std::string_view name) {
    // Must contain exactly one colon
    size_t colonPos = name.find(':');
    if (colonPos == std::string_view::npos) {
        return false;  // No colon
    }
    if (name.find(':', colonPos + 1) != std::string_view::npos) {
        return false;  // Multiple colons
    }

    // Namespace must be non-empty
    if (colonPos == 0) {
        return false;
    }

    // Local name must be non-empty
    if (colonPos == name.size() - 1) {
        return false;
    }

    // Both parts should only contain alphanumeric and underscore
    auto isValidChar = [](char c) {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_';
    };

    for (size_t i = 0; i < colonPos; ++i) {
        if (!isValidChar(name[i])) {
            return false;
        }
    }

    for (size_t i = colonPos + 1; i < name.size(); ++i) {
        if (!isValidChar(name[i])) {
            return false;
        }
    }

    return true;
}

std::string_view BlockRegistry::getNamespace(std::string_view name) {
    size_t colonPos = name.find(':');
    if (colonPos == std::string_view::npos) {
        return {};
    }
    return name.substr(0, colonPos);
}

std::string_view BlockRegistry::getLocalName(std::string_view name) {
    size_t colonPos = name.find(':');
    if (colonPos == std::string_view::npos) {
        return name;  // No namespace, entire thing is local name
    }
    return name.substr(colonPos + 1);
}

std::string BlockRegistry::makeQualifiedName(std::string_view ns, std::string_view localName) {
    std::string result;
    result.reserve(ns.size() + 1 + localName.size());
    result.append(ns);
    result.push_back(':');
    result.append(localName);
    return result;
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
