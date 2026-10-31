#include "finevox/render/scene_geometry.hpp"
#include "finevox/core/position.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace finevox::render {

namespace {

void pushQuad(std::vector<SceneVertex>& out, const glm::vec3& a, const glm::vec3& b,
              const glm::vec3& c, const glm::vec3& d, const glm::vec3& normal,
              const glm::vec4& color) {
    out.push_back({a, normal, color});
    out.push_back({b, normal, color});
    out.push_back({c, normal, color});
    out.push_back({a, normal, color});
    out.push_back({c, normal, color});
    out.push_back({d, normal, color});
}

}  // namespace

void appendBox(std::vector<SceneVertex>& out, const glm::vec3& c, const glm::vec3& h,
               const glm::vec4& color, bool lit, uint8_t faces) {
    glm::vec3 p[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
        {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
    };
    auto normal = [lit](float x, float y, float z) {
        return lit ? glm::vec3(x, y, z) : glm::vec3(0.0f);
    };
    auto side = [faces](Face face) { return (faces >> static_cast<uint8_t>(face)) & 1; };
    if (side(Face::PosX)) pushQuad(out, p[1], p[5], p[6], p[2], normal(1, 0, 0), color);
    if (side(Face::NegX)) pushQuad(out, p[4], p[0], p[3], p[7], normal(-1, 0, 0), color);
    if (side(Face::PosY)) pushQuad(out, p[3], p[2], p[6], p[7], normal(0, 1, 0), color);
    if (side(Face::NegY)) pushQuad(out, p[4], p[5], p[1], p[0], normal(0, -1, 0), color);
    if (side(Face::PosZ)) pushQuad(out, p[5], p[4], p[7], p[6], normal(0, 0, 1), color);
    if (side(Face::NegZ)) pushQuad(out, p[0], p[1], p[2], p[3], normal(0, 0, -1), color);
}

void appendRing(std::vector<SceneVertex>& out, const glm::vec3& center, float radius, float width,
                const glm::vec4& color, int segments) {
    const glm::vec3 unlit(0.0f);
    float inner = radius - width * 0.5f;
    float outer = radius + width * 0.5f;
    for (int i = 0; i < segments; ++i) {
        float a0 = glm::two_pi<float>() * i / segments;
        float a1 = glm::two_pi<float>() * (i + 1) / segments;
        glm::vec3 dir0(std::cos(a0), 0.0f, std::sin(a0));
        glm::vec3 dir1(std::cos(a1), 0.0f, std::sin(a1));
        pushQuad(out, center + dir0 * inner, center + dir1 * inner, center + dir1 * outer,
                 center + dir0 * outer, unlit, color);
    }
}

void appendBillboard(std::vector<SceneVertex>& out, const glm::vec3& center, const glm::vec3& right,
                     const glm::vec3& up, float halfWidth, float halfHeight,
                     const glm::vec4& color) {
    glm::vec3 r = right * halfWidth;
    glm::vec3 u = up * halfHeight;
    pushQuad(out, center - r - u, center + r - u, center + r + u, center - r + u, glm::vec3(0.0f),
             color);
}

void appendEntity(std::vector<SceneVertex>& out, const EntityModel& model,
                  const EntityRenderState& state, const glm::dvec3& cameraPos,
                  const glm::vec3& teamColor, float light, float tickRate,
                  EntityPoseScratch& scratch) {
    const Skeleton& skeleton = *model.skeleton;
    skeleton.computeRestPose(scratch.local);
    state.animator.sample(scratch.local);
    skeleton.computeWorldTransforms(scratch.local, scratch.world);

    float alpha = state.interpolationAlpha(tickRate);
    glm::vec3 viewPos(state.interpolatedPosition(alpha) - cameraPos);
    glm::mat4 root = glm::rotate(glm::translate(glm::mat4(1.0f), viewPos),
                                 state.interpolatedYawRadians(alpha), glm::vec3(0.0f, 1.0f, 0.0f));
    for (auto& pose : scratch.world) pose = root * pose;

    model.mesh.buildVertices(scratch.world, scratch.vertices, scratch.indices, teamColor);

    out.reserve(out.size() + scratch.indices.size());
    for (uint32_t index : scratch.indices) {
        const EntityVertex& v = scratch.vertices[index];
        out.push_back({v.position, v.normal, glm::vec4(v.color * light, 1.0f)});
    }
}

}  // namespace finevox::render
