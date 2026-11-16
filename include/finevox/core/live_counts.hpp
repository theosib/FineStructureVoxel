#pragma once

/**
 * @file live_counts.hpp
 * @brief How many of the engine's large objects exist now, and what the process holds
 *
 * For checking that a game started and stopped leaves nothing behind (worlds, columns, subchunks,
 * entities, sessions, threads, heap), and for diagnostics. A class is counted by holding a
 * LiveCounter member, which counts copies and moves as the objects they make.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace finevox {

enum class Counted : uint8_t { World, ChunkColumn, SubChunk, Entity, GameSession, Kinds };

namespace detail {
void addLive(Counted kind, int64_t delta) noexcept;
}

/// How many objects of this kind exist now
[[nodiscard]] int64_t liveCount(Counted kind) noexcept;

/// Threads in this process, or 0 where the platform doesn't say
[[nodiscard]] size_t processThreadCount();

/// Bytes the allocator has handed out and not had back, or 0 where the platform doesn't say.
/// Steadier than resident memory, which keeps pages the allocator has cached.
[[nodiscard]] size_t heapBytesInUse();

/// A member that counts the live instances of the class holding it
template <Counted Kind>
class LiveCounter {
public:
    LiveCounter() noexcept { detail::addLive(Kind, 1); }
    LiveCounter(const LiveCounter&) noexcept { detail::addLive(Kind, 1); }
    LiveCounter& operator=(const LiveCounter&) noexcept { return *this; }
    ~LiveCounter() { detail::addLive(Kind, -1); }
};

}  // namespace finevox
