#pragma once

/**
 * @file blueprint.hpp
 * @brief Structure templates and in-world construction sites
 *
 * A Schematic says what a structure is. A BlueprintSite says how far along one is
 * in the world: which cells are finished, which an agent has reserved, what comes
 * next. Sites compare against the live world rather than keeping their own record,
 * so a block destroyed mid-build simply becomes work again, and upgrading a
 * structure in place is a new site over the old one.
 *
 * Plain-text `.blueprint` files keep structures diffable. A legend, then one
 * layer per `---` from the ground up. Rows run along +Z and columns along +X:
 *
 *     # A 3x3 hut
 *     p = planks
 *     t = thatch
 *     ---
 *     ppp
 *     p p      <- space leaves the world as it is
 *     ppp
 *     ---
 *     ttt
 *     t_t      <- underscore requires air (clears whatever is there)
 *     ttt
 */

#include "finevox/worldgen/schematic.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {
class World;
}

namespace finevox::worldgen {

/// Parse .blueprint text. nullopt if malformed; `error` says why.
[[nodiscard]] std::optional<Schematic> parseBlueprint(std::string_view text,
                                                      std::string* error = nullptr);

/// Named schematics loaded from a directory (.blueprint and .vxsc)
class BlueprintLibrary {
public:
    size_t loadDirectory(const std::filesystem::path& dir);
    void add(std::string name, Schematic schematic);

    [[nodiscard]] const Schematic* find(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, std::unique_ptr<Schematic>> blueprints_;
};

struct SiteCell {
    BlockCoord pos;
    BlockTypeId block;   ///< Air means clear this cell
    Rotation rotation = Rotation::IDENTITY;
    EntityId claimedBy = INVALID_ENTITY_ID;
};

struct SiteProgress {
    size_t total = 0;
    size_t done = 0;
    [[nodiscard]] bool complete() const { return done >= total; }
};

class BlueprintSite {
public:
    BlueprintSite() = default;

    /// Stamp a schematic at origin (its minimum corner), rotated in quarter turns
    BlueprintSite(const Schematic& schematic, BlockCoord origin, int quarterTurns);

    /// A site as it was saved. Claims aren't kept: workers choose again on resuming.
    [[nodiscard]] static BlueprintSite restored(BlockCoord origin, std::vector<SiteCell> cells) {
        BlueprintSite site;
        site.origin_ = origin;
        site.cells_ = std::move(cells);
        return site;
    }

    /// Add a single cell, for structures built up by game logic. A position that
    /// already has a target keeps it, so foundations can't fight the structure.
    void addCell(BlockCoord pos, BlockTypeId block, Rotation rotation = Rotation::IDENTITY);

    /// Closest unfinished, unclaimed cell on the lowest unfinished layer, claimed for
    /// `agent`. Bottom-up order means nothing is ever placed on thin air.
    [[nodiscard]] std::optional<SiteCell> claimNext(const World& world, const glm::dvec3& near,
                                                    EntityId agent);

    void release(BlockCoord pos, EntityId agent);
    void releaseAgent(EntityId agent);

    [[nodiscard]] SiteProgress progress(const World& world) const;

    /// Unfinished cells: what a material count or a refit needs
    [[nodiscard]] std::vector<SiteCell> remaining(const World& world) const;

    /// Every cell a structure occupies; used to decide whether a spot is free
    [[nodiscard]] const std::vector<SiteCell>& cells() const { return cells_; }

    [[nodiscard]] BlockCoord origin() const { return origin_; }

private:
    [[nodiscard]] static bool isDone(const World& world, const SiteCell& cell);

    BlockCoord origin_;
    std::vector<SiteCell> cells_;
};

}  // namespace finevox::worldgen
