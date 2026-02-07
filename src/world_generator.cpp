/**
 * @file world_generator.cpp
 * @brief GenerationContext and GenerationPipeline implementation
 *
 * Design: [27-world-generation.md] Sections 27.4.1-27.4.3
 */

#include "finevox/world_generator.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/world.hpp"

#include <algorithm>

namespace finevox {

// ============================================================================
// GenerationContext
// ============================================================================

uint64_t GenerationContext::columnSeed() const {
    return NoiseHash::deriveSeed(worldSeed,
        static_cast<uint64_t>(pos.x) * 73856093ULL ^
        static_cast<uint64_t>(pos.z) * 19349669ULL);
}

// ============================================================================
// GenerationPipeline
// ============================================================================

void GenerationPipeline::addPass(std::unique_ptr<GenerationPass> pass) {
    if (!pass) return;
    passes_.push_back(std::move(pass));
    sortPasses();
}

bool GenerationPipeline::removePass(std::string_view name) {
    auto it = std::find_if(passes_.begin(), passes_.end(),
        [&](const auto& p) { return p->name() == name; });
    if (it == passes_.end()) return false;
    passes_.erase(it);
    return true;
}

bool GenerationPipeline::replacePass(std::unique_ptr<GenerationPass> pass) {
    if (!pass) return false;
    auto name = pass->name();
    auto it = std::find_if(passes_.begin(), passes_.end(),
        [&](const auto& p) { return p->name() == name; });
    if (it == passes_.end()) return false;
    *it = std::move(pass);
    sortPasses();
    return true;
}

void GenerationPipeline::generateColumn(ChunkColumn& column, World& world,
                                         const BiomeMap& biomeMap) {
    GenerationContext ctx{
        column,
        column.position(),
        world,
        biomeMap,
        worldSeed_,
        {},  // heightmap
        {},  // biomes
    };

    for (auto& pass : passes_) {
        pass->generate(ctx);
    }
}

GenerationPass* GenerationPipeline::getPass(std::string_view name) const {
    auto it = std::find_if(passes_.begin(), passes_.end(),
        [&](const auto& p) { return p->name() == name; });
    return (it != passes_.end()) ? it->get() : nullptr;
}

void GenerationPipeline::sortPasses() {
    std::stable_sort(passes_.begin(), passes_.end(),
        [](const auto& a, const auto& b) {
            return a->priority() < b->priority();
        });
}

}  // namespace finevox
