#pragma once

/**
 * @file distances.hpp
 * @brief Distance zone calculations for rendering, loading, and processing
 *
 * Design: [23-distance-and-loading.md] §23.1 Distance Zones
 */

#include <glm/glm.hpp>

namespace finevox {

// Forward declaration
class LODConfig;

// ============================================================================
// FogConfig - Fog rendering configuration
// ============================================================================

/// Configuration for distance-based fog rendering
struct FogConfig {
    bool enabled = true;                    // Enable fog rendering
    float startDistance = 200.0f;           // Where fog begins (0% density)
    float endDistance = 256.0f;             // Where fog is complete (100% density)
    glm::vec3 color = {0.7f, 0.8f, 0.9f};   // Fog color (sky-like default)
    bool dynamicColor = true;               // Tie fog color to sky color

    /// Calculate fog factor for a given distance (0.0 = no fog, 1.0 = full fog)
    [[nodiscard]] float getFogFactor(float distance) const {
        if (!enabled) return 0.0f;
        if (distance <= startDistance) return 0.0f;
        if (distance >= endDistance) return 1.0f;
        return (distance - startDistance) / (endDistance - startDistance);
    }
};

// ============================================================================
// RenderDistanceConfig - Rendering distance configuration
// ============================================================================

/// Configuration for rendering distances
struct RenderDistanceConfig {
    float chunkRenderDistance = 256.0f;     // Max chunk render distance in blocks
    float entityRenderDistance = 128.0f;    // Entity visibility distance in blocks

    // Hysteresis for unloading (prevents thrashing)
    float unloadMultiplier = 1.2f;          // Unload at distance * this
};

// ============================================================================
// LoadingDistanceConfig - Chunk loading distance configuration
// ============================================================================

/// Configuration for chunk loading distances
struct LoadingDistanceConfig {
    float loadDistance = 384.0f;            // Keep chunks loaded within this distance
    float unloadHysteresis = 32.0f;         // Extra buffer before unloading

    /// Get the effective unload distance (loadDistance + hysteresis)
    [[nodiscard]] float unloadDistance() const {
        return loadDistance + unloadHysteresis;
    }
};

// ============================================================================
// ProcessingDistanceConfig - Processing distance configuration
// ============================================================================

/// Configuration for processing distances (block updates, entity AI)
/// Note: Game layer sets policy; engine enforces distances
struct ProcessingDistanceConfig {
    float blockUpdateDistance = 128.0f;     // Block update processing range
    float entityProcessDistance = 192.0f;   // Entity AI/physics range
    float simulationDistance = 512.0f;      // Maximum processing range
};

// ============================================================================
// DistanceConfig - Combined distance configuration
// ============================================================================

/// Master configuration for all distance-based systems
/// This is the single source of truth for distance thresholds.
struct DistanceConfig {
    RenderDistanceConfig rendering;
    FogConfig fog;
    LoadingDistanceConfig loading;
    ProcessingDistanceConfig processing;

    /// Global hysteresis scale multiplier
    /// Applied to all hysteresis values (e.g., 0.5 = half hysteresis, 2.0 = double)
    float hysteresisScale = 1.0f;

    /// Convenience: get effective chunk render distance
    [[nodiscard]] float chunkRenderDistance() const {
        return rendering.chunkRenderDistance;
    }

    /// Convenience: get effective entity render distance
    [[nodiscard]] float entityRenderDistance() const {
        return rendering.entityRenderDistance;
    }

    /// Convenience: get fog start distance
    [[nodiscard]] float fogStartDistance() const {
        return fog.startDistance;
    }

    /// Convenience: get fog end distance
    [[nodiscard]] float fogEndDistance() const {
        return fog.endDistance;
    }

    /// Convenience: check if fog is enabled
    [[nodiscard]] bool fogEnabled() const {
        return fog.enabled;
    }

    /// Calculate fog factor for a given distance
    [[nodiscard]] float getFogFactor(float distance) const {
        return fog.getFogFactor(distance);
    }

    /// Validate and clamp all distance values to sensible ranges
    void validate() {
        // Ensure render distance is positive
        if (rendering.chunkRenderDistance < 16.0f) {
            rendering.chunkRenderDistance = 16.0f;
        }

        // Ensure fog distances are within render distance
        if (fog.endDistance > rendering.chunkRenderDistance) {
            fog.endDistance = rendering.chunkRenderDistance;
        }
        if (fog.startDistance > fog.endDistance) {
            fog.startDistance = fog.endDistance * 0.75f;
        }

        // Ensure loading distance >= render distance
        if (loading.loadDistance < rendering.chunkRenderDistance) {
            loading.loadDistance = rendering.chunkRenderDistance;
        }

        // Ensure hysteresis is positive
        if (loading.unloadHysteresis < 0.0f) {
            loading.unloadHysteresis = 0.0f;
        }

        // Ensure unload multiplier is >= 1.0
        if (rendering.unloadMultiplier < 1.0f) {
            rendering.unloadMultiplier = 1.0f;
        }
    }
};

}  // namespace finevox
