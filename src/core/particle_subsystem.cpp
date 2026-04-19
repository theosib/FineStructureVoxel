#include "finevox/core/particle_subsystem.hpp"
#include "finevox/core/particle_system.hpp"

namespace finevox {

void ParticleSubsystem::tick(float dt) {
    system_.tick(dt, entities_);
}

} // namespace finevox
