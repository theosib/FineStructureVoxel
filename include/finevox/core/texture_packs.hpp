#pragma once

/**
 * @file texture_packs.hpp
 * @brief Texture packs: folders of pictures a player picks to change how a game looks
 *
 * Design: docs/systems/texture-packs.md
 */

#include "finevox/core/block_model.hpp"  // BlockBlend

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

/// One pack: a folder holding files at the same paths as the game's, and a pack.conf
struct TexturePack {
    std::string id;           ///< The folder's name
    std::string name;         ///< As pack.conf gives it, or the id
    std::string description;
    std::filesystem::path root;
    /// Whether faces spread over their neighbours' (BlockBlend) while it's on, over the game's own
    /// `blending`; nullopt where it doesn't say
    std::optional<bool> blending;
    /// Blends by block name, over the blocks' own `blend:`; nullopt for a block that keeps hard edges
    std::unordered_map<std::string, std::optional<BlockBlend>> blends;

    /// Read the pack in this folder. Nothing in pack.conf is needed, nor pack.conf itself.
    [[nodiscard]] static TexturePack load(const std::filesystem::path& root);
};

/// The packs a client can choose from, and the one in use. Only pictures and UI skins can be
/// replaced, never scripts or blocks, so a pack is the player's own business: the server never
/// knows, and it can't change how the game plays.
class TexturePacks {
public:
    /// Find packs, one per subfolder of each folder. A pack in a later folder replaces an earlier
    /// one with the same id. The active pack stays active if it's still there.
    void scan(const std::vector<std::filesystem::path>& folders);

    [[nodiscard]] const std::vector<TexturePack>& available() const { return packs_; }

    /// Use the pack with this id, or none for an empty id. An unknown id leaves none and gives false.
    bool select(const std::string& id);

    /// The pack in use, or nullptr
    [[nodiscard]] const TexturePack* active() const;

    /// Whether a pack may replace the file at this content path: a .png, or a UI skin (.fgs) and the
    /// fonts beside it
    [[nodiscard]] static bool replaceable(std::string_view relative);

    /// The file to read for a content path: the active pack's if it has one and may replace it,
    /// otherwise the one under `base`. `relative` must already be a safe content path.
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& base,
                                                std::string_view relative) const;

    /// The stems of the files with this extension (".png") in a content folder, the game's and the
    /// active pack's together, sorted
    [[nodiscard]] std::vector<std::string> list(const std::filesystem::path& base, std::string_view folder,
                                                std::string_view extension) const;

private:
    std::vector<TexturePack> packs_;
    std::string active_;
};

}  // namespace finevox
