#pragma once

#include "finevox/string_interner.hpp"
#include "finevox/physics.hpp"
#include "finevox/rotation.hpp"

#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <array>

namespace finevox {

/**
 * @brief Properties for a block type
 *
 * BlockType stores the collision and hit shapes for a block,
 * along with other properties needed for physics and rendering.
 *
 * Shapes are stored with all 24 rotations precomputed for O(1) lookup
 * based on block rotation state.
 */
class BlockType {
public:
    BlockType() = default;

    // ========================================================================
    // Builder-style setters (return *this for chaining)
    // ========================================================================

    /// Set the collision shape (used for physics)
    /// Precomputes all 24 rotations
    BlockType& setCollisionShape(const CollisionShape& shape);

    /// Set the hit shape (used for raycasting/selection)
    /// Precomputes all 24 rotations
    /// If not set, falls back to collision shape
    BlockType& setHitShape(const CollisionShape& shape);

    /// Set both collision and hit shapes to the same value
    BlockType& setShape(const CollisionShape& shape);

    /// Mark this block as having no collision (pass-through)
    BlockType& setNoCollision();

    /// Mark this block as having no hit box (can't be selected)
    BlockType& setNoHit();

    /// Set whether block is opaque (blocks light, enables face culling)
    BlockType& setOpaque(bool opaque);

    /// Set whether block is transparent (for render sorting)
    BlockType& setTransparent(bool transparent);

    /// Set light emission level (0-15)
    BlockType& setLightEmission(uint8_t level);

    /// Set hardness (mining time factor)
    BlockType& setHardness(float hardness);

    // ========================================================================
    // Accessors
    // ========================================================================

    /// Get collision shape for given rotation
    /// Returns empty shape if no collision
    [[nodiscard]] const CollisionShape& collisionShape(const Rotation& rotation = Rotation::IDENTITY) const;

    /// Get hit shape for given rotation
    /// Falls back to collision shape if hit shape not explicitly set
    [[nodiscard]] const CollisionShape& hitShape(const Rotation& rotation = Rotation::IDENTITY) const;

    /// Check if this block has collision (non-empty collision shape)
    [[nodiscard]] bool hasCollision() const;

    /// Check if this block has a hit shape
    [[nodiscard]] bool hasHitShape() const;

    /// Check if block is opaque
    [[nodiscard]] bool isOpaque() const { return opaque_; }

    /// Check if block is transparent
    [[nodiscard]] bool isTransparent() const { return transparent_; }

    /// Get light emission level
    [[nodiscard]] uint8_t lightEmission() const { return lightEmission_; }

    /// Get hardness
    [[nodiscard]] float hardness() const { return hardness_; }

private:
    // Precomputed rotations for collision and hit shapes
    // Index 0 = identity rotation
    std::array<CollisionShape, 24> collisionShapes_;
    std::array<CollisionShape, 24> hitShapes_;

    bool hasCollision_ = true;       // True if collision shape is non-empty
    bool hasExplicitHit_ = false;    // True if hit shape was explicitly set
    bool opaque_ = true;             // Blocks light by default
    bool transparent_ = false;       // Not transparent by default
    uint8_t lightEmission_ = 0;      // No light emission by default
    float hardness_ = 1.0f;          // Default mining difficulty
};

/**
 * @brief Registry mapping BlockTypeId to BlockType data
 *
 * Thread-safe registry for block type definitions.
 * Block types should be registered during game initialization,
 * then looked up during gameplay.
 */
class BlockRegistry {
public:
    /// Get the global registry instance (singleton)
    static BlockRegistry& global();

    /// Register a block type
    /// Returns false if ID is already registered (won't overwrite)
    bool registerType(BlockTypeId id, BlockType type);

    /// Register a block type by name (interns the name automatically)
    /// Returns false if name is already registered
    bool registerType(std::string_view name, BlockType type);

    /// Get block type for given ID
    /// Returns default (full block) if not registered
    [[nodiscard]] const BlockType& getType(BlockTypeId id) const;

    /// Get block type by name
    /// Returns default if not registered
    [[nodiscard]] const BlockType& getType(std::string_view name) const;

    /// Check if a type is registered
    [[nodiscard]] bool hasType(BlockTypeId id) const;

    /// Get number of registered types
    [[nodiscard]] size_t size() const;

    /// Get the default block type (full solid block)
    [[nodiscard]] static const BlockType& defaultType();

    /// Get the air block type (no collision, no hit)
    [[nodiscard]] static const BlockType& airType();

    // Non-copyable singleton
    BlockRegistry(const BlockRegistry&) = delete;
    BlockRegistry& operator=(const BlockRegistry&) = delete;

private:
    BlockRegistry();

    mutable std::shared_mutex mutex_;
    std::unordered_map<BlockTypeId, BlockType> types_;
};

/**
 * @brief Create a BlockShapeProvider that uses the BlockRegistry
 *
 * This creates a callback suitable for PhysicsSystem that looks up
 * collision/hit shapes from the BlockRegistry based on block type.
 *
 * @param world The world to query for block types
 * @return BlockShapeProvider callback
 */
class World;  // Forward declaration
BlockShapeProvider createBlockShapeProvider(World& world);

}  // namespace finevox
