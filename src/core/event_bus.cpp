#include "finevox/core/event_bus.hpp"

namespace finevox {

EventSubscription EventBus::subscribeRaw(EventTypeId typeId, RawHandler handler) {
    EventSubscription id = nextSubscriptionId_++;
    subscribers_[typeId].push_back({id, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(EventSubscription sub) {
    for (auto& [typeId, entries] : subscribers_) {
        auto it = std::find_if(entries.begin(), entries.end(),
            [sub](const SubscriberEntry& e) { return e.id == sub; });
        if (it != entries.end()) {
            entries.erase(it);
            return;
        }
    }
}

void EventBus::publishRaw(EventTypeId typeId, const void* data) {
    auto it = subscribers_.find(typeId);
    if (it == subscribers_.end()) return;

    for (const auto& entry : it->second) {
        entry.handler(data);
    }
}

void EventBus::publish(const GameEventHolder& holder) {
    if (!holder.hasValue()) return;
    publishRaw(holder.typeId(), holder.data());
}

size_t EventBus::dispatchQueued() {
    auto events = crossThreadQueue_.drainAll();
    for (const auto& holder : events) {
        publish(holder);
    }
    return events.size();
}

void EventBus::shutdown() {
    crossThreadQueue_.shutdown();
}

size_t EventBus::subscriberCount() const {
    size_t total = 0;
    for (const auto& [typeId, entries] : subscribers_) {
        total += entries.size();
    }
    return total;
}

size_t EventBus::subscriberCount(EventTypeId typeId) const {
    auto it = subscribers_.find(typeId);
    if (it == subscribers_.end()) return 0;
    return it->second.size();
}

size_t EventBus::queuedEventCount() const {
    return crossThreadQueue_.size();
}

} // namespace finevox
