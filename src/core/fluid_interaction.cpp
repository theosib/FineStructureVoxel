#include "finevox/core/fluid_interaction.hpp"

namespace finevox {

FluidInteractionRegistry& FluidInteractionRegistry::global() {
    static FluidInteractionRegistry instance;
    return instance;
}

uint64_t FluidInteractionRegistry::makeKey(FluidTypeId a, FluidTypeId b) {
    // Canonical form: smaller ID in high bits, larger in low bits
    uint32_t lo = std::min(a.id, b.id);
    uint32_t hi = std::max(a.id, b.id);
    return (static_cast<uint64_t>(lo) << 32) | hi;
}

bool FluidInteractionRegistry::registerInteraction(FluidInteraction interaction) {
    uint64_t key = makeKey(interaction.fluidA, interaction.fluidB);

    std::unique_lock lock(mutex_);
    auto [it, inserted] = interactions_.emplace(key, std::move(interaction));
    return inserted;
}

const FluidInteraction* FluidInteractionRegistry::getInteraction(
    FluidTypeId a, FluidTypeId b) const
{
    uint64_t key = makeKey(a, b);

    std::shared_lock lock(mutex_);
    auto it = interactions_.find(key);
    return it != interactions_.end() ? &it->second : nullptr;
}

bool FluidInteractionRegistry::hasInteraction(FluidTypeId a, FluidTypeId b) const {
    return getInteraction(a, b) != nullptr;
}

size_t FluidInteractionRegistry::size() const {
    std::shared_lock lock(mutex_);
    return interactions_.size();
}

void FluidInteractionRegistry::clear() {
    std::unique_lock lock(mutex_);
    interactions_.clear();
}

}  // namespace finevox
