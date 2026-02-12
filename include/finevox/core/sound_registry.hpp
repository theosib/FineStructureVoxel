#pragma once

/**
 * @file sound_registry.hpp
 * @brief Registry mapping sound set names to variant definitions
 *
 * Lives in core so game logic can register and look up sound definitions
 * without depending on the audio implementation.
 */

#include "finevox/core/sound_event.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

namespace finevox {

// ============================================================================
// SoundVariant - A single audio file within a sound group
// ============================================================================

struct SoundVariant {
    std::string path;           // Resource path, e.g., "sounds/stone/place1.wav"
    float volumeScale = 1.0f;  // Per-variant volume multiplier
    float pitchScale = 1.0f;   // Per-variant pitch multiplier
};

// ============================================================================
// SoundGroup - Variants for one action (e.g., "place" might have 3 files)
// ============================================================================

struct SoundGroup {
    std::vector<SoundVariant> variants;

    [[nodiscard]] bool empty() const { return variants.empty(); }
    [[nodiscard]] size_t size() const { return variants.size(); }
};

// ============================================================================
// SoundSetDefinition - All actions for one material (e.g., "stone")
// ============================================================================

struct SoundSetDefinition {
    std::string name;  // e.g., "stone"
    std::unordered_map<SoundAction, SoundGroup> actions;

    // Optional global modifiers for this set
    float volume = 1.0f;
    float pitchVariance = 0.1f;  // Random pitch variation +/- this amount

    [[nodiscard]] bool hasAction(SoundAction action) const;
    [[nodiscard]] const SoundGroup* getAction(SoundAction action) const;
};

// ============================================================================
// SoundRegistry - Global registry of sound set definitions
// ============================================================================

class SoundRegistry {
public:
    /// Get the global registry instance (singleton)
    static SoundRegistry& global();

    /// Register a sound set definition
    /// Returns false if a set with this name already exists
    bool registerSoundSet(const std::string& name, SoundSetDefinition def);

    /// Look up a sound set by ID
    [[nodiscard]] const SoundSetDefinition* getSoundSet(SoundSetId id) const;

    /// Look up a sound set by name
    [[nodiscard]] const SoundSetDefinition* getSoundSet(const std::string& name) const;

    /// Get the SoundSetId for a name (returns invalid if not registered)
    [[nodiscard]] SoundSetId getSoundSetId(const std::string& name) const;

    /// Get number of registered sound sets
    [[nodiscard]] size_t size() const;

    /// Clear all registrations (for testing)
    void clear();

    // Non-copyable singleton
    SoundRegistry(const SoundRegistry&) = delete;
    SoundRegistry& operator=(const SoundRegistry&) = delete;

private:
    SoundRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, SoundSetDefinition> definitions_;
    std::unordered_map<SoundSetId, std::string> idToName_;  // Reverse lookup
};

}  // namespace finevox
