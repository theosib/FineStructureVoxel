#pragma once

/**
 * @file particle_atlas.hpp
 * @brief Texture atlas for particle sprites (Phase 26-B).
 *
 * Loads a directory of PNGs, packs them into a single texture at startup,
 * and exposes named region lookup.  Follows the BlockAtlas pattern:
 * grid-based layout with a 2-pixel border per cell to avoid bilinear
 * bleed at cell edges.
 *
 * Usage:
 * @code
 * ParticleAtlas atlas;
 * atlas.loadFromDirectory(device, commandPool, "resources/textures/particles");
 * atlas.applyToRegistry(ParticleTypeRegistry::global());
 * // Now sim spawns particles with correct atlasUV pointing into atlas.texture()
 * @endcode
 */

#include "finevox/core/particle_registry.hpp"

#include <finevk/high/texture.hpp>
#include <finevk/device/command.hpp>

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox::render {

class ParticleAtlas {
public:
    ParticleAtlas() = default;

    /// Load all PNGs in `dirPath` into a single atlas texture.
    /// Each file becomes a region named by its stem (e.g. "spark.png" → "spark").
    /// Grid cell size and layout are chosen to fit N tiles with minimum waste.
    /// Returns the number of regions loaded.  0 on failure or empty directory.
    size_t loadFromDirectory(
        finevk::LogicalDevice* device,
        finevk::CommandPool* commandPool,
        const std::string& dirPath);

    /// Build a 1×1 placeholder white texture — usable for tests and for
    /// engine paths that want particles to draw as tinted quads without any
    /// art assets on disk.  Every region returned is the full [0,1]² rect.
    void createPlaceholder(
        finevk::LogicalDevice* device,
        finevk::CommandPool* commandPool);

    /// Look up a region by name (file stem). Returns (0,0,1,1) if missing.
    [[nodiscard]] glm::vec4 getRegion(std::string_view name) const;
    [[nodiscard]] bool hasRegion(std::string_view name) const;

    /// Iterate all registered particle types and patch their atlasUV
    /// based on their `atlasRegion` string.  Unknown regions keep the
    /// default (full-texture) UV; missing regions are reported via `warnings`.
    /// Thread-safe wrt. the simulator — the registry uses a unique_lock.
    void applyToRegistry(
        ParticleTypeRegistry& registry,
        std::vector<std::string>* warnings = nullptr) const;

    [[nodiscard]] finevk::Texture* texture() const { return texture_.get(); }
    [[nodiscard]] bool isLoaded() const { return texture_ != nullptr; }
    [[nodiscard]] uint32_t width() const { return atlasWidth_; }
    [[nodiscard]] uint32_t height() const { return atlasHeight_; }
    [[nodiscard]] size_t regionCount() const { return regions_.size(); }

private:
    finevk::TextureRef texture_;
    uint32_t atlasWidth_ = 0;
    uint32_t atlasHeight_ = 0;
    std::unordered_map<std::string, glm::vec4> regions_;
};

} // namespace finevox::render
