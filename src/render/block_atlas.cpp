#include "finevox/render/block_atlas.hpp"

#include <finevk/device/logical_device.hpp>

// finevk's texture.cpp provides the implementation
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>

namespace finevox::render {

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
    // Each cell has 2-pixel border on all sides for texture filtering safety
    // Content is 12x12, border adds 4 pixels (2 on each side) = 16x16 total
    // 2-pixel border ensures bilinear filtering (which samples 2x2) stays within cell
    const uint32_t borderSize = 2;
    const uint32_t contentSize = 12;  // Inner content area
    const uint32_t cellSize = contentSize + borderSize * 2;  // 16 total

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

            // Fill the entire cell (including border) with this color
            // The border pixels duplicate the edge pixels to prevent bleed
            for (uint32_t py = 0; py < cellSize; ++py) {
                for (uint32_t px = 0; px < cellSize; ++px) {
                    uint32_t x = gx * cellSize + px;
                    uint32_t y = gy * cellSize + py;
                    uint32_t idx = (y * atlasWidth_ + x) * 4;

                    // Check if this pixel is on the inner edge of the content area
                    // Content area is from borderSize to (cellSize - borderSize - 1)
                    // Inner edge is the 1-pixel border just inside the content area
                    bool isInnerEdge = false;
                    if (px >= borderSize && px < cellSize - borderSize &&
                        py >= borderSize && py < cellSize - borderSize) {
                        // We're in the content area - check if on inner edge
                        uint32_t contentX = px - borderSize;
                        uint32_t contentY = py - borderSize;
                        if (contentX == 0 || contentX == contentSize - 1 ||
                            contentY == 0 || contentY == contentSize - 1) {
                            isInnerEdge = true;
                        }
                    }

                    if (isInnerEdge) {
                        // Dark border on inner edge
                        pixels[idx + 0] = r / 3;
                        pixels[idx + 1] = g / 3;
                        pixels[idx + 2] = b / 3;
                    } else {
                        // Normal color
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

namespace {

constexpr int ATLAS_TILE = 32;
constexpr int ATLAS_BORDER = 2;

void magenta(std::vector<uint8_t>& pixels) {
    pixels.resize(ATLAS_TILE * ATLAS_TILE * 4);
    for (int i = 0; i < ATLAS_TILE * ATLAS_TILE; ++i) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 0;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = 255;
    }
}

bool loadTile(const std::string& path, std::vector<uint8_t>& pixels) {
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) return false;

    // Nearest-neighbour resample, so a stray size can't corrupt the grid
    pixels.resize(ATLAS_TILE * ATLAS_TILE * 4);
    for (int y = 0; y < ATLAS_TILE; ++y) {
        for (int x = 0; x < ATLAS_TILE; ++x) {
            int sx = x * width / ATLAS_TILE;
            int sy = y * height / ATLAS_TILE;
            std::memcpy(&pixels[(y * ATLAS_TILE + x) * 4], &data[(sy * width + sx) * 4], 4);
        }
    }
    stbi_image_free(data);
    return true;
}

}  // namespace

void BlockAtlas::buildFromTextures(finevk::LogicalDevice* device, finevk::CommandPool* commandPool,
                                   const std::string& textureRoot,
                                   const std::unordered_map<uint32_t, BlockTextureSet>& blocks) {
    // std::map: stable tile order regardless of registry iteration
    std::map<std::string, uint32_t> tiles;
    for (const auto& [id, set] : blocks) {
        tiles.try_emplace(set.top, 0);
        tiles.try_emplace(set.bottom, 0);
        tiles.try_emplace(set.side, 0);
    }
    uint32_t index = 0;
    for (auto& [name, tile] : tiles) tile = index++;

    uint32_t grid = std::max<uint32_t>(
        1, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(tiles.size())))));
    const int cell = ATLAS_TILE + ATLAS_BORDER * 2;

    atlasWidth_ = grid * cell;
    atlasHeight_ = grid * cell;
    gridWidth_ = grid;
    gridHeight_ = grid;
    borderPixels_ = ATLAS_BORDER;
    cellWidth_ = 1.0f / static_cast<float>(grid);
    cellHeight_ = 1.0f / static_cast<float>(grid);

    std::vector<uint8_t> atlas(static_cast<size_t>(atlasWidth_) * atlasHeight_ * 4, 0);
    std::vector<uint8_t> pixels;
    for (const auto& [name, tile] : tiles) {
        std::string path = (std::filesystem::path(textureRoot) / (name + ".png")).string();
        if (!loadTile(path, pixels)) {
            std::cerr << "[BlockAtlas] missing texture: " << path << "\n";
            magenta(pixels);
        }

        int originX = static_cast<int>(tile % grid) * cell;
        int originY = static_cast<int>(tile / grid) * cell;
        // Wrap into the border so filtering at a tile edge sees the tile's far
        // edge, which is what a repeating surface should show.
        for (int y = 0; y < cell; ++y) {
            for (int x = 0; x < cell; ++x) {
                int sx = (x - ATLAS_BORDER + ATLAS_TILE) % ATLAS_TILE;
                int sy = (y - ATLAS_BORDER + ATLAS_TILE) % ATLAS_TILE;
                size_t dest = (static_cast<size_t>(originY + y) * atlasWidth_ + originX + x) * 4;
                std::memcpy(&atlas[dest], &pixels[(sy * ATLAS_TILE + sx) * 4], 4);
            }
        }
    }

    texture_ = finevk::Texture::fromMemory(device, atlas.data(), atlasWidth_, atlasHeight_,
                                           commandPool, true, true);

    for (const auto& [id, set] : blocks) {
        auto face = [&](const std::string& name) {
            uint32_t tile = tiles.at(name);
            return gridToUV(tile % grid, tile / grid);
        };
        BlockTextureInfo info;
        info.setTopBottom(face(set.top), face(set.bottom), face(set.side));
        blockTextures_[id] = info;
    }

    // Unmapped blocks stay obvious rather than picking up tile 0
    defaultTexture_.setAll(gridToUV(0, 0));
}

BlockFaceTexture BlockAtlas::gridToUV(uint32_t gridX, uint32_t gridY) const {
    BlockFaceTexture tex;

    // Calculate UV coordinates that point to the inner content area,
    // excluding the 2-pixel border used for filtering safety.
    // Cell layout: [2px border][12px content][2px border] = 16px total
    // UV should span the content area only.
    const float borderPixels = borderPixels_;

    // Border size in UV coordinates
    float borderU = borderPixels / static_cast<float>(atlasWidth_);
    float borderV = borderPixels / static_cast<float>(atlasHeight_);

    // Cell bounds in UV (including border)
    float cellMinU = gridX * cellWidth_;
    float cellMinV = gridY * cellHeight_;

    // Content bounds (inset by border)
    tex.uvMin.x = cellMinU + borderU;
    tex.uvMin.y = cellMinV + borderV;
    tex.uvMax.x = cellMinU + cellWidth_ - borderU;
    tex.uvMax.y = cellMinV + cellHeight_ - borderV;

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

}  // namespace finevox::render
