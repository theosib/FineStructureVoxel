#pragma once

/**
 * @file client_session.hpp
 * @brief A client's side of the protocol, with no window
 *
 * Holds everything a client knows, and is the only way anything it knows gets there. `GameClient`
 * draws from this and `finevox_bot` asserts on it. It works the same over a socket and over a
 * local connection, so single player is not a special case.
 *
 * Runs a sync thread, which applies what arrives. Everything here is safe to call from the render
 * thread. What's for a script to see comes out of takeEvents().
 */

#include "finevox/core/entity_state.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/projectile_system.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/net/client_world.hpp"
#include "finevox/net/content.hpp"
#include "finevox/net/protocol.hpp"
#include "finevox/worldgen/blueprint.hpp"

#include <finenet/connection.hpp>
#include <finescript/value.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace finevox::net {

enum class ClientPhase : uint8_t {
    Idle,          ///< Not connected
    Connecting,    ///< Finding the server
    Joining,       ///< Connected, saying hello
    Content,       ///< Fetching what the server has and this client doesn't
    Lobby,         ///< In the pre-game
    Loading,       ///< The game's starting, and the world's on its way
    Running,       ///< Playing
    Reconnecting,  ///< Lost the server mid-game, and trying to get back
    Ended,         ///< The game's over, or this client left
    Failed,        ///< Couldn't join: see failure()
};

[[nodiscard]] std::string_view phaseName(ClientPhase phase);

/// What the server says about the world, from GameStarting
struct WorldInfo {
    int32_t radius = 0;
    int32_t seaLevel = 0;
    uint32_t tickRate = 30;
    uint32_t entityRate = 30;      ///< How often entity updates come
    bool fog = false;
    float viewDistance = 256.0f;
    bool lod = true;
    float stepHeight = 0.6f;
    std::string playerEntity;
    int32_t visionCell = 4;
    int32_t visionMinX = 0;
    int32_t visionMinZ = 0;
    int32_t visionCellsX = 0;
    int32_t visionCellsZ = 0;
    bool resumed = false;
    std::vector<uint8_t> clientState;
};

/// Who this player is, in the world
struct Binding {
    uint16_t faction = 0;
    EntityId body = INVALID_ENTITY_ID;
    glm::dvec3 spawn{0.0};
};

/// Something a script should hear about. Bodies are CBOR, so that they're decoded by whoever owns
/// the interpreter on its own thread.
struct SessionEvent {
    enum class Kind : uint8_t {
        Phase,       ///< `text` is the new phase's name
        Failed,      ///< `text` says why
        Lobby,       ///< Body is LobbyState
        Chat,        ///< Body is Chat
        Ui,          ///< Body is the game's UiMessage
        WorldReady,  ///< Everything for a first frame has arrived
    };
    Kind kind = Kind::Phase;
    std::string text;
    std::vector<uint8_t> body;
};

class ClientSession {
public:
    struct Options {
        std::string name = "Player";
        std::string game;                    ///< Namespace, which a server must match
        std::filesystem::path gameRoot;      ///< What's already here. Empty for a generic client.
        std::filesystem::path cacheDir;      ///< Empty means the default
        std::string password;
        std::string token;                   ///< To take back a seat
        std::vector<std::string> caps;
    };

    explicit ClientSession(Options options);
    ~ClientSession();

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    /// Join over the network. With several addresses for one server, each is tried in turn.
    void connect(std::vector<std::string> addresses, uint16_t defaultPort = DEFAULT_PORT);

    /// Join over a connection someone else made: a local pair, for a host in this process
    void attach(std::unique_ptr<finenet::Connection> connection);

    /// Go, for good, and give up the seat. The session can't be used again.
    void leave();

    [[nodiscard]] ClientPhase phase() const { return phase_.load(); }
    [[nodiscard]] std::string failure() const;
    [[nodiscard]] std::vector<SessionEvent> takeEvents();

    // ── Who we are ──
    [[nodiscard]] int64_t playerId() const { return playerId_.load(); }
    [[nodiscard]] std::string token() const;
    [[nodiscard]] std::string serverTitle() const;
    [[nodiscard]] float rttMs() const;
    /// Where to find content for this game: the game root, or a session directory. Set once
    /// content is done.
    [[nodiscard]] std::filesystem::path contentRoot() const;
    [[nodiscard]] bool isLocal() const { return local_.load(); }

    /// The game's lobby declaration and how far along content is
    [[nodiscard]] std::vector<uint8_t> lobbyDeclaration() const;
    [[nodiscard]] float contentProgress() const;

    // ── The world ──
    [[nodiscard]] ClientWorld& world() { return world_; }
    [[nodiscard]] GraphicsEventQueue& graphics() { return graphics_; }
    [[nodiscard]] WorldInfo worldInfo() const;
    [[nodiscard]] std::optional<Binding> binding() const;
    [[nodiscard]] bool worldReady() const { return worldReady_.load(); }

    /// Vision cells, one byte each. Empty until the server sends some.
    [[nodiscard]] std::vector<uint8_t> vision() const;
    [[nodiscard]] std::vector<ProjectileView> projectiles() const;
    [[nodiscard]] std::shared_ptr<const std::vector<worldgen::SiteGhost>> ghosts() const;
    [[nodiscard]] float timeOfDay() const;
    [[nodiscard]] size_t entitiesKnown() const;
    /// Every entity this client has been told about, as last told
    [[nodiscard]] std::vector<EntityState> entities() const;

    // ── What the player does ──
    void setSetting(std::string_view key, const finescript::Value& value);
    void setReady(bool ready);
    void launch(bool launch);
    void chat(std::string_view text, bool teamOnly = false);
    void kick(int64_t player);
    void sendLoaded();
    void sendCommand(const finescript::Value& command);
    void sendPlayerState(const EntityState& state);
    void sendViewHint(double x, double z);
    void sendClientState(std::span<const uint8_t> state);

    /// The player's own block, shown now and settled by the server
    void predictBlock(BlockCoord pos, BlockTypeId block);
    /// Ask the server to look at this column soon
    void expectChange(BlockCoord pos);

private:
    void run();
    void registerHandlers();
    void dial();
    void sayHello();
    void setPhase(ClientPhase phase);
    void fail(std::string reason);
    void push(SessionEvent::Kind kind, std::string text = {}, std::vector<uint8_t> body = {});
    void onConnectionLost();
    void forgetEntities();
    void send(finenet::ChannelId channel, uint32_t type, std::vector<uint8_t> body);
    void sendMap(finenet::ChannelId channel, uint32_t type, const finescript::Value& map);

    void onWelcome(const finenet::Message& message);
    void onReject(const finenet::Message& message);
    void onContentManifest(const finenet::Message& message);
    void onContentChunk(const finenet::Message& message);
    void onGameStarting(const finenet::Message& message);
    void onBound(const finenet::Message& message);
    void onEntitySpawn(const finenet::Message& message);
    void onEntityGone(const finenet::Message& message);
    void onEntityBatch(const finenet::Message& message);
    void onProjectiles(const finenet::Message& message);
    void onVision(const finenet::Message& message);
    void onTime(const finenet::Message& message);
    void onGhosts(const finenet::Message& message);

    Options options_;
    ClientWorld world_;
    GraphicsEventQueue graphics_;

    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<ClientPhase> phase_{ClientPhase::Idle};
    std::atomic<int64_t> playerId_{0};
    std::atomic<bool> worldReady_{false};
    std::atomic<bool> local_{false};
    std::atomic<bool> leaving_{false};
    std::atomic<float> contentProgress_{0.0f};

    /// Guards everything below, which both threads touch
    mutable std::mutex mutex_;
    std::shared_ptr<finenet::Connection> connection_;
    std::vector<std::string> addresses_;
    uint16_t defaultPort_ = DEFAULT_PORT;
    size_t addressIndex_ = 0;
    bool hadWelcome_ = false;
    std::chrono::steady_clock::time_point reconnectUntil_{};
    std::chrono::steady_clock::time_point nextDial_{};
    std::string failure_;
    std::string token_;
    std::string title_;
    std::vector<uint8_t> lobbyDeclaration_;
    std::filesystem::path contentRoot_;
    std::optional<WorldInfo> worldInfo_;
    std::optional<Binding> binding_;
    std::vector<SessionEvent> events_;
    std::vector<uint8_t> vision_;
    std::vector<ProjectileView> projectiles_;
    std::shared_ptr<const std::vector<worldgen::SiteGhost>> ghosts_;
    WorldTime time_;
    std::chrono::steady_clock::time_point timeStamp_{};
    std::unordered_map<EntityId, EntityState> entities_;
    bool helloSent_ = false;

    // Sync thread only
    std::unique_ptr<ContentSync> contentSync_;
    std::unordered_map<uint32_t, std::string> entityTypeNames_;
    std::unordered_map<uint32_t, BlockTypeId> ghostBlocks_;
};

}  // namespace finevox::net
