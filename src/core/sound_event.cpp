#include "finevox/core/sound_event.hpp"

#include <algorithm>

namespace finevox {

// ============================================================================
// SoundSetId
// ============================================================================

SoundSetId SoundSetId::fromName(std::string_view name) {
    if (name.empty()) return SoundSetId{};
    return SoundSetId{StringInterner::global().intern(name)};
}

std::string_view SoundSetId::name() const {
    return StringInterner::global().lookup(id);
}

// ============================================================================
// SoundEvent Factory Methods
// ============================================================================

SoundEvent SoundEvent::blockPlace(SoundSetId set, BlockPos pos) {
    SoundEvent event;
    event.soundSet = set;
    event.action = SoundAction::Place;
    event.category = SoundCategory::Effects;
    event.setPosition(pos);
    return event;
}

SoundEvent SoundEvent::blockBreak(SoundSetId set, BlockPos pos) {
    SoundEvent event;
    event.soundSet = set;
    event.action = SoundAction::Break;
    event.category = SoundCategory::Effects;
    event.setPosition(pos);
    return event;
}

SoundEvent SoundEvent::footstep(SoundSetId set, glm::vec3 pos) {
    SoundEvent event;
    event.soundSet = set;
    event.action = SoundAction::Step;
    event.category = SoundCategory::Effects;
    event.setPosition(pos);
    event.volume = 0.5f;
    return event;
}

SoundEvent SoundEvent::fall(SoundSetId set, glm::vec3 pos, float fallDistance) {
    SoundEvent event;
    event.soundSet = set;
    event.action = SoundAction::Fall;
    event.category = SoundCategory::Effects;
    event.setPosition(pos);
    event.volume = std::clamp(fallDistance / 10.0f, 0.3f, 1.0f);
    return event;
}

SoundEvent SoundEvent::music(SoundSetId trackId) {
    SoundEvent event;
    event.soundSet = trackId;
    event.action = SoundAction::Place;  // Unused for music
    event.category = SoundCategory::Music;
    event.positional = false;
    return event;
}

SoundEvent SoundEvent::ambient(SoundSetId ambientId, glm::vec3 pos) {
    SoundEvent event;
    event.soundSet = ambientId;
    event.action = SoundAction::Place;  // Unused for ambient
    event.category = SoundCategory::Ambient;
    event.setPosition(pos);
    return event;
}

}  // namespace finevox
