#pragma once

/**
 * @file game_event.hpp
 * @brief Type-erased event system with Small Buffer Optimization
 *
 * GameEventHolder stores any event type inline (up to 96 bytes) or on the heap.
 * EventTypeId provides runtime type identification via a static counter.
 *
 * Usage:
 *   auto holder = GameEventHolder::create<BlockPlacedEvent>(pos, newType, oldType);
 *   if (auto* e = holder.tryGet<BlockPlacedEvent>()) { ... }
 */

#include <atomic>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace finevox {

/// Unique runtime type identifier for event types
using EventTypeId = uint32_t;
constexpr EventTypeId INVALID_EVENT_TYPE = 0;

namespace detail {

/// Global counter for generating unique EventTypeIds
inline std::atomic<EventTypeId> nextEventTypeId{1};

/// Get or assign a unique EventTypeId for type T
template<typename T>
EventTypeId eventTypeIdFor() {
    static const EventTypeId id = nextEventTypeId.fetch_add(1, std::memory_order_relaxed);
    return id;
}

} // namespace detail

/// Register/retrieve the EventTypeId for a concrete event type.
/// Thread-safe. Idempotent: same type always returns same ID.
template<typename T>
EventTypeId registerEventType() {
    return detail::eventTypeIdFor<T>();
}

// ============================================================================
// EventConsolidation trait
// ============================================================================

/**
 * @brief Trait for event consolidation (merging duplicate events)
 *
 * Events that support consolidation (e.g., NeighborUpdatedEvent merging
 * face masks) specialize this trait. The EventOutbox uses it to merge
 * events with the same key.
 *
 * Default: no consolidation.
 */
template<typename T>
struct EventConsolidation {
    static constexpr bool supported = false;
};

// ============================================================================
// GameEventHolder - Type-erased event container with SBO
// ============================================================================

/**
 * @brief Type-erased event container with small-buffer optimization
 *
 * Stores any registered event type. Events up to SBO_SIZE bytes are stored
 * inline (no heap allocation). Larger events spill to the heap.
 *
 * Move-only. Events flow through queues without copying.
 *
 * Thread safety: NOT thread-safe. Queues provide synchronization.
 */
class GameEventHolder {
public:
    static constexpr size_t SBO_SIZE = 128;
    static constexpr size_t SBO_ALIGN = alignof(std::max_align_t);

    GameEventHolder() = default;

    ~GameEventHolder() {
        destroy();
    }

    // Move-only
    GameEventHolder(GameEventHolder&& other) noexcept
        : typeId_(other.typeId_), ops_(other.ops_), isInline_(other.isInline_) {
        if (other.typeId_ == INVALID_EVENT_TYPE) return;

        if (other.isInline_) {
            ops_->moveConstruct(storage_.inline_, other.storage_.inline_);
        } else {
            storage_.heap_ = other.storage_.heap_;
            other.storage_.heap_ = nullptr;
        }
        other.typeId_ = INVALID_EVENT_TYPE;
        other.ops_ = nullptr;
    }

    GameEventHolder& operator=(GameEventHolder&& other) noexcept {
        if (this != &other) {
            destroy();
            typeId_ = other.typeId_;
            ops_ = other.ops_;
            isInline_ = other.isInline_;

            if (other.typeId_ != INVALID_EVENT_TYPE) {
                if (other.isInline_) {
                    ops_->moveConstruct(storage_.inline_, other.storage_.inline_);
                } else {
                    storage_.heap_ = other.storage_.heap_;
                    other.storage_.heap_ = nullptr;
                }
            }
            other.typeId_ = INVALID_EVENT_TYPE;
            other.ops_ = nullptr;
        }
        return *this;
    }

    GameEventHolder(const GameEventHolder&) = delete;
    GameEventHolder& operator=(const GameEventHolder&) = delete;

    /// Construct a holder containing a T, built in-place
    template<typename T, typename... Args>
    static GameEventHolder create(Args&&... args) {
        static_assert(std::is_move_constructible_v<T>,
            "Event types must be move-constructible");

        GameEventHolder holder;
        holder.typeId_ = registerEventType<T>();
        holder.ops_ = &opsFor<T>();

        if constexpr (sizeof(T) <= SBO_SIZE && alignof(T) <= SBO_ALIGN) {
            new (holder.storage_.inline_) T(std::forward<Args>(args)...);
            holder.isInline_ = true;
        } else {
            holder.storage_.heap_ = new T(std::forward<Args>(args)...);
            holder.isInline_ = false;
        }
        return holder;
    }

    /// Get the event's type ID
    [[nodiscard]] EventTypeId typeId() const { return typeId_; }

    /// Check if this holder contains an event
    [[nodiscard]] bool hasValue() const { return typeId_ != INVALID_EVENT_TYPE; }

    /// Check if this holder contains a specific event type
    template<typename T>
    [[nodiscard]] bool is() const {
        return typeId_ == registerEventType<T>();
    }

    /// Get a reference to the stored event (UB if wrong type)
    template<typename T>
    [[nodiscard]] T& get() {
        return *static_cast<T*>(data());
    }

    template<typename T>
    [[nodiscard]] const T& get() const {
        return *static_cast<const T*>(data());
    }

    /// Get a pointer to the stored event (nullptr if wrong type)
    template<typename T>
    [[nodiscard]] T* tryGet() {
        if (!is<T>()) return nullptr;
        return static_cast<T*>(data());
    }

    template<typename T>
    [[nodiscard]] const T* tryGet() const {
        if (!is<T>()) return nullptr;
        return static_cast<const T*>(data());
    }

    /// Get raw pointer to stored data
    [[nodiscard]] void* data() {
        return isInline_ ? static_cast<void*>(storage_.inline_)
                         : storage_.heap_;
    }

    [[nodiscard]] const void* data() const {
        return isInline_ ? static_cast<const void*>(storage_.inline_)
                         : storage_.heap_;
    }

private:
    struct Ops {
        void (*destroy)(void*);
        void (*moveConstruct)(void* dst, void* src);
        size_t size;
    };

    template<typename T>
    static const Ops& opsFor() {
        static const Ops ops = {
            // destroy
            [](void* p) { static_cast<T*>(p)->~T(); },
            // moveConstruct
            [](void* dst, void* src) {
                new (dst) T(std::move(*static_cast<T*>(src)));
            },
            sizeof(T)
        };
        return ops;
    }

    void destroy() {
        if (typeId_ == INVALID_EVENT_TYPE) return;

        if (isInline_) {
            ops_->destroy(storage_.inline_);
        } else {
            if (storage_.heap_) {
                ops_->destroy(storage_.heap_);
                ::operator delete(storage_.heap_);
            }
        }
        typeId_ = INVALID_EVENT_TYPE;
        ops_ = nullptr;
    }

    EventTypeId typeId_ = INVALID_EVENT_TYPE;
    const Ops* ops_ = nullptr;
    bool isInline_ = true;

    union Storage {
        alignas(SBO_ALIGN) char inline_[SBO_SIZE];
        void* heap_;
        Storage() : heap_(nullptr) {}
    } storage_;
};

} // namespace finevox
