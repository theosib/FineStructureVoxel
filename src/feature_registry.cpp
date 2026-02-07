/**
 * @file feature_registry.cpp
 * @brief FeatureRegistry implementation
 *
 * Design: [27-world-generation.md] Sections 27.5.3-27.5.4
 */

#include "finevox/feature_registry.hpp"

namespace finevox {

FeatureRegistry& FeatureRegistry::global() {
    static FeatureRegistry instance;
    return instance;
}

void FeatureRegistry::registerFeature(std::shared_ptr<Feature> feature) {
    if (!feature) return;
    std::unique_lock lock(mutex_);
    std::string key(feature->name());
    features_[key] = std::move(feature);
}

void FeatureRegistry::addPlacement(FeaturePlacement placement) {
    std::unique_lock lock(mutex_);
    placements_.push_back(std::move(placement));
}

Feature* FeatureRegistry::getFeature(std::string_view name) const {
    std::shared_lock lock(mutex_);
    auto it = features_.find(std::string(name));
    return (it != features_.end()) ? it->second.get() : nullptr;
}

std::vector<FeaturePlacement> FeatureRegistry::allPlacements() const {
    std::shared_lock lock(mutex_);
    return placements_;
}

std::vector<const FeaturePlacement*> FeatureRegistry::placementsForBiome(BiomeId biome) const {
    std::shared_lock lock(mutex_);
    std::vector<const FeaturePlacement*> result;
    for (const auto& p : placements_) {
        // Empty biomes list means all biomes
        if (p.biomes.empty()) {
            result.push_back(&p);
        } else {
            for (const auto& b : p.biomes) {
                if (b == biome) {
                    result.push_back(&p);
                    break;
                }
            }
        }
    }
    return result;
}

size_t FeatureRegistry::featureCount() const {
    std::shared_lock lock(mutex_);
    return features_.size();
}

size_t FeatureRegistry::placementCount() const {
    std::shared_lock lock(mutex_);
    return placements_.size();
}

void FeatureRegistry::clear() {
    std::unique_lock lock(mutex_);
    features_.clear();
    placements_.clear();
}

}  // namespace finevox
