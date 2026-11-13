#include <glm/gtc/matrix_transform.hpp>
#include "finevox/core/entity_mesh.hpp"

#include <algorithm>

namespace finevox {

bool EntityMeshPart::shownDuring(std::string_view animation) const {
    auto names = [animation](const std::vector<std::string>& list) {
        return std::find(list.begin(), list.end(), animation) != list.end();
    };
    if (!onlyIn.empty() && !names(onlyIn)) return false;
    return !names(hiddenIn);
}

namespace {

// The usual box unwrap, in texture pixels, for a corner of a unit box (each axis -0.5..0.5) on
// face f (+X -X +Y -Y +Z -Z). With the box w wide, h high and d deep from (u, v): top and bottom
// side by side at (u+d, v), then the sides in a strip below them: right, front, left, back, for a
// box facing -Z (turned here for rigs, which face +Z). Each face reads as seen from outside.
glm::vec2 boxUV(const EntityMeshPart& part, int f, glm::vec3 c, const glm::vec4& atlas, const glm::vec2& size) {
    const float w = part.uvBox.x, h = part.uvBox.y, d = part.uvBox.z;
    const float u = part.uvPixels.x, v = part.uvPixels.y;
    // Rigs face +Z, the unwrap's front faces -Z: turn the box half round to read it
    float x = -c.x, y = c.y, z = -c.z;
    static const int halfTurn[6] = {1, 0, 2, 3, 5, 4};
    int face = halfTurn[f];
    if (part.mirror) {
        x = -x;
        if (face == 0) face = 1;
        else if (face == 1) face = 0;
    }
    glm::vec2 px;
    switch (face) {
        case 0: px = {u + (0.5f - z) * d, v + d + (0.5f - y) * h}; break;              // +X
        case 1: px = {u + d + w + (z + 0.5f) * d, v + d + (0.5f - y) * h}; break;      // -X
        case 2: px = {u + d + (0.5f - x) * w, v + (0.5f - z) * d}; break;              // +Y
        case 3: px = {u + d + w + (0.5f - x) * w, v + (0.5f - z) * d}; break;          // -Y
        case 4: px = {u + d + w + d + (x + 0.5f) * w, v + d + (0.5f - y) * h}; break;  // +Z
        default: px = {u + d + (0.5f - x) * w, v + d + (0.5f - y) * h}; break;         // -Z
    }
    glm::vec2 t = px / size;
    return {atlas.x + t.x * (atlas.z - atlas.x), atlas.y + t.y * (atlas.w - atlas.y)};
}

}  // namespace

void EntityMesh::buildVertices(const std::vector<glm::mat4>& worldPoses,
                                std::vector<EntityVertex>& vertices,
                                std::vector<uint32_t>& indices,
                                const glm::vec3& teamColor,
                                std::string_view animation,
                                const glm::vec4& atlas,
                                const glm::vec2& textureSize) const
{
    vertices.clear();
    indices.clear();

    // Reserve: 24 verts + 36 indices per part
    vertices.reserve(parts.size() * 24);
    indices.reserve(parts.size() * 36);

    // Box faces: 6 faces, each with 4 vertices
    // Face definitions: normal direction, and 4 corner offsets
    struct FaceDef {
        glm::vec3 normal;
        glm::vec3 corners[4];  // CCW winding
    };

    // Half-size corners for a unit box centered at origin
    // We'll scale by part.size later
    static const FaceDef faces[6] = {
        // +X
        { glm::vec3(1,0,0), { glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(0.5f,-0.5f,0.5f), glm::vec3(0.5f,0.5f,0.5f), glm::vec3(0.5f,0.5f,-0.5f) } },
        // -X
        { glm::vec3(-1,0,0), { glm::vec3(-0.5f,-0.5f,0.5f), glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(-0.5f,0.5f,-0.5f), glm::vec3(-0.5f,0.5f,0.5f) } },
        // +Y
        { glm::vec3(0,1,0), { glm::vec3(-0.5f,0.5f,-0.5f), glm::vec3(0.5f,0.5f,-0.5f), glm::vec3(0.5f,0.5f,0.5f), glm::vec3(-0.5f,0.5f,0.5f) } },
        // -Y
        { glm::vec3(0,-1,0), { glm::vec3(-0.5f,-0.5f,0.5f), glm::vec3(0.5f,-0.5f,0.5f), glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(-0.5f,-0.5f,-0.5f) } },
        // +Z
        { glm::vec3(0,0,1), { glm::vec3(-0.5f,-0.5f,0.5f), glm::vec3(-0.5f,0.5f,0.5f), glm::vec3(0.5f,0.5f,0.5f), glm::vec3(0.5f,-0.5f,0.5f) } },
        // -Z
        { glm::vec3(0,0,-1), { glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(0.5f,0.5f,-0.5f), glm::vec3(-0.5f,0.5f,-0.5f), glm::vec3(-0.5f,-0.5f,-0.5f) } },
    };

    // UV coordinates for each corner of a face
    static const glm::vec2 faceUVs[4] = {
        glm::vec2(0.0f, 1.0f),
        glm::vec2(0.0f, 0.0f),
        glm::vec2(1.0f, 0.0f),
        glm::vec2(1.0f, 1.0f),
    };

    for (const auto& part : parts) {
        if (!part.shownDuring(animation)) continue;

        // Get bone transform (or identity if out of range)
        glm::mat4 boneTransform(1.0f);
        if (part.boneIndex >= 0 &&
            part.boneIndex < static_cast<int32_t>(worldPoses.size())) {
            boneTransform = worldPoses[part.boneIndex];
        }

        // Turned about its own centre first
        glm::mat4 turn(1.0f);
        if (part.turnDegrees != glm::vec3(0.0f)) {
            turn = glm::translate(glm::mat4(1.0f), part.offset);
            turn = glm::rotate(turn, glm::radians(part.turnDegrees.z), glm::vec3(0, 0, 1));
            turn = glm::rotate(turn, glm::radians(part.turnDegrees.y), glm::vec3(0, 1, 0));
            turn = glm::rotate(turn, glm::radians(part.turnDegrees.x), glm::vec3(1, 0, 0));
            turn = glm::translate(turn, -part.offset);
        }
        boneTransform = boneTransform * turn;

        // Normal matrix (upper-left 3x3 of bone transform)
        glm::mat3 normalMat(boneTransform);

        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

        for (int f = 0; f < 6; ++f) {
            const auto& face = faces[f];
            glm::vec3 transformedNormal = glm::normalize(normalMat * face.normal);

            for (int c = 0; c < 4; ++c) {
                // Scale corner by part size, offset by part offset
                glm::vec3 localPos = face.corners[c] * part.size + part.offset;
                // Transform by bone
                glm::vec4 worldPos = boneTransform * glm::vec4(localPos, 1.0f);

                EntityVertex v;
                v.position = glm::vec3(worldPos);
                v.normal = transformedNormal;
                v.texCoord = part.textured ? boxUV(part, f, face.corners[c], atlas, textureSize)
                                           : part.uvOffset + faceUVs[c] * part.uvSize;
                v.skinned = part.textured;
                v.color = glm::mix(part.color, teamColor, part.teamTint);
                vertices.push_back(v);
            }

            // Two triangles per face: 0-1-2, 0-2-3
            uint32_t base = baseVertex + f * 4;
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
    }
}

}  // namespace finevox
