#pragma once

/**
 * @file mesh_rebuild_queue.hpp
 * @brief Priority queue for mesh rebuild scheduling
 *
 * Design: [06-rendering.md] §6.3 Priority Queue
 */

#include "finevox/keyed_queue.hpp"
#include "finevox/position.hpp"
#include "finevox/lod.hpp"
#include <cstdint>
#include <limits>

namespace finevox {

// ============================================================================
// MeshRebuildRequest - Request data for mesh generation
// ============================================================================

/// Request to rebuild mesh for a subchunk
/// Contains priority information and target version for version-based updates
struct MeshRebuildRequest {
    /// Target block version to build against
    /// If the SubChunk's version changes before processing, we build that instead
    uint64_t targetVersion = 0;

    /// Target light version to build against
    uint64_t targetLightVersion = 0;

    /// Priority for rebuild queue (lower = more urgent)
    /// Typical values:
    /// - 0-99: Immediate (player-initiated changes, visible chunks)
    /// - 100-999: Normal (newly loaded chunks)
    /// - 1000+: Background (proactive rebuilds)
    uint32_t priority = 100;

    /// Requested LOD level with hysteresis encoding
    /// Uses 2x encoding: even values (0,2,4,6,8) are exact LOD matches,
    /// odd values (1,3,5,7) accept either neighboring LOD level.
    /// The buildLevel() method returns the actual LOD level to build.
    LODRequest lodRequest = LODRequest::exact(LODLevel::LOD0);

    /// Default constructor
    MeshRebuildRequest() = default;

    /// Create request with specific priority, versions, and LOD request
    MeshRebuildRequest(uint64_t blockVersion, uint64_t lightVersion, uint32_t prio, LODRequest lod)
        : targetVersion(blockVersion), targetLightVersion(lightVersion), priority(prio), lodRequest(lod) {}

    /// Create request with specific priority, versions, and exact LOD level
    MeshRebuildRequest(uint64_t blockVersion, uint64_t lightVersion, uint32_t prio, LODLevel lod = LODLevel::LOD0)
        : targetVersion(blockVersion), targetLightVersion(lightVersion), priority(prio), lodRequest(LODRequest::exact(lod)) {}

    /// High priority for immediate rebuild (player action, visible change)
    static MeshRebuildRequest immediate(uint64_t blockVersion, uint64_t lightVersion, LODRequest lod) {
        return MeshRebuildRequest(blockVersion, lightVersion, 0, lod);
    }
    static MeshRebuildRequest immediate(uint64_t blockVersion, uint64_t lightVersion, LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(blockVersion, lightVersion, 0, LODRequest::exact(lod));
    }

    /// Normal priority for regular rebuilds
    static MeshRebuildRequest normal(uint64_t blockVersion, uint64_t lightVersion, LODRequest lod) {
        return MeshRebuildRequest(blockVersion, lightVersion, 100, lod);
    }
    static MeshRebuildRequest normal(uint64_t blockVersion, uint64_t lightVersion, LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(blockVersion, lightVersion, 100, LODRequest::exact(lod));
    }

    /// Low priority for background/proactive rebuilds
    static MeshRebuildRequest background(uint64_t blockVersion, uint64_t lightVersion, LODRequest lod) {
        return MeshRebuildRequest(blockVersion, lightVersion, 1000, lod);
    }
    static MeshRebuildRequest background(uint64_t blockVersion, uint64_t lightVersion, LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(blockVersion, lightVersion, 1000, LODRequest::exact(lod));
    }
};

// ============================================================================
// MeshRebuildQueue - Priority queue with version tracking
// ============================================================================

/// Merge function for mesh rebuild requests:
/// - Keep the higher urgency (lower priority number)
/// - Update target versions to latest
inline MeshRebuildRequest mergeMeshRebuildRequest(
    const MeshRebuildRequest& existing,
    const MeshRebuildRequest& newReq)
{
    return MeshRebuildRequest(
        newReq.targetVersion,       // Always use latest block version
        newReq.targetLightVersion,  // Always use latest light version
        std::min(existing.priority, newReq.priority),  // Keep highest urgency
        newReq.lodRequest  // Use latest LOD request
    );
}

/// Thread-safe mesh rebuild queue with priority, version tracking, and alarm support.
///
/// Features:
/// - Deduplication by ChunkPos (same chunk = merged request)
/// - Priority merging (keeps highest urgency when re-queued)
/// - Version tracking (builds against latest SubChunk version)
/// - Alarm-based wakeup for frame-synchronized background scanning
/// - Non-popping wait (waitForWork) for efficient worker loops
/// - WakeSignal attachment for multi-queue coordination
///
/// Usage:
///   MeshRebuildQueue queue;
///   queue.push(pos, MeshRebuildRequest::immediate(version));
///
///   // Worker thread loop:
///   while (running) {
///       if (auto req = queue.tryPop()) {
///           process(*req);
///           continue;
///       }
///       // No explicit work - block until push, alarm, or shutdown
///       queue.waitForWork();
///   }
///
///   // Graphics thread (once per frame, when no explicit work queued):
///   queue.setAlarm(now + halfFrameTime);
///
///   queue.shutdown();
///
using MeshRebuildQueue = KeyedQueue<ChunkPos, MeshRebuildRequest>;

/// Create a MeshRebuildQueue with proper merge semantics
inline MeshRebuildQueue createMeshRebuildQueue() {
    return MeshRebuildQueue(mergeMeshRebuildRequest);
}

}  // namespace finevox
