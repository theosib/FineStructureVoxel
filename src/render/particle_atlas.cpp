#include "finevox/render/particle_atlas.hpp"

#include <finevk/device/logical_device.hpp>

// Use the stb_image that ships with finevk's external deps.
// We do NOT define STB_IMAGE_IMPLEMENTATION — finevk already does that in
// its own texture_loader.cpp; we only want the declarations here.
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace finevox::render {

namespace {

// Cell layout: [border][content][border].  2-pixel border keeps bilinear
// filtering inside the cell.
constexpr uint32_t kBorderPx = 2;
constexpr uint32_t kContentPx = 28;               // → 32×32 cell total
constexpr uint32_t kCellPx = kContentPx + kBorderPx * 2;

// Load a PNG into an RGBA8 buffer sized contentW × contentH.  If the source
// is a different resolution we blit-scale (nearest-neighbor) so every tile
// ends up at the same cell size.  For the placeholder PNGs we ship, sizes
// match exactly.
struct LoadedImage {
    std::vector<uint8_t> pixels;  // RGBA, row-major
    int width = 0;
    int height = 0;
};

[[nodiscard]] bool loadPng(const std::string& path, LoadedImage& out) {
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[ParticleAtlas] stbi_load failed for " << path
                  << ": " << stbi_failure_reason() << "\n";
        return false;
    }
    out.width = w;
    out.height = h;
    out.pixels.assign(data, data + (w * h * 4));
    stbi_image_free(data);
    return true;
}

// Nearest-neighbor scale `src` (srcW × srcH RGBA8) into `dst` (dstW × dstH RGBA8).
// Used only when the source PNG doesn't match `kContentPx` exactly.
void scaleIntoCell(
    const uint8_t* src, int srcW, int srcH,
    uint8_t* dst, uint32_t dstW, uint32_t dstH)
{
    for (uint32_t dy = 0; dy < dstH; ++dy) {
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            int sx = static_cast<int>(
                (static_cast<double>(dx) + 0.5) * srcW / dstW);
            int sy = static_cast<int>(
                (static_cast<double>(dy) + 0.5) * srcH / dstH);
            sx = std::clamp(sx, 0, srcW - 1);
            sy = std::clamp(sy, 0, srcH - 1);
            std::memcpy(dst + (dy * dstW + dx) * 4,
                        src + (sy * srcW + sx) * 4, 4);
        }
    }
}

} // anonymous namespace

size_t ParticleAtlas::loadFromDirectory(
    finevk::LogicalDevice* device,
    finevk::CommandPool* commandPool,
    const std::string& dirPath)
{
    namespace fs = std::filesystem;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    // Collect PNG files (sorted for reproducible packing).
    std::vector<fs::path> pngs;
    for (const auto& e : fs::directory_iterator(dirPath)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".png" && e.path().extension() != ".PNG") continue;
        pngs.push_back(e.path());
    }
    if (pngs.empty()) return 0;
    std::sort(pngs.begin(), pngs.end());

    // Square-ish grid that fits every tile.
    uint32_t tileCount = static_cast<uint32_t>(pngs.size());
    uint32_t gridW = static_cast<uint32_t>(std::ceil(std::sqrt(
        static_cast<double>(tileCount))));
    uint32_t gridH = (tileCount + gridW - 1) / gridW;

    atlasWidth_ = gridW * kCellPx;
    atlasHeight_ = gridH * kCellPx;

    std::vector<uint8_t> pixels(atlasWidth_ * atlasHeight_ * 4, 0);
    regions_.clear();

    size_t loaded = 0;
    for (uint32_t i = 0; i < tileCount; ++i) {
        LoadedImage img;
        if (!loadPng(pngs[i].string(), img)) continue;

        uint32_t gx = i % gridW;
        uint32_t gy = i / gridW;
        uint32_t cellX = gx * kCellPx;
        uint32_t cellY = gy * kCellPx;

        // Scale (or copy) source into content area.
        std::vector<uint8_t> content(kContentPx * kContentPx * 4, 0);
        if (img.width == static_cast<int>(kContentPx) &&
            img.height == static_cast<int>(kContentPx)) {
            std::memcpy(content.data(), img.pixels.data(), content.size());
        } else {
            scaleIntoCell(img.pixels.data(), img.width, img.height,
                          content.data(), kContentPx, kContentPx);
        }

        // Blit content + fill border rows/cols from edge pixels to avoid
        // sampler bleed (replicates BlockAtlas strategy).
        for (uint32_t py = 0; py < kCellPx; ++py) {
            for (uint32_t px = 0; px < kCellPx; ++px) {
                // Nearest interior pixel inside the content area.
                int cx = static_cast<int>(px) - static_cast<int>(kBorderPx);
                int cy = static_cast<int>(py) - static_cast<int>(kBorderPx);
                cx = std::clamp(cx, 0, static_cast<int>(kContentPx) - 1);
                cy = std::clamp(cy, 0, static_cast<int>(kContentPx) - 1);
                uint32_t srcIdx = (cy * kContentPx + cx) * 4;
                uint32_t dstIdx = ((cellY + py) * atlasWidth_ + (cellX + px)) * 4;
                std::memcpy(&pixels[dstIdx], &content[srcIdx], 4);
            }
        }

        // UV of content area (inset by border).
        const float borderU = static_cast<float>(kBorderPx) / atlasWidth_;
        const float borderV = static_cast<float>(kBorderPx) / atlasHeight_;
        float cellMinU = static_cast<float>(cellX) / atlasWidth_;
        float cellMinV = static_cast<float>(cellY) / atlasHeight_;
        float cellMaxU = cellMinU + static_cast<float>(kCellPx) / atlasWidth_;
        float cellMaxV = cellMinV + static_cast<float>(kCellPx) / atlasHeight_;
        glm::vec4 uv(
            cellMinU + borderU,
            cellMinV + borderV,
            cellMaxU - borderU,
            cellMaxV - borderV);

        regions_[pngs[i].stem().string()] = uv;
        ++loaded;
    }

    texture_ = finevk::Texture::fromMemory(
        device, pixels.data(), atlasWidth_, atlasHeight_,
        commandPool, /*generateMipmaps=*/false, /*srgb=*/true);

    return loaded;
}

void ParticleAtlas::createPlaceholder(
    finevk::LogicalDevice* device,
    finevk::CommandPool* commandPool)
{
    // 1×1 white RGBA pixel — draws as a tinted quad everywhere.
    atlasWidth_ = 1;
    atlasHeight_ = 1;
    uint8_t pixel[4] = {255, 255, 255, 255};
    texture_ = finevk::Texture::fromMemory(
        device, pixel, 1, 1, commandPool,
        /*generateMipmaps=*/false, /*srgb=*/true);
    regions_.clear();
    // No named regions — the default (0,0,1,1) UV keeps particles sampling
    // the single white texel.
}

glm::vec4 ParticleAtlas::getRegion(std::string_view name) const {
    auto it = regions_.find(std::string(name));
    if (it != regions_.end()) return it->second;
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
}

bool ParticleAtlas::hasRegion(std::string_view name) const {
    return regions_.find(std::string(name)) != regions_.end();
}

void ParticleAtlas::applyToRegistry(
    ParticleTypeRegistry& registry,
    std::vector<std::string>* warnings) const
{
    // Two-phase: collect (id, region, uv) tuples while the registry's
    // shared_lock is held by forEach(), then release it before calling
    // setAtlasUV() which takes a unique_lock on the same mutex.
    // (Upgrading shared→unique from inside the callback would deadlock.)
    struct Patch {
        ParticleTypeId id;
        glm::vec4 uv;
    };
    std::vector<Patch> patches;
    std::vector<std::string> missing;

    registry.forEach([&](ParticleTypeId id, const ParticleTypeDef& def) {
        if (def.atlasRegion.empty()) return;
        if (!hasRegion(def.atlasRegion)) {
            missing.push_back(
                "particle type '" + std::string(def.name) +
                "' references unknown atlas region '" + def.atlasRegion + "'");
            return;
        }
        patches.push_back({id, getRegion(def.atlasRegion)});
    });

    for (const auto& p : patches) {
        registry.setAtlasUV(p.id, p.uv);
    }
    if (warnings) {
        warnings->insert(warnings->end(), missing.begin(), missing.end());
    }
}

} // namespace finevox::render
