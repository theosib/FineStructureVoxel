#pragma once

/**
 * @file particle_subsystem.hpp
 * @brief GameSubsystem wrapper that ticks a ParticleSystem each game tick.
 */

#include "finevox/core/game_subsystem.hpp"

#include <string_view>

namespace finevox {

class ParticleSystem;
class EntityManager;

class ParticleSubsystem : public GameSubsystem {
public:
    ParticleSubsystem(ParticleSystem& system, EntityManager* entities = nullptr)
        : system_(system), entities_(entities) {}

    [[nodiscard]] std::string_view name() const override { return "Particles"; }

    // LateTick so attached-emitter positions see entity physics from PostTick.
    [[nodiscard]] TickPhase phase() const override { return TickPhase::LateTick; }
    [[nodiscard]] int32_t priority() const override { return 50; }

    void tick(float dt) override;

private:
    ParticleSystem& system_;
    EntityManager* entities_ = nullptr;
};

} // namespace finevox
