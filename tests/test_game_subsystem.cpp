#include <gtest/gtest.h>
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/event_bus.hpp"
#include "finevox/core/block_events.hpp"
#include "finevox/core/world.hpp"

#include <vector>
#include <string>

using namespace finevox;

// ============================================================================
// Test subsystem that records tick calls
// ============================================================================

class RecordingSubsystem : public GameSubsystem {
public:
    RecordingSubsystem(std::string n, TickPhase p, int32_t pri,
                       std::vector<std::string>& log)
        : name_(std::move(n)), phase_(p), priority_(pri), log_(log) {}

    std::string_view name() const override { return name_; }
    TickPhase phase() const override { return phase_; }
    int32_t priority() const override { return priority_; }

    void onAttach(GameSession& session, EventBus& bus) override {
        log_.push_back(name_ + ":attach");
        attached_ = true;
    }

    void onDetach() override {
        log_.push_back(name_ + ":detach");
        attached_ = false;
    }

    void tick(float dt) override {
        log_.push_back(name_ + ":tick");
        tickCount_++;
        lastDt_ = dt;
    }

    bool attached_ = false;
    int tickCount_ = 0;
    float lastDt_ = 0.0f;

private:
    std::string name_;
    TickPhase phase_;
    int32_t priority_;
    std::vector<std::string>& log_;
};

// ============================================================================
// TickPhase enum tests
// ============================================================================

TEST(GameSubsystemTest, TickPhaseOrdering) {
    EXPECT_LT(static_cast<uint8_t>(TickPhase::PreTick),
              static_cast<uint8_t>(TickPhase::Tick));
    EXPECT_LT(static_cast<uint8_t>(TickPhase::Tick),
              static_cast<uint8_t>(TickPhase::PostTick));
    EXPECT_LT(static_cast<uint8_t>(TickPhase::PostTick),
              static_cast<uint8_t>(TickPhase::LateTick));
}

// ============================================================================
// Subsystem lifecycle tests
// ============================================================================

TEST(GameSubsystemTest, OnAttachCalledOnAdd) {
    // log must be declared before session so it outlives the session destructor
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto sub = std::make_shared<RecordingSubsystem>(
        "TestSub", TickPhase::Tick, 0, log);

    EXPECT_FALSE(sub->attached_);
    session->addSubsystem(sub);
    EXPECT_TRUE(sub->attached_);
    EXPECT_EQ(log.back(), "TestSub:attach");
}

TEST(GameSubsystemTest, OnDetachCalledOnRemove) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto sub = std::make_shared<RecordingSubsystem>(
        "TestSub", TickPhase::Tick, 0, log);

    session->addSubsystem(sub);
    session->removeSubsystem(sub);
    EXPECT_FALSE(sub->attached_);
    EXPECT_EQ(log.back(), "TestSub:detach");
}

TEST(GameSubsystemTest, OnDetachCalledOnDestruction) {
    std::vector<std::string> log;
    auto sub = std::make_shared<RecordingSubsystem>(
        "TestSub", TickPhase::Tick, 0, log);

    {
        auto session = GameSession::createLocal();
        session->addSubsystem(sub);
        EXPECT_TRUE(sub->attached_);
    }
    // Session destroyed - onDetach should have been called
    EXPECT_FALSE(sub->attached_);
}

// ============================================================================
// Tick ordering tests
// ============================================================================

TEST(GameSubsystemTest, SubsystemsTickedInPhaseOrder) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto post = std::make_shared<RecordingSubsystem>(
        "Post", TickPhase::PostTick, 0, log);
    auto pre = std::make_shared<RecordingSubsystem>(
        "Pre", TickPhase::PreTick, 0, log);
    auto tick = std::make_shared<RecordingSubsystem>(
        "Tick", TickPhase::Tick, 0, log);

    // Add in deliberate wrong order
    session->addSubsystem(post);
    session->addSubsystem(pre);
    session->addSubsystem(tick);

    // Clear attach logs
    log.clear();

    session->tick(1.0f / 30.0f);

    // Find the tick entries
    std::vector<std::string> tickEntries;
    for (const auto& entry : log) {
        if (entry.find(":tick") != std::string::npos) {
            tickEntries.push_back(entry);
        }
    }

    auto findIndex = [&](const std::string& name) -> int {
        for (int i = 0; i < (int)tickEntries.size(); i++) {
            if (tickEntries[i] == name) return i;
        }
        return -1;
    };

    int preIdx = findIndex("Pre:tick");
    int tickIdx = findIndex("Tick:tick");
    int postIdx = findIndex("Post:tick");

    ASSERT_NE(preIdx, -1);
    ASSERT_NE(tickIdx, -1);
    ASSERT_NE(postIdx, -1);
    EXPECT_LT(preIdx, tickIdx);
    EXPECT_LT(tickIdx, postIdx);
}

TEST(GameSubsystemTest, PriorityWithinSamePhase) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto high = std::make_shared<RecordingSubsystem>(
        "High", TickPhase::LateTick, 200, log);
    auto low = std::make_shared<RecordingSubsystem>(
        "Low", TickPhase::LateTick, 50, log);
    auto mid = std::make_shared<RecordingSubsystem>(
        "Mid", TickPhase::LateTick, 100, log);

    session->addSubsystem(high);
    session->addSubsystem(low);
    session->addSubsystem(mid);

    log.clear();
    session->tick(1.0f / 30.0f);

    std::vector<std::string> tickEntries;
    for (const auto& entry : log) {
        if (entry.find(":tick") != std::string::npos) {
            tickEntries.push_back(entry);
        }
    }

    auto findIndex = [&](const std::string& name) -> int {
        for (int i = 0; i < (int)tickEntries.size(); i++) {
            if (tickEntries[i] == name) return i;
        }
        return -1;
    };

    int lowIdx = findIndex("Low:tick");
    int midIdx = findIndex("Mid:tick");
    int highIdx = findIndex("High:tick");

    ASSERT_NE(lowIdx, -1);
    ASSERT_NE(midIdx, -1);
    ASSERT_NE(highIdx, -1);
    EXPECT_LT(lowIdx, midIdx);
    EXPECT_LT(midIdx, highIdx);
}

// ============================================================================
// Tick dispatching tests
// ============================================================================

TEST(GameSubsystemTest, TickDtPassedCorrectly) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto sub = std::make_shared<RecordingSubsystem>(
        "Sub", TickPhase::Tick, 0, log);
    session->addSubsystem(sub);

    session->tick(0.0625f);
    EXPECT_FLOAT_EQ(sub->lastDt_, 0.0625f);
    EXPECT_EQ(sub->tickCount_, 1);

    session->tick(0.125f);
    EXPECT_FLOAT_EQ(sub->lastDt_, 0.125f);
    EXPECT_EQ(sub->tickCount_, 2);
}

TEST(GameSubsystemTest, MultipleTicksAccumulate) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto sub = std::make_shared<RecordingSubsystem>(
        "Sub", TickPhase::Tick, 0, log);
    session->addSubsystem(sub);

    for (int i = 0; i < 10; i++) {
        session->tick(1.0f / 30.0f);
    }
    EXPECT_EQ(sub->tickCount_, 10);
}

// ============================================================================
// Subsystem list management
// ============================================================================

TEST(GameSubsystemTest, SubsystemsListAccessible) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    // Built-in subsystems are already registered
    size_t builtinCount = session->subsystems().size();
    EXPECT_GT(builtinCount, 0u);

    auto sub = std::make_shared<RecordingSubsystem>(
        "Custom", TickPhase::Tick, 0, log);
    session->addSubsystem(sub);

    EXPECT_EQ(session->subsystems().size(), builtinCount + 1);
}

TEST(GameSubsystemTest, RemoveSubsystemReducesList) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto sub = std::make_shared<RecordingSubsystem>(
        "Custom", TickPhase::Tick, 0, log);
    session->addSubsystem(sub);

    size_t countWithCustom = session->subsystems().size();
    session->removeSubsystem(sub);

    EXPECT_EQ(session->subsystems().size(), countWithCustom - 1);
}

TEST(GameSubsystemTest, RemoveNonExistentSubsystemNoOp) {
    std::vector<std::string> log;
    auto session = GameSession::createLocal();

    auto sub = std::make_shared<RecordingSubsystem>(
        "NotAdded", TickPhase::Tick, 0, log);

    size_t countBefore = session->subsystems().size();
    session->removeSubsystem(sub);  // Should be a no-op
    EXPECT_EQ(session->subsystems().size(), countBefore);
}

// ============================================================================
// Built-in subsystem tests
// ============================================================================

TEST(GameSubsystemTest, BuiltInSubsystemsPresent) {
    auto session = GameSession::createLocal();
    const auto& subs = session->subsystems();

    // Should have WorldTime, BlockEvents, Entities, FluidSim
    std::vector<std::string> names;
    for (const auto& s : subs) {
        names.push_back(std::string(s->name()));
    }

    EXPECT_NE(std::find(names.begin(), names.end(), "WorldTime"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "BlockEvents"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Entities"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "FluidSim"), names.end());
}

TEST(GameSubsystemTest, BuiltInSubsystemPhases) {
    auto session = GameSession::createLocal();
    const auto& subs = session->subsystems();

    for (const auto& s : subs) {
        if (s->name() == "WorldTime") {
            EXPECT_EQ(s->phase(), TickPhase::PreTick);
        } else if (s->name() == "BlockEvents") {
            EXPECT_EQ(s->phase(), TickPhase::Tick);
        } else if (s->name() == "FluidSim") {
            EXPECT_EQ(s->phase(), TickPhase::Tick);
        } else if (s->name() == "Entities") {
            EXPECT_EQ(s->phase(), TickPhase::PostTick);
        }
    }
}

// ============================================================================
// EventBus integration tests
// ============================================================================

TEST(GameSubsystemTest, EventBusAccessible) {
    auto session = GameSession::createLocal();
    EventBus& bus = session->eventBus();

    // Should be functional
    int count = 0;
    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent&) { count++; });
    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    EXPECT_EQ(count, 1);
}

TEST(GameSubsystemTest, SubsystemCanSubscribeInOnAttach) {
    auto session = GameSession::createLocal();

    // Custom subsystem that subscribes to events in onAttach
    class EventListenerSubsystem : public GameSubsystem {
    public:
        std::string_view name() const override { return "EventListener"; }
        TickPhase phase() const override { return TickPhase::LateTick; }
        void onAttach(GameSession& session, EventBus& bus) override {
            bus.subscribe<BlockBrokenEvent>([this](const BlockBrokenEvent& e) {
                receivedCount++;
            });
        }
        void tick(float) override {}
        int receivedCount = 0;
    };

    auto listener = std::make_shared<EventListenerSubsystem>();
    session->addSubsystem(listener);

    // Publish event - listener should receive it
    session->eventBus().publish(BlockBrokenEvent{BlockCoord{1, 2, 3}, BlockTypeId{1}});
    EXPECT_EQ(listener->receivedCount, 1);
}
