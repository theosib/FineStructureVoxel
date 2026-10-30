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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace finevox::game {

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

    [[nodiscard]] const ClientSave& clientSave() const { return clientSave_; }
    void setClientSave(const ClientSave& save) { clientSave_ = save; }

    /// Start/stop threaded operation (client)
    void startThreads();
    void stopThreads();

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
    [[nodiscard]] WorldReplica& replica() { return *replica_; }

    /// Sites under construction, by the id scripts hold
    [[nodiscard]] std::unordered_map<int64_t, worldgen::BlueprintSite>& sites() { return sites_; }
    [[nodiscard]] int64_t allocateSiteId() { return nextSiteId_++; }

    /// CBOR messages from ui_publish, for the client to drain
    [[nodiscard]] Queue<std::vector<uint8_t>>& uiOutbox() { return uiOutbox_; }

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
    std::shared_ptr<WorldReplica> replica_;
    WorkClaimRegistry workClaims_;
    worldgen::BlueprintLibrary blueprints_;
    std::unordered_map<int64_t, worldgen::BlueprintSite> sites_;
    finescript::Value savedScriptState_;
    ClientSave clientSave_;
    bool resumed_ = false;
    int64_t nextSiteId_ = 1;
    Queue<std::vector<uint8_t>> uiOutbox_;
    bool threadsRunning_ = false;
    bool exitRequested_ = false;
    int exitCode_ = 0;
    double worldgenSeconds_ = 0.0;
};

}  // namespace finevox::game
