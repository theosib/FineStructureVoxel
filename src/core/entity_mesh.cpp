#include "finevox/core/entity_mesh.hpp"

namespace finevox {

void EntityMesh::buildVertices(const std::vector<glm::mat4>& worldPoses,
                                std::vector<EntityVertex>& vertices,
                                std::vector<uint32_t>& indices) const
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
        // Get bone transform (or identity if out of range)
        glm::mat4 boneTransform(1.0f);
        if (part.boneIndex >= 0 &&
            part.boneIndex < static_cast<int32_t>(worldPoses.size())) {
            boneTransform = worldPoses[part.boneIndex];
        }

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
                v.texCoord = part.uvOffset + faceUVs[c] * part.uvSize;
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
