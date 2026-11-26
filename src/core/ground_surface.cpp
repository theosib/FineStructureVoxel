#include "finevox/core/ground_surface.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace finevox {

namespace {

// The cells a face reads (groundPatch), from its block: one across, and three behind it and four
// out in front, which for a top is below and above, and for an underside the other way round. The
// corners follow links a cell up or down from the columns beside, and the diagonal one a cell
// further, and each looks a cell behind itself and two out.
constexpr int READS_ACROSS = 1;
constexpr int READS_UP_DOWN = 4;

// Past this many changes waiting, forgetting every face is quicker than going through them
constexpr size_t MAX_PENDING = 4096;

// Far enough for a whole column, near enough that nothing overflows
constexpr int32_t ALL_LOW = -(1 << 22);
constexpr int32_t ALL_HIGH = 1 << 22;

uint32_t faceKey(const BlockCoord& block) {
    return (static_cast<uint32_t>(block.y) << 8) | static_cast<uint32_t>((block.z & 0xF) << 4) |
           static_cast<uint32_t>(block.x & 0xF);
}

bool isOpen(GroundCell cell) { return cell == GroundCell::Open || cell == GroundCell::Wet; }

}  // namespace

GroundSurface::GroundSurface(const World& world, std::function<bool(BlockTypeId)> smooth)
    : world_(world), smooth_(std::move(smooth)) {
    cells_ = [this](const BlockCoord& pos) { return cellAt(pos); };
}

void GroundSurface::cellChanged(BlockCoord pos) {
    regionChanged(pos, pos);
}

void GroundSurface::regionChanged(BlockCoord low, BlockCoord high) {
    Range range{BlockCoord(low.x - READS_ACROSS, low.y - READS_UP_DOWN, low.z - READS_ACROSS),
                BlockCoord(high.x + READS_ACROSS, high.y + READS_UP_DOWN, high.z + READS_ACROSS)};
    std::lock_guard lock(pendingMutex_);
    if (pending_.size() >= MAX_PENDING) {
        pending_.clear();
        pending_.push_back(Range{BlockCoord(ALL_LOW, ALL_LOW, ALL_LOW), BlockCoord(ALL_HIGH, ALL_HIGH, ALL_HIGH)});
    } else {
        pending_.push_back(range);
    }
    hasPending_.store(true, std::memory_order_release);
}

void GroundSurface::columnChanged(ColumnPos pos) {
    // The faces along its edges in the columns round it read it too
    regionChanged(BlockCoord(pos.x * 16, ALL_LOW, pos.z * 16), BlockCoord(pos.x * 16 + 15, ALL_HIGH, pos.z * 16 + 15));
}

void GroundSurface::catchUp() {
    if (!hasPending_.load(std::memory_order_acquire)) return;
    std::vector<Range> ranges;
    {
        std::lock_guard lock(pendingMutex_);
        ranges.swap(pending_);
        hasPending_.store(false, std::memory_order_relaxed);
    }
    for (const Range& range : ranges) forget(range);
}

void GroundSurface::forget(const Range& range) {
    const BlockCoord& lo = range.low;
    const BlockCoord& hi = range.high;
    if (lo.x <= ALL_LOW && hi.x >= ALL_HIGH) {
        columns_.clear();
        return;
    }
    for (int32_t cx = lo.x >> 4; cx <= hi.x >> 4; ++cx) {
        for (int32_t cz = lo.z >> 4; cz <= hi.z >> 4; ++cz) {
            auto column = columns_.find(ColumnPos(cx, cz).pack());
            if (column == columns_.end()) continue;
            const int32_t x0 = std::max(lo.x, cx * 16), x1 = std::min(hi.x, cx * 16 + 15);
            const int32_t z0 = std::max(lo.z, cz * 16), z1 = std::min(hi.z, cz * 16 + 15);
            const bool wholeColumn = x0 == cx * 16 && x1 == cx * 16 + 15 && z0 == cz * 16 && z1 == cz * 16 + 15;
            if (wholeColumn && lo.y <= ALL_LOW && hi.y >= ALL_HIGH) {
                columns_.erase(column);
                continue;
            }
            for (Faces* faces : {&column->second.tops, &column->second.undersides}) {
                const int64_t cells = int64_t(x1 - x0 + 1) * (z1 - z0 + 1) * (int64_t(hi.y) - lo.y + 1);
                if (cells <= static_cast<int64_t>(faces->size())) {
                    for (int32_t y = lo.y; y <= hi.y; ++y) {
                        for (int32_t z = z0; z <= z1; ++z) {
                            for (int32_t x = x0; x <= x1; ++x) faces->erase(faceKey(BlockCoord(x, y, z)));
                        }
                    }
                } else {
                    std::erase_if(*faces, [&](const auto& entry) {
                        const uint32_t key = entry.first;
                        const int32_t y = static_cast<int32_t>(key) >> 8;
                        const int32_t x = cx * 16 + int32_t(key & 0xF);
                        const int32_t z = cz * 16 + int32_t((key >> 4) & 0xF);
                        return y >= lo.y && y <= hi.y && x >= x0 && x <= x1 && z >= z0 && z <= z1;
                    });
                }
            }
            if (column->second.tops.empty() && column->second.undersides.empty()) columns_.erase(column);
        }
    }
}

GroundCell GroundSurface::typeClass(BlockTypeId type) {
    TypeSlot& slot = types_[(type.id * 2654435761u) >> 26];
    if (slot.type != type.id) slot = TypeSlot{type.id, groundCellOf(type, smooth_)};
    return slot.cell;
}

GroundCell GroundSurface::cellAt(const BlockCoord& pos) {
    const GroundCell cell = typeClass(world_.getBlock(pos));
    if (cell != GroundCell::Open) return cell;
    return world_.hasFluid(pos) ? GroundCell::Wet : GroundCell::Open;
}

const GroundSurface::Face& GroundSurface::face(const BlockCoord& block, int up) {
    Column& column = columns_[ColumnPos::fromBlock(block).pack()];
    Faces& faces = up > 0 ? column.tops : column.undersides;
    auto [it, inserted] = faces.try_emplace(faceKey(block));
    Face& f = it->second;
    if (!inserted) return f;

    const auto patch = groundPatch(cells_, block, up);
    if (!patch || patch->flat()) return f;
    for (int i = 0; i < GROUND_COLUMNS; ++i) {
        for (int j = 0; j < GROUND_COLUMNS; ++j) {
            const float u = (float(i) + 0.5f) / float(GROUND_COLUMNS);
            const float v = (float(j) + 0.5f) / float(GROUND_COLUMNS);
            const long q = std::lround(patch->height(u, v) * float(GROUND_QUANTUM));
            const auto h = static_cast<int8_t>(std::clamp<long>(q, -GROUND_QUANTUM, GROUND_QUANTUM));
            f.height[i][j] = h;
            f.intoBlock |= h < 0;
            f.intoOpen |= h > 0;
        }
    }
    return f;
}

std::optional<GroundSurface::Solid> GroundSurface::solidIn(BlockCoord pos, BlockTypeId type) {
    catchUp();
    auto blocks = [](int8_t h) { return float(h) / float(GROUND_QUANTUM); };
    Solid solid;
    auto add = [&](int i, int j, float low, float high) {
        if (high > low) solid.runs[i][j][solid.count[i][j]++] = glm::vec2(low, high);
    };

    const GroundCell cell = typeClass(type);
    if (cell == GroundCell::Ground) {
        // Its own faces: a top cuts it down from above, an underside up from below
        const Face* top = isOpen(cellAt(BlockCoord(pos.x, pos.y + 1, pos.z))) ? &face(pos, 1) : nullptr;
        const Face* under = isOpen(cellAt(BlockCoord(pos.x, pos.y - 1, pos.z))) ? &face(pos, -1) : nullptr;
        if (!(top && top->intoBlock) && !(under && under->intoBlock)) return std::nullopt;
        for (int i = 0; i < GROUND_COLUMNS; ++i) {
            for (int j = 0; j < GROUND_COLUMNS; ++j) {
                float low = under ? std::max(-blocks(under->height[i][j]), 0.0f) : 0.0f;
                float high = top ? 1.0f + std::min(blocks(top->height[i][j]), 0.0f) : 1.0f;
                // A block a cell thick keeps its top and underside each on its own half, and they
                // can meet: drawn, an edge; solid, the thinnest step, or there'd be a hole there
                constexpr float THINNEST = 1.0f / float(GROUND_QUANTUM);
                if (high - low < THINNEST) {
                    const float middle = (low + high) * 0.5f;
                    low = middle - THINNEST * 0.5f;
                    high = middle + THINNEST * 0.5f;
                }
                add(i, j, low, high);
            }
        }
        return solid;
    }
    if (cell != GroundCell::Open) return std::nullopt;

    // The faces round it: ground rising into it from below, and hanging into it from above
    const BlockCoord below(pos.x, pos.y - 1, pos.z), above(pos.x, pos.y + 1, pos.z);
    const Face* floor = typeClass(world_.getBlock(below)) == GroundCell::Ground ? &face(below, 1) : nullptr;
    const Face* roof = typeClass(world_.getBlock(above)) == GroundCell::Ground ? &face(above, -1) : nullptr;
    if (!(floor && floor->intoOpen) && !(roof && roof->intoOpen)) return std::nullopt;
    for (int i = 0; i < GROUND_COLUMNS; ++i) {
        for (int j = 0; j < GROUND_COLUMNS; ++j) {
            if (floor) add(i, j, 0.0f, std::max(blocks(floor->height[i][j]), 0.0f));
            if (roof) add(i, j, 1.0f - std::max(blocks(roof->height[i][j]), 0.0f), 1.0f);
        }
    }
    return solid;
}

const CollisionShape* GroundSurface::shape(BlockCoord pos, BlockTypeId type) {
    const auto solid = solidIn(pos, type);
    if (!solid) return nullptr;
    thread_local CollisionShape scratch;
    scratch.clear();
    constexpr float SIDE = 1.0f / float(GROUND_COLUMNS);
    auto same = [&](int i, int a, int b) {
        if (solid->count[i][a] != solid->count[i][b]) return false;
        for (int r = 0; r < solid->count[i][a]; ++r) {
            if (solid->runs[i][a][r] != solid->runs[i][b][r]) return false;
        }
        return true;
    };
    for (int i = 0; i < GROUND_COLUMNS; ++i) {
        // Columns solid over the same heights as the one before make one box
        int j = 0;
        while (j < GROUND_COLUMNS) {
            int end = j + 1;
            while (end < GROUND_COLUMNS && same(i, j, end)) ++end;
            for (int r = 0; r < solid->count[i][j]; ++r) {
                const glm::vec2 run = solid->runs[i][j][r];
                scratch.addBox(AABB(float(i) * SIDE, run.x, float(j) * SIDE, float(i + 1) * SIDE, run.y, float(end) * SIDE));
            }
            j = end;
        }
    }
    return &scratch;
}

std::optional<bool> GroundSurface::solidAt(const glm::dvec3& point) {
    const BlockCoord cell(static_cast<int32_t>(std::floor(point.x)), static_cast<int32_t>(std::floor(point.y)),
                          static_cast<int32_t>(std::floor(point.z)));
    const auto solid = solidIn(cell, world_.getBlock(cell));
    if (!solid) {
        // A ground block the surface leaves whole, or open ground with nothing reaching into it
        const GroundCell here = typeClass(world_.getBlock(cell));
        if (here != GroundCell::Ground && here != GroundCell::Open) return std::nullopt;
        const bool faced = here == GroundCell::Ground
            ? isOpen(cellAt(BlockCoord(cell.x, cell.y + 1, cell.z))) || isOpen(cellAt(BlockCoord(cell.x, cell.y - 1, cell.z)))
            : typeClass(world_.getBlock(BlockCoord(cell.x, cell.y - 1, cell.z))) == GroundCell::Ground ||
              typeClass(world_.getBlock(BlockCoord(cell.x, cell.y + 1, cell.z))) == GroundCell::Ground;
        if (!faced) return std::nullopt;
        return here == GroundCell::Ground;
    }
    const int i = std::clamp(int((point.x - cell.x) * GROUND_COLUMNS), 0, GROUND_COLUMNS - 1);
    const int j = std::clamp(int((point.z - cell.z) * GROUND_COLUMNS), 0, GROUND_COLUMNS - 1);
    const float up = float(point.y - cell.y);
    for (int r = 0; r < solid->count[i][j]; ++r) {
        if (up >= solid->runs[i][j][r].x && up < solid->runs[i][j][r].y) return true;
    }
    return false;
}

bool GroundSurface::reachesInto(BlockCoord pos) {
    const BlockTypeId type = world_.getBlock(pos);
    if (typeClass(type) != GroundCell::Open) return false;
    return solidIn(pos, type).has_value();
}

size_t GroundSurface::facesKept() const {
    size_t count = 0;
    for (const auto& [key, column] : columns_) count += column.tops.size() + column.undersides.size();
    return count;
}

}  // namespace finevox
