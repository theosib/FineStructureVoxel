#pragma once

/**
 * @file entity_mesh.hpp
 * @brief CPU-side entity mesh: box-based body parts attached to bones
 *
 * Each EntityMeshPart is a textured box attached to a skeleton bone.
 * buildVertices() transforms all parts by skeleton world poses to
 * produce a flat vertex/index array ready for GPU upload.
 */

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace finevox {

// ============================================================================
// EntityVertex — per-vertex data for entity rendering
// ============================================================================

struct EntityVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

// ============================================================================
// EntityMeshPart — one textured box attached to a bone
// ============================================================================

struct EntityMeshPart {
    int32_t boneIndex = 0;
    glm::vec3 offset{0.0f};     // Offset from bone origin (center of box)
    glm::vec3 size{1.0f};       // Full box dimensions (width, height, depth)
    glm::vec2 uvOffset{0.0f};   // UV start on texture atlas
    glm::vec2 uvSize{1.0f};     // UV dimensions for one face
};

// ============================================================================
// EntityMesh — collection of box parts forming an entity model
// ============================================================================

class EntityMesh {
public:
    std::vector<EntityMeshPart> parts;

    /// Generate vertices and indices from skeleton world poses
    /// Each part produces 24 vertices (4 per face × 6 faces) and 36 indices
    void buildVertices(const std::vector<glm::mat4>& worldPoses,
                       std::vector<EntityVertex>& vertices,
                       std::vector<uint32_t>& indices) const;

    [[nodiscard]] size_t partCount() const { return parts.size(); }
    [[nodiscard]] bool isEmpty() const { return parts.empty(); }
};

}  // namespace finevox
