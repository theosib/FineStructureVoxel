#pragma once

/**
 * @file game_host.hpp
 * @brief A game that players can be in: its lobby, its players, and its way in
 *
 * Exists before the world does. Takes connections, runs the pre-game, starts the GameRuntime when
 * it's time, and keeps talking to everyone while the game runs. A host with no listener and one
 * local connection is single player, and starting a listener later is all it takes to open that
 * game to a LAN. With no local connection it's a server.
 *
 * Runs a host thread, which does all of this, so a long tick stalls nothing here. It's the game
 * thread that sends world state, through ReplicationServer.
 */

#include "finevox/game/game_runtime.hpp"
#include "finevox/net/content.hpp"
#include "finevox/net/protocol.hpp"

#include <finenet/discovery.hpp>
#include <finenet/listener.hpp>
#include <finenet/wake_signal.hpp>
#include <finescript/value.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace finevox::game {

class ClientView;
class ReplicationServer;

/// What a server's owner sets, from a file or from a menu
struct ServerConfig {
    std::string title;
    uint16_t port = net::DEFAULT_PORT;
    std::string password;
    int maxPlayers = 0;                   ///< 0 takes the game's own limit
    std::vector<std::string> admins;      ///< Names who may launch, on a server with no host player
    std::map<std::string, std::string> settings;  ///< Key to text, as a config file holds them
    std::vector<std::string> locked;      ///< Settings players can't change
    float loadTimeoutSeconds = 60.0f;     ///< Players who haven't loaded by then are dropped
    bool saves = false;                   ///< Keep this game in `saves/server`, and resume it

    /// Parse `key: value` lines. nullopt if the file can't be read.
    [[nodiscard]] static std::optional<ServerConfig> load(const std::filesystem::path& path);
};

struct HostConfig {
    RuntimeOptions runtime;      ///< gameRoot is required; the host fills in players and settings
    ServerConfig server;
    /// Start the game as soon as the first player has joined. That's single player.
    bool autoLaunch = false;
    /// Offer the game on the network from the start
    bool listen = false;
    uint16_t discoveryPort = finenet::DISCOVERY_PORT;
};

/// Run a server until it's killed or the game ends: all there is to a dedicated server. A config
/// path that's empty means defaults, and a port overrides what the config says. Returns an exit
/// code.
int serve(RuntimeOptions options, const std::filesystem::path& configPath, std::optional<int> port);

class GameHost {
public:
    enum class Phase : uint8_t { Lobby, Loading, Running, Ended };

    explicit GameHost(HostConfig config);
    ~GameHost();

    GameHost(const GameHost&) = delete;
    GameHost& operator=(const GameHost&) = delete;

    /// Load the game's declaration and start the host thread. False with `error` if it can't.
    bool start(std::string& error);

    /// A connection into this host from the same process, for its own client
    [[nodiscard]] std::unique_ptr<finenet::Connection> connectLocal();

    /// Start taking connections and answering discovery. Works at any time, game running or not.
    bool openToNetwork(uint16_t port, std::string& error);
    void closeNetwork();
    [[nodiscard]] uint16_t networkPort() const { return networkPort_.load(); }
    [[nodiscard]] bool isOpen() const { return networkPort_.load() != 0; }

    [[nodiscard]] Phase phase() const { return phase_.load(); }
    [[nodiscard]] std::string failure() const;

    /// Null until the game has loaded. Its threads are the host's business.
    [[nodiscard]] GameRuntime* runtime();

    /// Say goodbye to everyone, stop the game, and save it. Blocks.
    void stop();

    /// What the game's script said to exit with, or 1 if it never got that far
    [[nodiscard]] int exitCode();

private:
    struct Player {
        int64_t id = 0;
        std::string name;
        std::string token;
        bool connected = true;
        bool ready = false;
        bool voted = false;
        bool admin = false;
        bool local = false;
        bool contentReady = false;
    };

    struct ContentSending {
        net::ContentFile file;
        uint64_t offset = 0;
    };

    struct Peer {
        std::shared_ptr<finenet::Connection> connection;
        int64_t player = 0;
        bool loaded = false;
        bool gameStartSent = false;
        std::shared_ptr<ClientView> view;
        std::deque<ContentSending> sending;
        std::chrono::steady_clock::time_point secondStart{};
        int commandsThisSecond = 0;
        int chatsThisSecond = 0;
    };

    void loop();
    void acceptConnections();
    void addPeer(std::shared_ptr<finenet::Connection> connection);
    void registerHandlers(Peer& peer);
    void dropClosedPeers();
    void pumpContent(Peer& peer);
    void updatePhase();
    void updateBeacon();

    void onHello(Peer& peer, const finenet::Message& message);
    void onSetSetting(Peer& peer, const finenet::Message& message);
    void onSetReady(Peer& peer, const finenet::Message& message);
    void onLaunch(Peer& peer, const finenet::Message& message);
    void onChat(Peer& peer, const finenet::Message& message);
    void onKick(Peer& peer, const finenet::Message& message);
    void onContentRequest(Peer& peer, const finenet::Message& message);
    void onContentReady(Peer& peer);
    void onLoaded(Peer& peer);
    void onCommand(Peer& peer, const finenet::Message& message);
    void onPlayerState(Peer& peer, const finenet::Message& message);
    void onExpectChange(Peer& peer, const finenet::Message& message);
    void onViewHint(Peer& peer, const finenet::Message& message);
    void onClientState(Peer& peer, const finenet::Message& message);

    void reject(Peer& peer, std::string_view reason, std::string_view text);
    void sendWelcome(Peer& peer, const Player& player);
    void sendManifest(Peer& peer);
    void sendLobby();
    void sendChatLine(int64_t from, std::string_view name, std::string_view text, uint16_t onlyFaction = 0);
    void systemChat(std::string_view text);

    void loadDeclaration();
    [[nodiscard]] bool settingAllowed(const std::string& key, const finescript::Value& value,
                                      finescript::Value& normalised) const;
    void applyPresets();
    [[nodiscard]] bool launchAllowed() const;
    void launchIfDue();
    void beginLoading();
    void onGameLoaded();
    void startGameIfReady();
    void startGame();
    void joinRunningGame(Peer& peer);
    void sendGameStarting(Peer& peer);
    void endGame(std::string_view text);

    [[nodiscard]] Player* findPlayer(int64_t id);
    [[nodiscard]] Peer* findPeer(int64_t player);
    [[nodiscard]] std::string uniqueName(std::string wanted) const;
    [[nodiscard]] int connectedPlayers() const;
    [[nodiscard]] int maxPlayers() const;
    [[nodiscard]] bool isLateJoinOpen() const;
    [[nodiscard]] finescript::Value lobbyStateValue() const;
    [[nodiscard]] finescript::Value worldInfoValue(const Peer& peer) const;
    [[nodiscard]] std::string title() const;
    void noteError(std::string text);

    HostConfig config_;
    GameManifest manifest_;

    std::thread thread_;
    std::thread loader_;
    finenet::WakeSignal signal_;
    std::atomic<bool> stopping_{false};
    std::atomic<Phase> phase_{Phase::Lobby};
    std::atomic<bool> worldLoaded_{false};
    std::atomic<bool> loadFailed_{false};
    std::atomic<uint16_t> networkPort_{0};

    mutable std::mutex mutex_;   // Guards what other threads touch: arrivals, the network, failure
    std::vector<std::shared_ptr<finenet::Connection>> arrivals_;
    std::unique_ptr<finenet::Listener> listener_;
    std::unique_ptr<finenet::DiscoveryBeacon> beacon_;
    std::string failure_;
    bool beaconDirty_ = true;

    // The lobby's declaration
    std::string startPolicy_ = "lobby";
    std::string launchPolicy_ = "host";
    std::string lateJoin_ = "never";
    int minPlayers_ = 1;
    int declaredMaxPlayers_ = 16;
    finescript::Value declaration_;
    std::vector<finescript::Value> settingSchema_;

    // Host thread only, but for runtime_, which is published when the game has loaded
    std::vector<std::unique_ptr<Peer>> peers_;
    std::map<int64_t, Player> players_;
    int64_t nextPlayerId_ = 1;
    std::map<std::string, finescript::Value> settings_;
    bool lobbyDirty_ = true;
    std::optional<std::vector<net::ContentFile>> manifestFiles_;
    std::chrono::steady_clock::time_point loadingStarted_{};
    bool gameStarted_ = false;

    std::unique_ptr<GameRuntime> runtime_;
    std::shared_ptr<ReplicationServer> replication_;
    std::mutex runtimeMutex_;
};

}  // namespace finevox::game
