#include "finevox/render/icon_atlas.hpp"
#include "finevox/core/string_interner.hpp"

namespace finevox::render {

void IconAtlas::buildFromBlockAtlas(const BlockAtlas& atlas) {
    auto blockTypes = atlas.registeredBlockTypes();
    for (auto blockId : blockTypes) {
        auto name = StringInterner::global().lookup(blockId.id);
        if (name.empty()) continue;

        // Use front face (NegZ) as the icon — this is what players typically see
        auto uv = atlas.getUV(blockId, Face::NegZ);

        IconRegion region;
        region.uv0 = {uv.x, uv.y};  // minU, minV
        region.uv1 = {uv.z, uv.w};  // maxU, maxV

        icons_[std::string(name)] = region;
    }
}

void IconAtlas::registerIcon(std::string_view typeName, glm::vec2 uv0, glm::vec2 uv1) {
    icons_[std::string(typeName)] = IconRegion{uv0, uv1};
}

const IconRegion* IconAtlas::getIcon(std::string_view typeName) const {
    auto it = icons_.find(typeName);
    if (it != icons_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool IconAtlas::hasIcon(std::string_view typeName) const {
    return icons_.find(typeName) != icons_.end();
}

}  // namespace finevox::render
