#include "finevox/core/live_counts.hpp"

#include <array>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FINEVOX_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define FINEVOX_ASAN 1
#endif
#ifdef FINEVOX_ASAN
#include <sanitizer/allocator_interface.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <filesystem>
#include <malloc.h>
#endif

namespace finevox {

namespace {

std::array<std::atomic<int64_t>, static_cast<size_t>(Counted::Kinds)>& counters() {
    // Never destroyed: objects in statics may still count down as the process exits
    static auto* instance = new std::array<std::atomic<int64_t>, static_cast<size_t>(Counted::Kinds)>{};
    return *instance;
}

}  // namespace

void detail::addLive(Counted kind, int64_t delta) noexcept {
    counters()[static_cast<size_t>(kind)].fetch_add(delta, std::memory_order_relaxed);
}

int64_t liveCount(Counted kind) noexcept {
    return counters()[static_cast<size_t>(kind)].load(std::memory_order_relaxed);
}

size_t processThreadCount() {
#if defined(__APPLE__)
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) return 0;
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                  sizeof(thread_t) * count);
    return count;
#elif defined(__linux__)
    std::error_code ec;
    size_t count = 0;
    for (auto it = std::filesystem::directory_iterator("/proc/self/task", ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        ++count;
    }
    return ec ? 0 : count;
#else
    return 0;
#endif
}

size_t heapBytesInUse() {
#if defined(FINEVOX_ASAN)
    // The sanitizer's allocator holds freed blocks back (its quarantine), which the platform's
    // figures count as in use; this is what the program itself still holds
    return __sanitizer_get_current_allocated_bytes();
#elif defined(__APPLE__)
    malloc_statistics_t stats{};
    malloc_zone_statistics(nullptr, &stats);
    return stats.size_in_use;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    struct mallinfo2 info = mallinfo2();
    return info.uordblks + info.hblkhd;
#else
    return 0;
#endif
}

}  // namespace finevox
