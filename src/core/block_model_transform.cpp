#include "finevox/core/block_model_transform.hpp"

#include "finevox/core/mesh.hpp"

#include <cmath>

namespace finevox {

namespace {

constexpr float EPS = 1e-4f;

int quarterTurns(int degrees) {
    int q = (degrees / 90) % 4;
    return q < 0 ? q + 4 : q;
}

// One clockwise quarter turn about X, seen from +X: up goes to north (-Z), south to up
glm::vec3 turnX(glm::vec3 v) { return {v.x, v.z, -v.y}; }
// One clockwise quarter turn about Y, seen from above: north goes to east
glm::vec3 turnY(glm::vec3 v) { return {-v.z, v.y, v.x}; }

glm::vec3 turnDirection(glm::vec3 v, int xq, int yq) {
    for (int i = 0; i < xq; ++i) v = turnX(v);
    for (int i = 0; i < yq; ++i) v = turnY(v);
    return v;
}

glm::vec3 turnPoint(glm::vec3 p, int xq, int yq) {
    const glm::vec3 centre(0.5f);
    return turnDirection(p - centre, xq, yq) + centre;
}

glm::vec3 cubeFaceNormal(Face face) { return MeshBuilder::FACE_DATA[static_cast<int>(face)].normal; }

int faceFromDirection(glm::vec3 d) {
    for (int f = 0; f < 6; ++f) {
        if (glm::length(cubeFaceNormal(static_cast<Face>(f)) - d) < EPS) return f;
    }
    return -1;
}

glm::vec3 polygonNormal(const FaceGeometry& face) {
    const glm::vec3& v0 = face.vertices[0].position;
    glm::vec3 n = glm::cross(face.vertices[2].position - v0, face.vertices[1].position - v0);
    float len = glm::length(n);
    return len > 0.0f ? n / len : n;
}

// Texture coordinates a world-aligned face shows at a point: the projection a cube's own
// faces use (MeshBuilder::FACE_DATA), so locked UVs line up with the cubes around them
glm::vec2 projectedUV(Face face, glm::vec3 p) {
    switch (face) {
        case Face::NegX: return {p.z, 1.0f - p.y};
        case Face::PosX: return {1.0f - p.z, 1.0f - p.y};
        case Face::NegY: return {p.x, 1.0f - p.z};
        case Face::PosY: return {p.x, p.z};
        case Face::NegZ: return {1.0f - p.x, 1.0f - p.y};
        case Face::PosZ: return {p.x, 1.0f - p.y};
    }
    return {0.0f, 0.0f};
}

// One clockwise quarter turn of a texture, as the coordinate it samples: a texture turned
// this way shows at p what the original shows at quarterTurnUV(p)
glm::vec2 quarterTurnUV(glm::vec2 uv) { return {uv.y, 1.0f - uv.x}; }

CollisionShape turnedShape(const CollisionShape& shape, int xq, int yq) {
    CollisionShape out;
    for (const AABB& box : shape.boxes()) {
        glm::vec3 a = turnPoint(glm::vec3(box.min.x, box.min.y, box.min.z), xq, yq);
        glm::vec3 b = turnPoint(glm::vec3(box.max.x, box.max.y, box.max.z), xq, yq);
        glm::vec3 lo = glm::min(a, b);
        glm::vec3 hi = glm::max(a, b);
        out.addBox(AABB(lo.x, lo.y, lo.z, hi.x, hi.y, hi.z));
    }
    return out;
}

BlockGeometry turnedGeometry(const BlockGeometry& geometry, int xq, int yq, bool lockUV) {
    BlockGeometry out;
    for (FaceGeometry face : geometry.faces()) {
        for (auto& v : face.vertices) v.position = turnPoint(v.position, xq, yq);
        if (face.isStandardFace()) {
            // A standard face is culled by the neighbour in its direction, which turns with it
            int turned = faceFromDirection(
                turnDirection(cubeFaceNormal(static_cast<Face>(face.faceIndex)), xq, yq));
            face.faceIndex = turned;
            face.name = faceName(turned);
        }
        if (lockUV && face.vertices.size() >= 3) {
            int facing = faceFromDirection(polygonNormal(face));
            if (facing >= 0) {
                for (auto& v : face.vertices) v.uv = projectedUV(static_cast<Face>(facing), v.position);
            }
        }
        out.addFace(std::move(face));
    }
    return out;
}

}  // namespace

void splitTextureTurn(const std::string& texture, std::string& base, int& degrees) {
    auto at = texture.rfind('@');
    degrees = 0;
    base = texture;
    if (at == std::string::npos) return;
    std::string suffix = texture.substr(at + 1);
    if (suffix != "90" && suffix != "180" && suffix != "270") return;
    base = texture.substr(0, at);
    degrees = std::stoi(suffix);
}

std::string turnedTextureName(const std::string& texture, int degrees) {
    std::string base;
    int current = 0;
    splitTextureTurn(texture, base, current);
    int total = quarterTurns(current + degrees) * 90;
    return total == 0 ? base : base + "@" + std::to_string(total);
}

BlockGeometry cubeGeometry(const BlockModel& model) {
    BlockGeometry geometry;
    for (int f = 0; f < 6; ++f) {
        const auto& data = MeshBuilder::FACE_DATA[f];
        FaceGeometry face;
        face.name = faceName(f);
        face.faceIndex = f;
        face.isSolid = true;
        face.textureRef = model.cubeFaceTexture(static_cast<Face>(f));
        face.texture = face.textureRef;
        face.tintSlot = model.faceTint(static_cast<Face>(f));
        for (int i = 0; i < 4; ++i) face.vertices.emplace_back(data.positions[i], data.uvOffsets[i]);
        geometry.addFace(std::move(face));
    }
    return geometry;
}

BlockModel turnedModel(const BlockModel& model, int xDegrees, int yDegrees, bool lockUV) {
    const int xq = quarterTurns(xDegrees);
    const int yq = quarterTurns(yDegrees);
    BlockModel out = model;
    if (xq == 0 && yq == 0 && !lockUV) return out;

    const bool wasCube = !model.hasCustomGeometry();
    const BlockGeometry source = wasCube ? cubeGeometry(model) : model.geometry();
    out.setGeometry(turnedGeometry(source, xq, yq, lockUV));
    if (model.hasFarGeometry()) out.setFarGeometry(turnedGeometry(model.farGeometry(), xq, yq, lockUV));
    if (model.hasExplicitCollision()) out.setCollision(turnedShape(model.resolvedCollision(), xq, yq));
    if (model.hasExplicitHit()) out.setHit(turnedShape(model.resolvedHit(), xq, yq));
    // A turned cube with nothing but its own cube faces goes back to being one
    if (wasCube) drawAsCubeIfPlain(out);
    return out;
}

bool drawAsCubeIfPlain(BlockModel& model) {
    const BlockGeometry& geometry = model.geometry();
    if (geometry.faces().size() != 6) return false;

    std::array<std::string, 6> textures;
    std::array<int, 6> tints{};
    std::array<bool, 6> seen{};
    for (const FaceGeometry& face : geometry.faces()) {
        if (face.vertices.size() != 4 || face.texture.empty() || face.connection >= 0) return false;
        int f = faceFromDirection(polygonNormal(face));
        if (f < 0 || seen[f]) return false;
        // A cube face is culled by its neighbour; one that isn't wouldn't draw the same
        if (face.faceIndex != f) return false;
        const auto& data = MeshBuilder::FACE_DATA[f];

        // Each corner must be one of the cube face's corners, its texture coordinate that
        // corner's, turned the same number of quarter turns for all four
        int turns = -1;
        for (int k = 0; k < 4 && turns < 0; ++k) {
            bool all = true;
            for (const auto& v : face.vertices) {
                int corner = -1;
                for (int i = 0; i < 4; ++i) {
                    if (glm::length(v.position - data.positions[i]) < EPS) corner = i;
                }
                if (corner < 0) return false;
                glm::vec2 expected = data.uvOffsets[corner];
                for (int t = 0; t < k; ++t) expected = quarterTurnUV(expected);
                if (glm::length(v.uv - expected) > EPS) {
                    all = false;
                    break;
                }
            }
            if (all) turns = k;
        }
        if (turns < 0) return false;
        seen[f] = true;
        textures[f] = turnedTextureName(face.texture, turns * 90);
        tints[f] = face.tintSlot;
    }

    for (int f = 0; f < 6; ++f) {
        model.setFaceTexture(static_cast<Face>(f), textures[f]);
        model.setFaceTint(static_cast<Face>(f), tints[f]);
    }
    model.clearGeometry();
    return true;
}

BlockModel combinedLook(const std::vector<BlockModel>& parts) {
    BlockModel out;
    if (parts.empty()) return out;
    out = parts.front();

    BlockGeometry geometry;
    CollisionShape collision;
    CollisionShape hit;
    for (const BlockModel& part : parts) {
        const BlockGeometry faces = part.hasCustomGeometry() ? part.geometry() : cubeGeometry(part);
        for (FaceGeometry face : faces.faces()) {
            if (face.faceIndex >= 6) face.faceIndex = geometry.nextCustomFaceIndex();
            // Each part's variables are its own: keep what they gave
            if (!face.texture.empty() && face.texture[0] != '#') face.textureRef = face.texture;
            geometry.addFace(std::move(face));
        }
        for (const AABB& box : part.resolvedCollision().boxes()) collision.addBox(box);
        for (const AABB& box : part.resolvedHit().boxes()) hit.addBox(box);
    }
    out.setGeometry(std::move(geometry));
    out.setCollision(std::move(collision));
    out.setHit(std::move(hit));
    return out;
}

}  // namespace finevox
