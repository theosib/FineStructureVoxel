#include "finevox/render/block_atlas.hpp"

#include "finevox/core/block_model_transform.hpp"

#include <finevk/device/logical_device.hpp>

// finevk's texture.cpp provides the implementation
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
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

    // Fixed seed for reproducibility. No std::uniform_int_distribution: it gives different numbers
    // on different standard libraries, and goldens are compared across platforms.
    std::mt19937 rng(42);
    auto colorDist = [](std::mt19937& gen) { return 64 + static_cast<int>(gen() % 192); };

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

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

// A vertical strip of square frames gives its first one
std::optional<Image> loadImage(const std::string& path) {
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) return std::nullopt;
    if (height > width && width > 0 && height % width == 0) height = width;
    Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
    stbi_image_free(data);
    return image;
}

// Nearest-neighbour resample to a square tile, so a stray size can't corrupt the grid, turned
// clockwise: what shows at (x, y) comes from (y, N-1-x), once per turn
void toTile(const Image& image, int turns, int tile, std::vector<uint8_t>& pixels) {
    pixels.resize(static_cast<size_t>(tile) * tile * 4);
    for (int y = 0; y < tile; ++y) {
        for (int x = 0; x < tile; ++x) {
            int tx = x, ty = y;
            for (int t = 0; t < turns; ++t) {
                int nx = ty;
                ty = tile - 1 - tx;
                tx = nx;
            }
            int sx = tx * image.width / tile;
            int sy = ty * image.height / tile;
            std::memcpy(&pixels[(static_cast<size_t>(y) * tile + x) * 4],
                        &image.pixels[(static_cast<size_t>(sy) * image.width + sx) * 4], 4);
        }
    }
}

void magenta(int tile, std::vector<uint8_t>& pixels) {
    pixels.resize(static_cast<size_t>(tile) * tile * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 255;
        pixels[i + 1] = 0;
        pixels[i + 2] = 255;
        pixels[i + 3] = 255;
    }
}

}  // namespace

int BlockAtlas::tileSizeFor(int largestSide) {
    int tile = MIN_TILE;
    while (tile < largestSide && tile < MAX_TILE) tile *= 2;
    return tile;
}

void BlockAtlas::buildFromTextures(finevk::LogicalDevice* device, finevk::CommandPool* commandPool,
                                   const std::string& textureRoot,
                                   const std::unordered_map<uint32_t, BlockTextureSet>& blocks,
                                   const std::vector<std::string>& moreTextures) {
    buildFromTextures(device, commandPool,
                      [&](const std::string& name) { return (std::filesystem::path(textureRoot) / (name + ".png")).string(); },
                      blocks, moreTextures);
}

void BlockAtlas::buildFromTextures(finevk::LogicalDevice* device, finevk::CommandPool* commandPool,
                                   const TexturePath& pathOf,
                                   const std::unordered_map<uint32_t, BlockTextureSet>& blocks,
                                   const std::vector<std::string>& moreTextures) {
    // std::map: stable tile order regardless of registry iteration
    std::map<std::string, uint32_t> tiles;
    for (const auto& [id, set] : blocks) {
        for (int f = 0; f < 6; ++f) tiles.try_emplace(set.forFace(static_cast<Face>(f)), 0);
    }
    for (const auto& name : moreTextures) tiles.try_emplace(name, 0);
    uint32_t index = 0;
    for (auto& [name, tile] : tiles) tile = index++;

    // Each picture once, however many turns of it are used; the largest sets the tile size
    std::map<std::string, std::optional<Image>> images;
    int largest = 0;
    for (const auto& [name, tile] : tiles) {
        std::string base;
        int degrees = 0;
        splitTextureTurn(name, base, degrees);
        auto [it, added] = images.try_emplace(base);
        if (!added) continue;
        std::string path = pathOf(base);
        it->second = loadImage(path);
        if (!it->second) {
            std::cerr << "[BlockAtlas] missing texture: " << path << "\n";
            continue;
        }
        largest = std::max({largest, it->second->width, it->second->height});
    }
    const int size = tileSizeFor(largest);
    const int border = size / 16;

    uint32_t grid = std::max<uint32_t>(
        1, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(tiles.size())))));
    const int cell = size + border * 2;

    tileSize_ = size;
    atlasWidth_ = grid * cell;
    atlasHeight_ = grid * cell;
    gridWidth_ = grid;
    gridHeight_ = grid;
    borderPixels_ = static_cast<float>(border);
    cellWidth_ = 1.0f / static_cast<float>(grid);
    cellHeight_ = 1.0f / static_cast<float>(grid);

    std::vector<uint8_t> atlas(static_cast<size_t>(atlasWidth_) * atlasHeight_ * 4, 0);
    std::vector<uint8_t> pixels;
    for (const auto& [name, tile] : tiles) {
        std::string base;
        int degrees = 0;
        splitTextureTurn(name, base, degrees);
        const auto& image = images.at(base);
        if (image) toTile(*image, degrees / 90, size, pixels);
        else magenta(size, pixels);

        int originX = static_cast<int>(tile % grid) * cell;
        int originY = static_cast<int>(tile / grid) * cell;
        // Wrap into the border so filtering at a tile edge sees the tile's far
        // edge, which is what a repeating surface should show.
        for (int y = 0; y < cell; ++y) {
            for (int x = 0; x < cell; ++x) {
                int sx = (x - border + size) % size;
                int sy = (y - border + size) % size;
                size_t dest = (static_cast<size_t>(originY + y) * atlasWidth_ + originX + x) * 4;
                std::memcpy(&atlas[dest], &pixels[(static_cast<size_t>(sy) * size + sx) * 4], 4);
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
        for (int f = 0; f < 6; ++f) info.faces[f] = face(set.forFace(static_cast<Face>(f)));
        blockTextures_[id] = info;
    }
    namedTextures_.clear();
    for (const auto& [name, tile] : tiles) namedTextures_[name] = gridToUV(tile % grid, tile / grid);

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

std::optional<glm::vec4> BlockAtlas::textureUV(const std::string& name) const {
    auto it = namedTextures_.find(name);
    if (it == namedTextures_.end()) return std::nullopt;
    return it->second.bounds();
}

// A copy of the mappings, not this atlas: a mesh worker may still be building with it after the
// atlas has been replaced
BlockTextureProvider BlockAtlas::createProvider() const {
    auto textures = std::make_shared<const std::unordered_map<uint32_t, BlockTextureInfo>>(blockTextures_);
    auto fallback = defaultTexture_;
    return [textures, fallback](BlockTypeId id, Face face) {
        auto it = textures->find(id.id);
        return (it != textures->end() ? it->second : fallback).get(face).bounds();
    };
}

bool BlockAtlas::sameLayout(const BlockAtlas& other) const {
    auto same = [](const BlockFaceTexture& a, const BlockFaceTexture& b) {
        return a.uvMin == b.uvMin && a.uvMax == b.uvMax;
    };
    if (blockTextures_.size() != other.blockTextures_.size() ||
        namedTextures_.size() != other.namedTextures_.size()) {
        return false;
    }
    for (const auto& [id, info] : blockTextures_) {
        auto it = other.blockTextures_.find(id);
        if (it == other.blockTextures_.end()) return false;
        for (int f = 0; f < 6; ++f) {
            if (!same(info.faces[f], it->second.faces[f])) return false;
        }
    }
    for (const auto& [name, tex] : namedTextures_) {
        auto it = other.namedTextures_.find(name);
        if (it == other.namedTextures_.end() || !same(tex, it->second)) return false;
    }
    return true;
}

}  // namespace finevox::render
