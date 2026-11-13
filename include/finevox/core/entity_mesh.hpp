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
#include <string>
#include <string_view>
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
    glm::vec3 color{1.0f};
    bool skinned = false;  ///< texCoord is from the box unwrap, in the model's atlas place
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
    glm::vec3 color{1.0f};      // Flat colour, for models that aren't textured
    float teamTint = 0.0f;      // 0..1 blend from color toward the faction colour

    /// Textured with the usual box unwrap: from `uvPixels` on the model's texture, a strip of
    /// the sides below the top and bottom, each face as many texture pixels as the box is in
    /// `uvBox` (width, height, depth). The front is the rig's front, +Z. `mirror` flips it, for a left limb
    /// drawn from a right one's pixels.
    bool textured = false;
    glm::vec2 uvPixels{0.0f};
    glm::vec3 uvBox{0.0f};
    bool mirror = false;
    /// Turned about its own centre before the bone moves it, degrees about X then Y then Z: a
    /// body lying along a beast drawn from a box standing up keeps the standing box's unwrap
    glm::vec3 turnDegrees{0.0f};
    /// A skin of its own instead of the model's (wool over a body, armour over a person), by
    /// name under textures/, with its pixel size; where a renderer placed it, zero if nowhere
    std::string texture;
    glm::vec2 textureSize{64.0f, 32.0f};
    glm::vec4 atlas{0.0f};
    /// Shown only while one of these animations plays (a tool in hand), or never during one of
    /// these (the same tool put away). Empty means always, and never.
    std::vector<std::string> onlyIn;
    std::vector<std::string> hiddenIn;

    [[nodiscard]] bool shownDuring(std::string_view animation) const;
};

// ============================================================================
// EntityMesh — collection of box parts forming an entity model
// ============================================================================

class EntityMesh {
public:
    std::vector<EntityMeshPart> parts;

    /// Generate vertices and indices from skeleton world poses
    /// Each part produces 24 vertices (4 per face × 6 faces) and 36 indices
    /// Textured parts map into `atlas` (minU minV maxU maxV) of a texture `textureSize`
    /// pixels across, and are marked skinned; others keep uvOffset + uvSize per face.
    void buildVertices(const std::vector<glm::mat4>& worldPoses,
                       std::vector<EntityVertex>& vertices,
                       std::vector<uint32_t>& indices,
                       const glm::vec3& teamColor = glm::vec3(1.0f),
                       std::string_view animation = {},
                       const glm::vec4& atlas = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                       const glm::vec2& textureSize = glm::vec2(64.0f, 64.0f)) const;

    [[nodiscard]] size_t partCount() const { return parts.size(); }
    [[nodiscard]] bool isEmpty() const { return parts.empty(); }
};

}  // namespace finevox
