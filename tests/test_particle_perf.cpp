#include <gtest/gtest.h>

#include "finevox/core/particle.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/particle_type.hpp"
#include "finevox/core/world.hpp"

#include <chrono>
#include <cstdio>

using namespace finevox;

// Rough CPU-side profile: useful only as a smoke/budget check.
// Captures the wall-clock cost of ticking N particles over K ticks.
// Not part of CI correctness — the test itself always passes; use the
// printed numbers to judge whether GPU compute is worth building.
TEST(ParticlePerf, TickBudget) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(2026);
    sim.setMaxParticles(8192);

    // Long-lived particle to populate the vector without early dying.
    ParticleTypeDef type;
    type.name = "perf:type";
    type.lifetime = Range{100.0f, 100.0f};
    type.initialSpeed = Range{0.0f, 0.2f};
    type.gravity = 1.0f;
    type.drag = 0.05f;
    ParticleTypeRegistry::global().registerType("perf:type", type);

    EmitterPresetDef preset;
    preset.name = "perf:burst";
    preset.shape = EmitterShape::Sphere;
    preset.shapeSize = glm::vec3(1.0f);
    preset.burstCount = 8192;
    preset.duration = 0.0f;
    preset.particleType = "perf:type";
    EmitterPresetRegistry::global().registerPreset("perf:burst", preset);

    sim.burst("perf:burst", glm::dvec3(0.0, 64.0, 0.0));
    ASSERT_EQ(sim.activeParticleCount(), 8192u);

    constexpr int ticks = 30;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < ticks; ++i) {
        sim.tick(1.0f / 30.0f);
    }
    auto t1 = std::chrono::steady_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double perTickMs = totalMs / ticks;
    std::printf("\n[perf] 8192 particles × 30 ticks: %.2f ms total, %.3f ms/tick "
                "(budget at 30 TPS = 33.33 ms)\n",
                totalMs, perTickMs);

    // The test always passes — we just want visibility into the cost.
    SUCCEED();
}
