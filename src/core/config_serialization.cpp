#include "finevox/core/block_event.hpp"
#include "finevox/core/distances.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/config.hpp"

namespace finevox {

// ============================================================================
// TickConfig serialization
// ============================================================================

TickConfig TickConfig::fromDataContainer(const DataContainer& dc) {
    TickConfig c;
    c.gameTickIntervalMs = dc.get<uint32_t>("game_tick_interval_ms", 33);
    c.randomTicksPerSubchunk = dc.get<uint32_t>("random_ticks_per_subchunk", 4);
    c.randomSeed = dc.get<uint64_t>("random_seed", 0);
    c.gameTicksEnabled = dc.get<bool>("game_ticks_enabled", true);
    c.randomTicksEnabled = dc.get<bool>("random_ticks_enabled", true);
    return c;
}

DataContainer TickConfig::toDataContainer() const {
    DataContainer dc;
    dc.set("game_tick_interval_ms", gameTickIntervalMs);
    dc.set("random_ticks_per_subchunk", randomTicksPerSubchunk);
    dc.set("random_seed", randomSeed);
    dc.set("game_ticks_enabled", gameTicksEnabled);
    dc.set("random_ticks_enabled", randomTicksEnabled);
    return dc;
}

// ============================================================================
// DistanceConfig serialization
// ============================================================================

DistanceConfig DistanceConfig::fromConfig(const ConfigManager& config) {
    DistanceConfig d;

    // Rendering
    if (auto v = config.get<float>("render.chunk_distance"))
        d.rendering.chunkRenderDistance = *v;
    if (auto v = config.get<float>("render.entity_distance"))
        d.rendering.entityRenderDistance = *v;
    if (auto v = config.get<float>("render.unload_multiplier"))
        d.rendering.unloadMultiplier = *v;

    // Fog
    if (auto v = config.get<bool>("fog.enabled"))
        d.fog.enabled = *v;
    if (auto v = config.get<float>("fog.start_distance"))
        d.fog.startDistance = *v;
    if (auto v = config.get<float>("fog.end_distance"))
        d.fog.endDistance = *v;
    if (auto v = config.get<bool>("fog.dynamic_color"))
        d.fog.dynamicColor = *v;

    // Loading
    if (auto v = config.get<float>("loading.load_distance"))
        d.loading.loadDistance = *v;
    if (auto v = config.get<float>("loading.unload_hysteresis"))
        d.loading.unloadHysteresis = *v;

    // Processing
    if (auto v = config.get<float>("processing.block_update_distance"))
        d.processing.blockUpdateDistance = *v;
    if (auto v = config.get<float>("processing.entity_process_distance"))
        d.processing.entityProcessDistance = *v;
    if (auto v = config.get<float>("processing.simulation_distance"))
        d.processing.simulationDistance = *v;

    // Hysteresis scale
    if (auto v = config.get<float>("hysteresis_scale"))
        d.hysteresisScale = *v;

    d.validate();
    return d;
}

DataContainer DistanceConfig::toDataContainer() const {
    DataContainer dc;

    // Rendering
    dc.set("render.chunk_distance", rendering.chunkRenderDistance);
    dc.set("render.entity_distance", rendering.entityRenderDistance);
    dc.set("render.unload_multiplier", rendering.unloadMultiplier);

    // Fog
    dc.set("fog.enabled", fog.enabled);
    dc.set("fog.start_distance", fog.startDistance);
    dc.set("fog.end_distance", fog.endDistance);
    dc.set("fog.dynamic_color", fog.dynamicColor);

    // Loading
    dc.set("loading.load_distance", loading.loadDistance);
    dc.set("loading.unload_hysteresis", loading.unloadHysteresis);

    // Processing
    dc.set("processing.block_update_distance", processing.blockUpdateDistance);
    dc.set("processing.entity_process_distance", processing.entityProcessDistance);
    dc.set("processing.simulation_distance", processing.simulationDistance);

    // Hysteresis scale
    dc.set("hysteresis_scale", hysteresisScale);

    return dc;
}

}  // namespace finevox
