#pragma once

#include "finevox/alarm_queue.hpp"
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

    /// Create request with specific priority, version, and LOD request
    MeshRebuildRequest(uint64_t version, uint32_t prio, LODRequest lod)
        : targetVersion(version), priority(prio), lodRequest(lod) {}

    /// Create request with specific priority, version, and exact LOD level
    MeshRebuildRequest(uint64_t version, uint32_t prio, LODLevel lod = LODLevel::LOD0)
        : targetVersion(version), priority(prio), lodRequest(LODRequest::exact(lod)) {}

    /// High priority for immediate rebuild (player action, visible change)
    static MeshRebuildRequest immediate(uint64_t version, LODRequest lod) {
        return MeshRebuildRequest(version, 0, lod);
    }
    static MeshRebuildRequest immediate(uint64_t version, LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(version, 0, LODRequest::exact(lod));
    }

    /// Normal priority for regular rebuilds
    static MeshRebuildRequest normal(uint64_t version, LODRequest lod) {
        return MeshRebuildRequest(version, 100, lod);
    }
    static MeshRebuildRequest normal(uint64_t version, LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(version, 100, LODRequest::exact(lod));
    }

    /// Low priority for background/proactive rebuilds
    static MeshRebuildRequest background(uint64_t version, LODRequest lod) {
        return MeshRebuildRequest(version, 1000, lod);
    }
    static MeshRebuildRequest background(uint64_t version, LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(version, 1000, LODRequest::exact(lod));
    }
};

// ============================================================================
// MeshRebuildQueue - Priority queue with version tracking
// ============================================================================

/// Merge function for mesh rebuild requests:
/// - Keep the higher urgency (lower priority number)
/// - Update target version to latest
inline MeshRebuildRequest mergeMeshRebuildRequest(
    const MeshRebuildRequest& existing,
    const MeshRebuildRequest& newReq)
{
    return MeshRebuildRequest(
        newReq.targetVersion,  // Always use latest version
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
using MeshRebuildQueue = AlarmQueueWithData<ChunkPos, MeshRebuildRequest>;

/// Create a MeshRebuildQueue with proper merge semantics
inline MeshRebuildQueue createMeshRebuildQueue() {
    return MeshRebuildQueue(mergeMeshRebuildRequest);
}

}  // namespace finevox
