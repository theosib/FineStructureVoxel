#pragma once

#include "finevox/string_interner.hpp"
#include "finevox/physics.hpp"
#include "finevox/rotation.hpp"

#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <array>
#include <memory>
#include <functional>

namespace finevox {

// Forward declaration
class BlockHandler;

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

    /// Set light attenuation (how much light decreases passing through, 1-15)
    /// Default is 15 for opaque blocks (blocks all light), 1 for transparent
    BlockType& setLightAttenuation(uint8_t attenuation);

    /// Set whether this block blocks sky light (affects heightmap calculation)
    /// Default is true for opaque blocks, false for transparent
    BlockType& setBlocksSkyLight(bool blocks);

    /// Set hardness (mining time factor)
    BlockType& setHardness(float hardness);

    /// Set whether this block type wants to receive game tick events
    /// Blocks with this enabled are auto-registered in per-subchunk registry
    BlockType& setWantsGameTicks(bool wants);

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

    /// Get light attenuation (how much light decreases passing through)
    [[nodiscard]] uint8_t lightAttenuation() const { return lightAttenuation_; }

    /// Check if block blocks sky light (affects heightmap)
    [[nodiscard]] bool blocksSkyLight() const { return blocksSkyLight_; }

    /// Get hardness
    [[nodiscard]] float hardness() const { return hardness_; }

    /// Check if block wants game tick events
    [[nodiscard]] bool wantsGameTicks() const { return wantsGameTicks_; }

private:
    // Precomputed rotations for collision and hit shapes
    // Index 0 = identity rotation
    std::array<CollisionShape, 24> collisionShapes_;
    std::array<CollisionShape, 24> hitShapes_;

    bool hasCollision_ = true;       // True if collision shape is non-empty
    bool hasExplicitHit_ = false;    // True if hit shape was explicitly set
    bool opaque_ = true;             // Blocks light by default
    bool transparent_ = false;       // Not transparent by default
    bool blocksSkyLight_ = true;     // Blocks sky light by default
    uint8_t lightEmission_ = 0;      // No light emission by default
    uint8_t lightAttenuation_ = 15;  // Full attenuation by default (opaque)
    float hardness_ = 1.0f;          // Default mining difficulty
    bool wantsGameTicks_ = false;    // Wants game tick events (auto-registered)
};

/**
 * @brief Registry mapping BlockTypeId to BlockType data and handlers
 *
 * Thread-safe registry for block type definitions and behavior handlers.
 * Block types should be registered during game initialization (module loading),
 * then looked up during gameplay.
 *
 * The registry supports:
 * - BlockType: Static properties (collision, opacity, hardness)
 * - BlockHandler: Dynamic behavior (events, ticks, interactions)
 * - Handler factories: For lazy loading of handler code
 *
 * Namespace convention: Block names use "namespace:localname" format.
 * Example: "blockgame:stone", "mymod:custom_ore"
 */
class BlockRegistry {
public:
    /// Factory function type for lazy handler creation
    using HandlerFactory = std::function<std::unique_ptr<BlockHandler>()>;

    /// Get the global registry instance (singleton)
    static BlockRegistry& global();

    // ========================================================================
    // Block Type Registration
    // ========================================================================

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

    /// Check if a type is registered by name
    [[nodiscard]] bool hasType(std::string_view name) const;

    /// Get number of registered types
    [[nodiscard]] size_t size() const;

    /// Get the default block type (full solid block)
    [[nodiscard]] static const BlockType& defaultType();

    /// Get the air block type (no collision, no hit)
    [[nodiscard]] static const BlockType& airType();

    // ========================================================================
    // Block Handler Registration
    // ========================================================================

    /// Register a block handler directly
    /// Takes ownership of the handler
    /// Returns false if a handler is already registered for this name
    bool registerHandler(std::string_view name, std::unique_ptr<BlockHandler> handler);

    /// Register a handler factory for lazy loading
    /// The factory is called the first time the handler is requested
    /// Returns false if a handler or factory is already registered
    bool registerHandlerFactory(std::string_view name, HandlerFactory factory);

    /// Get handler for a block type (may trigger lazy loading)
    /// Returns nullptr if no handler is registered
    [[nodiscard]] BlockHandler* getHandler(BlockTypeId id);

    /// Get handler by name (may trigger lazy loading)
    /// Returns nullptr if no handler is registered
    [[nodiscard]] BlockHandler* getHandler(std::string_view name);

    /// Check if a handler is registered (or has a factory)
    [[nodiscard]] bool hasHandler(BlockTypeId id) const;

    /// Check if a handler is registered by name
    [[nodiscard]] bool hasHandler(std::string_view name) const;

    // ========================================================================
    // Namespace Utilities
    // ========================================================================

    /// Check if a name has valid namespace format ("namespace:localname")
    [[nodiscard]] static bool isValidNamespacedName(std::string_view name);

    /// Get the namespace portion of a name
    /// Returns empty string_view if name has no namespace
    [[nodiscard]] static std::string_view getNamespace(std::string_view name);

    /// Get the local name portion (after the colon)
    /// Returns the full name if there's no namespace
    [[nodiscard]] static std::string_view getLocalName(std::string_view name);

    /// Build a fully-qualified name from namespace and local name
    [[nodiscard]] static std::string makeQualifiedName(std::string_view ns, std::string_view localName);

    // Non-copyable singleton
    BlockRegistry(const BlockRegistry&) = delete;
    BlockRegistry& operator=(const BlockRegistry&) = delete;

private:
    BlockRegistry();

    // Handler entry: either a loaded handler or a factory to create one
    struct HandlerEntry {
        std::unique_ptr<BlockHandler> handler;
        HandlerFactory factory;  // Used if handler is null

        bool hasHandler() const { return handler != nullptr; }
        bool hasFactory() const { return factory != nullptr; }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<BlockTypeId, BlockType> types_;
    std::unordered_map<std::string, HandlerEntry> handlers_;  // Keyed by name string
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
