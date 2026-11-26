#include "finevox/core/texture_packs.hpp"
#include "finevox/core/config_parser.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace finevox {

namespace fs = std::filesystem;

TexturePack TexturePack::load(const fs::path& root) {
    TexturePack pack;
    pack.root = root;
    pack.id = root.filename().string();
    pack.name = pack.id;

    ConfigParser parser;
    auto doc = parser.parseFile((root / "pack.conf").string());
    if (!doc) return pack;
    if (const ConfigEntry* name = doc->get("name")) pack.name = name->value.asStringOwned();
    if (const ConfigEntry* description = doc->get("description")) {
        pack.description = description->value.asStringOwned();
    }
    if (const ConfigEntry* blending = doc->get("blending")) pack.blending = blending->value.asBool(false);
    // blend:<block>: <priority> [width], or none
    for (const ConfigEntry* entry : doc->getAll("blend")) {
        if (!entry->hasSuffix()) continue;
        std::istringstream iss(entry->value.asStringOwned());
        int priority = 0;
        float width = 0.35f;
        if (iss >> priority) {
            iss >> width;
            pack.blends[entry->suffix] = BlockBlend{priority, std::clamp(width, 0.0f, 1.0f)};
        } else {
            pack.blends[entry->suffix] = std::nullopt;
        }
    }
    return pack;
}

void TexturePacks::scan(const std::vector<fs::path>& folders) {
    packs_.clear();
    for (const auto& folder : folders) {
        std::error_code ec;
        std::vector<fs::path> roots;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            if (entry.is_directory(ec)) roots.push_back(entry.path());
        }
        std::sort(roots.begin(), roots.end());
        for (const auto& root : roots) {
            TexturePack pack = TexturePack::load(root);
            auto same = std::find_if(packs_.begin(), packs_.end(),
                                     [&](const TexturePack& p) { return p.id == pack.id; });
            if (same != packs_.end()) *same = std::move(pack);
            else packs_.push_back(std::move(pack));
        }
    }
    std::sort(packs_.begin(), packs_.end(), [](const TexturePack& a, const TexturePack& b) { return a.name < b.name; });
    if (!active()) active_.clear();
}

bool TexturePacks::select(const std::string& id) {
    active_ = id;
    if (id.empty() || active()) return true;
    active_.clear();
    return false;
}

const TexturePack* TexturePacks::active() const {
    if (active_.empty()) return nullptr;
    for (const auto& pack : packs_) {
        if (pack.id == active_) return &pack;
    }
    return nullptr;
}

bool TexturePacks::replaceable(std::string_view relative) {
    std::string extension = fs::path(relative).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".png" || extension == ".fgs" || extension == ".ttf" || extension == ".otf";
}

fs::path TexturePacks::resolve(const fs::path& base, std::string_view relative) const {
    if (const TexturePack* pack = active(); pack && replaceable(relative)) {
        fs::path mine = pack->root / std::string(relative);
        std::error_code ec;
        if (fs::is_regular_file(mine, ec)) return mine;
    }
    return base / std::string(relative);
}

std::vector<std::string> TexturePacks::list(const fs::path& base, std::string_view folder,
                                            std::string_view extension) const {
    std::set<std::string> stems;
    auto add = [&](const fs::path& dir) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == extension) {
                stems.insert(entry.path().stem().string());
            }
        }
    };
    add(base / std::string(folder));
    if (const TexturePack* pack = active()) add(pack->root / std::string(folder));
    return {stems.begin(), stems.end()};
}

}  // namespace finevox
