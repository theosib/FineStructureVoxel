#include "finevox/block_atlas.hpp"

#include <finevk/device/logical_device.hpp>

#include <cmath>
#include <random>

namespace finevox {

void BlockAtlas::loadGridAtlas(
    finevk::LogicalDevice* device,
    finevk::CommandPool* commandPool,
    const std::string& path,
    uint32_t gridWidth,
    uint32_t gridHeight,
    bool srgb
) {
    // Load the texture
    auto builder = finevk::Texture::load(device, commandPool, path);
    if (srgb) {
        builder.srgb();
    }
    builder.generateMipmaps();
    texture_ = builder.build();

    if (!texture_) {
        return;
    }

    atlasWidth_ = texture_->width();
    atlasHeight_ = texture_->height();
    gridWidth_ = gridWidth;
    gridHeight_ = gridHeight;

    // Calculate cell size in UV coordinates
    cellWidth_ = 1.0f / static_cast<float>(gridWidth);
    cellHeight_ = 1.0f / static_cast<float>(gridHeight);
}

void BlockAtlas::createPlaceholderAtlas(
    finevk::LogicalDevice* device,
    finevk::CommandPool* commandPool,
    uint32_t gridWidth,
    uint32_t gridHeight
) {
    const uint32_t cellSize = 16;  // 16x16 pixels per cell
    atlasWidth_ = gridWidth * cellSize;
    atlasHeight_ = gridHeight * cellSize;
    gridWidth_ = gridWidth;
    gridHeight_ = gridHeight;

    // Generate placeholder texture with different colors per cell
    std::vector<uint8_t> pixels(atlasWidth_ * atlasHeight_ * 4);

    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int> colorDist(64, 255);

    for (uint32_t gy = 0; gy < gridHeight; ++gy) {
        for (uint32_t gx = 0; gx < gridWidth; ++gx) {
            // Generate a random but consistent color for each cell
            uint8_t r = static_cast<uint8_t>(colorDist(rng));
            uint8_t g = static_cast<uint8_t>(colorDist(rng));
            uint8_t b = static_cast<uint8_t>(colorDist(rng));

            // Fill the cell with this color
            for (uint32_t py = 0; py < cellSize; ++py) {
                for (uint32_t px = 0; px < cellSize; ++px) {
                    uint32_t x = gx * cellSize + px;
                    uint32_t y = gy * cellSize + py;
                    uint32_t idx = (y * atlasWidth_ + x) * 4;

                    // Add a simple border effect
                    bool border = (px == 0 || px == cellSize - 1 ||
                                   py == 0 || py == cellSize - 1);

                    if (border) {
                        pixels[idx + 0] = r / 2;
                        pixels[idx + 1] = g / 2;
                        pixels[idx + 2] = b / 2;
                    } else {
                        pixels[idx + 0] = r;
                        pixels[idx + 1] = g;
                        pixels[idx + 2] = b;
                    }
                    pixels[idx + 3] = 255;
                }
            }
        }
    }

    // Create texture from memory
    texture_ = finevk::Texture::fromMemory(
        device, pixels.data(), atlasWidth_, atlasHeight_,
        commandPool, false, false);

    cellWidth_ = 1.0f / static_cast<float>(gridWidth);
    cellHeight_ = 1.0f / static_cast<float>(gridHeight);
}

BlockFaceTexture BlockAtlas::gridToUV(uint32_t gridX, uint32_t gridY) const {
    BlockFaceTexture tex;
    tex.uvMin.x = gridX * cellWidth_;
    tex.uvMin.y = gridY * cellHeight_;
    tex.uvMax.x = (gridX + 1) * cellWidth_;
    tex.uvMax.y = (gridY + 1) * cellHeight_;
    return tex;
}

void BlockAtlas::setBlockTexture(BlockTypeId id, uint32_t gridX, uint32_t gridY) {
    BlockTextureInfo info;
    BlockFaceTexture tex = gridToUV(gridX, gridY);
    info.setAll(tex);
    blockTextures_[id.id] = info;
}

void BlockAtlas::setBlockTexture(BlockTypeId id,
                                  uint32_t topX, uint32_t topY,
                                  uint32_t bottomX, uint32_t bottomY,
                                  uint32_t sideX, uint32_t sideY) {
    BlockTextureInfo info;
    info.setTopBottom(gridToUV(topX, topY),
                      gridToUV(bottomX, bottomY),
                      gridToUV(sideX, sideY));
    blockTextures_[id.id] = info;
}

void BlockAtlas::setBlockTexturePerFace(BlockTypeId id,
                                         uint32_t posXx, uint32_t posXy,
                                         uint32_t negXx, uint32_t negXy,
                                         uint32_t posYx, uint32_t posYy,
                                         uint32_t negYx, uint32_t negYy,
                                         uint32_t posZx, uint32_t posZy,
                                         uint32_t negZx, uint32_t negZy) {
    BlockTextureInfo info;
    info.faces[static_cast<size_t>(Face::PosX)] = gridToUV(posXx, posXy);
    info.faces[static_cast<size_t>(Face::NegX)] = gridToUV(negXx, negXy);
    info.faces[static_cast<size_t>(Face::PosY)] = gridToUV(posYx, posYy);
    info.faces[static_cast<size_t>(Face::NegY)] = gridToUV(negYx, negYy);
    info.faces[static_cast<size_t>(Face::PosZ)] = gridToUV(posZx, posZy);
    info.faces[static_cast<size_t>(Face::NegZ)] = gridToUV(negZx, negZy);
    blockTextures_[id.id] = info;
}

void BlockAtlas::setBlockTextureUV(BlockTypeId id, const BlockTextureInfo& info) {
    blockTextures_[id.id] = info;
}

glm::vec4 BlockAtlas::getUV(BlockTypeId id, Face face) const {
    auto it = blockTextures_.find(id.id);
    if (it != blockTextures_.end()) {
        return it->second.get(face).bounds();
    }
    return defaultTexture_.get(face).bounds();
}

const BlockTextureInfo* BlockAtlas::getTextureInfo(BlockTypeId id) const {
    auto it = blockTextures_.find(id.id);
    if (it != blockTextures_.end()) {
        return &it->second;
    }
    return &defaultTexture_;
}

BlockTextureProvider BlockAtlas::createProvider() const {
    return [this](BlockTypeId id, Face face) {
        return this->getUV(id, face);
    };
}

}  // namespace finevox
