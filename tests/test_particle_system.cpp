#include <gtest/gtest.h>

#include "finevox/core/block_type.hpp"
#include "finevox/core/particle.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/particle_type.hpp"
#include "finevox/core/world.hpp"
#include "finevox/script/event_value.hpp"

#include <finescript/value.h>
#include <finescript/map_data.h>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Helpers
// ============================================================================

namespace {

int gUniqueCounter = 0;

std::string uniq(const char* base) {
    return std::string(base) + "_particle_" + std::to_string(++gUniqueCounter);
}

void registerSimpleType(const std::string& name, float lifetime = 5.0f) {
    ParticleTypeDef def;
    def.name = name;
    def.lifetime = Range{lifetime, lifetime};
    def.initialSpeed = Range{0.0f, 0.0f};
    def.velocityBias = glm::vec3(0.0f);
    def.gravity = 0.0f;
    def.size = Range{0.1f, 0.1f};
    ParticleTypeRegistry::global().registerType(name, std::move(def));
}

void registerBurstPreset(const std::string& presetName,
                         const std::string& typeName,
                         uint32_t burstCount) {
    EmitterPresetDef preset;
    preset.name = presetName;
    preset.shape = EmitterShape::Point;
    preset.burstCount = burstCount;
    preset.duration = 0.0f;
    preset.particleType = typeName;
    EmitterPresetRegistry::global().registerPreset(presetName, std::move(preset));
}

} // anonymous namespace

// ============================================================================
// File parsing
// ============================================================================

TEST(ParticleLoader, ParsesFullTypeDefinition) {
    constexpr const char* src = R"(
name: test:shard
lifetime: 1.0-2.0
initial_speed: 3.0-5.0
velocity_bias: 0 4 0
gravity: 9.8
drag: 0.5
size: 0.1-0.2
size_end_scale: 0.3
start_tint: 1 0.5 0 1
end_tint: 1 0 0 0
atlas_region: test_region
billboard: velocity
collides: true
kill_on_collision: false
restitution: 0.4
)";

    auto def = ParticleLoader::loadTypeFromString(src);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->name, "test:shard");
    EXPECT_FLOAT_EQ(def->lifetime.min, 1.0f);
    EXPECT_FLOAT_EQ(def->lifetime.max, 2.0f);
    EXPECT_FLOAT_EQ(def->initialSpeed.min, 3.0f);
    EXPECT_FLOAT_EQ(def->initialSpeed.max, 5.0f);
    EXPECT_FLOAT_EQ(def->velocityBias.y, 4.0f);
    EXPECT_FLOAT_EQ(def->gravity, 9.8f);
    EXPECT_FLOAT_EQ(def->drag, 0.5f);
    EXPECT_FLOAT_EQ(def->sizeEndScale, 0.3f);
    EXPECT_FLOAT_EQ(def->startTint.r, 1.0f);
    EXPECT_FLOAT_EQ(def->endTint.a, 0.0f);
    EXPECT_EQ(def->atlasRegion, "test_region");
    EXPECT_EQ(def->billboard, BillboardMode::VelocityStretched);
    EXPECT_TRUE(def->collidesWithTerrain);
    EXPECT_FALSE(def->killOnCollision);
    EXPECT_FLOAT_EQ(def->restitution, 0.4f);
}

TEST(ParticleLoader, ParsesEmitterPreset) {
    constexpr const char* src = R"(
name: test:puff
shape: box
shape_size: 0.5 0.2 0.5
spawn_rate: 20.0
burst: 5
duration: 2.0
particle: test:shard
tint: 0.5 0.5 0.5 1.0
)";

    auto def = ParticleLoader::loadPresetFromString(src);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->name, "test:puff");
    EXPECT_EQ(def->shape, EmitterShape::Box);
    EXPECT_FLOAT_EQ(def->shapeSize.x, 0.5f);
    EXPECT_FLOAT_EQ(def->shapeSize.y, 0.2f);
    EXPECT_FLOAT_EQ(def->spawnRate, 20.0f);
    EXPECT_EQ(def->burstCount, 5u);
    EXPECT_FLOAT_EQ(def->duration, 2.0f);
    EXPECT_EQ(def->particleType, "test:shard");
    EXPECT_TRUE(def->hasTint);
    EXPECT_FLOAT_EQ(def->tint.r, 0.5f);
}

TEST(ParticleLoader, DefaultsForMissingFields) {
    constexpr const char* src = R"(
name: test:minimal
particle: something
)";
    auto def = ParticleLoader::loadPresetFromString(src);
    ASSERT_TRUE(def.has_value());
    // Defaults
    EXPECT_EQ(def->shape, EmitterShape::Point);
    EXPECT_EQ(def->burstCount, 0u);
    EXPECT_FLOAT_EQ(def->spawnRate, 0.0f);
    EXPECT_FLOAT_EQ(def->duration, -1.0f);
    EXPECT_FALSE(def->hasTint);
}

TEST(RangeParse, HandlesSingleAndRange) {
    auto r1 = Range::parse("2.5");
    EXPECT_FLOAT_EQ(r1.min, 2.5f);
    EXPECT_FLOAT_EQ(r1.max, 2.5f);

    auto r2 = Range::parse("1-3");
    EXPECT_FLOAT_EQ(r2.min, 1.0f);
    EXPECT_FLOAT_EQ(r2.max, 3.0f);

    auto r3 = Range::parse("-0.5");
    EXPECT_FLOAT_EQ(r3.min, -0.5f);
    EXPECT_FLOAT_EQ(r3.max, -0.5f);
}

// ============================================================================
// Registry
// ============================================================================

TEST(ParticleRegistry, RegisterAndLookup) {
    auto typeName = uniq("reg_type");
    ParticleTypeDef def;
    def.lifetime = Range{1.0f, 1.0f};
    ASSERT_TRUE(ParticleTypeRegistry::global().registerType(typeName, def));

    const auto* fetched = ParticleTypeRegistry::global().get(typeName);
    ASSERT_NE(fetched, nullptr);
    EXPECT_EQ(fetched->name, typeName);

    // Duplicate registration fails
    EXPECT_FALSE(ParticleTypeRegistry::global().registerType(typeName, def));

    auto presetName = uniq("reg_preset");
    EmitterPresetDef preset;
    preset.burstCount = 1;
    preset.particleType = typeName;
    ASSERT_TRUE(EmitterPresetRegistry::global().registerPreset(presetName, preset));
    EXPECT_TRUE(EmitterPresetRegistry::global().has(presetName));
}

// ============================================================================
// Simulation — burst
// ============================================================================

TEST(ParticleSystem, BurstSpawnsExpectedCount) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(1234);

    auto type = uniq("burst_type");
    registerSimpleType(type);
    auto preset = uniq("burst_preset");
    registerBurstPreset(preset, type, 10);

    size_t spawned = sim.burst(preset, glm::dvec3(0.0, 64.0, 0.0));
    EXPECT_EQ(spawned, 10u);
    EXPECT_EQ(sim.activeParticleCount(), 10u);
}

TEST(ParticleSystem, VelocityDistributionFollowsInitialSpeed) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(42);

    auto type = uniq("vel_type");
    ParticleTypeDef def;
    def.lifetime = Range{5.0f, 5.0f};
    def.initialSpeed = Range{2.0f, 2.0f};   // constant speed
    ParticleTypeRegistry::global().registerType(type, def);

    auto preset = uniq("vel_preset");
    registerBurstPreset(preset, type, 50);

    sim.burst(preset, glm::dvec3(0.0, 64.0, 0.0));
    auto snap = sim.snapshot();
    ASSERT_EQ(snap.particles.size(), 50u);
    for (const auto& p : snap.particles) {
        float speed = glm::length(p.velocity);
        EXPECT_NEAR(speed, 2.0f, 1e-4f);
    }
}

// ============================================================================
// Simulation — lifetime
// ============================================================================

TEST(ParticleSystem, ParticlesExpireAfterMaxAge) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(7);

    auto type = uniq("life_type");
    ParticleTypeDef def;
    def.name = type;
    def.lifetime = Range{0.5f, 0.5f};
    ParticleTypeRegistry::global().registerType(type, def);

    auto preset = uniq("life_preset");
    registerBurstPreset(preset, type, 5);

    // Burst doesn't tick — particles are just freshly spawned (age 0).
    sim.burst(preset, glm::dvec3(0.0, 64.0, 0.0));
    EXPECT_EQ(sim.activeParticleCount(), 5u);

    // Advance less than lifetime
    sim.tick(0.2f);
    EXPECT_EQ(sim.activeParticleCount(), 5u);

    // Advance past lifetime
    sim.tick(0.5f);  // total 0.7 > 0.5
    EXPECT_EQ(sim.activeParticleCount(), 0u);
}

TEST(ParticleSystem, GlobalCapEvictsOldest) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(11);
    sim.setMaxParticles(20);

    auto type = uniq("cap_type");
    registerSimpleType(type);
    auto preset = uniq("cap_preset");
    registerBurstPreset(preset, type, 15);

    sim.burst(preset, glm::dvec3(0.0));
    EXPECT_EQ(sim.activeParticleCount(), 15u);

    // Second burst pushes past cap → older particles evicted
    sim.burst(preset, glm::dvec3(10.0));
    EXPECT_EQ(sim.activeParticleCount(), 20u);
}

// ============================================================================
// Simulation — emitter duration / continuous spawn
// ============================================================================

TEST(ParticleSystem, ContinuousEmitterSpawnsOverTime) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(31);

    auto type = uniq("cont_type");
    registerSimpleType(type);

    auto presetName = uniq("cont_preset");
    EmitterPresetDef preset;
    preset.name = presetName;
    preset.shape = EmitterShape::Point;
    preset.spawnRate = 10.0f;    // 10/sec
    preset.duration = 1.0f;
    preset.particleType = type;
    EmitterPresetRegistry::global().registerPreset(presetName, preset);

    auto id = sim.spawnEmitter(presetName, glm::dvec3(0.0));
    EXPECT_NE(id, INVALID_EMITTER_ID);
    EXPECT_TRUE(sim.isEmitterActive(id));

    // Half a second should spawn about 5 particles
    sim.tick(0.5f);
    EXPECT_GE(sim.activeParticleCount(), 4u);
    EXPECT_LE(sim.activeParticleCount(), 6u);

    // Past duration, emitter expires (1.0 s total)
    sim.tick(0.6f);
    EXPECT_FALSE(sim.isEmitterActive(id));
}

TEST(ParticleSystem, DespawnEmitterStopsSpawning) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(91);

    auto type = uniq("desp_type");
    registerSimpleType(type);

    auto presetName = uniq("desp_preset");
    EmitterPresetDef preset;
    preset.shape = EmitterShape::Point;
    preset.spawnRate = 20.0f;
    preset.duration = -1.0f;     // continuous
    preset.particleType = type;
    EmitterPresetRegistry::global().registerPreset(presetName, preset);

    auto id = sim.spawnEmitter(presetName, glm::dvec3(0.0));
    EXPECT_TRUE(sim.despawnEmitter(id));
    EXPECT_FALSE(sim.isEmitterActive(id));

    sim.tick(0.5f);
    EXPECT_EQ(sim.activeParticleCount(), 0u);
}

// ============================================================================
// Voxel collision
// ============================================================================

TEST(ParticleSystem, CollideKillsParticleOnSolidBlock) {
    // Build a small world with a solid block at y=0 directly below a
    // particle falling downward.
    World world;

    // Register a generic solid block type (if not already) — BlockType defaults
    // have hasCollision() == true because the default collision shape is a
    // full cube.
    auto blockName = uniq("solid_block");
    auto blockId = BlockTypeId::fromName(blockName);
    BlockRegistry::global().registerType(blockId, BlockType{});

    world.setBlock({0, 0, 0}, blockId);

    ParticleSystem sim(world);
    sim.setSeed(5);

    auto type = uniq("collide_kill_type");
    ParticleTypeDef def;
    def.lifetime = Range{5.0f, 5.0f};
    def.initialSpeed = Range{0.0f, 0.0f};
    def.velocityBias = glm::vec3(0.0f, -5.0f, 0.0f); // straight down
    def.collidesWithTerrain = true;
    def.killOnCollision = true;
    ParticleTypeRegistry::global().registerType(type, def);

    auto preset = uniq("collide_kill_preset");
    registerBurstPreset(preset, type, 1);

    // Spawn slightly above the solid block
    sim.burst(preset, glm::dvec3(0.5, 1.5, 0.5));
    ASSERT_EQ(sim.activeParticleCount(), 1u);

    // Tick enough for the particle to cross y=0 (1.5m at 5m/s → 0.3s)
    sim.tick(0.5f);
    EXPECT_EQ(sim.activeParticleCount(), 0u);
}

TEST(ParticleSystem, ReflectPreservesParticle) {
    World world;
    auto blockName = uniq("solid_reflect");
    auto blockId = BlockTypeId::fromName(blockName);
    BlockRegistry::global().registerType(blockId, BlockType{});
    world.setBlock({0, 0, 0}, blockId);

    ParticleSystem sim(world);
    sim.setSeed(13);

    auto type = uniq("reflect_type");
    ParticleTypeDef def;
    def.lifetime = Range{5.0f, 5.0f};
    def.velocityBias = glm::vec3(0.0f, -5.0f, 0.0f);
    def.collidesWithTerrain = true;
    def.killOnCollision = false;
    def.restitution = 0.5f;
    ParticleTypeRegistry::global().registerType(type, def);

    auto preset = uniq("reflect_preset");
    registerBurstPreset(preset, type, 1);

    sim.burst(preset, glm::dvec3(0.5, 1.5, 0.5));
    sim.tick(0.5f);

    // Particle should still be alive (reflected, not killed)
    EXPECT_EQ(sim.activeParticleCount(), 1u);
    auto snap = sim.snapshot();
    ASSERT_EQ(snap.particles.size(), 1u);
    // Velocity should now be upward (reflected with damping)
    EXPECT_GT(snap.particles[0].velocity.y, 0.0f);
    // Speed magnitude should be less than original (restitution < 1)
    EXPECT_LT(std::abs(snap.particles[0].velocity.y), 5.0f);
}

TEST(ParticleSystem, AirPassesThroughWithoutCollision) {
    // No blocks placed; particle should never trigger collision path.
    World world;
    ParticleSystem sim(world);
    sim.setSeed(17);

    auto type = uniq("air_type");
    ParticleTypeDef def;
    def.lifetime = Range{5.0f, 5.0f};
    def.velocityBias = glm::vec3(0.0f, -5.0f, 0.0f);
    def.collidesWithTerrain = true;
    def.killOnCollision = true;
    ParticleTypeRegistry::global().registerType(type, def);

    auto preset = uniq("air_preset");
    registerBurstPreset(preset, type, 1);

    sim.burst(preset, glm::dvec3(0.5, 64.0, 0.5));
    sim.tick(0.5f);
    EXPECT_EQ(sim.activeParticleCount(), 1u);
}

// ============================================================================
// Event-value round-trip
// ============================================================================

TEST(ParticleEventValue, SpawnRoundTrip) {
    auto v = makeParticleSpawnValue("finevox:my_preset",
                                     glm::dvec3(1.5, 2.5, 3.5),
                                     INVALID_ENTITY_ID, 4.0f);
    EXPECT_EQ(readEventType(v), "particle_spawn");

    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readString(v, s.preset), "finevox:my_preset");
    EXPECT_DOUBLE_EQ(v.asMap().get(s.pos_x).asFloat(), 1.5);
    EXPECT_DOUBLE_EQ(v.asMap().get(s.pos_y).asFloat(), 2.5);
    EXPECT_DOUBLE_EQ(v.asMap().get(s.pos_z).asFloat(), 3.5);
    EXPECT_FLOAT_EQ(readFloat(v, s.duration), 4.0f);
}

TEST(ParticleEventValue, DespawnRoundTrip) {
    auto v = makeParticleDespawnValue(42);
    EXPECT_EQ(readEventType(v), "particle_despawn");
    EXPECT_EQ(readInt(v, EventSymbols::instance().emitter_id), 42);
}

TEST(ParticleEventValue, BurstRoundTrip) {
    auto v = makeParticleBurstValue("fx:pop", glm::dvec3(0.0, 1.0, 2.0));
    EXPECT_EQ(readEventType(v), "particle_burst");
    EXPECT_EQ(readString(v, EventSymbols::instance().preset), "fx:pop");
    EXPECT_DOUBLE_EQ(v.asMap().get(EventSymbols::instance().pos_y).asFloat(), 1.0);
}
