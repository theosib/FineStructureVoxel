#include "finevox/core/fluid_simulator.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_interaction.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/physics.hpp"  // CollisionShape::FULL_BLOCK
#include "finevox/core/light_engine.hpp"

#include <queue>
#include <algorithm>
#include <array>

namespace finevox {

// ============================================================================
// Construction
// ============================================================================

FluidSimulator::FluidSimulator(World& world)
    : world_(world) {}

// ============================================================================
// Public API
// ============================================================================

void FluidSimulator::simulateTick() {
    if (!config_.enabled) return;

    // Move deferred updates that are ready into pending
    auto it = deferredUpdates_.begin();
    while (it != deferredUpdates_.end()) {
        if (--it->tickDelay <= 0) {
            pendingUpdates_.push_back(*it);
            it = deferredUpdates_.erase(it);
        } else {
            ++it;
        }
    }

    // Process pending updates up to the budget
    processedThisTick_.clear();
    int32_t processed = 0;

    while (!pendingUpdates_.empty() && processed < config_.maxUpdatesPerTick) {
        FluidUpdate update = pendingUpdates_.front();
        pendingUpdates_.pop_front();

        // Skip if already processed this tick — re-queue for next tick
        if (processedThisTick_.count(update.pos)) {
            update.tickDelay = 1;
            deferredUpdates_.push_back(update);
            continue;
        }
        processedThisTick_.insert(update.pos);

        // Two kinds of updates:
        // 1. Flow update (type is valid, level > 0): try to place fluid, then propagate
        // 2. Re-evaluation (type is empty): re-evaluate existing fluid at this position
        if (!update.type.isEmpty() && update.level > 0) {
            applyFlowUpdate(update);
        } else {
            processFluidAt(update.pos);
        }
        ++processed;
    }
}

void FluidSimulator::scheduleUpdate(BlockCoord pos, FluidTypeId type, uint8_t level, int32_t tickDelay) {
    FluidUpdate update{pos, type, level, tickDelay};
    if (tickDelay <= 0) {
        pendingUpdates_.push_back(update);
    } else {
        deferredUpdates_.push_back(update);
    }
}

void FluidSimulator::notifyFluidChanged(BlockCoord pos) {
    // Schedule this position and all neighbors for processing
    scheduleUpdate(pos, EMPTY_FLUID_TYPE, 0, 0);

    for (int i = 0; i < 6; ++i) {
        Face face = static_cast<Face>(i);
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };
        scheduleUpdate(neighbor, EMPTY_FLUID_TYPE, 0, 0);
    }
}

void FluidSimulator::notifyBlockChanged(BlockCoord pos) {
    // A block changed — check all neighbors plus the position itself
    scheduleUpdate(pos, EMPTY_FLUID_TYPE, 0, 0);

    for (int i = 0; i < 6; ++i) {
        Face face = static_cast<Face>(i);
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };
        scheduleUpdate(neighbor, EMPTY_FLUID_TYPE, 0, 0);
    }
}

bool FluidSimulator::hasPendingUpdate(BlockCoord pos) const {
    for (const auto& u : pendingUpdates_) {
        if (u.pos == pos) return true;
    }
    for (const auto& u : deferredUpdates_) {
        if (u.pos == pos) return true;
    }
    return false;
}

void FluidSimulator::clearPendingUpdates() {
    pendingUpdates_.clear();
    deferredUpdates_.clear();
}

// ============================================================================
// Flow Logic
// ============================================================================

void FluidSimulator::applyFlowUpdate(const FluidUpdate& update) {
    const FluidType* ft = FluidRegistry::global().getType(update.type);
    if (!ft) return;

    // A flow update: try to place fluid at this position
    if (!canFluidEnter(update.pos, update.type, *ft)) {
        return;
    }

    FluidTypeId existingType = world_.getFluid(update.pos);
    uint8_t existingLevel = world_.getFluidLevel(update.pos);

    // If different fluid exists, check interaction
    if (!existingType.isEmpty() && existingType != update.type) {
        handleFluidInteraction(update.pos, update.type, existingType);
        return;
    }

    // Same fluid already at same or higher level — skip
    if (existingType == update.type && existingLevel >= update.level) return;

    // Validate: would this new fluid have valid supply?
    // This prevents stale deferred updates from resurrecting drained fluid.
    if (!hasValidSupply(update.pos, update.type, update.level)) {
        return;
    }

    setFluidAndNotify(update.pos, update.type, update.level, *ft);

    // Now propagate from this new position
    processFluidAt(update.pos);
}

void FluidSimulator::processFluidAt(BlockCoord pos) {
    FluidTypeId type = world_.getFluid(pos);
    uint8_t level = world_.getFluidLevel(pos);

    // If no fluid here, nothing to do
    if (type.isEmpty() || level == 0) return;

    const FluidType* fluidType = FluidRegistry::global().getType(type);
    if (!fluidType) return;

    bool isSource = (level == FLUID_SOURCE_LEVEL);

    // Static source optimization: source blocks surrounded by same-type sources
    // or solid blocks cannot flow anywhere — skip them entirely.
    if (isSource && isStaticSource(pos, type)) {
        return;
    }

    // For flowing cells: check source formation first, then supply
    if (!isSource && fluidType->sourceFormation) {
        if (checkSourceFormation(pos, type, *fluidType)) {
            // Became a source — re-read level and continue as source
            level = world_.getFluidLevel(pos);
            isSource = true;
        }
    }

    // For flowing cells without supply: only drain, don't spread
    if (!isSource && !hasValidSupply(pos, type, level)) {
        drainCell(pos, type, level, *fluidType);
        return;
    }

    // 1. Try gravity flow (down)
    bool flowedDown = tryFlowDown(pos, type, level, *fluidType);

    // 2. Horizontal spread (if source, or flowing that didn't fully drain down)
    if (isSource || (!flowedDown && level > fluidType->spreadDecay)) {
        spreadHorizontally(pos, type, level, *fluidType);
    }

    // 3. Equalization (balance with neighbors)
    if (!isSource) {
        equalizeNeighbors(pos, type, level, *fluidType);
    }
}

bool FluidSimulator::tryFlowDown(BlockCoord pos, FluidTypeId type, uint8_t /*level*/, const FluidType& fluidType) {
    BlockCoord below{pos.x, pos.y - 1, pos.z};

    if (!canFluidEnter(below, type, fluidType)) return false;

    FluidTypeId existingType = world_.getFluid(below);
    uint8_t existingLevel = world_.getFluidLevel(below);

    // Different fluid below — check interaction
    if (!existingType.isEmpty() && existingType != type) {
        return handleFluidInteraction(below, type, existingType);
    }

    // Same fluid below at source level — nothing to do
    if (existingType == type && existingLevel == FLUID_SOURCE_LEVEL) return false;

    // Flow down at max level (not source level — only user-placed sources should be level 15)
    setFluidAndNotify(below, type, fluidType.maxLevel, fluidType);
    return true;
}

void FluidSimulator::spreadHorizontally(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType) {
    int32_t newLevel;
    if (level == FLUID_SOURCE_LEVEL) {
        newLevel = fluidType.maxLevel;
    } else {
        newLevel = level - fluidType.spreadDecay;
    }

    if (newLevel <= 0) return;

    // Slope detection: prefer directions that lead to drops
    uint8_t slopeMask = findSlopeDirections(pos, type, config_.slopeLookahead);

    static const Face horizontalFaces[] = {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ};

    for (Face face : horizontalFaces) {
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };

        if (!canFluidEnter(neighbor, type, fluidType)) continue;

        FluidTypeId existingType = world_.getFluid(neighbor);
        uint8_t existingLevel = world_.getFluidLevel(neighbor);

        // Different fluid — check interaction
        if (!existingType.isEmpty() && existingType != type) {
            handleFluidInteraction(neighbor, type, existingType);
            continue;
        }

        // Already has same or higher level — skip
        if (existingType == type && existingLevel >= newLevel) continue;

        // If slope detection found preferred directions, only flow those ways
        uint8_t faceBit = (1 << static_cast<uint8_t>(face));
        if (slopeMask != 0 && !(slopeMask & faceBit)) continue;

        // Schedule a FLOW update with flow speed delay
        // This carries the fluid type and level to actually place
        int32_t delay = fluidType.flowSpeed;
        scheduleUpdate(neighbor, type, static_cast<uint8_t>(newLevel), delay);
    }
}

bool FluidSimulator::checkSourceFormation(BlockCoord pos, FluidTypeId type, const FluidType& fluidType) {
    if (!fluidType.sourceFormation) return false;

    int32_t sourceCount = countAdjacentSources(pos, type);
    if (sourceCount >= fluidType.sourceFormationCount) {
        setFluidAndNotify(pos, type, FLUID_SOURCE_LEVEL, fluidType);
        return true;
    }
    return false;
}

void FluidSimulator::equalizeNeighbors(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType) {
    static const Face horizontalFaces[] = {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ};

    for (Face face : horizontalFaces) {
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };

        FluidTypeId neighborType = world_.getFluid(neighbor);
        if (neighborType != type) continue;

        uint8_t neighborLevel = world_.getFluidLevel(neighbor);
        if (neighborLevel == FLUID_SOURCE_LEVEL) continue;

        int32_t diff = static_cast<int32_t>(level) - static_cast<int32_t>(neighborLevel);
        if (diff >= 2) {
            uint8_t newLevel = level - 1;
            uint8_t newNeighborLevel = neighborLevel + 1;

            world_.setFluid(pos, type, newLevel);
            world_.setFluid(neighbor, type, newNeighborLevel);
            dirtySubChunks_.insert(ChunkPos::fromBlock(pos));
            dirtySubChunks_.insert(ChunkPos::fromBlock(neighbor));

            scheduleUpdate(pos, EMPTY_FLUID_TYPE, 0, fluidType.flowSpeed);
            scheduleUpdate(neighbor, EMPTY_FLUID_TYPE, 0, fluidType.flowSpeed);
        }
    }
}

void FluidSimulator::drainCell(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType) {
    // Caller already verified no valid supply — just drain by 1 level
    uint8_t newLevel = level - 1;
    if (newLevel <= 0) {
        removeFluidAndNotify(pos);
    } else {
        setFluidAndNotify(pos, type, newLevel, fluidType);
        scheduleUpdate(pos, EMPTY_FLUID_TYPE, 0, fluidType.flowSpeed);
    }
}

// ============================================================================
// Slope Detection
// ============================================================================

uint8_t FluidSimulator::findSlopeDirections(BlockCoord pos, FluidTypeId type, int32_t maxDepth) {
    if (maxDepth <= 0) return 0;

    const FluidType* ft = FluidRegistry::global().getType(type);
    if (!ft) return 0;

    static const Face horizontalFaces[] = {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ};

    uint8_t resultMask = 0;

    for (Face startFace : horizontalFaces) {
        BlockCoord start = BlockCoord{
            pos.x + faceOffset(startFace).x,
            pos.y + faceOffset(startFace).y,
            pos.z + faceOffset(startFace).z
        };

        // Quick check: is the position below this neighbor open?
        BlockCoord belowStart{start.x, start.y - 1, start.z};
        if (canFluidEnter(belowStart, type, *ft)) {
            resultMask |= (1 << static_cast<uint8_t>(startFace));
            continue;
        }

        // BFS to find a drop within maxDepth steps
        struct BFSEntry {
            BlockCoord pos;
            int32_t depth;
        };
        std::queue<BFSEntry> bfsQueue;
        std::unordered_set<BlockCoord> visited;

        if (!isBlockFull(start)) {
            bfsQueue.push({start, 1});
            visited.insert(start);
        }

        bool foundDrop = false;
        while (!bfsQueue.empty() && !foundDrop) {
            auto [bfsPos, depth] = bfsQueue.front();
            bfsQueue.pop();

            BlockCoord below{bfsPos.x, bfsPos.y - 1, bfsPos.z};
            if (canFluidEnter(below, type, *ft)) {
                foundDrop = true;
                break;
            }

            if (depth < maxDepth) {
                for (Face face : horizontalFaces) {
                    BlockCoord next = BlockCoord{
                        bfsPos.x + faceOffset(face).x,
                        bfsPos.y + faceOffset(face).y,
                        bfsPos.z + faceOffset(face).z
                    };
                    if (visited.count(next) == 0 && !isBlockFull(next)) {
                        visited.insert(next);
                        bfsQueue.push({next, depth + 1});
                    }
                }
            }
        }

        if (foundDrop) {
            resultMask |= (1 << static_cast<uint8_t>(startFace));
        }
    }

    return resultMask;
}

// ============================================================================
// Helpers
// ============================================================================

bool FluidSimulator::canFluidEnter(BlockCoord pos, FluidTypeId /*type*/, const FluidType& fluidType) const {
    BlockTypeId blockType = world_.getBlock(pos);

    // Air is always enterable
    if (blockType.isAir()) return true;

    const auto& bt = BlockRegistry::global().getType(blockType);
    const auto& shape = bt.collisionShape();

    // Check for full solid block
    if (shape.boxes().size() == 1) {
        const auto& box = shape.boxes()[0];
        if (box.min.x <= 0.001f && box.min.y <= 0.001f && box.min.z <= 0.001f &&
            box.max.x >= 0.999f && box.max.y >= 0.999f && box.max.z >= 0.999f) {
            return false;
        }
    }

    // Non-full block with collision — check infiltration
    if (!shape.isEmpty()) {
        if (!fluidType.infiltratesNonFull) return false;
    }

    return true;
}

bool FluidSimulator::isBlockFull(BlockCoord pos) const {
    BlockTypeId blockType = world_.getBlock(pos);
    if (blockType.isAir()) return false;

    const auto& bt = BlockRegistry::global().getType(blockType);
    const auto& shape = bt.collisionShape();
    if (shape.boxes().size() == 1) {
        const auto& box = shape.boxes()[0];
        if (box.min.x <= 0.001f && box.min.y <= 0.001f && box.min.z <= 0.001f &&
            box.max.x >= 0.999f && box.max.y >= 0.999f && box.max.z >= 0.999f) {
            return true;
        }
    }
    return false;
}

bool FluidSimulator::handleFluidInteraction(BlockCoord pos, FluidTypeId flowing, FluidTypeId existing) {
    const auto* interaction = FluidInteractionRegistry::global().getInteraction(flowing, existing);
    if (!interaction) return false;

    dirtySubChunks_.insert(ChunkPos::fromBlock(pos));

    if (interaction->resultBlock.isValid() && !interaction->resultBlock.isAir()) {
        world_.setBlock(pos, interaction->resultBlock);
        world_.removeFluid(pos);
    }

    if (interaction->consumeA && interaction->consumeB) {
        world_.removeFluid(pos);
    } else if (interaction->resultFluid.isValid()) {
        world_.setFluid(pos, interaction->resultFluid, FLUID_SOURCE_LEVEL);
    }

    return true;
}

void FluidSimulator::setFluidAndNotify(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& /*fluidType*/) {
    // Capture old fluid type before the change (for lighting)
    FluidTypeId oldFluid = lightEngine_ ? world_.getFluid(pos) : EMPTY_FLUID_TYPE;

    bool changed = world_.setFluid(pos, type, level);
    if (changed) {
        dirtySubChunks_.insert(ChunkPos::fromBlock(pos));

        // Enqueue lighting update if fluid type changed at this position
        if (lightEngine_ && oldFluid != type) {
            LightingUpdate lu;
            lu.pos = pos;
            lu.oldType = world_.getBlock(pos);
            lu.newType = lu.oldType;  // Block didn't change
            lu.oldFluid = oldFluid;
            lu.newFluid = type;
            lu.triggerMeshRebuild = false;
            lightEngine_->enqueue(lu);
        }

        // Schedule neighbors for re-evaluation next tick
        for (int i = 0; i < 6; ++i) {
            Face face = static_cast<Face>(i);
            BlockCoord neighbor = BlockCoord{
                pos.x + faceOffset(face).x,
                pos.y + faceOffset(face).y,
                pos.z + faceOffset(face).z
            };
            if (!processedThisTick_.count(neighbor)) {
                scheduleUpdate(neighbor, EMPTY_FLUID_TYPE, 0, 0);
            }
        }
    }
}

void FluidSimulator::removeFluidAndNotify(BlockCoord pos) {
    // Capture old fluid type before the change (for lighting)
    FluidTypeId oldFluid = lightEngine_ ? world_.getFluid(pos) : EMPTY_FLUID_TYPE;

    bool had = world_.removeFluid(pos);
    if (had) {
        dirtySubChunks_.insert(ChunkPos::fromBlock(pos));

        // Enqueue lighting update if old fluid affected light
        if (lightEngine_ && !oldFluid.isEmpty()) {
            LightingUpdate lu;
            lu.pos = pos;
            lu.oldType = world_.getBlock(pos);
            lu.newType = lu.oldType;  // Block didn't change
            lu.oldFluid = oldFluid;
            lu.newFluid = EMPTY_FLUID_TYPE;
            lu.triggerMeshRebuild = false;
            lightEngine_->enqueue(lu);
        }

        for (int i = 0; i < 6; ++i) {
            Face face = static_cast<Face>(i);
            BlockCoord neighbor = BlockCoord{
                pos.x + faceOffset(face).x,
                pos.y + faceOffset(face).y,
                pos.z + faceOffset(face).z
            };
            if (!processedThisTick_.count(neighbor)) {
                scheduleUpdate(neighbor, EMPTY_FLUID_TYPE, 0, 0);
            }
        }
    }
}

bool FluidSimulator::hasValidSupply(BlockCoord pos, FluidTypeId type, uint8_t level) const {
    // Check above: if same fluid exists above, we have supply
    BlockCoord above{pos.x, pos.y + 1, pos.z};
    if (world_.getFluid(above) == type && world_.getFluidLevel(above) > 0) {
        return true;
    }

    // Check horizontal neighbors for higher-level same fluid
    static const Face horizontalFaces[] = {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ};
    for (Face face : horizontalFaces) {
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };
        FluidTypeId neighborType = world_.getFluid(neighbor);
        if (neighborType == type) {
            uint8_t neighborLevel = world_.getFluidLevel(neighbor);
            if (neighborLevel > level || neighborLevel == FLUID_SOURCE_LEVEL) {
                return true;
            }
        }
    }

    return false;
}

int32_t FluidSimulator::countAdjacentSources(BlockCoord pos, FluidTypeId type) const {
    int32_t count = 0;
    static const Face horizontalFaces[] = {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ};

    for (Face face : horizontalFaces) {
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };
        if (world_.getFluid(neighbor) == type && world_.getFluidLevel(neighbor) == FLUID_SOURCE_LEVEL) {
            ++count;
        }
    }
    return count;
}

bool FluidSimulator::isStaticSource(BlockCoord pos, FluidTypeId type) const {
    for (int i = 0; i < 6; ++i) {
        Face face = static_cast<Face>(i);
        BlockCoord neighbor = BlockCoord{
            pos.x + faceOffset(face).x,
            pos.y + faceOffset(face).y,
            pos.z + faceOffset(face).z
        };

        // Neighbor must be either: same-type source, or a full solid block
        if (isBlockFull(neighbor)) continue;

        FluidTypeId neighborType = world_.getFluid(neighbor);
        if (neighborType == type && world_.getFluidLevel(neighbor) == FLUID_SOURCE_LEVEL) continue;

        // This neighbor is not blocking — source is not static
        return false;
    }
    return true;
}

BlockCoord FluidSimulator::faceOffset(Face face) {
    auto normal = faceNormal(face);
    return BlockCoord{normal[0], normal[1], normal[2]};
}

}  // namespace finevox
