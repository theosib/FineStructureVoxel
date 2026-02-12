#include "finevox/core/sound_registry.hpp"

namespace finevox {

// ============================================================================
// SoundSetDefinition
// ============================================================================

bool SoundSetDefinition::hasAction(SoundAction action) const {
    auto it = actions.find(action);
    return it != actions.end() && !it->second.empty();
}

const SoundGroup* SoundSetDefinition::getAction(SoundAction action) const {
    auto it = actions.find(action);
    if (it != actions.end() && !it->second.empty()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// SoundRegistry
// ============================================================================

SoundRegistry& SoundRegistry::global() {
    static SoundRegistry instance;
    return instance;
}

bool SoundRegistry::registerSoundSet(const std::string& name, SoundSetDefinition def) {
    std::unique_lock lock(mutex_);

    if (definitions_.find(name) != definitions_.end()) {
        return false;  // Already registered
    }

    auto id = SoundSetId::fromName(name);
    def.name = name;
    idToName_[id] = name;
    definitions_[name] = std::move(def);
    return true;
}

const SoundSetDefinition* SoundRegistry::getSoundSet(SoundSetId id) const {
    std::shared_lock lock(mutex_);

    auto nameIt = idToName_.find(id);
    if (nameIt == idToName_.end()) {
        return nullptr;
    }

    auto it = definitions_.find(nameIt->second);
    if (it != definitions_.end()) {
        return &it->second;
    }
    return nullptr;
}

const SoundSetDefinition* SoundRegistry::getSoundSet(const std::string& name) const {
    std::shared_lock lock(mutex_);

    auto it = definitions_.find(name);
    if (it != definitions_.end()) {
        return &it->second;
    }
    return nullptr;
}

SoundSetId SoundRegistry::getSoundSetId(const std::string& name) const {
    std::shared_lock lock(mutex_);

    auto it = definitions_.find(name);
    if (it == definitions_.end()) {
        return SoundSetId{};  // Invalid
    }

    // The name is registered, so find its interned ID
    auto internedId = StringInterner::global().find(name);
    if (internedId.has_value()) {
        return SoundSetId{*internedId};
    }
    return SoundSetId{};
}

size_t SoundRegistry::size() const {
    std::shared_lock lock(mutex_);
    return definitions_.size();
}

void SoundRegistry::clear() {
    std::unique_lock lock(mutex_);
    definitions_.clear();
    idToName_.clear();
}

}  // namespace finevox
