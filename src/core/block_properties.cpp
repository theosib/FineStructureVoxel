#include "finevox/core/block_properties.hpp"

#include <algorithm>

namespace finevox {

BlockPropertyRegistry& BlockPropertyRegistry::global() {
    static BlockPropertyRegistry registry;
    return registry;
}

void BlockPropertyRegistry::addFamily(const std::string& family,
                                   std::vector<BlockProperty> properties) {
    families_[family] = std::move(properties);
}

const std::vector<BlockProperty>* BlockPropertyRegistry::properties(std::string_view family) const {
    auto it = families_.find(std::string(family));
    return it == families_.end() ? nullptr : &it->second;
}

std::string BlockPropertyRegistry::stateName(const std::string& family,
                                          const std::vector<std::string>& values) const {
    std::string local = family.substr(family.find(':') == std::string::npos ? 0 : family.find(':') + 1);
    const auto* props = properties(local);
    if (!props || props->size() != values.size()) return family;
    bool isDefault = true;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] != (*props)[i].values.front()) isDefault = false;
    }
    if (isDefault) return family;
    std::string name = family + "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) name += ",";
        name += (*props)[i].name + "=" + values[i];
    }
    return name + "]";
}

std::string BlockPropertyRegistry::familyOf(std::string_view stateName) {
    return std::string(stateName.substr(0, stateName.find('[')));
}

std::optional<std::vector<std::string>> BlockPropertyRegistry::valuesOf(std::string_view stateName,
                                                                     std::string& family,
                                                                     std::string& prefix) const {
    family = familyOf(stateName);
    auto colon = family.find(':');
    prefix = colon == std::string::npos ? "" : family.substr(0, colon + 1);
    const auto* props = properties(std::string_view(family).substr(prefix.size()));
    if (!props) return std::nullopt;

    std::vector<std::string> values;
    for (const auto& p : *props) values.push_back(p.values.front());

    auto open = stateName.find('[');
    if (open != std::string_view::npos) {
        std::string_view inner = stateName.substr(open + 1);
        if (!inner.empty() && inner.back() == ']') inner.remove_suffix(1);
        while (!inner.empty()) {
            auto comma = inner.find(',');
            std::string_view term = inner.substr(0, comma);
            auto eq = term.find('=');
            if (eq != std::string_view::npos) {
                for (size_t i = 0; i < props->size(); ++i) {
                    if ((*props)[i].name == term.substr(0, eq)) values[i] = std::string(term.substr(eq + 1));
                }
            }
            if (comma == std::string_view::npos) break;
            inner.remove_prefix(comma + 1);
        }
    }
    return values;
}

std::optional<std::string> BlockPropertyRegistry::property(std::string_view stateName,
                                                        std::string_view prop) const {
    std::string family, prefix;
    auto values = valuesOf(stateName, family, prefix);
    if (!values) return std::nullopt;
    const auto& props = *properties(std::string_view(family).substr(prefix.size()));
    for (size_t i = 0; i < props.size(); ++i) {
        if (props[i].name == prop) return (*values)[i];
    }
    return std::nullopt;
}

std::optional<std::string> BlockPropertyRegistry::withProperty(std::string_view stateName,
                                                            std::string_view prop,
                                                            std::string_view value) const {
    std::string family, prefix;
    auto values = valuesOf(stateName, family, prefix);
    if (!values) return std::nullopt;
    const auto& props = *properties(std::string_view(family).substr(prefix.size()));
    for (size_t i = 0; i < props.size(); ++i) {
        if (props[i].name != prop) continue;
        const auto& allowed = props[i].values;
        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) return std::nullopt;
        (*values)[i] = std::string(value);
        return this->stateName(family, *values);
    }
    return std::nullopt;
}

}  // namespace finevox
