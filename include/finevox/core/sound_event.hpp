#pragma once

/**
 * @file sound_event.hpp
 * @brief Core sound types for the audio system
 *
 * These types live in the core library so game logic can produce
 * sound events without depending on the audio implementation.
 */

#include "finevox/core/string_interner.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/queue.hpp"

#include <glm/glm.hpp>
#include <cstdint>

namespace finevox {

// ============================================================================
// SoundSetId - Type-safe ID for a sound set (e.g., "stone", "grass")
// ============================================================================

struct SoundSetId {
    InternedId id = 0;

    constexpr SoundSetId() = default;
    constexpr explicit SoundSetId(InternedId id_) : id(id_) {}

    /// Create from string name (interns if not already)
    [[nodiscard]] static SoundSetId fromName(std::string_view name);

    /// Get the string name
    [[nodiscard]] std::string_view name() const;

    /// Check if this is a valid (non-zero) sound set
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const SoundSetId&) const = default;
    constexpr auto operator<=>(const SoundSetId&) const = default;
};

// ============================================================================
// SoundAction - What kind of sound within a sound set
// ============================================================================

enum class SoundAction : uint8_t {
    Place,      // Block placed
    Break,      // Block broken
    Step,       // Footstep on this material
    Dig,        // While mining (repeated hits)
    Hit,        // Single hit on block
    Fall,       // Landed from height
};

// ============================================================================
// SoundCategory - For volume control grouping
// ============================================================================

enum class SoundCategory : uint8_t {
    Master,
    Effects,    // Block sounds, impacts
    Music,      // Background music
    Ambient,    // Environmental loops (wind, water, cave)
    UI,         // Menu clicks
};

// ============================================================================
// SoundEvent - Lightweight event passed through the sound queue
// ============================================================================

struct SoundEvent {
    SoundSetId soundSet;
    SoundAction action = SoundAction::Place;
    SoundCategory category = SoundCategory::Effects;

    // Position in world coordinates (float precision is fine for audio)
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;

    // Playback modifiers
    float volume = 1.0f;     // 0.0 - 1.0
    float pitch = 1.0f;      // 0.5 - 2.0
    bool positional = true;  // false for UI/music sounds

    // Helpers
    [[nodiscard]] glm::vec3 position() const { return {posX, posY, posZ}; }
    void setPosition(glm::vec3 p) { posX = p.x; posY = p.y; posZ = p.z; }
    void setPosition(const BlockPos& p) {
        posX = static_cast<float>(p.x) + 0.5f;
        posY = static_cast<float>(p.y) + 0.5f;
        posZ = static_cast<float>(p.z) + 0.5f;
    }

    // Factory methods
    static SoundEvent blockPlace(SoundSetId set, BlockPos pos);
    static SoundEvent blockBreak(SoundSetId set, BlockPos pos);
    static SoundEvent footstep(SoundSetId set, glm::vec3 pos);
    static SoundEvent fall(SoundSetId set, glm::vec3 pos, float fallDistance);
    static SoundEvent music(SoundSetId trackId);
    static SoundEvent ambient(SoundSetId ambientId, glm::vec3 pos);
};

// Thread-safe queue for game thread -> audio engine communication
using SoundEventQueue = Queue<SoundEvent>;

}  // namespace finevox

// Hash specialization for SoundSetId
template<>
struct std::hash<finevox::SoundSetId> {
    size_t operator()(const finevox::SoundSetId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
