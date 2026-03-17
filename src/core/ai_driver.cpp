#include "finevox/core/ai_driver.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/value.h>
#include <finescript/map_data.h>

namespace finevox {

// ============================================================================
// AIDriver base
// ============================================================================

bool AIDriver::subscribesTo(InternedId eventType) const {
    auto subs = subscribedEvents();
    for (auto id : subs) {
        if (id == eventType) return true;
    }
    return false;
}

// ============================================================================
// BrainAIDriver — delegates to AIBrain goal system
// ============================================================================

void BrainAIDriver::tick(MobEntity& /*mob*/, float /*dt*/) {
    // AIBrain::tick is called from MobEntity::tick directly,
    // so this driver doesn't need to tick the brain again.
}

void BrainAIDriver::onEvent(MobEntity& /*mob*/, const finescript::Value& /*event*/) {
    // BrainAIDriver doesn't handle events — MobEventHooks handles
    // onDamage/onDeath. Goal system reacts to state (wasRecentlyDamaged, etc.)
}

std::vector<InternedId> BrainAIDriver::subscribedEvents() const {
    return {};
}

// ============================================================================
// PlayerInputDriver — helper to read fields from Value maps
// ============================================================================

namespace {

/// Read the :type field from an event Value (symbol → string lookup)
std::string_view readType(const finescript::Value& event) {
    if (!event.isMap()) return "";
    auto& si = StringInterner::global();
    auto typeSym = si.intern("type");
    auto typeVal = event.asMap().get(typeSym);
    if (typeVal.isSymbol()) {
        return si.lookup(typeVal.asSymbol());
    }
    if (typeVal.isString()) {
        return typeVal.asString();
    }
    return "";
}

/// Read a float field from an event Value
float readFloatField(const finescript::Value& event, uint32_t fieldSym, float def = 0.0f) {
    if (!event.isMap()) return def;
    auto val = event.asMap().get(fieldSym);
    if (val.isFloat()) return static_cast<float>(val.asFloat());
    if (val.isInt()) return static_cast<float>(val.asInt());
    return def;
}

/// Read a bool field from an event Value
bool readBoolField(const finescript::Value& event, uint32_t fieldSym, bool def = false) {
    if (!event.isMap()) return def;
    auto val = event.asMap().get(fieldSym);
    if (val.isBool()) return val.asBool();
    if (val.isInt()) return val.asInt() != 0;
    return def;
}

}  // anonymous namespace

// ============================================================================
// PlayerInputDriver
// ============================================================================

const PlayerInputDriver::EventSyms& PlayerInputDriver::EventSyms::instance() {
    static EventSyms syms = [] {
        auto& si = StringInterner::global();
        return EventSyms{
            si.intern("player_position"),
            si.intern("player_look"),
            si.intern("player_jump"),
            si.intern("player_sprint"),
            si.intern("player_sneak"),
            si.intern("player_attack"),
            si.intern("player_use"),
        };
    }();
    return syms;
}

PlayerInputDriver::PlayerInputDriver() {
    (void)EventSyms::instance();
}

void PlayerInputDriver::tick(MobEntity& /*mob*/, float /*dt*/) {
    // Player movement is driven by input events, not tick-based decisions.
}

void PlayerInputDriver::onEvent(MobEntity& mob, const finescript::Value& event) {
    auto typeStr = readType(event);
    const auto& syms = EventSyms::instance();
    auto& si = StringInterner::global();
    auto typeSym = si.intern(typeStr);

    if (typeSym == syms.look) {
        auto yawSym = si.intern("yaw");
        auto pitchSym = si.intern("pitch");
        float yaw = readFloatField(event, yawSym);
        float pitch = readFloatField(event, pitchSym);
        mob.setLook(yaw, pitch);
    }
    else if (typeSym == syms.jump) {
        mob.jump();
    }
    else if (typeSym == syms.sprint) {
        auto startingSym = si.intern("starting");
        sprinting_ = readBoolField(event, startingSym);
        mob.setSpeedMultiplier(sprinting_ ? 1.3f : 1.0f);
    }
    else if (typeSym == syms.sneak) {
        auto startingSym = si.intern("starting");
        sneaking_ = readBoolField(event, startingSym);
        mob.setSpeedMultiplier(sneaking_ ? 0.3f : (sprinting_ ? 1.3f : 1.0f));
    }
}

std::vector<InternedId> PlayerInputDriver::subscribedEvents() const {
    const auto& syms = EventSyms::instance();
    return {syms.move, syms.look, syms.jump, syms.sprint, syms.sneak,
            syms.attack, syms.use_block};
}

}  // namespace finevox
