#include "finevox/core/ground_smoothing.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace finevox {

namespace {

constexpr int EDGE_STEP[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

// How far under its face a shore goes: a little past a source's surface, 14/16 up the block beside
// it, so the water drawn over it never meets it. No further, since a body only stands on ground
// within a block of its face (ground_surface.hpp), and under water it isn't seen.
constexpr float SHORE_DEPTH = 1.0f + 3.0f / 16.0f;

// A face seen from its own block, with k counted outward: 0 the block, 1 the cell it looks into
struct Frame {
    const GroundCellProvider& cells;
    BlockCoord block;
    int up;

    [[nodiscard]] GroundCell at(int dx, int k, int dz) const {
        return cells(BlockCoord(block.x + dx, block.y + up * k, block.z + dz));
    }
};

struct EdgeView {
    GroundEdge kind;
    int8_t link;
};

// Water is as open as air: ground runs down into it as into air, and the fluid mesh draws water
// over the part of a bank that dips under it (fluid_mesh.hpp)
bool isOpen(GroundCell cell) { return cell == GroundCell::Open || cell == GroundCell::Wet; }

// What lies past the face's edge toward (dx, dz). Two faces call each other a Link, or neither
// does: a step up from one is a step down from the other, by the same cells.
EdgeView classify(const Frame& f, int dx, int dz) {
    const GroundCell beside = f.at(dx, 1, dz);
    if (!isOpen(beside)) {
        if (beside != GroundCell::Ground) return {GroundEdge::Pin, 0};
        // Ground level with what this face looks into: a step up, if it has room above as this
        // face does. Otherwise a wall.
        const GroundCell above = f.at(dx, 2, dz);
        if (isOpen(above) && isOpen(f.at(0, 2, 0))) return {GroundEdge::Link, 1};
        if (above == GroundCell::Ground || isOpen(above)) return {GroundEdge::Up, 0};
        return {GroundEdge::Pin, 0};
    }
    const GroundCell level = f.at(dx, 0, dz);
    if (level == GroundCell::Ground) return {GroundEdge::Link, 0};
    if (!isOpen(level)) return {GroundEdge::Pin, 0};
    if (f.at(dx, -1, dz) == GroundCell::Ground) return {GroundEdge::Link, -1};
    // Water's surface a block under this face: rounded off half a block, it would stand as a wall
    // over the waterline, where the drop under it is hidden anyway
    if (f.up > 0 && level == GroundCell::Open && f.at(dx, -1, dz) == GroundCell::Wet) return {GroundEdge::Shore, 0};
    return {GroundEdge::Down, 0};
}

// The height a neighbour stands in for, relative to this face: a step of two or more counts as
// one, so the face goes half way to meet it and no further
float standIn(const EdgeView& e) {
    switch (e.kind) {
        case GroundEdge::Link: return float(e.link);
        case GroundEdge::Up: return 1.0f;
        case GroundEdge::Down: return -1.0f;
        // Steeply, down under the water's surface, so that the edge is at SHORE_DEPTH
        case GroundEdge::Shore: return -2.0f * SHORE_DEPTH;
        case GroundEdge::Pin: return 0.0f;
    }
    return 0.0f;
}

// The height of the face's corner toward (sx, sz). Every face that meets there works out the
// same number, or a sliver of wall would show between them. The four columns round the corner
// are joined where their faces link, the same group from any of them. Each in the group counts
// at its own height, and each that isn't at what its neighbours in the group take it for.
float cornerHeight(const Frame& f, int sx, int sz) {
    // The columns: 0 this one, 1 across x, 2 across z, 3 diagonally, and which are beside which
    static constexpr int OX[4] = {0, 1, 0, 1};
    static constexpr int OZ[4] = {0, 0, 1, 1};
    static constexpr int BESIDE[4][2] = {{1, 2}, {0, 3}, {0, 3}, {1, 2}};

    std::array<std::optional<int32_t>, 4> level;  // The y of the block whose face joins in
    std::array<std::array<std::optional<EdgeView>, 4>, 4> view;  // view[p][q]: what p sees at q
    level[0] = f.block.y;
    int queue[4] = {0, 0, 0, 0};
    int head = 0, tail = 1;
    while (head < tail) {
        const int p = queue[head++];
        const Frame here{f.cells, BlockCoord(f.block.x + OX[p] * sx, *level[p], f.block.z + OZ[p] * sz), f.up};
        for (int q : BESIDE[p]) {
            const EdgeView v = classify(here, (OX[q] - OX[p]) * sx, (OZ[q] - OZ[p]) * sz);
            view[p][q] = v;
            if (v.kind == GroundEdge::Link && !level[q]) {
                level[q] = *level[p] + f.up * v.link;
                queue[tail++] = q;
            }
        }
    }

    auto own = [&](int p) { return float((*level[p] - f.block.y) * f.up); };
    std::array<std::optional<float>, 4> value;
    for (int q = 0; q < 4; ++q) {
        if (level[q]) {
            value[q] = own(q);
            continue;
        }
        float sum = 0.0f;
        int n = 0;
        for (int p : BESIDE[q]) {
            if (!level[p]) continue;
            sum += own(p) + standIn(*view[p][q]);
            ++n;
        }
        if (n > 0) value[q] = sum / float(n);
    }
    // Only a face on its own leaves a column beside nothing in its group: the diagonal one,
    // which goes between the two beside it
    if (!value[3]) value[3] = (*value[1] + *value[2]) * 0.5f;

    // A shore goes no further under the water than SHORE_DEPTH below the lowest face here, the
    // same from every face in the group. Nothing else gets as low.
    float lowest = 0.0f;
    for (int q = 0; q < 4; ++q) {
        if (level[q]) lowest = std::min(lowest, own(q));
    }
    return std::max((*value[0] + *value[1] + *value[2] + *value[3]) * 0.25f, lowest - SHORE_DEPTH);
}

float bernstein(int i, float t) {
    switch (i) {
        case 0: return (1.0f - t) * (1.0f - t);
        case 1: return 2.0f * t * (1.0f - t);
        default: return t * t;
    }
}

float bernsteinSlope(int i, float t) {
    switch (i) {
        case 0: return -2.0f * (1.0f - t);
        case 1: return 2.0f - 4.0f * t;
        default: return 2.0f * t;
    }
}

}  // namespace

GroundCell groundCellOf(BlockTypeId type, const std::function<bool(BlockTypeId)>& smooth) {
    if (type == AIR_BLOCK_TYPE) return GroundCell::Open;
    const BlockType& block = BlockRegistry::global().getType(type);
    if (block.isOpaque()) {
        return smooth(type) && !block.hasCustomMesh() ? GroundCell::Ground : GroundCell::Solid;
    }
    // A plant: a shape of its own that nothing bumps into
    if (block.hasCustomMesh() && !block.hasCollision()) return GroundCell::Open;
    return GroundCell::Solid;
}

GroundCellProvider groundCells(const World& world, std::function<bool(BlockTypeId)> smooth) {
    return [&world, smooth = std::move(smooth)](const BlockCoord& pos) {
        const GroundCell cell = groundCellOf(world.getBlock(pos), smooth);
        if (cell != GroundCell::Open) return cell;
        return world.hasFluid(pos) ? GroundCell::Wet : GroundCell::Open;
    };
}

bool GroundPatch::flat() const {
    for (const auto& row : control) {
        for (float c : row) {
            if (c != 0.0f) return false;
        }
    }
    return true;
}

float GroundPatch::height(float u, float v) const {
    float h = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float bu = bernstein(i, u);
        for (int j = 0; j < 3; ++j) h += bu * bernstein(j, v) * control[i][j];
    }
    return std::clamp(h, lowest, highest);
}

glm::vec2 GroundPatch::slope(float u, float v) const {
    glm::vec2 d(0.0f);
    const float h = height(u, v);
    if (h <= lowest || h >= highest) return d;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            d.x += bernsteinSlope(i, u) * bernstein(j, v) * control[i][j];
            d.y += bernstein(i, u) * bernsteinSlope(j, v) * control[i][j];
        }
    }
    return d;
}

float GroundPatch::edgeHeight(int e, float s) const {
    switch (e) {
        case GROUND_EDGE_X_LOW: return height(0.0f, s);
        case GROUND_EDGE_X_HIGH: return height(1.0f, s);
        case GROUND_EDGE_Z_LOW: return height(s, 0.0f);
        default: return height(s, 1.0f);
    }
}

glm::vec3 GroundPatch::normal(float u, float v) const {
    const glm::vec2 d = slope(u, v);
    return glm::normalize(glm::vec3(-d.x, float(up), -d.y));
}

std::optional<GroundPatch> groundPatch(const GroundCellProvider& cells, BlockCoord block, int up) {
    if (cells(block) != GroundCell::Ground) return std::nullopt;
    if (!isOpen(cells(BlockCoord(block.x, block.y + up, block.z)))) return std::nullopt;

    GroundPatch patch;
    patch.block = block;
    patch.up = up;
    const Frame frame{cells, block, up};

    EdgeView views[4];
    float standIns[4];
    for (int e = 0; e < 4; ++e) {
        views[e] = classify(frame, EDGE_STEP[e][0], EDGE_STEP[e][1]);
        standIns[e] = standIn(views[e]);
        patch.edge[e] = views[e].kind;
        patch.link[e] = views[e].link;
    }

    // The B-spline's control net as a Bezier patch: the middle is this face's own height, each
    // edge the mean of it and the neighbour past it, each corner the mean of the four columns
    // that meet there (cornerHeight)
    auto& c = patch.control;
    c[1][1] = 0.0f;
    c[0][1] = standIns[GROUND_EDGE_X_LOW] * 0.5f;
    c[2][1] = standIns[GROUND_EDGE_X_HIGH] * 0.5f;
    c[1][0] = standIns[GROUND_EDGE_Z_LOW] * 0.5f;
    c[1][2] = standIns[GROUND_EDGE_Z_HIGH] * 0.5f;
    for (int sx : {-1, 1}) {
        for (int sz : {-1, 1}) {
            c[sx < 0 ? 0 : 2][sz < 0 ? 0 : 2] = cornerHeight(frame, sx, sz);
        }
    }

    // A way across that's a straight line (a slope, or flat) needs only one piece
    auto straight = [&](bool alongX) {
        for (int j = 0; j < 3; ++j) {
            const float a = alongX ? c[0][j] : c[j][0];
            const float m = alongX ? c[1][j] : c[j][1];
            const float b = alongX ? c[2][j] : c[j][2];
            if (std::abs(m - (a + b) * 0.5f) > 1e-4f) return false;
        }
        return true;
    };
    const GroundCell behind = frame.at(0, -1, 0);
    if (isOpen(behind)) patch.lowest = -0.5f;
    if (frame.at(0, 2, 0) == GroundCell::Ground) patch.highest = 0.5f;

    patch.steps[0] = straight(true) ? 1 : GROUND_CURVED_STEPS;
    patch.steps[1] = straight(false) ? 1 : GROUND_CURVED_STEPS;
    return patch;
}

std::optional<double> groundSurfaceY(const GroundCellProvider& cells, const glm::dvec3& feet) {
    const int32_t x = static_cast<int32_t>(std::floor(feet.x));
    const int32_t z = static_cast<int32_t>(std::floor(feet.z));
    const int32_t top = static_cast<int32_t>(std::floor(feet.y + 0.01));
    for (int32_t y = top - 1; y >= top - 2; --y) {
        const BlockCoord block(x, y, z);
        const GroundCell cell = cells(block);
        if (cell == GroundCell::Ground) {
            const auto patch = groundPatch(cells, block, 1);
            if (!patch) return std::nullopt;
            return patch->worldY(float(feet.x - x), float(feet.z - z));
        }
        if (cell == GroundCell::Solid) return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace finevox
