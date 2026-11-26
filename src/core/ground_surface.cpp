#include "finevox/core/ground_surface.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace finevox {

namespace {

// The cells a top face reads (groundPatch), from its block: one across, three below, four above.
// The corners follow links a cell up or down from the columns beside, and the diagonal one a
// cell further, and each looks a cell below itself and two above.
constexpr int READS_ACROSS = 1;
constexpr int READS_BELOW = 3;
constexpr int READS_ABOVE = 4;

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
    Range range{BlockCoord(low.x - READS_ACROSS, low.y - READS_ABOVE, low.z - READS_ACROSS),
                BlockCoord(high.x + READS_ACROSS, high.y + READS_BELOW, high.z + READS_ACROSS)};
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
            Faces& faces = column->second;
            const int32_t x0 = std::max(lo.x, cx * 16), x1 = std::min(hi.x, cx * 16 + 15);
            const int32_t z0 = std::max(lo.z, cz * 16), z1 = std::min(hi.z, cz * 16 + 15);
            const bool wholeColumn = x0 == cx * 16 && x1 == cx * 16 + 15 && z0 == cz * 16 && z1 == cz * 16 + 15;
            if (wholeColumn && lo.y <= ALL_LOW && hi.y >= ALL_HIGH) {
                columns_.erase(column);
                continue;
            }
            const int64_t cells = int64_t(x1 - x0 + 1) * (z1 - z0 + 1) * (int64_t(hi.y) - lo.y + 1);
            if (cells <= static_cast<int64_t>(faces.size())) {
                for (int32_t y = lo.y; y <= hi.y; ++y) {
                    for (int32_t z = z0; z <= z1; ++z) {
                        for (int32_t x = x0; x <= x1; ++x) faces.erase(faceKey(BlockCoord(x, y, z)));
                    }
                }
            } else {
                std::erase_if(faces, [&](const auto& entry) {
                    const uint32_t key = entry.first;
                    const int32_t y = static_cast<int32_t>(key) >> 8;
                    const int32_t x = cx * 16 + int32_t(key & 0xF);
                    const int32_t z = cz * 16 + int32_t((key >> 4) & 0xF);
                    return y >= lo.y && y <= hi.y && x >= x0 && x <= x1 && z >= z0 && z <= z1;
                });
            }
            if (faces.empty()) columns_.erase(column);
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

const GroundSurface::Face& GroundSurface::face(const BlockCoord& block) {
    Faces& faces = columns_[ColumnPos::fromBlock(block).pack()];
    auto [it, inserted] = faces.try_emplace(faceKey(block));
    Face& f = it->second;
    if (!inserted) return f;

    const auto patch = groundPatch(cells_, block, 1);
    if (!patch || patch->flat()) return f;
    for (int i = 0; i < GROUND_COLUMNS; ++i) {
        for (int j = 0; j < GROUND_COLUMNS; ++j) {
            const float u = (float(i) + 0.5f) / float(GROUND_COLUMNS);
            const float v = (float(j) + 0.5f) / float(GROUND_COLUMNS);
            const long q = std::lround(patch->height(u, v) * float(GROUND_QUANTUM));
            const auto h = static_cast<int8_t>(std::clamp<long>(q, -GROUND_QUANTUM, GROUND_QUANTUM));
            f.height[i][j] = h;
            f.dips |= h < 0;
            f.rises |= h > 0;
        }
    }
    return f;
}

const CollisionShape* GroundSurface::shape(BlockCoord pos, BlockTypeId type) {
    catchUp();
    const GroundCell cell = typeClass(type);
    const Face* f = nullptr;
    float base = 0.0f;  // What a column's height counts up from, in the cell
    if (cell == GroundCell::Ground) {
        if (!isOpen(cellAt(BlockCoord(pos.x, pos.y + 1, pos.z)))) return nullptr;
        f = &face(pos);
        if (!f->dips) return nullptr;
        base = 1.0f;
    } else if (cell == GroundCell::Open) {
        const BlockCoord below(pos.x, pos.y - 1, pos.z);
        if (typeClass(world_.getBlock(below)) != GroundCell::Ground) return nullptr;
        f = &face(below);
        if (!f->rises) return nullptr;
    } else {
        return nullptr;
    }

    // In the cell: a dip cuts the block down, a rise stands in the open cell over it
    auto top = [&](int i, int j) {
        const float h = float(f->height[i][j]) / float(GROUND_QUANTUM);
        return base > 0.0f ? base + std::min(h, 0.0f) : std::max(h, 0.0f);
    };
    thread_local CollisionShape scratch;
    scratch.clear();
    constexpr float SIDE = 1.0f / float(GROUND_COLUMNS);
    for (int i = 0; i < GROUND_COLUMNS; ++i) {
        // Columns as high as the one before make one box
        int j = 0;
        while (j < GROUND_COLUMNS) {
            const float height = top(i, j);
            int end = j + 1;
            while (end < GROUND_COLUMNS && top(i, end) == height) ++end;
            if (height > 0.0f) {
                scratch.addBox(AABB(float(i) * SIDE, 0.0f, float(j) * SIDE, float(i + 1) * SIDE, height, float(end) * SIDE));
            }
            j = end;
        }
    }
    return &scratch;
}

std::optional<bool> GroundSurface::solidAt(const glm::dvec3& point) {
    catchUp();
    const BlockCoord cell(static_cast<int32_t>(std::floor(point.x)), static_cast<int32_t>(std::floor(point.y)),
                          static_cast<int32_t>(std::floor(point.z)));
    const int i = std::clamp(int((point.x - cell.x) * GROUND_COLUMNS), 0, GROUND_COLUMNS - 1);
    const int j = std::clamp(int((point.z - cell.z) * GROUND_COLUMNS), 0, GROUND_COLUMNS - 1);
    const double up = point.y - cell.y;
    const GroundCell here = typeClass(world_.getBlock(cell));
    if (here == GroundCell::Ground) {
        if (!isOpen(cellAt(BlockCoord(cell.x, cell.y + 1, cell.z)))) return std::nullopt;
        const int h = face(cell).height[i][j];
        return up < 1.0 + double(std::min(h, 0)) / GROUND_QUANTUM;
    }
    if (here == GroundCell::Open) {
        const BlockCoord below(cell.x, cell.y - 1, cell.z);
        if (typeClass(world_.getBlock(below)) != GroundCell::Ground) return std::nullopt;
        const int h = face(below).height[i][j];
        return up < double(std::max(h, 0)) / GROUND_QUANTUM;
    }
    return std::nullopt;
}

size_t GroundSurface::facesKept() const {
    size_t count = 0;
    for (const auto& [key, faces] : columns_) count += faces.size();
    return count;
}

}  // namespace finevox
