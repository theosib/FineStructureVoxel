#pragma once

/**
 * @file replication_server.hpp
 * @brief What each client gets told, and when
 *
 * Each client has a view: what its faction's memory holds, what its faction can see now. Nothing
 * that's latest-state is ever queued. Subchunks wait in a dirty set, so a block changing twice goes
 * once, and everything's built fresh when the connection has room for it. A slow client gets
 * coarser updates, not older ones.
 *
 * Runs on the game thread, last in the tick, so it reads a world nobody's writing. It also keeps
 * the faction replicas, because a player can join a new faction at any moment and a session's
 * subsystem list can't change while it's running. Before the game thread starts, whoever owns the
 * game calls pump() in its place.
 */

#include "finevox/core/entity_state.hpp"
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/physics.hpp"
#include "finevox/core/position.hpp"

#include <finenet/connection.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace finevox::worldgen {
struct SiteGhost;
}

namespace finevox::game {

class GameRuntime;

/// One client, as the data plane sees it. The public atomics are for the host thread.
class ClientView {
public:
    ClientView(int64_t player, std::shared_ptr<finenet::Connection> connection);

    [[nodiscard]] int64_t player() const { return player_; }

    /// Everything a first frame needs has gone
    [[nodiscard]] bool synced() const { return synced_.load(); }

    void setViewHint(float x, float z) {
        hintX_ = x;
        hintZ_ = z;
    }

private:
    friend class ReplicationServer;

    struct SentEntity {
        Vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        uint8_t animation = 0;
        bool onGround = false;
    };

    int64_t player_;
    std::shared_ptr<finenet::Connection> connection_;
    std::atomic<bool> synced_{false};
    std::atomic<float> hintX_{0.0f};
    std::atomic<float> hintZ_{0.0f};

    // Game thread only
    uint16_t faction_ = 0;
    EntityId body_ = INVALID_ENTITY_ID;
    bool bound_ = false;
    bool syncSent_ = false;
    std::unordered_map<uint64_t, ChunkPos> dirty_;
    std::unordered_set<uint32_t> blockNames_;
    std::unordered_set<uint32_t> fluidNames_;
    std::unordered_set<uint32_t> typeNames_;
    std::unordered_map<EntityId, SentEntity> known_;
    std::vector<uint8_t> lastVision_;
    const void* lastGhosts_ = nullptr;
    bool hadProjectiles_ = false;
};

class ReplicationServer : public GameSubsystem {
public:
    explicit ReplicationServer(GameRuntime& runtime);

    [[nodiscard]] std::string_view name() const override { return "ReplicationServer"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::LateTick; }
    [[nodiscard]] int32_t priority() const override { return 100000; }
    void tick(float dt) override;

    /// One pass over every client, for when the game thread isn't running yet
    void pump();

    /// Safe from any thread. Takes effect at the next pass.
    void addView(std::shared_ptr<ClientView> view);
    void removeView(int64_t player);

    /// Run this on the game thread, at the next pass. Safe from any thread.
    void post(std::function<void()> work);

    /// A game's message for its UIs: one player, one faction, or with both 0, everyone. Game thread.
    void publish(std::vector<uint8_t> message, int64_t player, int64_t faction);

    /// Bytes sent to clients, for measuring
    [[nodiscard]] uint64_t bytesSent() const { return bytesSent_.load(); }

private:
    void adoptChanges();
    void collectDeaths();
    void runPosted();

    void sendBinding(ClientView& view);
    void sendWorld(ClientView& view);
    void sendSubchunk(ClientView& view, ChunkPos pos);
    void sendEntities(ClientView& view);
    void sendVision(ClientView& view);
    void sendTime(ClientView& view);
    void sendProjectiles(ClientView& view);
    void sendGhosts(ClientView& view);
    void send(ClientView& view, finenet::ChannelId channel, uint32_t type, std::vector<uint8_t> body);
    [[nodiscard]] bool factionSees(const ClientView& view, const Vec3& position) const;

    GameRuntime& runtime_;

    std::mutex mutex_;
    std::vector<std::function<void()>> posted_;
    std::vector<std::shared_ptr<ClientView>> arriving_;
    std::vector<int64_t> leaving_;

    std::vector<std::shared_ptr<ClientView>> views_;
    std::unordered_set<EntityId> died_;
    uint64_t pass_ = 0;
    std::atomic<uint64_t> bytesSent_{0};
};

}  // namespace finevox::game
