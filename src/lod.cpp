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
    heights_.resize(v, 0);
}

BlockTypeId LODSubChunk::getBlock(int x, int y, int z) const {
    int r = resolution();
    if (x < 0 || x >= r || y < 0 || y >= r || z < 0 || z >= r) {
        return AIR_BLOCK_TYPE;
    }
    return blocks_[toIndex(x, y, z)];
}

LODBlockInfo LODSubChunk::getBlockInfo(int x, int y, int z) const {
    int r = resolution();
    if (x < 0 || x >= r || y < 0 || y >= r || z < 0 || z >= r) {
        return LODBlockInfo{};
    }
    int idx = toIndex(x, y, z);
    return LODBlockInfo{blocks_[idx], heights_[idx]};
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

void LODSubChunk::setBlockInfo(int x, int y, int z, const LODBlockInfo& info) {
    int r = resolution();
    if (x < 0 || x >= r || y < 0 || y >= r || z < 0 || z >= r) {
        return;
    }

    int idx = toIndex(x, y, z);
    BlockTypeId oldType = blocks_[idx];

    if (oldType != info.type) {
        // Update non-air count
        if (oldType == AIR_BLOCK_TYPE && info.type != AIR_BLOCK_TYPE) {
            ++nonAirCount_;
        } else if (oldType != AIR_BLOCK_TYPE && info.type == AIR_BLOCK_TYPE) {
            --nonAirCount_;
        }

        blocks_[idx] = info.type;
        ++version_;
    }
    heights_[idx] = info.height;
}

void LODSubChunk::clear() {
    std::fill(blocks_.begin(), blocks_.end(), AIR_BLOCK_TYPE);
    std::fill(heights_.begin(), heights_.end(), 0);
    nonAirCount_ = 0;
    ++version_;
}

void LODSubChunk::downsampleFrom(const SubChunk& source, LODMergeMode mergeMode) {
    clear();

    int r = resolution();
    int g = grouping();

    for (int ly = 0; ly < r; ++ly) {
        for (int lz = 0; lz < r; ++lz) {
            for (int lx = 0; lx < r; ++lx) {
                LODBlockInfo info = selectRepresentativeBlock(source, lx, ly, lz);
                if (info.type != AIR_BLOCK_TYPE) {
                    // Adjust height based on merge mode
                    if (mergeMode == LODMergeMode::FullHeight) {
                        info.height = static_cast<uint8_t>(g);  // Full height
                    }
                    // HeightLimited keeps the computed height
                    // NoMerge would need different handling (not implemented here)
                    setBlockInfo(lx, ly, lz, info);
                }
            }
        }
    }
}

LODBlockInfo LODSubChunk::selectRepresentativeBlock(
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
        return LODBlockInfo{AIR_BLOCK_TYPE, 0};
    }

    // Height is topY + 1 (since topY is 0-indexed within the group)
    uint8_t height = static_cast<uint8_t>(topY + 1);

    // For surface-like scenarios (top layer has blocks), prefer the top block
    // This preserves grass on top of dirt, etc.
    // Check if there's a block in the top half of the group
    if (topY >= g / 2) {
        return LODBlockInfo{topBlock, height};
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

    return LODBlockInfo{mostCommon, height};
}

}  // namespace finevox
