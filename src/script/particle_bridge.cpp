#include "finevox/script/particle_bridge.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/value.h>
#include <finescript/execution_context.h>

namespace finevox::script {

namespace {

std::string extractString(const finescript::Value& v) {
    if (v.isString()) return v.asString();
    if (v.isSymbol()) return std::string(StringInterner::global().lookup(v.asSymbol()));
    return {};
}

double extractNumber(const finescript::Value& v, double defaultVal = 0.0) {
    if (v.isFloat()) return v.asFloat();
    if (v.isInt())   return static_cast<double>(v.asInt());
    return defaultVal;
}

} // anonymous namespace

void ParticleBridge::registerNativeFunctions(finescript::ScriptEngine& engine) {
    // spawn_particle_emitter(preset, x, y, z, [duration]) → emitter_id
    engine.registerFunction("spawn_particle_emitter",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::integer(0);
            std::string preset = extractString(args[0]);
            if (preset.empty()) return finescript::Value::integer(0);
            glm::dvec3 pos(
                extractNumber(args[1]),
                extractNumber(args[2]),
                extractNumber(args[3]));
            float duration = -2.0f;
            if (args.size() >= 5) duration = static_cast<float>(extractNumber(args[4], -2.0));
            auto id = system_.spawnEmitter(preset, pos, duration);
            return finescript::Value::integer(static_cast<int64_t>(id));
        });

    // spawn_particle_emitter_on_entity(preset, entity_id, [duration]) → emitter_id
    engine.registerFunction("spawn_particle_emitter_on_entity",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 2) return finescript::Value::integer(0);
            std::string preset = extractString(args[0]);
            if (preset.empty() || !args[1].isInt()) return finescript::Value::integer(0);
            auto entityId = static_cast<EntityId>(args[1].asInt());
            float duration = -2.0f;
            if (args.size() >= 3) duration = static_cast<float>(extractNumber(args[2], -2.0));
            auto id = system_.spawnEmitterAttached(preset, entityId, duration);
            return finescript::Value::integer(static_cast<int64_t>(id));
        });

    // despawn_particle_emitter(emitter_id) → bool
    engine.registerFunction("despawn_particle_emitter",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.empty() || !args[0].isInt()) return finescript::Value::boolean(false);
            auto id = static_cast<EmitterId>(args[0].asInt());
            return finescript::Value::boolean(system_.despawnEmitter(id));
        });

    // particle_emitter_active(emitter_id) → bool
    engine.registerFunction("particle_emitter_active",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.empty() || !args[0].isInt()) return finescript::Value::boolean(false);
            auto id = static_cast<EmitterId>(args[0].asInt());
            return finescript::Value::boolean(system_.isEmitterActive(id));
        });

    // particle_burst(preset, x, y, z) → spawned_count
    engine.registerFunction("particle_burst",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 4) return finescript::Value::integer(0);
            std::string preset = extractString(args[0]);
            if (preset.empty()) return finescript::Value::integer(0);
            glm::dvec3 pos(
                extractNumber(args[1]),
                extractNumber(args[2]),
                extractNumber(args[3]));
            return finescript::Value::integer(
                static_cast<int64_t>(system_.burst(preset, pos)));
        });

    // active_particle_count() → int
    engine.registerFunction("active_particle_count",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            return finescript::Value::integer(
                static_cast<int64_t>(system_.activeParticleCount()));
        });

    // active_emitter_count() → int
    engine.registerFunction("active_emitter_count",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            return finescript::Value::integer(
                static_cast<int64_t>(system_.activeEmitterCount()));
        });
}

} // namespace finevox::script
