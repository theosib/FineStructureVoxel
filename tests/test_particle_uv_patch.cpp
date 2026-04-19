#include <gtest/gtest.h>

#include "finevox/core/particle.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/particle_type.hpp"
#include "finevox/core/world.hpp"

using namespace finevox;

// Verifies the 26-B contract between ParticleAtlas and the simulator:
// whoever patches ParticleTypeDef::atlasUV (the atlas, at startup) must see
// that UV flow through to freshly-spawned particles.  The atlas itself
// needs a GPU device to load, so we exercise the contract through
// ParticleTypeRegistry::setAtlasUV directly — same method the atlas uses.

namespace {
int gUniq = 0;
std::string uniq(const char* base) {
    return std::string(base) + "_uv_" + std::to_string(++gUniq);
}
} // anon

TEST(ParticleUVPatch, SetAtlasUVFlowsToSpawnedParticles) {
    auto name = uniq("uv_flow");

    ParticleTypeDef def;
    def.name = name;
    def.lifetime = Range{5.0f, 5.0f};
    def.size = Range{0.1f, 0.1f};
    ASSERT_TRUE(ParticleTypeRegistry::global().registerType(name, def));

    // Patch the UV rect — this is exactly what ParticleAtlas::applyToRegistry
    // does for every type whose atlasRegion matches a packed PNG.
    glm::vec4 wantedUV(0.25f, 0.50f, 0.375f, 0.625f);
    ASSERT_TRUE(ParticleTypeRegistry::global().setAtlasUV(name, wantedUV));

    // Spawn via a trivial burst preset.
    auto presetName = uniq("uv_preset");
    EmitterPresetDef preset;
    preset.name = presetName;
    preset.shape = EmitterShape::Point;
    preset.burstCount = 3;
    preset.duration = 0.0f;
    preset.particleType = name;
    ASSERT_TRUE(EmitterPresetRegistry::global().registerPreset(presetName, preset));

    World world;
    ParticleSystem sim(world);
    sim.setSeed(5);
    ASSERT_EQ(sim.burst(presetName, glm::dvec3(0.0, 64.0, 0.0)), 3u);

    auto snap = sim.snapshot();
    ASSERT_EQ(snap.particles.size(), 3u);
    for (const auto& p : snap.particles) {
        EXPECT_FLOAT_EQ(p.atlasUV.x, wantedUV.x);
        EXPECT_FLOAT_EQ(p.atlasUV.y, wantedUV.y);
        EXPECT_FLOAT_EQ(p.atlasUV.z, wantedUV.z);
        EXPECT_FLOAT_EQ(p.atlasUV.w, wantedUV.w);
    }
}

TEST(ParticleUVPatch, UnknownTypeReturnsFalse) {
    EXPECT_FALSE(ParticleTypeRegistry::global().setAtlasUV(
        "does_not_exist_anywhere", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));
}

TEST(ParticleUVPatch, DefaultUVIsFullTexture) {
    auto name = uniq("uv_default");
    ParticleTypeDef def;
    def.name = name;
    def.lifetime = Range{5.0f, 5.0f};
    ASSERT_TRUE(ParticleTypeRegistry::global().registerType(name, def));

    const auto* fetched = ParticleTypeRegistry::global().get(name);
    ASSERT_NE(fetched, nullptr);
    EXPECT_FLOAT_EQ(fetched->atlasUV.x, 0.0f);
    EXPECT_FLOAT_EQ(fetched->atlasUV.y, 0.0f);
    EXPECT_FLOAT_EQ(fetched->atlasUV.z, 1.0f);
    EXPECT_FLOAT_EQ(fetched->atlasUV.w, 1.0f);
}
