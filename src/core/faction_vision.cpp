#include "finevox/core/faction_vision.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace finevox {

namespace {

/// Ticks between a faction's sight updates. Staggered across factions.
constexpr uint32_t UPDATE_INTERVAL = 15;

/// Extra sight per block of height above the viewed ground
constexpr float ELEVATION_REACH = 0.5f;

/// See-through blocks (canopy) thin a sight line rather than end it
constexpr float FOLIAGE_DENSITY = 0.34f;

/// A column counts as seen if a body this tall standing on it would be visible
constexpr double TARGET_HEIGHT = 1.5;

/// Horizontal blocks between rays at their far end
constexpr float RAY_SPACING = 1.5f;

/// Furthest a lookout's height can extend its range
constexpr float MAX_ELEVATION_RANGE = 40.0f;

/// Periodic updates a cell stays visible after sight of it is lost. Cells at the
/// edge of a moving unit's view would otherwise blink on every update.
constexpr uint8_t VISIBLE_LINGER_UPDATES = 4;

/// Chunk columns checked for changed blocks each tick
constexpr int32_t CHUNKS_CHECKED_PER_TICK = 12;

constexpr float EYE_HEIGHT = 1.6f;

}  // namespace

FactionVision::FactionVision(World& world, EntityManager& entities, VisionBounds bounds)
    : world_(world), entities_(entities), bounds_(bounds) {
    cellsX_ = std::max(1, (bounds.maxX - bounds.minX + CELL_SIZE - 1) / CELL_SIZE);
    cellsZ_ = std::max(1, (bounds.maxZ - bounds.minZ + CELL_SIZE - 1) / CELL_SIZE);
}

int32_t FactionVision::cellIndex(int32_t x, int32_t z) const {
    if (x < bounds_.minX || x >= bounds_.maxX || z < bounds_.minZ || z >= bounds_.maxZ) return -1;
    int32_t cx = (x - bounds_.minX) / CELL_SIZE;
    int32_t cz = (z - bounds_.minZ) / CELL_SIZE;
    return cz * cellsX_ + cx;
}

FactionVisionState FactionVision::exportState() const {
    std::shared_lock lock(mutex_);
    FactionVisionState state;
    state.nextHandle = nextSource_;
    for (const auto& [faction, cells] : factions_) state.factions.emplace_back(faction, cells);
    for (const auto& [handle, source] : sources_) {
        state.lookouts.push_back({handle, source.faction, source.eye, source.radius});
    }
    return state;
}

void FactionVision::importState(const FactionVisionState& state) {
    for (const auto& [faction, cells] : state.factions) addFaction(faction);

    std::unique_lock lock(mutex_);
    for (const auto& [faction, cells] : state.factions) {
        auto found = factions_.find(faction);
        if (found != factions_.end() && found->second.size() == cells.size()) found->second = cells;
    }
    for (const FactionVisionState::Lookout& lookout : state.lookouts) {
        sources_[lookout.handle] = Source{lookout.faction, lookout.eye, lookout.radius};
    }
    nextSource_ = state.nextHandle;
}

void FactionVision::addFaction(uint16_t faction) {
    std::unique_lock lock(mutex_);
    if (factions_.count(faction)) return;
    factions_[faction].assign(static_cast<size_t>(cellsX_) * cellsZ_, 0);
    order_.push_back(faction);
}

int64_t FactionVision::addSource(uint16_t faction, const glm::dvec3& eye, float radius) {
    addFaction(faction);
    std::unique_lock lock(mutex_);
    int64_t handle = nextSource_++;
    sources_[handle] = Source{faction, eye, radius};
    return handle;
}

void FactionVision::removeSource(int64_t handle) {
    std::unique_lock lock(mutex_);
    sources_.erase(handle);
}

VisionState FactionVision::stateAt(uint16_t faction, int32_t x, int32_t z) const {
    std::shared_lock lock(mutex_);
    auto it = factions_.find(faction);
    int32_t index = cellIndex(x, z);
    if (it == factions_.end() || index < 0) return VisionState::Unknown;
    return static_cast<VisionState>(it->second[index]);
}

std::vector<uint8_t> FactionVision::snapshot(uint16_t faction) const {
    std::shared_lock lock(mutex_);
    auto it = factions_.find(faction);
    return it == factions_.end() ? std::vector<uint8_t>{} : it->second;
}

size_t FactionVision::countCells(uint16_t faction, VisionState atLeast) const {
    std::shared_lock lock(mutex_);
    auto it = factions_.find(faction);
    if (it == factions_.end()) return 0;
    return static_cast<size_t>(std::count_if(it->second.begin(), it->second.end(), [&](uint8_t c) {
        return c >= static_cast<uint8_t>(atLeast);
    }));
}

void FactionVision::reveal(uint16_t faction, const glm::dvec3& center, float radius) {
    addFaction(faction);
    std::unique_lock lock(mutex_);
    auto& cells = factions_[faction];
    int32_t minX = static_cast<int32_t>(std::floor(center.x - radius));
    int32_t maxX = static_cast<int32_t>(std::ceil(center.x + radius));
    int32_t minZ = static_cast<int32_t>(std::floor(center.z - radius));
    int32_t maxZ = static_cast<int32_t>(std::ceil(center.z + radius));
    for (int32_t z = minZ; z <= maxZ; z += CELL_SIZE) {
        for (int32_t x = minX; x <= maxX; x += CELL_SIZE) {
            double dx = x - center.x;
            double dz = z - center.z;
            if (dx * dx + dz * dz > radius * radius) continue;
            int32_t index = cellIndex(x, z);
            if (index >= 0 && cells[index] == 0) {
                cells[index] = static_cast<uint8_t>(VisionState::Explored);
            }
        }
    }
}

bool FactionVision::hasLineOfSight(const glm::dvec3& from, const glm::dvec3& to) const {
    glm::dvec3 delta = to - from;
    double length = glm::length(delta);
    if (length < 0.001) return true;

    // Half-block steps: quick, and accurate enough for a whole cell's verdict
    int32_t steps = static_cast<int32_t>(length * 2.0);
    glm::dvec3 step = delta / double(std::max(steps, 1));
    glm::dvec3 point = from;
    float density = 0.0f;
    BlockCoord last(INT32_MIN, 0, 0);

    for (int32_t i = 1; i < steps; ++i) {
        point += step;
        BlockCoord cell(static_cast<int32_t>(std::floor(point.x)),
                        static_cast<int32_t>(std::floor(point.y)),
                        static_cast<int32_t>(std::floor(point.z)));
        if (cell == last) continue;
        last = cell;

        BlockTypeId block = world_.getBlock(cell);
        if (block.isAir()) continue;
        const BlockType& type = BlockRegistry::global().getType(block);
        if (type.isOpaque()) return false;
        density += FOLIAGE_DENSITY;
        if (density >= 1.0f) return false;
    }
    return true;
}

int32_t FactionVision::columnIndex(int32_t x, int32_t z) const {
    if (x < bounds_.minX || x >= bounds_.maxX || z < bounds_.minZ || z >= bounds_.maxZ) return -1;
    return (z - bounds_.minZ) * (bounds_.maxX - bounds_.minX) + (x - bounds_.minX);
}

void FactionVision::refreshChunkHeights(int32_t chunkX, int32_t chunkZ) {
    int32_t worldChunkX = (bounds_.minX >> 4) + chunkX;
    int32_t worldChunkZ = (bounds_.minZ >> 4) + chunkZ;
    const ChunkColumn* column = world_.getColumn(ColumnPos(worldChunkX, worldChunkZ));

    uint64_t version = 0;
    int32_t top = INT32_MIN;
    if (column) {
        column->forEachSubChunk([&](int32_t chunkY, const SubChunk& sub) {
            version += sub.blockVersion();
            top = std::max(top, chunkY);
        });
    }
    uint64_t& seen = chunkVersions_[size_t(chunkZ) * chunksX_ + chunkX];
    if (seen == version && version != 0) return;
    seen = version;

    // Block properties sit behind a registry lock; most columns share a few types
    std::unordered_map<uint32_t, uint8_t> kinds;
    auto kindOf = [&kinds](BlockTypeId block) -> uint8_t {
        auto [it, inserted] = kinds.try_emplace(block.id, uint8_t{0});
        if (inserted) {
            it->second = BlockRegistry::global().getType(block).isOpaque() ? 2 : 1;
        }
        return it->second;
    };

    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            int32_t index = columnIndex(worldChunkX * 16 + lx, worldChunkZ * 16 + lz);
            if (index < 0) continue;

            ColumnHeight height;
            if (column) {
                for (int32_t chunkY = top; chunkY >= top - 16 && height.solid < 0; --chunkY) {
                    const SubChunk* sub = column->getSubChunk(chunkY);
                    if (!sub) continue;
                    for (int32_t ly = 15; ly >= 0; --ly) {
                        BlockTypeId block = sub->getBlock(lx, ly, lz);
                        if (block.isAir()) continue;
                        int16_t y = static_cast<int16_t>(chunkY * 16 + ly);
                        if (kindOf(block) == 2) {
                            height.solid = y;
                            break;
                        }
                        if (height.foliage < 0) height.foliage = y;
                    }
                }
            }
            heights_[index] = height;
        }
    }
}

void FactionVision::ensureHeights() {
    if (!heights_.empty()) return;
    heights_.resize(size_t(bounds_.maxX - bounds_.minX) * (bounds_.maxZ - bounds_.minZ));
    chunksX_ = ((bounds_.maxX + 15) >> 4) - (bounds_.minX >> 4);
    chunksZ_ = ((bounds_.maxZ + 15) >> 4) - (bounds_.minZ >> 4);
    chunkVersions_.assign(size_t(chunksX_) * chunksZ_, 0);
    for (int32_t cz = 0; cz < chunksZ_; ++cz) {
        for (int32_t cx = 0; cx < chunksX_; ++cx) refreshChunkHeights(cx, cz);
    }
}

void FactionVision::refreshSomeHeights() {
    int32_t total = chunksX_ * chunksZ_;
    if (total == 0) return;
    for (int32_t i = 0; i < CHUNKS_CHECKED_PER_TICK; ++i) {
        heightCursor_ = (heightCursor_ + 1) % total;
        refreshChunkHeights(heightCursor_ % chunksX_, heightCursor_ / chunksX_);
    }
}

void FactionVision::castFrom(const glm::dvec3& eye, float radius, std::vector<uint8_t>& cells) {
    float maxRange = radius + MAX_ELEVATION_RANGE;
    int32_t rays = std::max(32, static_cast<int32_t>(2.0f * 3.14159f * maxRange / RAY_SPACING));

    for (int32_t ray = 0; ray < rays; ++ray) {
        float angle = 6.28318f * ray / rays;
        double dirX = std::cos(angle);
        double dirZ = std::sin(angle);

        double horizon = -1e9;
        float density = 0.0f;

        for (float distance = 1.0f; distance <= maxRange; distance += 1.0f) {
            int32_t x = static_cast<int32_t>(std::floor(eye.x + dirX * distance));
            int32_t z = static_cast<int32_t>(std::floor(eye.z + dirZ * distance));
            int32_t column = columnIndex(x, z);
            if (column < 0) break;

            const ColumnHeight height = heights_[column];
            double top = height.solid + 1.0;

            // Would something body-height standing here clear the horizon so far?
            double targetSlope = (top + TARGET_HEIGHT - eye.y) / distance;
            float allowed = radius + std::max(0.0f, static_cast<float>(eye.y - top)) * ELEVATION_REACH;
            if (targetSlope >= horizon && distance <= allowed) {
                int32_t cell = cellIndex(x, z);
                if (cell >= 0) cells[cell] = static_cast<uint8_t>(VisionState::Visible);
            }

            // Foliage thins a sight line that passes through it
            if (height.foliage >= 0) {
                double rayY = eye.y + std::max(horizon, targetSlope) * distance;
                if (rayY <= height.foliage + 1.0 && rayY >= height.solid + 1.0) {
                    density += FOLIAGE_DENSITY;
                    if (density >= 1.0f) break;
                }
            }

            horizon = std::max(horizon, (top - eye.y) / distance);
        }
    }
}

void FactionVision::updateFaction(uint16_t faction, std::vector<uint8_t>& cells, bool linger) {
    ensureHeights();

    // Work on a copy so readers on other threads never see a half-built map
    std::vector<uint8_t> next = cells;
    for (auto& cell : next) {
        if (cell == static_cast<uint8_t>(VisionState::Visible)) {
            cell = static_cast<uint8_t>(VisionState::Explored);
        }
    }

    {
        auto lock = entities_.lock();
        for (const auto& [id, entity] : entities_.entities()) {
            if (entity->faction() != faction || !entity->isAlive()) continue;
            Vec3 pos = entity->position();
            float radius = defaultRadius_;
            if (const auto* mob = dynamic_cast<const MobEntity*>(entity.get())) {
                if (const auto* def = mob->typeDef()) {
                    if (def->properties && def->properties->has("vision")) {
                        radius = static_cast<float>(def->properties->get<double>("vision", radius));
                    }
                }
            }
            castFrom(glm::dvec3(pos.x, pos.y + EYE_HEIGHT, pos.z), radius, next);
        }
    }

    {
        std::shared_lock lock(mutex_);
        for (const auto& [handle, source] : sources_) {
            if (source.faction == faction) castFrom(source.eye, source.radius, next);
        }
    }

    std::unique_lock lock(mutex_);
    auto& remaining = linger_[faction];
    remaining.resize(next.size(), 0);
    constexpr uint8_t VISIBLE = static_cast<uint8_t>(VisionState::Visible);
    for (size_t i = 0; i < next.size(); ++i) {
        if (next[i] == VISIBLE) {
            remaining[i] = VISIBLE_LINGER_UPDATES;
        } else if (remaining[i] > 0) {
            --remaining[i];
            if (linger && remaining[i] > 0) next[i] = VISIBLE;
        }
        if (!linger && next[i] != VISIBLE) remaining[i] = 0;
    }
    cells = std::move(next);
}

void FactionVision::refresh(uint16_t faction) {
    // An explicit refresh promises current terrain, so don't wait for the sweep
    ensureHeights();
    for (int32_t cz = 0; cz < chunksZ_; ++cz) {
        for (int32_t cx = 0; cx < chunksX_; ++cx) refreshChunkHeights(cx, cz);
    }

    std::vector<uint8_t>* cells = nullptr;
    {
        std::shared_lock lock(mutex_);
        auto it = factions_.find(faction);
        if (it != factions_.end()) cells = &it->second;
    }
    if (cells) updateFaction(faction, *cells, false);
}

void FactionVision::tick(float /*dt*/) {
    if (order_.empty()) return;
    ensureHeights();
    refreshSomeHeights();

    // Each faction gets a turn, spread evenly so they don't all land on one tick
    uint32_t spacing = std::max<uint32_t>(1, UPDATE_INTERVAL / static_cast<uint32_t>(order_.size()));
    if (++ticks_ % spacing != 0) return;
    turn_ = (turn_ + 1) % order_.size();

    uint16_t faction = order_[turn_];
    std::vector<uint8_t>* cells = nullptr;
    {
        std::shared_lock lock(mutex_);
        auto it = factions_.find(faction);
        if (it != factions_.end()) cells = &it->second;
    }
    if (cells) updateFaction(faction, *cells, true);
}

}  // namespace finevox
