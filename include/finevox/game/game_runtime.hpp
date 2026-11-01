#pragma once

/**
 * @file game_runtime.hpp
 * @brief Everything a game needs short of a window
 *
 * GameRuntime turns a content directory into a running game: manifest, content,
 * world generation, session, navigation, and the game script. It has no GPU
 * dependency, so `finevox_headless` can run a full match, and the client adds
 * rendering on top rather than owning the game.
 */

#include "finevox/core/content_pack_loader.hpp"
#include "finevox/core/faction_vision.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/navigation.hpp"
#include "finevox/core/projectile_system.hpp"
#include "finevox/core/queue.hpp"
#include "finevox/core/work_claims.hpp"
#include "finevox/core/world_replica.hpp"
#include "finevox/game/game_manifest.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/script_game_subsystem.hpp"
#include "finevox/worldgen/blueprint.hpp"

#include <finescript/resource_finder.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace finevox::game {

class McpServer;
class ReplicationServer;

/// Someone in the game when it starts
struct PlayerSeed {
    int64_t id = 0;
    std::string name;
    bool local = false;
};

/// A player's place in the game, which outlasts their connection
struct Seat {
    int64_t id = 0;
    std::string name;
    bool local = false;
    uint16_t faction = 0;           ///< 0 until a script binds them
    EntityId body = INVALID_ENTITY_ID;
    glm::dvec3 spawn{0.0};
};

struct RuntimeOptions {
    std::filesystem::path gameRoot;
    std::optional<uint64_t> seedOverride;
    std::optional<int32_t> radiusOverride;
    /// Extra game-thread scripts run after the main script (test scenarios)
    std::vector<std::filesystem::path> extraScripts;
    /// Values defined in the game script's scope before anything runs
    std::vector<std::pair<std::string, std::string>> scriptDefines;
    /// Where to save and resume from. Empty means the game's own slot, if it uses saves.
    std::filesystem::path savePath;
    /// Start over even if there's a save. The old one stays until the next save replaces it.
    bool newGame = false;
    /// Neither read nor write saves: for automation, which must not touch the player's game
    bool noSaves = false;
    /// Serve MCP on this port (see mcp_server.hpp). Unset, the game has no server at all.
    std::optional<int> mcpPort;
    /// Offer only the tools a player could fairly use, for an LLM to play with
    bool mcpPlayerOnly = false;
    /// Use this MCP server, for a process that has one before it has a game
    McpServer* mcpServer = nullptr;

    /// Who's playing, for `PLAYERS` in `on :start`
    std::vector<PlayerSeed> players;
    /// The lobby's settings, CBOR, for `SETTINGS` in `on :start`. Empty means none.
    std::vector<uint8_t> settings;
    /// Keys for GameManifest::applyOverride, from settings that shape the world
    std::map<std::string, std::string> manifestOverrides;
};

/// What the client wants back on resuming: the world says nothing about where it was looking
struct ClientSave {
    glm::dvec3 pivot{0.0};
    glm::dvec3 steward{0.0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 0.0f;
};

class GameRuntime {
public:
    explicit GameRuntime(RuntimeOptions options);
    ~GameRuntime();

    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;

    /// Load content, generate the world, and run the game script up to :start.
    /// Throws std::runtime_error when the game can't start.
    void load();

    /// Run ticks synchronously on this thread (headless)
    void runTicks(uint64_t count);

    /// Save the game. Call between ticks: on the game thread, or with threads stopped. A
    /// failure leaves any existing save as it was. True if saved, or if saving is off.
    bool saveGame(std::string& error);

    /// Empty if this run doesn't use saves
    [[nodiscard]] std::filesystem::path savePath() const;
    [[nodiscard]] bool saveExists() const;
    [[nodiscard]] bool resumed() const { return resumed_; }

    /// Start/stop threaded operation (client)
    void startThreads();
    void stopThreads();

    /// Bring lighting up to date with what `on :start` built, before anyone's sent any
    void settleLight();

    [[nodiscard]] const GameManifest& manifest() const { return manifest_; }
    [[nodiscard]] const std::filesystem::path& gameRoot() const { return options_.gameRoot; }
    [[nodiscard]] GameSession& session() { return *session_; }
    [[nodiscard]] ContentPackLoader& content() { return *content_; }
    [[nodiscard]] script::GameScriptEngine& scripts() { return *scripts_; }
    [[nodiscard]] script::ScriptGameSubsystem& gameScript() { return *gameScript_; }
    [[nodiscard]] NavigationSystem& navigation() { return *navigation_; }
    [[nodiscard]] WorkClaimRegistry& workClaims() { return workClaims_; }
    [[nodiscard]] worldgen::BlueprintLibrary& blueprints() { return blueprints_; }
    [[nodiscard]] FactionVision& vision() { return *vision_; }
    [[nodiscard]] ProjectileSystem& projectiles() { return *projectiles_; }
    /// The viewer faction's memory. replicaFor has everyone else's.
    [[nodiscard]] WorldReplica& replica() { return *replica_; }
    /// A faction's memory, made if it's the first time anyone's asked. Game thread.
    [[nodiscard]] WorldReplica& replicaFor(uint16_t faction);
    [[nodiscard]] const std::map<uint16_t, std::shared_ptr<WorldReplica>>& replicas() const { return replicas_; }
    void tickReplicas(float dt);
    void setRevealAll(bool reveal);
    [[nodiscard]] std::shared_ptr<ReplicationServer> replication() { return replication_; }

    /// Null unless this run asked for one
    [[nodiscard]] McpServer* mcp() { return mcp_; }

    /// A player's seat, if they have one. Safe from any thread.
    [[nodiscard]] std::optional<Seat> seat(int64_t player) const;
    [[nodiscard]] std::vector<Seat> seats() const;
    /// This player sees and acts as this faction. Game thread.
    void bindPlayer(int64_t player, uint16_t faction);
    /// Someone new is here, once the game has started. Game thread.
    void playerJoined(int64_t player, const std::string& name, bool local);
    /// They've gone, at the next chance. Safe from any thread.
    void postPlayerLeft(int64_t player);

    /// What the local client keeps in the save, as a map. Safe from any thread.
    [[nodiscard]] finescript::Value clientState() const;
    void setClientState(const finescript::Value& state);

    /// Sites under construction, by the id scripts hold
    [[nodiscard]] std::unordered_map<int64_t, worldgen::BlueprintSite>& sites() { return sites_; }
    [[nodiscard]] int64_t allocateSiteId() { return nextSiteId_++; }

    /// Sites a faction sees as ghosts. Game thread.
    void setSiteGhosts(uint16_t faction, std::vector<worldgen::SiteGhost> ghosts);
    [[nodiscard]] std::shared_ptr<const std::vector<worldgen::SiteGhost>> siteGhosts(uint16_t faction) const;

    /// Whether the game script has asked to end the run (`game_exit`)
    [[nodiscard]] bool exitRequested() const { return exitRequested_; }
    [[nodiscard]] int exitCode() const { return exitCode_; }
    void requestExit(int code);

    /// World-generation time from load(), for diagnostics
    [[nodiscard]] double worldgenSeconds() const { return worldgenSeconds_; }

private:
    void loadWorldContent();
    void generateWorld();
    void lightWorld();
    void registerNatives();
    void loadSavedWorld();
    void loadSavedState();
    [[nodiscard]] finescript::Value saveSites();
    void loadSites(const finescript::Value& saved, int64_t nextId);

    void startMcp();
    void bindSeeds();
    void bindTheUnbound();
    [[nodiscard]] ClientSave clientSaveCopy() const;
    [[nodiscard]] finescript::Value playerValue(const Seat& seat) const;

    RuntimeOptions options_;
    GameManifest manifest_;
    std::unique_ptr<ContentPackLoader> content_;
    std::unique_ptr<GameSession> session_;
    std::unique_ptr<finescript::ResourceFinder> resourceFinder_;
    std::unique_ptr<script::GameScriptEngine> scripts_;
    std::shared_ptr<script::ScriptGameSubsystem> gameScript_;
    std::shared_ptr<NavigationSystem> navigation_;
    std::shared_ptr<FactionVision> vision_;
    std::shared_ptr<ProjectileSystem> projectiles_;
    WorldReplica* replica_ = nullptr;
    std::map<uint16_t, std::shared_ptr<WorldReplica>> replicas_;
    bool revealAll_ = false;
    std::shared_ptr<ReplicationServer> replication_;
    std::unique_ptr<McpServer> ownedMcp_;
    McpServer* mcp_ = nullptr;
    mutable std::mutex seatMutex_;
    std::map<int64_t, Seat> seats_;
    /// What a save says about who was who: name, faction, and whether they were local
    struct SavedSeat {
        std::string name;
        uint16_t faction = 0;
        bool local = false;
    };
    std::vector<SavedSeat> savedSeats_;
    WorkClaimRegistry workClaims_;
    worldgen::BlueprintLibrary blueprints_;
    std::unordered_map<int64_t, worldgen::BlueprintSite> sites_;
    std::map<uint16_t, std::shared_ptr<const std::vector<worldgen::SiteGhost>>> siteGhosts_;
    finescript::Value savedScriptState_;
    mutable std::mutex clientSaveMutex_;
    ClientSave clientSave_;
    bool resumed_ = false;
    int64_t nextSiteId_ = 1;
    bool threadsRunning_ = false;
    std::atomic<bool> exitRequested_{false};
    int exitCode_ = 0;
    double worldgenSeconds_ = 0.0;
};

}  // namespace finevox::game
