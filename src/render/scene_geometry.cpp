#include "finevox/render/scene_geometry.hpp"
#include "finevox/core/entity_type_registry.hpp"
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

/// Lifted this far as it falls, or the body would lie half under the ground
constexpr float DEATH_LIE_HEIGHT = 0.2f;
/// Where a body shrinks to. Around its feet it would seem to be drawn away along the ground.
constexpr glm::vec3 DEATH_BODY_CENTRE(0.0f, 0.9f, 0.0f);
constexpr glm::vec3 DEATH_PALE_COLOR(0.92f, 0.92f, 0.88f);

}  // namespace

void appendBox(std::vector<SceneVertex>& out, const glm::vec3& c, const glm::vec3& h,
               const glm::vec4& color, bool lit, uint8_t faces, const glm::vec4* texture) {
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
    // Through the box, as a stencil would go: across a side along +X or +Z, whichever it runs
    // along, so that the faces opposite each other agree and boxes in a row can carry one picture
    auto quad = [&](int a, int b, int cc, int d, glm::vec3 n) {
        pushQuad(out, p[a], p[b], p[cc], p[d], n, color);
        if (!texture) return;
        const glm::vec4& r = *texture;
        for (size_t i = out.size() - 6; i < out.size(); ++i) {
            glm::vec3 t = (out[i].position - (c - h)) / (2.0f * glm::max(h, glm::vec3(1e-6f)));
            glm::vec2 st = std::abs(n.y) > 0.5f ? glm::vec2(t.x, t.z)
                         : std::abs(n.x) > 0.5f ? glm::vec2(t.z, 1.0f - t.y)
                                                : glm::vec2(t.x, 1.0f - t.y);
            out[i].uv = glm::vec2(r.x + st.x * (r.z - r.x), r.y + st.y * (r.w - r.y));
        }
    };
    if (side(Face::PosX)) quad(1, 5, 6, 2, normal(1, 0, 0));
    if (side(Face::NegX)) quad(4, 0, 3, 7, normal(-1, 0, 0));
    if (side(Face::PosY)) quad(3, 2, 6, 7, normal(0, 1, 0));
    if (side(Face::NegY)) quad(4, 5, 1, 0, normal(0, -1, 0));
    if (side(Face::PosZ)) quad(5, 4, 7, 6, normal(0, 0, 1));
    if (side(Face::NegZ)) quad(0, 1, 2, 3, normal(0, 0, -1));
}

void appendCloudLayer(std::vector<SceneVertex>& out, const CloudMask& mask, const glm::dvec3& cameraPos,
                      double height, double cell, double drift, int radius, const glm::vec4& color) {
    if (mask.width <= 0 || cell <= 0.0) return;
    const int64_t cx = static_cast<int64_t>(std::floor((cameraPos.x - drift) / cell));
    const int64_t cz = static_cast<int64_t>(std::floor(cameraPos.z / cell));
    const float y = static_cast<float>(height - cameraPos.y);
    const glm::vec3 up(0.0f, 0.0f, 0.0f);  // unlit: clouds are as bright as the sky makes them
    for (int64_t j = cz - radius; j <= cz + radius; ++j) {
        int64_t i = cx - radius;
        while (i <= cx + radius) {
            if (!mask.at(i, j)) {
                ++i;
                continue;
            }
            int64_t start = i;
            while (i <= cx + radius && mask.at(i, j)) ++i;
            float x0 = static_cast<float>(start * cell + drift - cameraPos.x);
            float x1 = static_cast<float>(i * cell + drift - cameraPos.x);
            float z0 = static_cast<float>(j * cell - cameraPos.z);
            float z1 = static_cast<float>((j + 1) * cell - cameraPos.z);
            pushQuad(out, {x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, up, color);
        }
    }
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

void appendBoxEdges(std::vector<SceneVertex>& out, const glm::vec3& minCorner, const glm::vec3& maxCorner,
                    float width, const glm::vec4& color) {
    auto corner = [&](int i) {
        return glm::vec3((i & 1) ? maxCorner.x : minCorner.x, (i & 2) ? maxCorner.y : minCorner.y,
                         (i & 4) ? maxCorner.z : minCorner.z);
    };
    // Flat on an axis, its two layers are one: draw it once, or a see-through line doubles up
    int flat = 0;
    for (int axis = 0; axis < 3; ++axis) {
        if (maxCorner[axis] <= minCorner[axis]) flat |= 1 << axis;
    }
    for (int i = 0; i < 8; ++i) {
        if (i & flat) continue;
        for (int axis = 0; axis < 3; ++axis) {
            int bit = 1 << axis;
            if (i & bit) continue;  // Each edge once, from its low end
            if (maxCorner[axis] <= minCorner[axis]) continue;
            glm::vec3 a = corner(i) * (1.0f - EDGE_LIFT);
            glm::vec3 b = corner(i | bit) * (1.0f - EDGE_LIFT);
            // Across the edge, square to the line of sight: the strip shows its full width
            glm::vec3 side = glm::cross(b - a, (a + b) * 0.5f);
            float length = glm::length(side);
            if (length < 1e-9f) continue;
            side /= length;
            glm::vec3 sa = side * (0.5f * width * glm::length(a));
            glm::vec3 sb = side * (0.5f * width * glm::length(b));
            pushQuad(out, a - sa, b - sb, b + sb, a + sa, glm::vec3(0.0f), color);
        }
    }
}

void appendEntity(std::vector<SceneVertex>& out, const EntityModel& model,
                  const EntityRenderState& state, const glm::dvec3& cameraPos,
                  const glm::vec3& teamColor, float light, float tickRate,
                  EntityPoseScratch& scratch, const std::array<glm::vec4, 6>* heldFaces) {
    // Which clip is playing, by the name the model's parts know it by
    std::string_view animation;
    if (const EntityTypeDef* def = EntityTypeRegistry::global().getType(state.typeId)) {
        for (const auto& [name, slot] : def->animationStates) {
            if (slot == state.currentAnimationId) animation = name;
        }
    }

    const Skeleton& skeleton = *model.skeleton;
    skeleton.computeRestPose(scratch.local);
    state.animator.sample(scratch.local);
    if (heldFaces && model.holdPose && model.mesh.showsHeld(animation)) model.holdPose->sample(0.0f, scratch.local);
    skeleton.computeWorldTransforms(scratch.local, scratch.world);

    float alpha = state.interpolationAlpha(tickRate);
    glm::vec3 viewPos(state.interpolatedPosition(alpha) - cameraPos);
    viewPos.y += state.groundLift;
    glm::mat4 root = glm::rotate(glm::translate(glm::mat4(1.0f), viewPos), state.shownYaw,
                                 glm::vec3(0.0f, 1.0f, 0.0f));

    float pale = 0.0f;
    if (state.isDying()) {
        // Over sideways from the feet, gathering speed as it goes. Which way depends on who it
        // is, so that a squad doesn't drop in step.
        float fall = std::min(state.dying / DEATH_FALL_END, 1.0f);
        float side = (state.id & 1) != 0 ? 1.0f : -1.0f;
        root = glm::translate(root, glm::vec3(0.0f, DEATH_LIE_HEIGHT * fall, 0.0f));
        root = glm::rotate(root, side * glm::half_pi<float>() * fall * fall, glm::vec3(0.0f, 0.0f, 1.0f));

        float restEnd = DEATH_FALL_END + DEATH_REST;
        pale = std::min(std::max(state.dying - DEATH_FALL_END, 0.0f) / DEATH_REST, 1.0f);
        float shrink = 1.0f - std::max(state.dying - restEnd, 0.0f) / (1.0f - restEnd);
        root = glm::scale(glm::translate(root, DEATH_BODY_CENTRE), glm::vec3(shrink));
        root = glm::translate(root, -DEATH_BODY_CENTRE);
    }
    for (auto& pose : scratch.world) pose = root * pose;

    model.mesh.buildVertices(scratch.world, scratch.vertices, scratch.indices, teamColor, animation,
                             model.atlasRect, model.textureSize, heldFaces);
    bool placed = model.atlasRect.z > model.atlasRect.x;
    for (const auto& part : model.mesh.parts) placed = placed || part.atlas.z > part.atlas.x;
    placed = placed || heldFaces;

    out.reserve(out.size() + scratch.indices.size());
    for (uint32_t index : scratch.indices) {
        const EntityVertex& v = scratch.vertices[index];
        glm::vec3 color = glm::mix(v.color * light, DEATH_PALE_COLOR, pale);
        glm::vec2 uv = (placed && v.skinned) ? v.texCoord : glm::vec2(-1.0f);
        out.push_back({v.position, v.normal, glm::vec4(color, 1.0f), uv});
    }
}

}  // namespace finevox::render
