#include "finevox/lod.hpp"
#include <unordered_map>
#include <algorithm>

namespace finevox {

// ============================================================================
// LODSubChunk Implementation
// ============================================================================

LODSubChunk::LODSubChunk(LODLevel level)
    : level_(level)
{
    // LOD0 should use regular SubChunk, not LODSubChunk
    if (level == LODLevel::LOD0) {
        level_ = LODLevel::LOD1;
    }

    // Allocate storage based on resolution
    int v = volume();
    blocks_.resize(v, AIR_BLOCK_TYPE);
}

BlockTypeId LODSubChunk::getBlock(int x, int y, int z) const {
    int r = resolution();
    if (x < 0 || x >= r || y < 0 || y >= r || z < 0 || z >= r) {
        return AIR_BLOCK_TYPE;
    }
    return blocks_[toIndex(x, y, z)];
}

void LODSubChunk::setBlock(int x, int y, int z, BlockTypeId type) {
    int r = resolution();
    if (x < 0 || x >= r || y < 0 || y >= r || z < 0 || z >= r) {
        return;
    }

    int idx = toIndex(x, y, z);
    BlockTypeId oldType = blocks_[idx];

    if (oldType != type) {
        // Update non-air count
        if (oldType == AIR_BLOCK_TYPE && type != AIR_BLOCK_TYPE) {
            ++nonAirCount_;
        } else if (oldType != AIR_BLOCK_TYPE && type == AIR_BLOCK_TYPE) {
            --nonAirCount_;
        }

        blocks_[idx] = type;
        ++version_;
    }
}

void LODSubChunk::clear() {
    std::fill(blocks_.begin(), blocks_.end(), AIR_BLOCK_TYPE);
    nonAirCount_ = 0;
    ++version_;
}

void LODSubChunk::downsampleFrom(const SubChunk& source) {
    clear();

    int r = resolution();

    for (int ly = 0; ly < r; ++ly) {
        for (int lz = 0; lz < r; ++lz) {
            for (int lx = 0; lx < r; ++lx) {
                BlockTypeId representative = selectRepresentativeBlock(source, lx, ly, lz);
                if (representative != AIR_BLOCK_TYPE) {
                    setBlock(lx, ly, lz, representative);
                }
            }
        }
    }
}

BlockTypeId LODSubChunk::selectRepresentativeBlock(
    const SubChunk& source,
    int groupX, int groupY, int groupZ) const
{
    int g = grouping();
    int startX = groupX * g;
    int startY = groupY * g;
    int startZ = groupZ * g;

    // Count occurrences of each block type in the group
    // Also track the topmost non-air block for surface preservation
    std::unordered_map<uint32_t, int> counts;
    BlockTypeId topBlock = AIR_BLOCK_TYPE;
    int topY = -1;

    for (int dy = 0; dy < g && startY + dy < 16; ++dy) {
        for (int dz = 0; dz < g && startZ + dz < 16; ++dz) {
            for (int dx = 0; dx < g && startX + dx < 16; ++dx) {
                BlockTypeId type = source.getBlock(startX + dx, startY + dy, startZ + dz);
                if (type != AIR_BLOCK_TYPE) {
                    ++counts[type.id];
                    // Track topmost block (for surface preservation)
                    if (dy > topY) {
                        topY = dy;
                        topBlock = type;
                    }
                }
            }
        }
    }

    // If no solid blocks, return air
    if (counts.empty()) {
        return AIR_BLOCK_TYPE;
    }

    // For surface-like scenarios (top layer has blocks), prefer the top block
    // This preserves grass on top of dirt, etc.
    // Check if there's a block in the top half of the group
    if (topY >= g / 2) {
        return topBlock;
    }

    // Otherwise, find the most common block type (mode)
    BlockTypeId mostCommon = AIR_BLOCK_TYPE;
    int maxCount = 0;

    for (const auto& [typeId, count] : counts) {
        if (count > maxCount) {
            maxCount = count;
            mostCommon = BlockTypeId(typeId);
        }
    }

    return mostCommon;
}

}  // namespace finevox
