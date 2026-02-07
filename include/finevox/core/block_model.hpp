#pragma once

/**
 * @file block_model.hpp
 * @brief Data structures for non-cube block geometry
 *
 * Design: [19-block-models.md]
 *
 * BlockModel represents a complete block definition including:
 * - Render geometry (faces with vertices and UVs)
 * - Collision shape (AABBs for physics)
 * - Hit shape (AABBs for raycasting)
 * - Rotation constraints
 * - Properties (hardness, sounds, etc.)
 */

#include "finevox/core/physics.hpp"
#include "finevox/core/position.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace finevox {

// ============================================================================
// ModelVertex - Single vertex with position and UV
// ============================================================================

struct ModelVertex {
    glm::vec3 position{0.0f};  // In [0,1] local block space
    glm::vec2 uv{0.0f};        // Texture coordinates

    ModelVertex() = default;
    ModelVertex(glm::vec3 pos, glm::vec2 texCoord)
        : position(pos), uv(texCoord) {}
    ModelVertex(float x, float y, float z, float u, float v)
        : position(x, y, z), uv(u, v) {}
};

// ============================================================================
// FaceGeometry - Per-face vertex data
// ============================================================================

/**
 * @brief Geometry for a single face of a block model
 *
 * Faces can have 3-6 vertices (triangles, quads, pentagons, hexagons).
 * Standard faces (0-5) correspond to cube directions.
 * Extra faces (6+) are custom geometry like stair steps.
 */
struct FaceGeometry {
    std::vector<ModelVertex> vertices;  // 3-6 vertices in CCW order
    std::string name;                    // Face name (e.g., "top", "step_top")
    int faceIndex = -1;                  // 0-5 for standard, 6+ for extra, -1 if unset
    bool isSolid = false;                // Does this face fully occlude neighbor?

    FaceGeometry() = default;

    /// Compute AABB from vertices (for collision fallback)
    [[nodiscard]] AABB computeBounds() const;

    /// Get the standard Face enum if this is a standard face (0-5)
    [[nodiscard]] std::optional<Face> standardFace() const;

    /// Check if this is a standard face (0-5)
    [[nodiscard]] bool isStandardFace() const { return faceIndex >= 0 && faceIndex < 6; }

    /// Check if this face is valid (has at least 3 vertices)
    [[nodiscard]] bool isValid() const { return vertices.size() >= 3; }
};

// ============================================================================
// BlockGeometry - Collection of faces for rendering
// ============================================================================

/**
 * @brief Complete render geometry for a block model
 *
 * Contains all faces (standard cube faces + extra faces) with their
 * vertices and UVs. Provides lookups by face index or name.
 */
class BlockGeometry {
public:
    BlockGeometry() = default;

    /// Add a face to the geometry
    void addFace(FaceGeometry face);

    /// Get all faces
    [[nodiscard]] const std::vector<FaceGeometry>& faces() const { return faces_; }

    /// Get face by name (returns nullptr if not found)
    [[nodiscard]] const FaceGeometry* getFace(const std::string& name) const;

    /// Get face by index (returns nullptr if not found)
    [[nodiscard]] const FaceGeometry* getFace(int index) const;

    /// Get standard face (0-5) if present
    [[nodiscard]] const FaceGeometry* getStandardFace(Face face) const;

    /// Check if geometry has any faces
    [[nodiscard]] bool isEmpty() const { return faces_.empty(); }

    /// Get overall bounding box of all faces
    [[nodiscard]] AABB bounds() const { return bounds_; }

    /// Get solid faces as bitmask (bit N set if face N is solid)
    [[nodiscard]] uint8_t solidFacesMask() const;

    /// Compute collision shape from solid faces
    /// Each solid face contributes an AABB covering its extent
    [[nodiscard]] CollisionShape computeCollisionFromFaces() const;

    /// Get the next available face index for custom faces
    [[nodiscard]] int nextCustomFaceIndex() const { return nextCustomIndex_; }

private:
    std::vector<FaceGeometry> faces_;
    std::unordered_map<std::string, size_t> facesByName_;
    std::unordered_map<int, size_t> facesByIndex_;
    AABB bounds_;
    int nextCustomIndex_ = 6;  // Next index for custom faces

    void updateBounds(const FaceGeometry& face);
};

// ============================================================================
// RotationSet - Predefined rotation constraints
// ============================================================================

/**
 * @brief Predefined sets of allowed rotations
 *
 * Not all 24 rotations make sense for every block:
 * - Slabs: top/bottom only (2 states)
 * - Stairs: horizontal + upside-down (8 states)
 * - Furnaces: horizontal only (4 states)
 */
enum class RotationSet {
    None,            // 1 orientation (identity only)
    Vertical,        // 2 orientations (top/bottom for slabs)
    Horizontal,      // 4 orientations (Y-axis rotations)
    HorizontalFlip,  // 8 orientations (horizontal + upside-down)
    All,             // All 24 orientations
    Custom           // Explicit list of allowed indices
};

/// Get the rotation indices for a predefined set
[[nodiscard]] std::vector<uint8_t> getRotationIndices(RotationSet set);

/// Parse rotation set from string (e.g., "horizontal", "all", "none")
[[nodiscard]] RotationSet parseRotationSet(const std::string& str);

// ============================================================================
// BlockModel - Complete block definition
// ============================================================================

/**
 * @brief Complete model for a block type
 *
 * Includes render geometry, collision/hit shapes, rotation constraints,
 * and various properties. Implements the fallback chain:
 *   hit → collision → geometry_faces → full_block
 */
class BlockModel {
public:
    BlockModel() = default;

    // ========================================================================
    // Builder-style setters
    // ========================================================================

    /// Set the render geometry
    BlockModel& setGeometry(BlockGeometry geometry);

    /// Set explicit collision shape
    BlockModel& setCollision(CollisionShape shape);

    /// Set explicit hit shape
    BlockModel& setHit(CollisionShape shape);

    /// Set rotation constraint
    BlockModel& setRotations(RotationSet set);

    /// Set custom rotation indices
    BlockModel& setRotations(std::vector<uint8_t> indices);

    /// Set hardness (mining time factor)
    BlockModel& setHardness(float hardness);

    /// Set texture name
    BlockModel& setTexture(const std::string& texture);

    /// Set sound set name
    BlockModel& setSounds(const std::string& sounds);

    /// Set light emission level (0-15)
    BlockModel& setLightEmission(uint8_t level);

    /// Set light attenuation (0 = transparent to light, higher = blocks more light)
    BlockModel& setLightAttenuation(uint8_t level);

    // ========================================================================
    // Accessors
    // ========================================================================

    /// Get render geometry
    [[nodiscard]] const BlockGeometry& geometry() const { return geometry_; }

    /// Get resolved collision shape (with fallback chain)
    [[nodiscard]] const CollisionShape& resolvedCollision() const;

    /// Get resolved hit shape (with fallback chain)
    [[nodiscard]] const CollisionShape& resolvedHit() const;

    /// Check if collision was explicitly set
    [[nodiscard]] bool hasExplicitCollision() const { return hasExplicitCollision_; }

    /// Check if hit was explicitly set
    [[nodiscard]] bool hasExplicitHit() const { return hasExplicitHit_; }

    /// Get rotation set
    [[nodiscard]] RotationSet rotationSet() const { return rotationSet_; }

    /// Get allowed rotation indices
    [[nodiscard]] std::vector<uint8_t> allowedRotations() const;

    /// Check if a rotation index is allowed
    [[nodiscard]] bool isRotationAllowed(uint8_t rotationIndex) const;

    /// Get hardness
    [[nodiscard]] float hardness() const { return hardness_; }

    /// Get texture name
    [[nodiscard]] const std::string& texture() const { return texture_; }

    /// Get sound set name
    [[nodiscard]] const std::string& sounds() const { return sounds_; }

    /// Get light emission level
    [[nodiscard]] uint8_t lightEmission() const { return lightEmission_; }

    /// Get light attenuation level
    [[nodiscard]] uint8_t lightAttenuation() const { return lightAttenuation_; }

    /// Check if this model has custom geometry (non-cube)
    [[nodiscard]] bool hasCustomGeometry() const { return !geometry_.isEmpty(); }

private:
    BlockGeometry geometry_;
    CollisionShape collision_;
    CollisionShape hit_;
    bool hasExplicitCollision_ = false;
    bool hasExplicitHit_ = false;
    RotationSet rotationSet_ = RotationSet::None;
    std::vector<uint8_t> customRotations_;
    float hardness_ = 1.0f;
    std::string texture_;
    std::string sounds_;
    uint8_t lightEmission_ = 0;
    uint8_t lightAttenuation_ = 15;  // Default: blocks all light

    // Cached resolved shapes (computed on first access)
    mutable CollisionShape resolvedCollision_;
    mutable CollisionShape resolvedHit_;
    mutable bool collisionResolved_ = false;
    mutable bool hitResolved_ = false;
};

// ============================================================================
// Face name utilities
// ============================================================================

/**
 * @brief Parse a face name to its index
 *
 * Supports multiple aliases:
 * - Standard faces: negx/west/w/-x (0), posx/east/e/+x (1), etc.
 * - Numeric: "6", "7", etc.
 * - Custom names: Returns -1 (caller assigns next available index)
 *
 * @param name Face name string
 * @return Face index (0-5 for standard, -1 for unknown custom name)
 */
[[nodiscard]] int parseFaceName(const std::string& name);

/**
 * @brief Check if a name is a standard face alias
 */
[[nodiscard]] bool isStandardFaceName(const std::string& name);

/**
 * @brief Get the canonical name for a face index
 */
[[nodiscard]] std::string faceName(int index);

}  // namespace finevox
