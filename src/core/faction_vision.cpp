#include "finevox/core/faction_vision.hpp"
#include "finevox/core/block_type.hpp"
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

void FactionVision::castFrom(const glm::dvec3& eye, float radius, std::vector<uint8_t>& cells) {
    // Height buys range: a lookout over a valley sees past where a walker could
    float reach = radius;
    int32_t eyeX = static_cast<int32_t>(std::floor(eye.x));
    int32_t eyeZ = static_cast<int32_t>(std::floor(eye.z));

    int32_t span = static_cast<int32_t>(std::ceil((radius + 24.0f) / CELL_SIZE));
    for (int32_t cz = -span; cz <= span; ++cz) {
        for (int32_t cx = -span; cx <= span; ++cx) {
            int32_t x = eyeX + cx * CELL_SIZE;
            int32_t z = eyeZ + cz * CELL_SIZE;
            int32_t index = cellIndex(x, z);
            if (index < 0 || cells[index] == static_cast<uint8_t>(VisionState::Visible)) continue;

            int32_t cellCenterX = x - ((x - bounds_.minX) % CELL_SIZE) + CELL_SIZE / 2;
            int32_t cellCenterZ = z - ((z - bounds_.minZ) % CELL_SIZE) + CELL_SIZE / 2;

            // Look at the top of whatever stands there
            int32_t surface = static_cast<int32_t>(eye.y) + 32;
            while (surface > 0 && world_.getBlock(cellCenterX, surface, cellCenterZ).isAir()) {
                --surface;
            }
            double dx = cellCenterX + 0.5 - eye.x;
            double dz = cellCenterZ + 0.5 - eye.z;
            double horizontal = std::sqrt(dx * dx + dz * dz);
            float drop = static_cast<float>(eye.y - surface);
            float allowed = reach + std::max(0.0f, drop) * ELEVATION_REACH;
            if (horizontal > allowed) continue;

            glm::dvec3 target(cellCenterX + 0.5, surface + 1.5, cellCenterZ + 0.5);
            if (hasLineOfSight(eye, target)) {
                cells[index] = static_cast<uint8_t>(VisionState::Visible);
            }
        }
    }
}

void FactionVision::updateFaction(uint16_t faction, std::vector<uint8_t>& cells) {
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
    cells = std::move(next);
}

void FactionVision::refresh(uint16_t faction) {
    std::vector<uint8_t>* cells = nullptr;
    {
        std::shared_lock lock(mutex_);
        auto it = factions_.find(faction);
        if (it != factions_.end()) cells = &it->second;
    }
    if (cells) updateFaction(faction, *cells);
}

void FactionVision::tick(float /*dt*/) {
    if (order_.empty()) return;
    // Each faction gets a turn, spread evenly so they don't all land on one tick
    uint32_t spacing = std::max<uint32_t>(1, UPDATE_INTERVAL / static_cast<uint32_t>(order_.size()));
    if (++ticks_ % spacing != 0) return;
    turn_ = (turn_ + 1) % order_.size();
    refresh(order_[turn_]);
}

}  // namespace finevox
