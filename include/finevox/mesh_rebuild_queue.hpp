#pragma once

#include "finevox/blocking_queue.hpp"
#include "finevox/position.hpp"

namespace finevox {

// Simple FIFO queue for mesh rebuild requests with deduplication.
//
// The render loop iterates visible chunks near-to-far, so FIFO order
// naturally provides approximate distance-based priority. Deduplication
// prevents the queue from growing unbounded when the same chunk is
// requested multiple times before being processed.
//
// Usage:
//   MeshRebuildQueue queue;
//   queue.push(pos);           // From render loop (near-to-far order)
//   auto pos = queue.popWait(); // From worker thread (blocks until available)
//   queue.shutdown();          // Signal workers to exit
//
using MeshRebuildQueue = BlockingQueue<ChunkPos>;

}  // namespace finevox
