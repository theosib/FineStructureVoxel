#pragma once

/**
 * @file mesh_rebuild_queue.hpp
 * @brief Priority queue for mesh rebuild scheduling
 *
 * Design: [06-rendering.md] §6.3 Priority Queue
 */

#include "finevox/core/keyed_queue.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/lod.hpp"
#include <cstdint>
#include <limits>

namespace finevox {

// ============================================================================
// MeshRebuildRequest - Request data for mesh generation
// ============================================================================

/// Request to rebuild mesh for a subchunk.
/// Pure push-based: no version tracking needed. If a chunk is modified during
/// a mesh build, the modifier pushes a new request; the consolidating queue
/// merges duplicates.
struct MeshRebuildRequest {
    /// Priority for rebuild queue (lower = more urgent)
    /// Typical values:
    /// - 0-99: Immediate (player-initiated changes, visible chunks)
    /// - 100-999: Normal (newly loaded chunks)
    /// - 1000+: Background (proactive rebuilds, safety net)
    uint32_t priority = 100;

    /// Requested LOD level with hysteresis encoding
    /// Uses 2x encoding: even values (0,2,4,6,8) are exact LOD matches,
    /// odd values (1,3,5,7) accept either neighboring LOD level.
    /// The buildLevel() method returns the actual LOD level to build.
    LODRequest lodRequest = LODRequest::exact(LODLevel::LOD0);

    /// Default constructor
    MeshRebuildRequest() = default;

    /// Create request with specific priority and LOD request
    MeshRebuildRequest(uint32_t prio, LODRequest lod)
        : priority(prio), lodRequest(lod) {}

    /// Create request with specific priority and exact LOD level
    explicit MeshRebuildRequest(uint32_t prio, LODLevel lod = LODLevel::LOD0)
        : priority(prio), lodRequest(LODRequest::exact(lod)) {}

    /// High priority for immediate rebuild (player action, visible change)
    static MeshRebuildRequest immediate(LODRequest lod) {
        return MeshRebuildRequest(0, lod);
    }
    static MeshRebuildRequest immediate(LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(0, lod);
    }

    /// Normal priority for regular rebuilds
    static MeshRebuildRequest normal(LODRequest lod) {
        return MeshRebuildRequest(100, lod);
    }
    static MeshRebuildRequest normal(LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(100, lod);
    }

    /// Low priority for background/proactive rebuilds
    static MeshRebuildRequest background(LODRequest lod) {
        return MeshRebuildRequest(1000, lod);
    }
    static MeshRebuildRequest background(LODLevel lod = LODLevel::LOD0) {
        return MeshRebuildRequest(1000, lod);
    }
};

// ============================================================================
// MeshRebuildQueue - Priority queue with deduplication
// ============================================================================

/// Merge function for mesh rebuild requests:
/// - Keep the higher urgency (lower priority number)
/// - Use latest LOD request
inline MeshRebuildRequest mergeMeshRebuildRequest(
    const MeshRebuildRequest& existing,
    const MeshRebuildRequest& newReq)
{
    return MeshRebuildRequest(
        std::min(existing.priority, newReq.priority),  // Keep highest urgency
        newReq.lodRequest  // Use latest LOD request
    );
}

/// Thread-safe mesh rebuild queue with priority and alarm support.
///
/// Features:
/// - Deduplication by ChunkPos (same chunk = merged request)
/// - Priority merging (keeps highest urgency when re-queued)
/// - Alarm-based wakeup for frame-synchronized background scanning
/// - Non-popping wait (waitForWork) for efficient worker loops
/// - WakeSignal attachment for multi-queue coordination
///
/// Usage:
///   MeshRebuildQueue queue;
///   queue.push(pos, MeshRebuildRequest::immediate());
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
///   queue.shutdown();
///
using MeshRebuildQueue = KeyedQueue<ChunkPos, MeshRebuildRequest>;

/// Create a MeshRebuildQueue with proper merge semantics
inline MeshRebuildQueue createMeshRebuildQueue() {
    return MeshRebuildQueue(mergeMeshRebuildRequest);
}

}  // namespace finevox
