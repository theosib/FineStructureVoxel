#include "finevox/subchunk.hpp"
#include "finevox/data_container.hpp"
#include "finevox/block_type.hpp"

namespace finevox {

SubChunk::SubChunk() {
    // Initialize all blocks to air (index 0)
    blocks_.fill(0);
    // Air starts with count equal to volume
    usageCounts_.push_back(VOLUME);
}

SubChunk::~SubChunk() = default;

BlockTypeId SubChunk::getBlock(LocalBlockPos pos) const {
    return getBlock(pos.toIndex());
}

BlockTypeId SubChunk::getBlock(uint16_t index) const {
    LocalIndex localIdx = blocks_[index];
    return palette_.getGlobalId(localIdx);
}

void SubChunk::setBlock(LocalBlockPos pos, BlockTypeId type) {
    uint16_t index = pos.toIndex();
    LocalIndex oldIndex = blocks_[index];
    BlockTypeId oldType = palette_.getGlobalId(oldIndex);

    // No change needed
    if (oldType == type) {
        return;
    }

    // Perform the actual block change
    setBlockInternal(index, type, oldType);

    // Increment block version (signals mesh needs rebuild)
    blockVersion_.fetch_add(1, std::memory_order_release);

    // Notify callback if set
    if (blockChangeCallback_) {
        blockChangeCallback_(position_, pos, oldType, type);
    }
}

void SubChunk::setBlock(uint16_t index, BlockTypeId type) {
    LocalIndex oldIndex = blocks_[index];
    BlockTypeId oldType = palette_.getGlobalId(oldIndex);

    // No change needed
    if (oldType == type) {
        return;
    }

    // Perform the actual block change
    setBlockInternal(index, type, oldType);

    // Increment block version (signals mesh needs rebuild)
    blockVersion_.fetch_add(1, std::memory_order_release);

    // Notify callback if set (convert index to LocalBlockPos)
    if (blockChangeCallback_) {
        blockChangeCallback_(position_, LocalBlockPos::fromIndex(index), oldType, type);
    }
}

void SubChunk::setBlockInternal(int32_t index, BlockTypeId type, BlockTypeId oldType) {
    LocalIndex oldIndex = blocks_[index];

    // Get or create local index for new type
    LocalIndex newIndex = palette_.addType(type);

    // Ensure usageCounts_ is large enough
    if (newIndex >= usageCounts_.size()) {
        usageCounts_.resize(newIndex + 1, 0);
    }

    // Update reference counts
    decrementUsage(oldIndex);
    incrementUsage(newIndex);

    // Update the block array
    blocks_[index] = newIndex;

    // Track non-air count
    if (oldType.isAir() && !type.isAir()) {
        ++nonAirCount_;
    } else if (!oldType.isAir() && type.isAir()) {
        --nonAirCount_;
    }
}

void SubChunk::decrementUsage(LocalIndex index) {
    if (index < usageCounts_.size() && usageCounts_[index] > 0) {
        --usageCounts_[index];
        // If usage drops to zero and it's not air, remove from palette
        if (usageCounts_[index] == 0 && index != 0) {
            BlockTypeId type = palette_.getGlobalId(index);
            if (!type.isAir()) {
                palette_.removeType(type);
            }
        }
    }
}

void SubChunk::incrementUsage(LocalIndex index) {
    if (index >= usageCounts_.size()) {
        usageCounts_.resize(index + 1, 0);
    }
    ++usageCounts_[index];
}

void SubChunk::clear() {
    bool wasNotEmpty = nonAirCount_ > 0;

    blocks_.fill(0);
    palette_.clear();
    usageCounts_.clear();
    usageCounts_.push_back(VOLUME);  // Air has all blocks
    nonAirCount_ = 0;

    // Increment block version if there was any content
    if (wasNotEmpty) {
        blockVersion_.fetch_add(1, std::memory_order_release);
    }
}

void SubChunk::fill(BlockTypeId type) {
    if (type.isAir()) {
        clear();
        return;
    }

    // Get or create local index for the type
    LocalIndex index = palette_.addType(type);

    // Fill all blocks with this index
    blocks_.fill(index);

    // Reset usage counts
    usageCounts_.clear();
    usageCounts_.resize(index + 1, 0);
    usageCounts_[index] = VOLUME;

    // Clear palette of unused entries (everything except air and the fill type)
    palette_.clear();
    (void)palette_.addType(type);  // Re-add the type (index will be 1)

    // Update blocks to use new index after palette clear
    blocks_.fill(1);
    usageCounts_.clear();
    usageCounts_.resize(2, 0);
    usageCounts_[0] = 0;  // Air
    usageCounts_[1] = VOLUME;

    nonAirCount_ = VOLUME;

    // Increment block version
    blockVersion_.fetch_add(1, std::memory_order_release);
}

std::vector<SubChunk::LocalIndex> SubChunk::compactPalette() {
    auto mapping = palette_.compact(usageCounts_);

    // Remap all block indices
    for (auto& blockIndex : blocks_) {
        LocalIndex newIndex = mapping[blockIndex];
        if (newIndex != SubChunkPalette::INVALID_LOCAL_INDEX) {
            blockIndex = newIndex;
        } else {
            // This shouldn't happen if usageCounts_ is accurate
            blockIndex = 0;  // Default to air
        }
    }

    // Rebuild usage counts for the compacted palette
    usageCounts_.clear();
    usageCounts_.resize(palette_.entries().size(), 0);
    for (auto blockIndex : blocks_) {
        ++usageCounts_[blockIndex];
    }

    return mapping;
}

// ============================================================================
// Light Data Implementation
// ============================================================================

uint8_t SubChunk::getSkyLight(int32_t x, int32_t y, int32_t z) const {
    return getSkyLight(toIndex(x, y, z));
}

uint8_t SubChunk::getSkyLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) return 0;
    return unpackSkyLight(light_[index]);
}

uint8_t SubChunk::getBlockLight(int32_t x, int32_t y, int32_t z) const {
    return getBlockLight(toIndex(x, y, z));
}

uint8_t SubChunk::getBlockLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) return 0;
    return unpackBlockLight(light_[index]);
}

uint8_t SubChunk::getCombinedLight(int32_t x, int32_t y, int32_t z) const {
    return getCombinedLight(toIndex(x, y, z));
}

uint8_t SubChunk::getCombinedLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) return 0;
    uint8_t sky = unpackSkyLight(light_[index]);
    uint8_t block = unpackBlockLight(light_[index]);
    return sky > block ? sky : block;
}

uint8_t SubChunk::getPackedLight(int32_t x, int32_t y, int32_t z) const {
    return getPackedLight(toIndex(x, y, z));
}

uint8_t SubChunk::getPackedLight(int32_t index) const {
    if (index < 0 || index >= VOLUME) return 0;
    return light_[index];
}

void SubChunk::setSkyLight(int32_t x, int32_t y, int32_t z, uint8_t level) {
    setSkyLight(toIndex(x, y, z), level);
}

void SubChunk::setSkyLight(int32_t index, uint8_t level) {
    if (index < 0 || index >= VOLUME) return;

    uint8_t oldPacked = light_[index];
    uint8_t newPacked = packLight(level & 0x0F, unpackBlockLight(oldPacked));

    if (oldPacked != newPacked) {
        light_[index] = newPacked;
        bumpLightVersion();
    }
}

void SubChunk::setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t level) {
    setBlockLight(toIndex(x, y, z), level);
}

void SubChunk::setBlockLight(int32_t index, uint8_t level) {
    if (index < 0 || index >= VOLUME) return;

    uint8_t oldPacked = light_[index];
    uint8_t newPacked = packLight(unpackSkyLight(oldPacked), level & 0x0F);

    if (oldPacked != newPacked) {
        light_[index] = newPacked;
        bumpLightVersion();
    }
}

void SubChunk::setLight(int32_t x, int32_t y, int32_t z, uint8_t skyLight, uint8_t blockLight) {
    setLight(toIndex(x, y, z), skyLight, blockLight);
}

void SubChunk::setLight(int32_t index, uint8_t skyLight, uint8_t blockLight) {
    if (index < 0 || index >= VOLUME) return;

    uint8_t oldPacked = light_[index];
    uint8_t newPacked = packLight(skyLight & 0x0F, blockLight & 0x0F);

    if (oldPacked != newPacked) {
        light_[index] = newPacked;
        bumpLightVersion();
    }
}

void SubChunk::setPackedLight(int32_t x, int32_t y, int32_t z, uint8_t packed) {
    setPackedLight(toIndex(x, y, z), packed);
}

void SubChunk::setPackedLight(int32_t index, uint8_t packed) {
    if (index < 0 || index >= VOLUME) return;

    if (light_[index] != packed) {
        light_[index] = packed;
        bumpLightVersion();
    }
}

void SubChunk::clearLight() {
    bool wasDark = isLightDark();
    light_.fill(0);
    if (!wasDark) {
        bumpLightVersion();
    }
}

void SubChunk::fillSkyLight(uint8_t level) {
    level &= 0x0F;
    bool changed = false;

    for (int32_t i = 0; i < VOLUME; ++i) {
        uint8_t oldPacked = light_[i];
        uint8_t newPacked = packLight(level, unpackBlockLight(oldPacked));
        if (oldPacked != newPacked) {
            light_[i] = newPacked;
            changed = true;
        }
    }

    if (changed) {
        bumpLightVersion();
    }
}

void SubChunk::fillBlockLight(uint8_t level) {
    level &= 0x0F;
    bool changed = false;

    for (int32_t i = 0; i < VOLUME; ++i) {
        uint8_t oldPacked = light_[i];
        uint8_t newPacked = packLight(unpackSkyLight(oldPacked), level);
        if (oldPacked != newPacked) {
            light_[i] = newPacked;
            changed = true;
        }
    }

    if (changed) {
        bumpLightVersion();
    }
}

bool SubChunk::isLightDark() const {
    for (int32_t i = 0; i < VOLUME; ++i) {
        if (light_[i] != 0) return false;
    }
    return true;
}

bool SubChunk::isFullSkyLight() const {
    for (int32_t i = 0; i < VOLUME; ++i) {
        if (unpackSkyLight(light_[i]) != MAX_LIGHT) return false;
    }
    return true;
}

void SubChunk::setLightData(const std::array<uint8_t, VOLUME>& data) {
    light_ = data;
    bumpLightVersion();
}

// ============================================================================
// Block Extra Data Implementation
// ============================================================================

DataContainer* SubChunk::blockData(int32_t index) {
    auto it = blockData_.find(index);
    return it != blockData_.end() ? it->second.get() : nullptr;
}

const DataContainer* SubChunk::blockData(int32_t index) const {
    auto it = blockData_.find(index);
    return it != blockData_.end() ? it->second.get() : nullptr;
}

DataContainer* SubChunk::blockData(int32_t x, int32_t y, int32_t z) {
    return blockData(toIndex(x, y, z));
}

const DataContainer* SubChunk::blockData(int32_t x, int32_t y, int32_t z) const {
    return blockData(toIndex(x, y, z));
}

DataContainer& SubChunk::getOrCreateBlockData(int32_t index) {
    auto it = blockData_.find(index);
    if (it != blockData_.end()) {
        return *it->second;
    }
    auto result = blockData_.emplace(index, std::make_unique<DataContainer>());
    return *result.first->second;
}

DataContainer& SubChunk::getOrCreateBlockData(int32_t x, int32_t y, int32_t z) {
    return getOrCreateBlockData(toIndex(x, y, z));
}

bool SubChunk::hasBlockData(int32_t index) const {
    return blockData_.find(index) != blockData_.end();
}

bool SubChunk::hasBlockData(int32_t x, int32_t y, int32_t z) const {
    return hasBlockData(toIndex(x, y, z));
}

void SubChunk::removeBlockData(int32_t index) {
    blockData_.erase(index);
}

void SubChunk::removeBlockData(int32_t x, int32_t y, int32_t z) {
    removeBlockData(toIndex(x, y, z));
}

size_t SubChunk::blockDataCount() const {
    return blockData_.size();
}

const std::unordered_map<int32_t, std::unique_ptr<DataContainer>>& SubChunk::allBlockData() const {
    return blockData_;
}

std::unordered_map<int32_t, std::unique_ptr<DataContainer>>& SubChunk::allBlockData() {
    return blockData_;
}

// ============================================================================
// SubChunk Extra Data Implementation
// ============================================================================

DataContainer* SubChunk::data() {
    return data_.get();
}

const DataContainer* SubChunk::data() const {
    return data_.get();
}

DataContainer& SubChunk::getOrCreateData() {
    if (!data_) {
        data_ = std::make_unique<DataContainer>();
    }
    return *data_;
}

bool SubChunk::hasData() const {
    return data_ != nullptr;
}

void SubChunk::removeData() {
    data_.reset();
}

// ============================================================================
// Game Tick Registry Implementation
// ============================================================================

void SubChunk::registerForGameTicks(int32_t index) {
    if (index < 0 || index >= VOLUME) return;
    gameTickBlocks_.insert(static_cast<uint16_t>(index));
}

void SubChunk::unregisterFromGameTicks(int32_t index) {
    if (index < 0 || index >= VOLUME) return;
    gameTickBlocks_.erase(static_cast<uint16_t>(index));
}

bool SubChunk::isRegisteredForGameTicks(int32_t index) const {
    if (index < 0 || index >= VOLUME) return false;
    return gameTickBlocks_.contains(static_cast<uint16_t>(index));
}

void SubChunk::rebuildGameTickRegistry() {
    gameTickBlocks_.clear();

    const BlockRegistry& registry = BlockRegistry::global();

    for (int32_t i = 0; i < VOLUME; ++i) {
        BlockTypeId typeId = getBlock(i);
        if (typeId.isAir()) continue;

        const BlockType& type = registry.getType(typeId);
        if (type.wantsGameTicks()) {
            gameTickBlocks_.insert(static_cast<uint16_t>(i));
        }
    }
}

}  // namespace finevox
