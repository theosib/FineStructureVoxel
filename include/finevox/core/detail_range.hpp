#pragma once

#include "finevox/core/position.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace finevox {

/// Where blocks are drawn in full and where in their far forms. A block can name a far-geometry,
/// a cheaper shape for where its detail is too small to see, and a subchunk uses it once all of
/// the subchunk is further from the camera than the near distance.
///
/// Every build decides the same way, from the camera where it is when the build runs, so a block
/// edit's rebuild picks what the camera's move would. The renderer asks for a rebuild only once a
/// subchunk is well past the near distance the other way, so one on the edge doesn't keep switching
/// as the camera drifts.
struct DetailRange {
    /// In blocks. Zero or less draws everything in full.
    float nearDistance = 0.0f;

    /// How far past the near distance, as a share of it, a subchunk has to go to switch
    static constexpr float MARGIN = 0.15f;

    /// From the camera to the nearest point of a subchunk, so nothing nearer than the near distance
    /// is ever drawn far
    [[nodiscard]] static float distance(const glm::dvec3& camera, ChunkPos pos) {
        auto axis = [](double eye, int32_t chunk) {
            double low = static_cast<double>(chunk) * 16.0;
            return eye - std::clamp(eye, low, low + 16.0);
        };
        double dx = axis(camera.x, pos.x), dy = axis(camera.y, pos.y), dz = axis(camera.z, pos.z);
        return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    /// The near distance at which a block is drawn `pixels` tall, on a view `viewportHeight` pixels
    /// high with that vertical field of view. Zero pixels draws everything in full.
    [[nodiscard]] static float forPixels(float pixels, float viewportHeight, float fovYDegrees) {
        if (pixels <= 0.0f || viewportHeight <= 0.0f) return 0.0f;
        float halfHeight = std::tan(glm::radians(fovYDegrees) * 0.5f);
        return viewportHeight / (2.0f * halfHeight * pixels);
    }

    /// What a build at this distance draws
    [[nodiscard]] bool detailed(float distance) const {
        return nearDistance <= 0.0f || distance <= nearDistance;
    }

    /// Whether a subchunk built one way should be built again the other
    [[nodiscard]] bool wantsRebuild(float distance, bool builtDetailed) const {
        if (nearDistance <= 0.0f) return !builtDetailed;
        if (builtDetailed) return distance > nearDistance * (1.0f + MARGIN);
        return distance < nearDistance * (1.0f - MARGIN);
    }
};

}  // namespace finevox
