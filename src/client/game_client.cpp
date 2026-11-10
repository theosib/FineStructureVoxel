#include "finevox/client/game_client.hpp"

#include "finevox/core/action_dispatch.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/content_pack_loader.hpp"
#include "finevox/core/entity_model.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/input_action_system.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/physics.hpp"
#include "finevox/core/player_controller.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/projectile_system.hpp"
#include "finevox/core/resource_locator.hpp"
#include "finevox/core/sky.hpp"
#include "finevox/core/tactical_camera.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/game/game_manifest.hpp"
#include "finevox/game/mcp_server.hpp"
#include "finevox/game/mcp_tools.hpp"
#include "finevox/net/client_session.hpp"
#include "finevox/net/content.hpp"
#include "finevox/worldgen/blueprint.hpp"
#include "finevox/render/block_atlas.hpp"
#include "finevox/render/engine.hpp"
#include "finevox/render/entity_renderer.hpp"
#include "finevox/render/frame_capture.hpp"
#include "finevox/core/region_tint.hpp"
#include "finevox/render/particle_atlas.hpp"
#include "finevox/render/particle_renderer.hpp"
#include "finevox/render/scene_renderer.hpp"
#include "finevox/render/world_render_layer.hpp"
#include "finevox/render/world_renderer.hpp"
#include "finevox/script/event_value.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/finevox_interner.hpp"

#include <finegui/finegui.hpp>
#include <imgui.h>
#include <finegui/map_renderer.hpp>
#include <finegui/script_bindings.hpp>
#include <finegui/script_gui_manager.hpp>
#include <finegui/skin_loader.hpp>
#include <finegui/texture_registry.hpp>

#include <finescript/cbor.h>
#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/resource_finder.h>
#include <finescript/script_engine.h>

#include <finevk/device/staging_pool.hpp>
#include <finevk/engine/camera.hpp>
#include <finevk/engine/input_manager.hpp>
#include <finevk/finevk.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/rendering/swapchain.hpp>
#include <finevk/window/window.hpp>

#include <finenet/discovery.hpp>
#include <finenet/endpoint.hpp>

#include <cmath>
#include <fstream>
#include <map>
#include <iostream>
#include <unordered_set>

namespace finevox::client {

namespace fs = std::filesystem;
using finescript::ExecutionContext;
using finescript::Value;
using Args = std::vector<Value>;
using render::SceneVertex;

namespace {

/// `path`, or with a number added if that's taken: a screenshot shouldn't cost you the last one
fs::path freshPath(const fs::path& path) {
    fs::path candidate = path;
    for (int n = 2; fs::exists(candidate); ++n) {
        candidate = path.parent_path() /
                    (path.stem().string() + "-" + std::to_string(n) + path.extension().string());
    }
    return candidate;
}

constexpr float FOV_DEGREES = 55.0f;
constexpr float NEAR_PLANE = 0.2f;
constexpr float FAR_PLANE = 900.0f;
constexpr float CAMERA_PAN_SPEED = 0.9f;   // Fraction of arm length per second
constexpr float CAMERA_ORBIT_SPEED = 1.6f;
constexpr float PICK_RANGE = 700.0f;
constexpr uint64_t FOG_REFRESH_FRAMES = 15;
constexpr float FOG_EXPLORED = 0.5f;                  ///< What was seen, now out of sight
constexpr glm::vec3 FOG_COLOR(0.015f, 0.02f, 0.04f);  ///< The clear colour, so unseen land vanishes

/// `source` in a UI script. A script may have come from a server, so it stays in the content root.
class ContentRootFinder : public finescript::ResourceFinder {
public:
    explicit ContentRootFinder(fs::path root) : root_(std::move(root)) {}

    void setRoot(fs::path root) { root_ = std::move(root); }

    fs::path resolve(std::string_view name) override {
        if (!net::isSafeContentPath(name)) return {};
        for (const char* suffix : {"", ".fs", ".fsc"}) {
            fs::path candidate = root_ / (std::string(name) + suffix);
            if (fs::is_regular_file(candidate)) return candidate;
        }
        return {};
    }

private:
    fs::path root_;
};

class GuiLayer : public RenderLayer {
public:
    std::function<void(const RenderContext&)> renderFn;
    [[nodiscard]] std::string_view name() const override { return "GUI"; }
    [[nodiscard]] RenderPhase phase() const override { return RenderPhase::GUI; }
    void render(const RenderContext& ctx) override {
        if (renderFn) renderFn(ctx);
    }
};

double number(const Value& v, double fallback = 0.0) {
    return v.isNumeric() ? v.asNumber() : fallback;
}

glm::vec4 colorArg(const Args& args, size_t index) {
    glm::vec4 color(1.0f);
    for (int c = 0; c < 4 && index + c < args.size(); ++c) {
        color[c] = static_cast<float>(number(args[index + c], c == 3 ? 1.0 : 0.0));
    }
    return color;
}

}  // namespace

struct GameClient::Impl {
    explicit Impl(ClientOptions opts) : options(std::move(opts)) {}

    ~Impl() {
        // Members below hold GPU resources that in-flight frames may still use
        if (engine) engine->device().waitIdle();
        finegui::setActiveSkin(nullptr);
    }

    ClientOptions options;
    game::GameManifest manifest;
    fs::path contentRoot;          ///< Where this game's files are: ours, or what a server sent

    std::unique_ptr<LocalHost> host;  // Declared before the session, so that it outlives it
    std::unique_ptr<net::ClientSession> session;
    std::unique_ptr<finenet::DiscoveryBrowser> browser;
    std::unique_ptr<ContentPackLoader> ownContent;
    ContentPackLoader* content = nullptr;
    worldgen::BlueprintLibrary blueprints;
    std::unique_ptr<ParticleSystem> particles;
    net::WorldInfo worldInfo;
    bool inWorld = false;
    bool directStart = false;
    bool restartRequested = false;
    float viewHintTimer = 0.0f;
    float playerStateTimer = 0.0f;

    std::unique_ptr<Engine> engine;
    std::unique_ptr<finegui::GuiSystem> gui;
    std::unique_ptr<finescript::ScriptEngine> uiEngine;
    std::unique_ptr<finegui::MapRenderer> mapRenderer;
    std::unique_ptr<finegui::ScriptGuiManager> guiManager;
    std::unique_ptr<finegui::TextureRegistry> textureRegistry;
    std::vector<std::unique_ptr<finegui::Skin>> skins;
    std::unique_ptr<ExecutionContext> uiCtx;
    std::vector<std::unique_ptr<ExecutionContext>> pastContexts;  ///< Hidden windows may still point here
    std::unique_ptr<ContentRootFinder> finder;
    script::FineVoxInterner interner;

    // Atlases outlive the renderers: mesh workers read them until WorldRenderer stops
    std::unique_ptr<render::BlockAtlas> atlas;
    std::unique_ptr<render::ParticleAtlas> particleAtlas;
    std::unique_ptr<render::WorldRenderer> worldRenderer;
    std::unique_ptr<render::ParticleRenderer> particleRenderer;
    std::unique_ptr<render::SceneRenderer> sceneRenderer;
    render::EntityRenderer entityRenderer;
    EntityModelRegistry models;
    render::EntityPoseScratch poseScratch;

    finevk::Camera camera;
    TacticalCamera tactical;
    ViewBasis view;

    // Embodied view: a first-person avatar sharing the same world and picking
    PlayerController steward;
    std::unique_ptr<PhysicsSystem> stewardPhysics;
    std::unique_ptr<SimplePhysicsBody> stewardBody;
    BlockShapeProvider stewardShapes;
    EntityId stewardId = INVALID_ENTITY_ID;
    bool embodied = false;
    bool jumpHeld = false;

    worldgen::SiteGhostView ghostView;
    glm::vec3 ghostColor{0.35f, 0.75f, 1.0f};
    float ghostFaint = 0.15f;
    float ghostSolid = 0.45f;
    glm::vec2 viewport{1600.0f, 900.0f};
    SkyParameters sky;
    std::unordered_map<uint16_t, glm::vec3> factionColors;

    InputActionSystem inputActions;
    bool panLeft = false, panRight = false, panForward = false, panBack = false;
    float groundCheckIn = 0.0f;     ///< Seconds until the camera next looks at the ground
    float stepLag = 0.0f;           ///< How far the eye is still to rise after a step up
    std::vector<render::SceneVertex> markers;  ///< From the UI, relative to markerCamera
    std::map<std::string, Value> prefs;  ///< The player's own settings, kept in prefs.txt
    std::unordered_map<EntityId, glm::vec4> xrayNext;  ///< marker_xray, for the next frame
    std::unordered_map<EntityId, glm::vec4> xray;      ///< And for this one
    glm::dvec3 markerCamera{0.0};
    bool newGameRequested = false;
    int grabHeld = 0;               ///< Bindings holding the world in hand: it takes either
    glm::vec2 grabCursor{0.0f};
    bool orbitLeft = false, orbitRight = false;
    glm::vec2 mouse{0.0f};

    std::unique_ptr<RegionTint> fog;
    bool fogStarted = false;

    finevk::StagingPoolPtr stagingPool;
    uint64_t frameCount = 0;
    bool screenshotRequested = false;
    std::string screenshotPath;
    game::McpServer* mcp = nullptr;  // Null unless this run asked for one
    std::string playerName;
    int fps = 0;
    int framesThisSecond = 0;
    uint64_t meshesBuiltBefore = 0;  ///< For --stats: how many were built in the last second
    float fpsTimer = 0.0f;

    void setupShell();
    void enterWorld();
    void beginSession(const std::string& name, const std::string& password);
    void startLocal(bool newGame);
    void pumpSession();
    void fireNet(std::string_view phase, std::string_view failure);
    void shutdown();
    [[nodiscard]] std::optional<fs::path> contentPath(std::string_view relative) const;
    [[nodiscard]] fs::path recentPath() const;
    [[nodiscard]] std::vector<std::string> recentServers() const;
    void noteRecentServer(const std::string& address);
    void loadPrefs();
    void savePrefs() const;
    void registerNatives();
    void registerNetNatives();
    void registerMcpTools();
    void loadUiScript(const std::string& relative);
    void fireUi(uint32_t symbol);
    void bindInput();
    void updateCamera(float dt);
    void buildScene();
    [[nodiscard]] bool hiddenByWorld(const glm::dvec3& feet, const BlockShapeProvider& shapes) const;
    void flushMarkers();
    void buildProjectiles();
    void updateFog(float dt);
    void setEmbodied(bool on);
    void applyDetail();
    std::optional<float> detailPixels;  ///< What a UI script set, over the game's own
    void updateStewardView(float dt);
    void buildGhosts(float dt);
    void startDeathBursts();
    glm::vec3 factionColor(uint16_t faction) const;
    [[nodiscard]] RaycastResult pickBlock(glm::vec2 cursor, RaycastMode mode) const;
    [[nodiscard]] Value pickResult(const RaycastResult& hit) const;
    bool handleAction(const ActionResult& result);
    [[nodiscard]] std::vector<int32_t> groundHeightsAround(const glm::dvec3& centre) const;

    uint32_t symFrame = 0, symMouse = 0, symAction = 0, symState = 0, symKey = 0;
    uint32_t symNet = 0, symLobby = 0, symChat = 0;
};

glm::vec3 GameClient::Impl::factionColor(uint16_t faction) const {
    auto it = factionColors.find(faction);
    return it == factionColors.end() ? glm::vec3(0.7f) : it->second;
}

void GameClient::Impl::fireUi(uint32_t symbol) {
    auto& handlers = uiCtx->eventHandlers();
    for (size_t i = 0; i < handlers.size(); ++i) {
        if (handlers[i].eventSymbol != symbol) continue;
        try {
            uiEngine->callFunction(handlers[i].handlerFunction, {}, *uiCtx);
        } catch (const std::exception& e) {
            static std::unordered_set<std::string> seen;
            if (seen.insert(e.what()).second) std::cerr << "[ui script] " << e.what() << "\n";
        }
    }
}

void GameClient::Impl::setupShell() {
    contentRoot = options.gameRoot;
    if (options.userRoot.empty()) options.userRoot = options.gameRoot;
    if (auto loaded = game::GameManifest::load(contentRoot)) manifest = *loaded;
    playerName = options.playerName;
    loadPrefs();

    EngineConfig engineConfig;
    engineConfig.windowTitle = manifest.windowTitle;
    engineConfig.windowWidth = options.width;
    engineConfig.windowHeight = options.height;
    engineConfig.enableValidation = options.validation;
    engineConfig.vsync = options.vsync;
    engine = std::make_unique<Engine>(engineConfig);

    auto& window = engine->window();
    auto& device = engine->device();
    auto& renderer = engine->renderer();

    stagingPool = finevk::StagingPool::create(&device).initialSize(4 * 1024 * 1024).build();

    finegui::GuiConfig guiConfig;
    guiConfig.fontSize = 16.0f;
    guiConfig.enableKeyboard = false;  // The keys are the game's, even after a panel is clicked
    guiConfig.msaaSamples = renderer.msaaSamples();
    guiConfig.iniPath = (options.userRoot / "imgui.ini").string();
    guiConfig.shaderDir = options.shaderDir.string();
    if (window.isHighDPI()) guiConfig.dpiScale = window.contentScale().x;
    gui = std::make_unique<finegui::GuiSystem>(&device, guiConfig);
    gui->initialize(&renderer);

    uiEngine = std::make_unique<finescript::ScriptEngine>();
    uiEngine->setInterner(&interner);
    finegui::registerGuiBindings(*uiEngine);
    script::registerLocalizationNatives(*uiEngine);
    mapRenderer = std::make_unique<finegui::MapRenderer>(*uiEngine);
    guiManager = std::make_unique<finegui::ScriptGuiManager>(*uiEngine, *mapRenderer);
    textureRegistry = std::make_unique<finegui::TextureRegistry>();
    mapRenderer->setTextureRegistry(textureRegistry.get());
    finder = std::make_unique<ContentRootFinder>(contentRoot);
    uiEngine->setResourceFinder(finder.get());
    uiCtx = std::make_unique<ExecutionContext>(*uiEngine);

    symFrame = uiEngine->intern("frame");
    symMouse = uiEngine->intern("mouse");
    symAction = uiEngine->intern("action");
    symState = uiEngine->intern("state");
    symKey = uiEngine->intern("key");
    symNet = uiEngine->intern("net");
    symLobby = uiEngine->intern("lobby");
    symChat = uiEngine->intern("chat");

    // The menu needs its words before there's a game to load them
    ContentPackLoader(manifest.contentNamespace).loadLocalization(contentRoot);

    std::string bindings = (contentRoot / "input" / "default.bindings").string();
    if (fs::exists(bindings)) inputActions.loadBindings(bindings);

    registerNatives();
    registerNetNatives();
    registerMcpTools();
    bindInput();
    uiCtx->set("view_mode", Value::symbol(uiEngine->intern("tactical")));
    uiCtx->set("resumed", Value::boolean(false));

    auto guiLayer = std::make_shared<GuiLayer>();
    guiLayer->renderFn = [this](const RenderContext& ctx) {
        gui->beginFrame();
        fireUi(symFrame);
        if (mcp) mcp->pump(game::McpSide::Frame);
        guiManager->processPendingMessages();
        mapRenderer->renderAll();
        gui->endFrame();
        if (ctx.commandBuffer) gui->render(*ctx.commandBuffer);
    };
    engine->addRenderLayer(guiLayer);

    FrameCallbacks callbacks;
    callbacks.onUpdate = [this](float dt) {
        auto& input = engine->inputManager();
        mouse = input.mousePosition();
        glm::ivec2 size = engine->window().windowSize();
        viewport = glm::vec2(float(size.x), float(size.y));

        pumpSession();

        ++framesThisSecond;
        fpsTimer += dt;
        if (fpsTimer >= 1.0f) {
            fps = framesThisSecond;
            framesThisSecond = 0;
            fpsTimer -= 1.0f;
            if (options.printStats && inWorld) {
                uint64_t built = 0;
                if (MeshWorkerPool* pool = worldRenderer->meshWorkerPool()) {
                    built = pool->stats().meshesBuilt.load(std::memory_order_relaxed);
                }
                std::cout << "[client] fps=" << fps << " chunks=" << worldRenderer->renderedChunkCount()
                          << "/" << worldRenderer->loadedChunkCount()
                          << " culled=" << worldRenderer->culledChunkCount()
                          << " tris=" << worldRenderer->renderedTriangleCount()
                          << " built=" << (built - std::exchange(meshesBuiltBefore, built))
                          << " entities=" << entityRenderer.entityCount()
                          << " cam=" << camera.positionD().x << "," << camera.positionD().y << ","
                          << camera.positionD().z << std::endl;
            }
        }

        if (!inWorld) return;

        auto framePeriod = worldRenderer->recordFrameStart();

        sky = computeSkyParameters(session->timeOfDay(), content->skyConfig());
        worldRenderer->setSkyParameters(sky);
        if (worldRenderer->fogDynamicColor()) worldRenderer->setFogColor(sky.fogColor);

        entityRenderer.processEvents(session->graphics());
        // The world holds still in a pause, whatever's looking at it
        float worldDt = session->paused() ? 0.0f : dt;
        entityRenderer.update(worldDt, camera.positionD());
        startDeathBursts();
        particles->tick(worldDt);

        updateCamera(dt);
        worldRenderer->updateCamera(camera.state(), camera.positionD());
        sceneRenderer->updateCamera(camera.state(), camera.positionD());
        sceneRenderer->setSky(sky.sunDirection, glm::vec3(sky.fogColor), 0.0f, 0.0f,
                              sky.skyBrightness, sky.ambientLevel);
        buildScene();
        buildProjectiles();
        buildGhosts(dt);
        flushMarkers();
        updateFog(dt);

        // Tell the server where the player's looking, so that what's near goes first
        viewHintTimer -= dt;
        if (viewHintTimer <= 0.0f) {
            viewHintTimer = 0.5f;
            glm::dvec3 where = embodied ? camera.positionD() : tactical.pivot();
            session->sendViewHint(where.x, where.z);
        }

        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::microseconds(framePeriod.count() / 2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!worldRenderer->waitForMeshUploads(deadline)) break;
            worldRenderer->updateMeshes(0);
        }
    };
    callbacks.onResize = [this](uint32_t width, uint32_t height) {
        if (height > 0) {
            camera.setPerspective(FOV_DEGREES, float(width) / float(height), NEAR_PLANE, FAR_PLANE);
            applyDetail();
        }
    };
    callbacks.getClearColor = [this]() -> glm::vec4 {
        // With fog on, the edge of the known world should read as darkness
        if (!inWorld || worldInfo.fog) return glm::vec4(FOG_COLOR, 1.0f);
        return sky.skyColor;
    };
    callbacks.onPostRender = [this]() {
        // A run that's going straight into a game counts from when it's there, as a script does
        if (inWorld || !directStart) ++frameCount;
        if (mcp) {
            mcp->noteFrame();
            mcp->pump(game::McpSide::AfterFrame);
        }
        if (!options.screenshotPath.empty() && options.screenshotFrame > 0) {
            int64_t since = static_cast<int64_t>(frameCount) - options.screenshotFrame;
            if (since == 0) {
                screenshotPath = options.screenshotPath;
                screenshotRequested = true;
            } else if (since > 0 && options.burstCount > 0 && since % options.burstInterval == 0 &&
                       since / options.burstInterval <= options.burstCount) {
                fs::path base(options.screenshotPath);
                screenshotPath = (base.parent_path() /
                                  (base.stem().string() + "_" +
                                   std::to_string(since / options.burstInterval) + ".png"))
                                     .string();
                screenshotRequested = true;
            }
        }
        if (screenshotRequested) {
            screenshotRequested = false;
            auto& r = engine->renderer();
            finevk::SwapChain* swapChain = r.swapChain();
            if (swapChain) {
                uint32_t index = engine->window().currentImageIndex();
                std::vector<uint8_t> rgba;
                uint32_t w = 0, h = 0;
                bool ok = render::FrameCapture::readbackRGBA(swapChain->image(index), *stagingPool, {},
                                                             rgba, w, h, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                // The window shows opaque whatever the alpha, and a cut-out texture's edge leaves
                // some, which a viewer would show over its own background
                for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
                ok = ok && render::FrameCapture::savePNG(screenshotPath, w, h, rgba);
                std::cout << (ok ? "Screenshot saved: " : "Screenshot FAILED: ") << screenshotPath
                          << std::endl;
            }
        }
        if (options.exitAfterFrames > 0 &&
            frameCount >= static_cast<uint64_t>(options.exitAfterFrames)) {
            engine->requestStop();
        }
    };
    engine->setFrameCallbacks(std::move(callbacks));

    // A menu lays itself out as it loads, before any frame has said how big the window is
    glm::ivec2 size = engine->window().windowSize();
    viewport = glm::vec2(float(size.x), float(size.y));

    switch (options.start) {
        case ClientStart::Menu:
            if (manifest.menuScript.empty()) {
                directStart = true;
                startLocal(false);
            } else {
                loadUiScript(manifest.menuScript);
            }
            break;
        case ClientStart::NewGame:
            directStart = true;
            startLocal(true);
            break;
        case ClientStart::Continue:
            directStart = true;
            startLocal(false);
            break;
        case ClientStart::Join:
            beginSession(playerName, options.password);
            session->connect({options.joinAddress}, manifest.netPort);
            if (!manifest.menuScript.empty()) loadUiScript(manifest.menuScript);
            break;
    }
}

void GameClient::Impl::beginSession(const std::string& name, const std::string& password) {
    net::ClientSession::Options sessionOptions;
    sessionOptions.name = name;
    sessionOptions.game = manifest.contentNamespace;
    sessionOptions.gameRoot = options.gameRoot;
    sessionOptions.password = password;
    session = std::make_unique<net::ClientSession>(std::move(sessionOptions));
}

void GameClient::Impl::startLocal(bool newGame) {
    if (!options.hostFactory) {
        std::cerr << "[client] this client can't host a game\n";
        return;
    }
    HostRequest request;
    request.newGame = newGame;
    request.autoLaunch = true;
    std::string error;
    host = options.hostFactory(request, error);
    if (!host) {
        std::cerr << "[client] couldn't start a game: " << error << "\n";
        return;
    }
    beginSession(playerName, "");
    session->attach(host->connect());
}

void GameClient::Impl::enterWorld() {
    inWorld = true;
    worldInfo = session->worldInfo();
    contentRoot = session->contentRoot();
    finder->setRoot(contentRoot);
    if (auto loaded = game::GameManifest::load(contentRoot)) manifest = *loaded;

    World& world = session->world().world();
    auto& window = engine->window();
    auto& device = engine->device();
    auto& renderer = engine->renderer();

    // A game in this process has loaded all this, into registries that are global
    content = host ? host->content() : nullptr;
    if (!content) {
        ResourceLocator::instance().setGameRoot(contentRoot);
        ownContent = std::make_unique<ContentPackLoader>(manifest.contentNamespace);
        ownContent->loadAll(contentRoot);
        content = ownContent.get();
    }
    blueprints.loadDirectory(contentRoot / "blueprints");
    particles = std::make_unique<ParticleSystem>(world);

    render::WorldRendererConfig worldConfig;
    worldConfig.viewDistance = worldInfo.viewDistance;
    if (worldInfo.fog) {
        fog = std::make_unique<RegionTint>(worldInfo.visionCellsX, worldInfo.visionCellsZ);
        worldConfig.regionTintCells = fog->cells().size();
    }
    worldRenderer = std::make_unique<render::WorldRenderer>(&device, &renderer, world, worldConfig);
    worldRenderer->setLODEnabled(worldInfo.lod);

    std::string shaderDir = options.shaderDir.string() + "/";
    worldRenderer->loadShaders(shaderDir + "chunk.vert.spv", shaderDir + "chunk.frag.spv");
    worldRenderer->loadFluidShader(shaderDir + "fluid.frag.spv");

    atlas = std::make_unique<render::BlockAtlas>();
    atlas->buildFromTextures(&device, renderer.commandPool(), (contentRoot / "textures").string(),
                             content->blockTextures());
    worldRenderer->setBlockAtlas(atlas->texture());
    worldRenderer->setTextureProvider(atlas->createProvider());
    worldRenderer->setGeometryProvider(content->geometryProvider());
    worldRenderer->setFarGeometryProvider(content->farGeometryProvider());
    worldRenderer->setFaceOccludesProvider(content->faceOccludesProvider(world));
    worldRenderer->setConnectsProvider(content->connectsProvider(world));
    worldRenderer->initialize();

    particleAtlas = std::make_unique<render::ParticleAtlas>();
    fs::path particleDir = contentRoot / "textures" / "particles";
    if (particleAtlas->loadFromDirectory(&device, renderer.commandPool(), particleDir.string()) == 0) {
        particleAtlas->createPlaceholder(&device, renderer.commandPool());
    }
    particleAtlas->applyToRegistry(ParticleTypeRegistry::global(), nullptr);
    particleRenderer = std::make_unique<render::ParticleRenderer>(&device, &renderer, *particles);
    particleRenderer->loadShaders(shaderDir + "particle.vert.spv", shaderDir + "particle.frag.spv");
    particleRenderer->setAtlas(particleAtlas.get());
    particleRenderer->initialize();
    worldRenderer->setParticleRenderer(particleRenderer.get());

    sceneRenderer = std::make_unique<render::SceneRenderer>(&device, &renderer);
    sceneRenderer->loadShaders(shaderDir + "scene.vert.spv", shaderDir + "scene.frag.spv");
    sceneRenderer->initialize();
    worldRenderer->setOpaquePassCallback(
        [this](finevk::CommandBuffer& cmd) { sceneRenderer->drawOpaque(cmd); });
    worldRenderer->setTranslucentPassCallback(
        [this](finevk::CommandBuffer& cmd) {
            sceneRenderer->drawTranslucent(cmd);
            sceneRenderer->drawXray(cmd);
        });

    models.loadDirectory(contentRoot / "models");
    EntityTypeRegistry::global().forEachType([this](EntityTypeId id, const EntityTypeDef& def) {
        if (def.model == 0) return;
        const EntityModel* model = models.find(StringInterner::global().lookup(def.model));
        if (!model) return;
        for (const auto& [state, slot] : def.animationStates) {
            auto clip = model->clips.find(state);
            if (clip != model->clips.end()) {
                entityRenderer.registerAnimationClip(id, slot, clip->second.get());
            }
        }
    });

    net::ClientWorld& known = session->world();
    worldRenderer->setLightProvider([&known](const BlockCoord& pos) -> uint8_t { return known.packedLight(pos); });
    worldRenderer->setSmoothLighting(true);
    world.setMeshRebuildQueue(worldRenderer->meshRebuildQueue());
    world.setAlwaysDeferMeshRebuild(false);
    glm::dvec3 start(0.0, double(worldInfo.seaLevel + 10), 0.0);
    worldRenderer->rebuildAllMeshesNear(start);

    int32_t radius = worldInfo.radius * 16;
    tactical.setBounds(glm::vec2(float(-radius)), glm::vec2(float(radius)));
    camera.setPerspective(FOV_DEGREES, float(window.width()) / float(window.height()), NEAR_PLANE, FAR_PLANE);
    applyDetail();
    tactical.setPivot(start);

    stewardShapes = createBlockShapeProvider(world);
    stewardPhysics = std::make_unique<PhysicsSystem>(stewardShapes);
    stewardBody = std::make_unique<SimplePhysicsBody>(
        Vec3(0.0f, float(worldInfo.seaLevel + 16), 0.0f), Vec3(0.35f, 0.9f, 0.35f));
    stewardBody->setMaxStepHeight(worldInfo.stepHeight);
    steward.setPhysics(stewardBody.get(), stewardPhysics.get());
    steward.setFlyMode(false);

    if (auto binding = session->binding()) {
        stewardId = binding->body;
        stewardBody->setPosition(Vec3(float(binding->spawn.x), float(binding->spawn.y), float(binding->spawn.z)));
    }

    if (worldInfo.resumed && !worldInfo.clientState.empty()) {
        Value saved = net::decodeBody(worldInfo.clientState);
        Value pivot = net::field(saved, "pivot");
        Value view = net::field(saved, "camera");
        if (pivot.isArray() && pivot.asArray().size() == 3) {
            const auto& p = pivot.asArray();
            tactical.setPivot(glm::dvec3(p[0].asNumber(), p[1].asNumber(), p[2].asNumber()));
        }
        if (view.isArray() && view.asArray().size() == 3) {
            const auto& v = view.asArray();
            tactical.orbit(float(v[0].asNumber()) - tactical.yaw(), float(v[1].asNumber()) - tactical.pitch());
            tactical.setDistance(float(v[2].asNumber()));
        }
    }

    // The menu's over. What it showed goes, and the game's own UI script starts afresh.
    mapRenderer->hideAll();
    pastContexts.push_back(std::move(uiCtx));
    uiCtx = std::make_unique<ExecutionContext>(*uiEngine);
    uiCtx->set("view_mode", Value::symbol(uiEngine->intern("tactical")));
    uiCtx->set("resumed", Value::boolean(worldInfo.resumed));
    uiCtx->set("my_player", Value::integer(session->playerId()));
    auto binding = session->binding();
    uiCtx->set("my_faction", Value::integer(binding ? binding->faction : 0));
    loadUiScript(manifest.uiScript);

    // Under the GUI, wherever it's added: layers go in order of their phase
    engine->addRenderLayer(std::make_shared<render::WorldRenderLayer>(*worldRenderer));
    engine->setFenceWakeSignal(worldRenderer->wakeSignal());

    // Counted from here, so that a script's idea of frames doesn't depend on how long this took
    frameCount = 0;
    session->sendLoaded();
}

void GameClient::Impl::fireNet(std::string_view phase, std::string_view failure) {
    Value event = Value::map();
    net::setField(event, "phase", net::symbol(phase));
    if (!failure.empty()) net::setField(event, "failure", Value::string(std::string(failure)));
    uiCtx->set("msg", std::move(event));
    fireUi(symNet);
}

void GameClient::Impl::pumpSession() {
    if (!session) return;

    for (auto& event : session->takeEvents()) {
        using Kind = net::SessionEvent::Kind;
        switch (event.kind) {
            case Kind::Phase:
                fireNet(event.text, "");
                break;
            case Kind::Failed:
                std::cerr << "[net] " << event.text << "\n";
                fireNet("failed", event.text);
                break;
            case Kind::Lobby:
            case Kind::Chat:
            case Kind::Ui: {
                Value message = finescript::cborDecode(event.body, interner);
                uiCtx->set("msg", std::move(message));
                fireUi(event.kind == Kind::Lobby ? symLobby : event.kind == Kind::Chat ? symChat : symState);
                break;
            }
            case Kind::WorldReady:
                if (inWorld) {
                    session->sendLoaded();
                } else {
                    enterWorld();
                }
                break;
        }
    }
}

std::optional<fs::path> GameClient::Impl::contentPath(std::string_view relative) const {
    if (!net::isSafeContentPath(relative)) return std::nullopt;
    return contentRoot / std::string(relative);
}

fs::path GameClient::Impl::recentPath() const {
    return net::defaultCacheDir() / ("recent_" + manifest.contentNamespace + ".txt");
}

std::vector<std::string> GameClient::Impl::recentServers() const {
    std::vector<std::string> out;
    std::ifstream in(recentPath());
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) out.push_back(std::move(line));
    }
    return out;
}

void GameClient::Impl::noteRecentServer(const std::string& address) {
    constexpr size_t KEPT = 8;
    auto recent = recentServers();
    std::erase(recent, address);
    recent.insert(recent.begin(), address);
    if (recent.size() > KEPT) recent.resize(KEPT);

    std::error_code ignored;
    fs::create_directories(recentPath().parent_path(), ignored);
    std::ofstream out(recentPath());
    for (const std::string& line : recent) out << line << "\n";
}

// One setting a line, "name: kind value", kind being b(ool), n(umber), y (symbol) or s(tring). The UI
// keeps what it likes there, and nothing in a game depends on it.
void GameClient::Impl::loadPrefs() {
    std::ifstream in(options.userRoot / "prefs.txt");
    for (std::string line; std::getline(in, line);) {
        auto colon = line.find(": ");
        if (colon == std::string::npos || line.size() < colon + 4) continue;
        std::string key = line.substr(0, colon);
        char kind = line[colon + 2];
        std::string text = line.substr(colon + 4);
        try {
            switch (kind) {
                case 'b': prefs[key] = Value::boolean(text == "true"); break;
                case 'n': prefs[key] = Value::number(std::stod(text)); break;
                case 'y': prefs[key] = Value::symbol(StringInterner::global().intern(text)); break;
                case 's': prefs[key] = Value::string(text); break;
                default: break;
            }
        } catch (const std::exception&) {
            // A number that isn't one is a setting lost, not a reason to stop
        }
    }
}

void GameClient::Impl::savePrefs() const {
    std::error_code ignored;
    fs::create_directories(options.userRoot, ignored);
    std::ofstream out(options.userRoot / "prefs.txt");
    for (const auto& [key, value] : prefs) {
        if (value.isBool()) {
            out << key << ": b " << (value.truthy() ? "true" : "false") << "\n";
        } else if (value.isNumeric()) {
            out << key << ": n " << value.asNumber() << "\n";
        } else if (value.isSymbol()) {
            out << key << ": y " << StringInterner::global().lookup(value.asSymbol()) << "\n";
        } else if (value.isString()) {
            std::string text(value.asString());
            std::erase(text, '\n');
            out << key << ": s " << text << "\n";
        }
    }
}

void GameClient::Impl::shutdown() {
    browser.reset();
    if (session) {
        if (inWorld && host) {
            Value state = Value::map();
            auto vector3 = [](const glm::dvec3& v) {
                return Value::array(std::vector<Value>{Value::number(v.x), Value::number(v.y), Value::number(v.z)});
            };
            Vec3 body = stewardBody->position();
            net::setField(state, "pivot", vector3(tactical.pivot()));
            net::setField(state, "steward", vector3(glm::dvec3(body.x, body.y, body.z)));
            net::setField(state, "camera", vector3(glm::dvec3(tactical.yaw(), tactical.pitch(), tactical.distance())));
            session->sendClientState(net::encodeBody(state));
        }
        session->leave();
    }
    if (host) host->stop();
}

void GameClient::Impl::bindInput() {
    auto& input = engine->inputManager();
    input.setCursorMode(finevk::CursorMode::Normal);
    gui->setGuiMode(finegui::GuiMode::Auto);
    gui->connectToInputManager(input);

    input.addListener(
        [this](const finevk::InputEvent& e) -> finevk::ListenerResult {
            switch (e.type) {
                case finevk::InputEventType::MouseButtonPress:
                case finevk::InputEventType::MouseButtonRelease: {
                    static const char* buttons[] = {"left", "right", "middle"};
                    int button = e.mouseButton;
                    uiCtx->set("mouse_event", Value::symbol(uiEngine->intern(
                        e.type == finevk::InputEventType::MouseButtonPress ? "down" : "up")));
                    uiCtx->set("mouse_button", Value::symbol(uiEngine->intern(
                        button >= 0 && button < 3 ? buttons[button] : "other")));
                    uiCtx->set("mouse_x", Value::number(e.state.mousePosition.x));
                    uiCtx->set("mouse_y", Value::number(e.state.mousePosition.y));
                    uiCtx->set("mouse_shift", Value::boolean(e.state.isShiftPressed()));
                    fireUi(symMouse);

                    RawInputEvent raw;
                    raw.type = e.type == finevk::InputEventType::MouseButtonPress
                                   ? RawEventType::MouseButtonPress
                                   : RawEventType::MouseButtonRelease;
                    raw.keyOrButton = e.mouseButton;
                    handleAction(inputActions.processEvent(raw));
                    return finevk::ListenerResult::Consumed;
                }
                case finevk::InputEventType::MouseScroll: {
                    float notch = e.state.scrollDelta.y;
                    if (notch != 0.0f) tactical.zoom(notch > 0.0f ? 0.88f : 1.0f / 0.88f);
                    return finevk::ListenerResult::Consumed;
                }
                case finevk::InputEventType::KeyPress:
                case finevk::InputEventType::KeyRelease: {
                    RawInputEvent raw;
                    raw.type = e.type == finevk::InputEventType::KeyPress ? RawEventType::KeyPress
                                                                          : RawEventType::KeyRelease;
                    raw.keyOrButton = e.key;
                    raw.modifiers = InputModifier::None;
                    if (e.state.isShiftPressed()) raw.modifiers = raw.modifiers | InputModifier::Shift;
                    if (e.state.isControlPressed()) raw.modifiers = raw.modifiers | InputModifier::Control;
                    if (e.state.isAltPressed()) raw.modifiers = raw.modifiers | InputModifier::Alt;
                    if (e.state.isSuperPressed()) raw.modifiers = raw.modifiers | InputModifier::Super;

                    return handleAction(inputActions.processEvent(raw)) ? finevk::ListenerResult::Consumed
                                                                        : finevk::ListenerResult::Reject;
                }
                default:
                    return finevk::ListenerResult::Reject;
            }
        },
        finevk::InputPriority::Game);
}

bool GameClient::Impl::handleAction(const ActionResult& result) {
    if (!result.matched()) return false;

    const std::string& action = result.actionName;
    if (action == "begin_pan_left") panLeft = true;
    else if (action == "end_pan_left") panLeft = false;
    else if (action == "begin_pan_right") panRight = true;
    else if (action == "end_pan_right") panRight = false;
    else if (action == "begin_pan_forward") panForward = true;
    else if (action == "end_pan_forward") panForward = false;
    else if (action == "begin_pan_back") panBack = true;
    else if (action == "end_pan_back") panBack = false;
    else if (action == "begin_grab") {
        if (grabHeld++ == 0) grabCursor = mouse;
    }
    else if (action == "end_grab") grabHeld = std::max(grabHeld - 1, 0);
    else if (action == "toggle_view") setEmbodied(!embodied);
    else if (action == "toggle_fullscreen") engine->window().toggleFullscreen();
    else if (action == "begin_jump" && embodied) {
        jumpHeld = true;
        steward.requestJump();
    }
    else if (action == "end_jump") jumpHeld = false;
    else if (action == "begin_orbit_left") orbitLeft = true;
    else if (action == "end_orbit_left") orbitLeft = false;
    else if (action == "begin_orbit_right") orbitRight = true;
    else if (action == "end_orbit_right") orbitRight = false;
    else {
        uiCtx->set("action", Value::symbol(uiEngine->intern(action)));
        uiCtx->set("action_arg", Value::string(result.argExpression));
        fireUi(symAction);
    }
    return true;
}

// On a bigger or sharper screen a block is more pixels tall, so detail reaches further
void GameClient::Impl::applyDetail() {
    if (!worldRenderer) return;
    float pixels = detailPixels.value_or(manifest.detailPixels);
    float height = static_cast<float>(engine->window().height());
    worldRenderer->setDetailDistance(DetailRange::forPixels(pixels, height, FOV_DEGREES));
}

void GameClient::Impl::setEmbodied(bool on) {
    embodied = on;
    auto& input = engine->inputManager();
    panLeft = panRight = panForward = panBack = false;
    grabHeld = 0;
    stepLag = 0.0f;
    steward.clearInput();

    // On foot the cursor is hidden, but it still has a position. Left alone it wanders over the
    // panels, and a click meant for the world lands on a button nobody can see.
    ImGuiIO& io = ImGui::GetIO();
    if (on) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    if (on) {
        // Step down at the point the overhead view was looking at
        glm::dvec3 pivot = tactical.pivot();
        stewardBody->setPosition(Vec3(static_cast<float>(pivot.x), static_cast<float>(pivot.y) + 1.0f,
                                      static_cast<float>(pivot.z)));
        stewardBody->setVelocity(Vec3(0.0f));
        input.setCursorMode(finevk::CursorMode::Disabled);
    } else {
        Vec3 pos = stewardBody->position();
        tactical.setPivot(glm::dvec3(pos.x, pos.y, pos.z));
        input.setCursorMode(finevk::CursorMode::Normal);
    }

    uiCtx->set("view_mode", Value::symbol(uiEngine->intern(on ? "embodied" : "tactical")));
}

void GameClient::Impl::updateStewardView(float dt) {
    auto& input = engine->inputManager();
    steward.setMoveForward(panForward);
    steward.setMoveBack(panBack);
    steward.setMoveLeft(panLeft);
    steward.setMoveRight(panRight);

    glm::vec2 delta = input.mouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f) steward.look(delta.x, delta.y);
    float footing = stewardBody->position().y;
    if (!session->paused()) steward.update(dt);

    Vec3 pos = stewardBody->position();

    // A step up is one frame's move for the body. Bring the eye up after it, so that a ledge
    // feels like a stair. It can't sink through anything: the body is clear down to its feet.
    constexpr float STEP_FRAME_RISE = 0.2f;  // More than a jump manages in a frame
    constexpr float EYE_RISE_RATE = 14.0f;
    if (pos.y - footing > STEP_FRAME_RISE) stepLag += pos.y - footing;
    stepLag *= std::exp(-EYE_RISE_RATE * dt);

    Vec3 eye(pos.x, pos.y + steward.eyeHeight() - stepLag, pos.z);
    Vec3 safeOrigin(pos.x, pos.y + 0.9f, pos.z);
    Vec3 adjusted = adjustCameraForWallCollision(safeOrigin, eye, stewardShapes);
    glm::dvec3 cameraPos(adjusted.x, adjusted.y, adjusted.z);

    camera.moveTo(cameraPos);
    camera.setOrientation(steward.forwardVector(), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();
    view = makeViewBasis(cameraPos, steward.forwardVector(), FOV_DEGREES,
                         viewport.x / std::max(viewport.y, 1.0f));

    EntityState state;
    state.position = glm::dvec3(pos.x, pos.y, pos.z);
    state.velocity = glm::dvec3(stewardBody->velocity());
    state.onGround = stewardBody->isOnGround();
    state.yaw = glm::degrees(steward.yaw());
    state.pitch = glm::degrees(steward.pitch());
    playerStateTimer -= dt;
    if (playerStateTimer <= 0.0f) {
        playerStateTimer = 1.0f / static_cast<float>(worldInfo.tickRate);
        session->sendPlayerState(state);
    }
}

void GameClient::Impl::updateCamera(float dt) {
    if (embodied) {
        updateStewardView(dt);
        return;
    }

    float pan = CAMERA_PAN_SPEED * tactical.distance() * dt;
    float right = (panRight ? pan : 0.0f) - (panLeft ? pan : 0.0f);
    float forward = (panForward ? pan : 0.0f) - (panBack ? pan : 0.0f);

    // Edge scroll, except while the cursor sits on a GUI widget. Polled, because no events
    // come from past the content area.
    constexpr float EDGE = 4.0f;
    auto& window = engine->window();
    if (!gui->wantCaptureMouse() && window.isFocused()) {
        glm::dvec2 cursor = window.mousePosition();
        glm::vec2 push = edgeScroll(glm::vec2(cursor), viewport, EDGE, window.spansScreen());
        right += push.x * pan;
        forward += push.y * pan;
    }
    if (right != 0.0f || forward != 0.0f) tactical.pan(right, forward);

    if (grabHeld > 0) {
        tactical.grab(view, grabCursor, mouse, viewport);
        grabCursor = mouse;
    }

    if (orbitLeft) tactical.orbit(CAMERA_ORBIT_SPEED * dt, 0.0f);
    if (orbitRight) tactical.orbit(-CAMERA_ORBIT_SPEED * dt, 0.0f);

    // Keep the pivot on the ground so zooming goes where you're looking. No hurry: this is
    // a slow glide, and looking at each column takes a moment.
    groundCheckIn -= dt;
    if (groundCheckIn <= 0.0f) {
        groundCheckIn = 0.1f;
        if (auto ground = groundLevel(groundHeightsAround(tactical.targetPivot()))) {
            tactical.followGround(*ground);
        }
    }
    tactical.update(dt);

    camera.moveTo(tactical.position());
    camera.setOrientation(tactical.forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    view = makeViewBasis(tactical.position(), tactical.forward(), FOV_DEGREES,
                         viewport.x / std::max(viewport.y, 1.0f));
}

std::vector<int32_t> GameClient::Impl::groundHeightsAround(const glm::dvec3& centre) const {
    // A grid across about a third of what's on screen, so that wider views steer by wider land
    constexpr int32_t REACH = 3;
    int32_t step = std::clamp(static_cast<int32_t>(tactical.distance() * 0.1f), 2, 10);
    int32_t centreX = static_cast<int32_t>(std::floor(centre.x));
    int32_t centreZ = static_cast<int32_t>(std::floor(centre.z));

    const World& world = session->world().world();
    const BlockRegistry& registry = BlockRegistry::global();
    std::vector<int32_t> heights;
    heights.reserve((REACH * 2 + 1) * (REACH * 2 + 1));

    for (int32_t dz = -REACH; dz <= REACH; ++dz) {
        for (int32_t dx = -REACH; dx <= REACH; ++dx) {
            int32_t x = centreX + dx * step;
            int32_t z = centreZ + dz * step;
            const ChunkColumn* column = world.getColumn(ColumnPos(x >> 4, z >> 4));
            auto bounds = column ? column->getYBounds() : std::nullopt;
            if (!bounds) continue;  // Never seen, which says nothing about the ground here

            // Down to something solid or a water surface. Foliage lets the eye through,
            // and a lake's bed is not where anyone thinks the land is.
            for (int32_t y = bounds->second * 16 + 15; y >= bounds->first * 16; --y) {
                BlockTypeId block = column->getBlock(x, y, z);
                if (world.hasFluid(BlockCoord(x, y, z)) ||
                    (!block.isAir() && registry.getType(block).isOpaque())) {
                    heights.push_back(y + 1);
                    break;
                }
            }
        }
    }
    return heights;
}

void GameClient::Impl::buildScene() {
    const net::ClientWorld& known = session->world();
    glm::dvec3 cameraPos = camera.positionD();
    float tickRate = static_cast<float>(worldInfo.entityRate);
    auto& opaque = sceneRenderer->opaque();
    // What the UI asked for last frame, which is as late as any marker
    xray.swap(xrayNext);
    xrayNext.clear();
    BlockShapeProvider shapes = createBlockShapeProvider(session->world().world());

    for (const auto& [id, state] : entityRenderer.entities()) {
        if (!state.isCurrent() && !state.isDying()) continue;

        // The camera is inside this one, or just past it: its drawn position trails the
        // live body by a tick, which on walking backward puts it in front of the eye
        if (embodied && id == stewardId) continue;

        const EntityTypeDef* def = EntityTypeRegistry::global().getType(state.typeId);
        float alpha = state.interpolationAlpha(tickRate);
        glm::dvec3 pos = state.interpolatedPosition(alpha);

        BlockCoord cell(static_cast<int32_t>(std::floor(pos.x)),
                        static_cast<int32_t>(std::floor(pos.y)) + 1,
                        static_cast<int32_t>(std::floor(pos.z)));
        uint8_t packed = known.packedLight(cell);
        float skyLight = (packed >> 4) / 15.0f * sky.skyBrightness;
        float blockLight = (packed & 0xF) / 15.0f;
        float brightness = std::max({skyLight, blockLight, 0.3f}) * 1.6f;

        glm::vec3 team = factionColor(state.faction);
        const EntityModel* model =
            def && def->model != 0 ? models.find(StringInterner::global().lookup(def->model)) : nullptr;
        size_t first = opaque.size();
        if (model) {
            render::appendEntity(opaque, *model, state, cameraPos, team, brightness, tickRate,
                                 poseScratch);
        } else {
            glm::vec3 half = def ? def->halfExtents : glm::vec3(0.3f, 0.9f, 0.3f);
            glm::vec3 center(pos - cameraPos);
            center.y += half.y;
            render::appendBox(opaque, center, half, glm::vec4(team * brightness, 1.0f));
        }

        // The same shape again, in one colour, for the pass that draws only what's hidden. Only
        // for a body the world stands in front of: in plain view, a far arm behind the near one
        // would show through it.
        if (auto shown = xray.find(id); shown != xray.end() && state.isCurrent() && hiddenByWorld(pos, shapes)) {
            auto& hidden = sceneRenderer->xray();
            for (size_t i = first; i < opaque.size(); ++i) {
                render::SceneVertex vertex = opaque[i];
                vertex.color = shown->second;
                hidden.push_back(vertex);
            }
        }
    }
}

// Is any of a body, standing at `feet`, out of sight behind blocks? Looks at its feet, middle and head.
bool GameClient::Impl::hiddenByWorld(const glm::dvec3& feet, const BlockShapeProvider& shapes) const {
    constexpr double HEIGHTS[] = {0.3, 1.0, 1.7};
    constexpr float SHORT_OF = 0.4f;  // The ray stops before the body, which isn't a block
    glm::dvec3 eye = camera.positionD();
    for (double height : HEIGHTS) {
        glm::dvec3 to = feet + glm::dvec3(0.0, height, 0.0) - eye;
        double length = glm::length(to);
        if (length < 1e-3) continue;
        glm::dvec3 dir = to / length;
        RaycastResult hit = raycastBlocks(Vec3(glm::vec3(eye)), Vec3(glm::vec3(dir)),
                                          static_cast<float>(length) - SHORT_OF, RaycastMode::Interaction, shapes);
        if (hit) return true;
    }
    return false;
}

void GameClient::Impl::startDeathBursts() {
    // Middle of the body, where it's lying
    constexpr double BODY_HEIGHT = 0.35;
    for (const auto& burst : entityRenderer.takeDeathBursts()) {
        particles->burst(burst.emitter, burst.position + glm::dvec3(0.0, BODY_HEIGHT, 0.0),
                         glm::vec4(factionColor(burst.faction), 1.0f));
    }
}

void GameClient::Impl::buildProjectiles() {
    glm::dvec3 cameraPos = camera.positionD();
    auto& opaque = sceneRenderer->opaque();
    for (const ProjectileView& p : session->projectiles()) {
        glm::vec3 center(p.position - cameraPos);
        glm::vec3 dir = glm::length(p.velocity) > 0.01f ? glm::normalize(p.velocity) : glm::vec3(0, 1, 0);
        for (float along : {-0.3f, -0.1f, 0.1f, 0.3f}) {
            render::appendBox(opaque, center + dir * along, glm::vec3(0.05f),
                              glm::vec4(0.25f, 0.18f, 0.1f, 1.0f));
        }
    }
}

void GameClient::Impl::flushMarkers() {
    // The UI runs after the scene has drawn, so its markers show on the next frame. They were
    // placed from this frame's camera, and a marker hugging the ground has no room for the
    // difference: with the camera coming down, it would sink out of sight.
    glm::dvec3 now = camera.positionD();
    glm::vec3 shift(markerCamera - now);
    auto& out = sceneRenderer->translucent();
    for (render::SceneVertex vertex : markers) {
        vertex.position += shift;
        out.push_back(vertex);
    }
    markers.clear();
    markerCamera = now;
}

void GameClient::Impl::buildGhosts(float dt) {
    const World& world = session->world().world();
    ghostView.update(session->ghosts(), dt, [&world](BlockCoord pos) { return world.getBlock(pos); });

    // A hair larger than a block, so that it doesn't fight the ground it stands on
    constexpr glm::vec3 HALF(0.505f);
    glm::dvec3 cameraPos = camera.positionD();
    auto& out = sceneRenderer->translucent();
    for (const auto& ghost : ghostView.faces()) {
        glm::vec3 center(glm::dvec3(ghost.cell.x + 0.5, ghost.cell.y + 0.5, ghost.cell.z + 0.5) - cameraPos);
        float opacity = ghostFaint + (ghostSolid - ghostFaint) * ghost.built;
        render::appendBox(out, center, HALF, glm::vec4(ghostColor, opacity), true, ghost.faces);
    }
}

void GameClient::Impl::updateFog(float dt) {
    if (!fog) return;

    bool firstLook = !fogStarted;
    if (firstLook || frameCount % FOG_REFRESH_FRAMES == 0) {
        auto cells = session->vision();
        if (!cells.empty()) {
            fog->setFromVision(cells, FOG_EXPLORED);
            fogStarted = true;
        }
    }
    if (firstLook && fogStarted) fog->snapToTargets();
    fog->advance(dt);

    worldRenderer->setRegionTint(FOG_COLOR, glm::vec2(float(worldInfo.visionMinX), float(worldInfo.visionMinZ)),
                                 float(worldInfo.visionCell), fog->columns(), fog->rows(), fog->cells().data());
}

RaycastResult GameClient::Impl::pickBlock(glm::vec2 cursor, RaycastMode mode) const {
    WorldRay ray = screenRayToWorld(view, cursor, viewport);
    BlockShapeProvider shapes = createBlockShapeProvider(session->world().world());
    Vec3 origin(static_cast<float>(ray.origin.x), static_cast<float>(ray.origin.y),
                static_cast<float>(ray.origin.z));
    return raycastBlocks(origin, Vec3(ray.direction), PICK_RANGE, mode, shapes);
}

Value GameClient::Impl::pickResult(const RaycastResult& hit) const {
    if (!hit.hit) return Value::nil();
    auto result = Value::map();
    auto& map = result.asMap();
    auto normal = faceNormal(hit.face);
    map.set(uiEngine->intern("x"), Value::integer(hit.blockPos.x));
    map.set(uiEngine->intern("y"), Value::integer(hit.blockPos.y));
    map.set(uiEngine->intern("z"), Value::integer(hit.blockPos.z));
    map.set(uiEngine->intern("nx"), Value::integer(normal[0]));
    map.set(uiEngine->intern("ny"), Value::integer(normal[1]));
    map.set(uiEngine->intern("nz"), Value::integer(normal[2]));
    map.set(uiEngine->intern("distance"), Value::number(hit.distance));
    map.set(uiEngine->intern("block"),
            Value::string(std::string(session->world().world().getBlock(hit.blockPos).name())));
    return result;
}

void GameClient::Impl::registerNatives() {
    auto& e = *uiEngine;

    e.registerFunction("send_command", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty() || !session) return Value::nil();
        session->sendCommand(args[0]);
        return Value::nil();
    });

    // ui_show tree [=immediate true] -> id. Replaces ui.show, which needs a ScriptGui
    // behind the context. This client has one flat UI context instead.
    e.registerFunction("ui_show", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty() || !args[0].isMap()) return Value::nil();
        bool immediate = false;
        if (args.size() > 1 && args.back().isMap()) {
            Value flag = const_cast<Value&>(args.back()).asMap().get(uiEngine->intern("immediate"));
            immediate = flag.truthy();
        }
        return Value::integer(mapRenderer->show(args[0], *uiCtx, immediate));
    });

    // ui_skin "ui/skin/x.fgs" -> true if loaded and now in use. With no path, back to plain ImGui.
    e.registerFunction("ui_skin", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty() || !args[0].isString()) {
            finegui::setActiveSkin(nullptr);
            return Value::boolean(true);
        }
        std::string error;
        auto found = contentPath(args[0].asString());
        if (!found) return Value::boolean(false);
        fs::path path = *found;
        auto skin = finegui::loadSkin(*gui, path, *uiEngine, *uiCtx, error);
        if (!skin) {
            std::cerr << "[ui script] skin " << path.string() << ": " << error << "\n";
            return Value::boolean(false);
        }
        skins.push_back(std::move(skin));
        finegui::setActiveSkin(skins.back().get());
        return Value::boolean(true);
    });

    // ui_texture name "ui/x.png" -> true if loaded, for ui.image to show by name
    e.registerFunction("ui_texture", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::boolean(false);
        auto found = contentPath(args[1].toString());
        if (!found) return Value::boolean(false);
        fs::path path = *found;
        finegui::TextureHandle handle = gui->loadSkinAtlas(path.string());
        if (!handle.valid()) {
            std::cerr << "[ui script] texture " << path.string() << " didn't load\n";
            return Value::boolean(false);
        }
        textureRegistry->registerTexture(args[0].toString(), handle);
        return Value::boolean(true);
    });

    e.registerFunction("ui_hide", [this](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty() && args[0].isNumeric()) mapRenderer->hide(static_cast<int>(args[0].asNumber()));
        return Value::nil();
    });

    // blueprint_size name -> [x y z], for footprint previews. The library doesn't
    // change after load, so reading it from this thread is safe.
    e.registerFunction("blueprint_size", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        std::string name = args[0].isSymbol()
            ? std::string(StringInterner::global().lookup(args[0].asSymbol()))
            : args[0].toString();
        const auto* schematic = blueprints.find(name);
        if (!schematic) return Value::nil();
        return Value::array(std::vector<Value>{Value::integer(schematic->sizeX()),
                                               Value::integer(schematic->sizeY()),
                                               Value::integer(schematic->sizeZ())});
    });

    e.registerFunction("window_size", [this](ExecutionContext&, const Args&) -> Value {
        return Value::array(std::vector<Value>{Value::number(viewport.x), Value::number(viewport.y)});
    });

    e.registerFunction("mouse_pos", [this](ExecutionContext&, const Args&) -> Value {
        return Value::array(std::vector<Value>{Value::number(mouse.x), Value::number(mouse.y)});
    });

    e.registerFunction("gui_has_mouse", [this](ExecutionContext&, const Args&) -> Value {
        return Value::boolean(gui->wantCaptureMouse());
    });

    e.registerFunction("fps", [this](ExecutionContext&, const Args&) -> Value {
        return Value::integer(fps);
    });

    // set_faction_color id r g b
    e.registerFunction("set_faction_color", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() >= 4) {
            factionColors[static_cast<uint16_t>(number(args[0]))] =
                glm::vec3(number(args[1]), number(args[2]), number(args[3]));
        }
        return Value::nil();
    });

    // pick_view -> what the embodied view is looking at, as pick_block
    e.registerFunction("pick_view", [this](ExecutionContext&, const Args&) -> Value {
        return pickResult(pickBlock(viewport * 0.5f, RaycastMode::Interaction));
    });

    e.registerFunction("cam_focus", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() >= 2) {
            tactical.setPivot(glm::dvec3(number(args[0]), tactical.pivot().y, number(args[1])));
        }
        return Value::nil();
    });

    e.registerFunction("cam_pivot", [this](ExecutionContext&, const Args&) -> Value {
        glm::dvec3 pivot = tactical.pivot();
        return Value::array(std::vector<Value>{Value::number(pivot.x), Value::number(pivot.y),
                                               Value::number(pivot.z)});
    });

    // detail_pixels [n] -> how far detail reaches, in blocks. With n, draws far forms under n
    // pixels tall from now on, 0 drawing everything in full.
    e.registerFunction("detail_pixels", [this](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty()) {
            detailPixels = static_cast<float>(number(args[0]));
            applyDetail();
        }
        return Value::number(worldRenderer ? worldRenderer->detailDistance() : 0.0);
    });

    e.registerFunction("cam_pos", [this](ExecutionContext&, const Args&) -> Value {
        glm::dvec3 pos = camera.positionD();
        return Value::array(std::vector<Value>{Value::number(pos.x), Value::number(pos.y),
                                               Value::number(pos.z)});
    });

    e.registerFunction("cam_set", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() >= 3) {
            tactical.setDistance(static_cast<float>(number(args[0])));
            tactical.orbit(static_cast<float>(number(args[1])) - tactical.yaw(),
                           static_cast<float>(number(args[2])) - tactical.pitch());
        }
        return Value::nil();
    });

    // pick_block mx my -> {=x =y =z =nx =ny =nz =distance =block} or nil: what's under the
    // cursor, as seen. pick_ground is the same, but looks through whatever a body can walk
    // through, so that an order aimed past a tree's leaves lands on the ground.
    e.registerFunction("pick_block", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        return pickResult(pickBlock(glm::vec2(number(args[0]), number(args[1])), RaycastMode::Interaction));
    });

    e.registerFunction("pick_ground", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        return pickResult(pickBlock(glm::vec2(number(args[0]), number(args[1])), RaycastMode::Collision));
    });

    // avatar_overlaps x y z -> whether a block in that cell would stand in the avatar's body
    e.registerFunction("avatar_overlaps", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 3) return Value::boolean(false);
        AABB body = stewardBody->boundingBox();
        Vec3 low(float(number(args[0])), float(number(args[1])), float(number(args[2])));
        Vec3 high = low + Vec3(1.0f);
        return Value::boolean(body.max.x > low.x && body.min.x < high.x && body.max.y > low.y &&
                              body.min.y < high.y && body.max.z > low.z && body.min.z < high.z);
    });

    // set_ghost_style r g b faint solid: the colour of unbuilt sites, and how opaque they are
    // when just begun and when nearly done
    e.registerFunction("set_ghost_style", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() >= 5) {
            ghostColor = glm::vec3(number(args[0]), number(args[1]), number(args[2]));
            ghostFaint = float(number(args[3]));
            ghostSolid = float(number(args[4]));
        }
        return Value::nil();
    });

    // do_action name: as if a key bound to that action had been pressed
    e.registerFunction("do_action", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        ActionResult action;
        action.actionName = args[0].toString();
        handleAction(action);
        return Value::nil();
    });

    // gui_click x y: a left click on the GUI, for scenarios that test it through the buttons
    e.registerFunction("gui_click", [](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(float(number(args[0])), float(number(args[1])));
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        return Value::nil();
    });

    // gui_hover x y -> rests the mouse there, as gui_click does, so a script can see a tooltip
    e.registerFunction("gui_hover", [](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        ImGui::GetIO().AddMousePosEvent(float(number(args[0])), float(number(args[1])));
        return Value::nil();
    });

    // input_key name down, input_button index down x y, input_move dx dy: real events, sent down
    // the whole listener chain. gui_click goes straight to ImGui, so it can't show who gets what.
    e.registerFunction("input_key", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        auto& input = engine->inputManager();
        finevk::InputEvent event;
        event.type = args[1].truthy() ? finevk::InputEventType::KeyPress
                                      : finevk::InputEventType::KeyRelease;
        event.key = InputActionSystem::keyNameToCode(args[0].toString());
        event.state.mousePosition = input.mousePosition();
        if (input.isKeyDown(InputActionSystem::keyNameToCode("left_shift")) ||
            input.isKeyDown(InputActionSystem::keyNameToCode("right_shift"))) {
            event.state.modifiers = finevk::Modifier::Shift;
        }
        input.injectEvent(event);
        return Value::nil();
    });

    e.registerFunction("input_button", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 4) return Value::nil();
        finevk::InputEvent event;
        event.type = args[1].truthy() ? finevk::InputEventType::MouseButtonPress
                                      : finevk::InputEventType::MouseButtonRelease;
        event.mouseButton = static_cast<int>(number(args[0]));
        event.state.mousePosition = glm::vec2(number(args[2]), number(args[3]));
        engine->inputManager().injectEvent(event);
        return Value::nil();
    });

    // input_text "abc": typing, for scripts that test a text field
    e.registerFunction("input_text", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        for (char c : args[0].toString()) {
            finevk::InputEvent event;
            event.type = finevk::InputEventType::CharInput;
            event.character = static_cast<uint32_t>(static_cast<unsigned char>(c));
            engine->inputManager().injectEvent(event);
        }
        return Value::nil();
    });

    e.registerFunction("input_move", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        auto& input = engine->inputManager();
        finevk::InputEvent event;
        event.type = finevk::InputEventType::MouseMove;
        event.state.mouseDelta = glm::vec2(number(args[0]), number(args[1]));
        event.state.mousePosition = input.mousePosition() + event.state.mouseDelta;
        input.injectEvent(event);
        return Value::nil();
    });

    // predict_block x y z name: the player's own action, shown now and settled by the server.
    // expect_change x y z: an action the client can't foresee, so look again soon.
    e.registerFunction("predict_block", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 4) return Value::nil();
        BlockCoord pos(static_cast<int32_t>(number(args[0])), static_cast<int32_t>(number(args[1])),
                       static_cast<int32_t>(number(args[2])));
        std::string name = args[3].toString();
        if (session) session->predictBlock(pos, name == "air" ? BlockTypeId() : BlockTypeId::fromName(name));
        return Value::nil();
    });

    e.registerFunction("expect_change", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 3) return Value::nil();
        if (session) {
            session->expectChange(BlockCoord(static_cast<int32_t>(number(args[0])), static_cast<int32_t>(number(args[1])),
                                             static_cast<int32_t>(number(args[2]))));
        }
        return Value::nil();
    });

    // input_held -> true if the game thinks any movement key is down
    e.registerFunction("input_held", [this](ExecutionContext&, const Args&) -> Value {
        return Value::boolean(panForward || panBack || panLeft || panRight);
    });

    // key_held name -> true while that key is down, whoever got the event
    e.registerFunction("key_held", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::boolean(false);
        return Value::boolean(engine->inputManager().isKeyDown(InputActionSystem::keyNameToCode(args[0].toString())));
    });

    // pick_entities x0 y0 x1 y1 -> ids whose bodies project into the rectangle
    e.registerFunction("pick_entities", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 4) return Value::array(std::vector<Value>{});
        float x0 = static_cast<float>(number(args[0])), y0 = static_cast<float>(number(args[1]));
        float x1 = static_cast<float>(number(args[2])), y1 = static_cast<float>(number(args[3]));
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);

        // Small rectangles are clicks: allow for a hand that isn't quite on the body
        constexpr float SLOP = 3.0f;
        constexpr float BODY_HEIGHT = 1.9f;  // To the top of a helmet
        constexpr float BODY_RADIUS = 0.45f;
        glm::vec2 rectMin(x0 - SLOP, y0 - SLOP), rectMax(x1 + SLOP, y1 + SLOP);

        std::vector<Value> found;
        for (const auto& [id, state] : entityRenderer.entities()) {
            if (!state.isCurrent()) continue;
            // The player's own body takes no orders, wherever it was left standing
            if (id == stewardId) continue;
            if (bodyInScreenRect(view, state.currentPosition, BODY_HEIGHT, BODY_RADIUS, viewport,
                                 rectMin, rectMax)) {
                found.push_back(Value::integer(static_cast<int64_t>(id)));
            }
        }
        return Value::array(std::move(found));
    });

    // entity_pos id -> [x y z] (as last received; nil if unknown or hidden)
    e.registerFunction("entity_pos", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        const auto* state = entityRenderer.getEntity(static_cast<EntityId>(number(args[0])));
        if (!state || !state->isCurrent()) return Value::nil();
        return Value::array(std::vector<Value>{Value::number(state->currentPosition.x),
                                               Value::number(state->currentPosition.y),
                                               Value::number(state->currentPosition.z)});
    });

    e.registerFunction("entity_faction", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        const auto* state = entityRenderer.getEntity(static_cast<EntityId>(number(args[0])));
        return state ? Value::integer(state->faction) : Value::nil();
    });

    // entity_type id -> what it is, as a symbol (:militia), or nil if it isn't known here
    e.registerFunction("entity_type", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        const auto* state = entityRenderer.getEntity(static_cast<EntityId>(number(args[0])));
        return state ? Value::symbol(state->typeId.id) : Value::nil();
    });

    // entity_health id -> 0 to 1, as last received. 1 for anything unhurt, or not known.
    e.registerFunction("entity_health", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty() || !session) return Value::number(1.0);
        return Value::number(session->entityHealth(static_cast<EntityId>(number(args[0]))));
    });

    // marker_xray id r g b [a]: where the world hides this entity, its shape shows through in
    // that colour. Like every marker it lasts a frame, so it's asked for again each frame.
    e.registerFunction("marker_xray", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 4) return Value::nil();
        xrayNext[static_cast<EntityId>(number(args[0]))] = colorArg(args, 1);
        return Value::nil();
    });

    // marker_ring x y z radius r g b [a]
    e.registerFunction("marker_ring", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 7) return Value::nil();
        glm::vec3 center(glm::dvec3(number(args[0]), number(args[1]), number(args[2])) -
                         camera.positionD());
        float radius = static_cast<float>(number(args[3]));
        render::appendRing(markers, center, radius, 0.12f, colorArg(args, 4));
        return Value::nil();
    });

    // marker_box x y z sx sy sz r g b [a]: min corner and size, unlit
    e.registerFunction("marker_box", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 9) return Value::nil();
        glm::vec3 size(number(args[3]), number(args[4]), number(args[5]));
        glm::vec3 center(glm::dvec3(number(args[0]), number(args[1]), number(args[2])) -
                         camera.positionD());
        render::appendBox(markers, center + size * 0.5f, size * 0.5f, colorArg(args, 6), false);
        return Value::nil();
    });

    // marker_bar x y z width fraction: health bar facing the camera
    e.registerFunction("marker_bar", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 5) return Value::nil();
        glm::vec3 center(glm::dvec3(number(args[0]), number(args[1]), number(args[2])) -
                         camera.positionD());
        float halfWidth = static_cast<float>(number(args[3])) * 0.5f;
        float fraction = std::clamp(static_cast<float>(number(args[4])), 0.0f, 1.0f);
        auto& out = markers;
        render::appendBillboard(out, center, view.right, view.up, halfWidth, 0.07f,
                                glm::vec4(0.08f, 0.08f, 0.08f, 0.85f));
        glm::vec3 barCenter = center - view.right * (halfWidth * (1.0f - fraction));
        glm::vec4 color = fraction > 0.5f ? glm::vec4(0.35f, 0.85f, 0.35f, 1.0f)
                                          : glm::vec4(0.9f, 0.45f, 0.2f, 1.0f);
        render::appendBillboard(out, barCenter, view.right, view.up, halfWidth * fraction, 0.05f, color);
        return Value::nil();
    });

    // ui_rect x0 y0 x1 y1 r g b a: screen-space outline, drawn over everything
    e.registerFunction("ui_rect", [](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 8) return Value::nil();
        ImU32 color = ImGui::GetColorU32(ImVec4(float(number(args[4])), float(number(args[5])),
                                                float(number(args[6])), float(number(args[7]))));
        ImGui::GetForegroundDrawList()->AddRect(
            ImVec2(float(number(args[0])), float(number(args[1]))),
            ImVec2(float(number(args[2])), float(number(args[3]))), color, 0.0f, 0, 1.5f);
        return Value::nil();
    });

    e.registerFunction("request_screenshot", [this](ExecutionContext&, const Args& args) -> Value {
        // Only a game's own script gets to name a place outside the game: this one could be a server's
        std::string path = args.empty() ? "screenshots/shot.png" : args[0].toString();
        if (contentRoot != options.gameRoot && !net::isSafeContentPath(path)) return Value::nil();
        screenshotPath = freshPath(options.userRoot / path).string();
        screenshotRequested = true;
        return Value::nil();
    });

    // to_menu: leave this game for the menu. The same goes as for new_game.
    e.registerFunction("to_menu", [this](ExecutionContext&, const Args&) -> Value {
        restartRequested = true;
        engine->requestStop();
        return Value::nil();
    });

    // new_game: save this game and start another. Only a fresh process is sure of leaving
    // nothing of the old one behind, so the caller of GameClient::run does the rest.
    e.registerFunction("new_game", [this](ExecutionContext&, const Args&) -> Value {
        newGameRequested = true;
        engine->requestStop();
        return Value::nil();
    });

    e.registerFunction("quit_game", [this](ExecutionContext&, const Args&) -> Value {
        engine->requestStop();
        return Value::nil();
    });
}

void GameClient::Impl::registerNetNatives() {
    auto& e = *uiEngine;

    auto option = [](const Args& args, std::string_view name) -> Value {
        if (args.empty() || !args.back().isMap()) return Value::nil();
        return net::field(args.back(), name);
    };

    // sym_name :x -> "x"
    e.registerFunction("sym_name", [](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        if (!args[0].isSymbol()) return Value::string(args[0].toString());
        return Value::string(std::string(StringInterner::global().lookup(args[0].asSymbol())));
    });

    // pref name [default] -> the player's own setting, kept between runs
    e.registerFunction("pref", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        std::string key = args[0].isSymbol() ? std::string(StringInterner::global().lookup(args[0].asSymbol()))
                                             : args[0].toString();
        auto it = prefs.find(key);
        if (it != prefs.end()) return it->second;
        return args.size() > 1 ? args[1] : Value::nil();
    });

    // set_pref name value: a bool, number, symbol or string, saved at once. nil forgets it.
    e.registerFunction("set_pref", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        std::string key = args[0].isSymbol() ? std::string(StringInterner::global().lookup(args[0].asSymbol()))
                                             : args[0].toString();
        if (key.empty() || key.find(':') != std::string::npos || key.find('\n') != std::string::npos) return Value::nil();
        if (args.size() < 2 || args[1].isNil()) {
            prefs.erase(key);
        } else {
            prefs[key] = args[1];
        }
        savePrefs();
        return Value::nil();
    });

    // has_save -> whether there's a game to continue
    e.registerFunction("has_save", [this](ExecutionContext&, const Args&) -> Value {
        return Value::boolean(fs::is_directory(options.userRoot / "saves" / "autosave"));
    });

    e.registerFunction("player_name", [this](ExecutionContext&, const Args&) -> Value {
        return Value::string(playerName);
    });

    e.registerFunction("set_player_name", [this](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty()) playerName = args[0].toString();
        return Value::nil();
    });

    // game_start [=new true]: a game for one, with no lobby
    e.registerFunction("game_start", [this, option](ExecutionContext&, const Args& args) -> Value {
        startLocal(option(args, "new").truthy());
        return Value::nil();
    });

    // net_host [=listen true] [=title t] [=port p] [=password pw] [=new true]: a game in this
    // process, with its pre-game. Returns nil, or what went wrong.
    e.registerFunction("net_host", [this, option](ExecutionContext&, const Args& args) -> Value {
        if (!options.hostFactory) return Value::string("this client can't host a game");
        HostRequest request;
        request.listen = option(args, "listen").truthy();
        request.newGame = option(args, "new").truthy();
        request.title = net::textField(args.empty() ? Value::nil() : args.back(), "title");
        request.password = net::textField(args.empty() ? Value::nil() : args.back(), "password");
        if (Value port = option(args, "port"); port.isNumeric()) {
            request.port = static_cast<uint16_t>(port.asNumber());
        } else {
            request.port = manifest.netPort;
        }

        std::string error;
        host = options.hostFactory(request, error);
        if (!host) return Value::string(error);
        beginSession(playerName, "");
        session->attach(host->connect());
        return Value::nil();
    });

    // net_join address [=password p]: an address as people type them, or a list of them for one
    // server, as net_servers gives
    e.registerFunction("net_join", [this, option](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        std::vector<std::string> addresses;
        if (args[0].isArray()) {
            for (const Value& address : args[0].asArray()) addresses.push_back(address.toString());
        } else {
            addresses.push_back(args[0].toString());
        }
        Value port = option(args, "port");
        beginSession(playerName, net::textField(args.back(), "password"));
        session->connect(addresses, port.isNumeric() ? static_cast<uint16_t>(port.asNumber()) : manifest.netPort);
        if (args[0].isString()) noteRecentServer(addresses[0]);
        return Value::nil();
    });

    e.registerFunction("net_leave", [this](ExecutionContext&, const Args&) -> Value {
        if (session) session->leave();
        session.reset();
        if (host) host->stop();
        host.reset();
        return Value::nil();
    });

    e.registerFunction("net_phase", [this](ExecutionContext&, const Args&) -> Value {
        return net::symbol(session ? net::phaseName(session->phase()) : "idle");
    });

    e.registerFunction("net_failure", [this](ExecutionContext&, const Args&) -> Value {
        return Value::string(session ? session->failure() : "");
    });

    e.registerFunction("net_content_progress", [this](ExecutionContext&, const Args&) -> Value {
        return Value::number(session ? session->contentProgress() : 0.0);
    });

    // net_browse on: start or stop looking for servers. Remembered ones are asked directly.
    e.registerFunction("net_browse", [this](ExecutionContext&, const Args& args) -> Value {
        bool on = args.empty() || args[0].truthy();
        if (!on) {
            browser.reset();
            return Value::nil();
        }
        if (browser) {
            browser->refresh();
            return Value::nil();
        }
        finenet::DiscoveryBrowser::Config config;
        config.app = manifest.contentNamespace;
        browser = std::make_unique<finenet::DiscoveryBrowser>(config);
        for (const std::string& address : recentServers()) {
            if (auto endpoint = finenet::parseEndpoint(address)) browser->addProbe(endpoint->host);
        }
        return Value::nil();
    });

    // net_servers -> what's been found: each a listing, with =addresses =port =rtt added
    e.registerFunction("net_servers", [this](ExecutionContext&, const Args&) -> Value {
        std::vector<Value> found;
        if (!browser) return Value::array(std::move(found));
        for (const auto& server : browser->servers()) {
            Value listing = net::decodeBody(server.listing);
            if (!listing.isMap()) continue;
            std::vector<Value> addresses;
            for (const std::string& address : server.addresses) addresses.push_back(Value::string(address));
            net::setField(listing, "addresses", Value::array(std::move(addresses)));
            net::setField(listing, "port", Value::integer(server.port));
            net::setField(listing, "rtt", Value::number(server.rttMs));
            found.push_back(std::move(listing));
        }
        return Value::array(std::move(found));
    });

    // net_blocked -> the OS is refusing this process the local network
    e.registerFunction("net_blocked", [this](ExecutionContext&, const Args&) -> Value {
        return Value::boolean(browser && browser->blocked());
    });

    e.registerFunction("net_recent", [this](ExecutionContext&, const Args&) -> Value {
        std::vector<Value> out;
        for (const std::string& address : recentServers()) out.push_back(Value::string(address));
        return Value::array(std::move(out));
    });

    // net_addresses -> this machine's addresses, to read out to a friend
    e.registerFunction("net_addresses", [](ExecutionContext&, const Args&) -> Value {
        std::vector<Value> out;
        for (const std::string& address : finenet::localAddresses()) out.push_back(Value::string(address));
        return Value::array(std::move(out));
    });

    // net_open [port] -> nil, or what went wrong: this game, in its current state, is now on the
    // network. Nothing stops for it.
    e.registerFunction("net_open", [this](ExecutionContext&, const Args& args) -> Value {
        if (!host) return Value::string("only the host can open a game");
        uint16_t port = !args.empty() && args[0].isNumeric() ? static_cast<uint16_t>(args[0].asNumber()) : manifest.netPort;
        std::string error;
        if (!host->openToNetwork(port, error)) return Value::string(error);
        return Value::nil();
    });

    // net_port -> the port this game is open on, or nil
    e.registerFunction("net_port", [this](ExecutionContext&, const Args&) -> Value {
        if (!host || host->networkPort() == 0) return Value::nil();
        return Value::integer(host->networkPort());
    });

    // game_paused -> the server has stopped the simulation
    e.registerFunction("game_paused", [this](ExecutionContext&, const Args&) -> Value {
        return Value::boolean(session && session->paused());
    });

    e.registerFunction("net_is_host", [this](ExecutionContext&, const Args&) -> Value {
        return Value::boolean(host != nullptr);
    });

    e.registerFunction("net_me", [this](ExecutionContext&, const Args&) -> Value {
        return Value::integer(session ? session->playerId() : 0);
    });

    // lobby_info -> the game's lobby declaration: what can be set, and how a game starts
    e.registerFunction("lobby_info", [this](ExecutionContext&, const Args&) -> Value {
        return session ? finescript::cborDecode(session->lobbyDeclaration(), interner) : Value::nil();
    });

    e.registerFunction("lobby_set", [this](ExecutionContext&, const Args& args) -> Value {
        if (!session || args.size() < 2) return Value::nil();
        std::string name = args[0].isSymbol() ? std::string(StringInterner::global().lookup(args[0].asSymbol()))
                                              : args[0].toString();
        session->setSetting(name, args[1]);
        return Value::nil();
    });

    e.registerFunction("lobby_ready", [this](ExecutionContext&, const Args& args) -> Value {
        if (session) session->setReady(args.empty() || args[0].truthy());
        return Value::nil();
    });

    e.registerFunction("lobby_launch", [this](ExecutionContext&, const Args& args) -> Value {
        if (session) session->launch(args.empty() || args[0].truthy());
        return Value::nil();
    });

    e.registerFunction("lobby_kick", [this](ExecutionContext&, const Args& args) -> Value {
        if (session && !args.empty()) session->kick(static_cast<int64_t>(number(args[0])));
        return Value::nil();
    });

    // chat_send text [=team true]
    e.registerFunction("chat_send", [this, option](ExecutionContext&, const Args& args) -> Value {
        if (session && !args.empty()) session->chat(args[0].toString(), option(args, "team").truthy());
        return Value::nil();
    });
}

void GameClient::Impl::registerMcpTools() {
    mcp = options.mcp;
    if (!mcp) return;
    using nlohmann::json;
    using game::McpResult;
    using game::McpSide;
    using game::McpTool;

    McpTool state;
    state.name = "client_state";
    state.description =
        "What the client is doing: its view, camera, window size, frame rate and what it's "
        "drawing. In tactical view the cursor is at `mouse`; on foot it's the crosshair.";
    state.inputSchema = {{"type", "object"}, {"properties", json::object()}};
    state.side = McpSide::Frame;
    state.player = true;
    state.run = [this](const json&) {
        if (!inWorld) {
            return McpResult::value({{"in_game", false},
                                     {"phase", session ? std::string(net::phaseName(session->phase())) : "menu"},
                                     {"fps", fps},
                                     {"frame", frameCount}});
        }
        glm::dvec3 eye = camera.positionD();
        glm::dvec3 pivot = tactical.pivot();
        Vec3 body = stewardBody->position();
        return McpResult::value(
            {{"embodied", embodied},
             {"in_game", true},
             {"phase", std::string(net::phaseName(session->phase()))},
             {"camera", {eye.x, eye.y, eye.z}},
             {"pivot", {pivot.x, pivot.y, pivot.z}},
             {"yaw", tactical.yaw()},
             {"pitch", tactical.pitch()},
             {"distance", tactical.distance()},
             {"player_body", {body.x, body.y, body.z}},
             {"viewport", {viewport.x, viewport.y}},
             {"mouse", {mouse.x, mouse.y}},
             {"fps", fps},
             {"frame", frameCount},
             {"chunks_drawn", worldRenderer->renderedChunkCount()},
             {"triangles", worldRenderer->renderedTriangleCount()},
             {"entities", entityRenderer.entityCount()}});
    };
    mcp->addTool(std::move(state));

    McpTool eval;
    eval.name = "client_eval";
    eval.description =
        "Run finescript on the render thread, with the UI script's globals and natives, where "
        "its :frame handlers run. Drives the game as a player would: gui_click, input_key, "
        "input_button, input_move, do_action, cam_focus, cam_set. Calls need braces.";
    eval.inputSchema = game::mcpEvalSchema();
    eval.side = McpSide::Frame;
    eval.run = [this](const json& args) { return game::mcpEval(args, *uiEngine, *uiCtx); };
    mcp->addTool(std::move(eval));

    McpTool entities;
    entities.name = "client_entities";
    entities.description =
        "Entities this client can see, so fog of war hides what it should. Compare with "
        "server_entities. Positions are as last received. Filter with type, faction, near "
        "and radius.";
    entities.inputSchema = game::mcpEntityFilterSchema();
    entities.side = McpSide::Frame;
    entities.player = true;
    entities.run = [this](const json& args) {
        size_t limit = static_cast<size_t>(std::max(0, args.value("limit", 100)));
        json out = json::array();
        size_t matched = 0;
        for (const auto& [id, view] : entityRenderer.entities()) {
            if (!view.isCurrent()) continue;
            const glm::dvec3& pos = view.currentPosition;
            std::string type(view.typeId.name());
            if (!game::mcpEntityMatches(args, type, view.faction, pos.x, pos.y, pos.z)) continue;
            if (++matched > limit) continue;
            out.push_back({{"id", id},
                           {"type", type},
                           {"faction", view.faction},
                           {"position", {pos.x, pos.y, pos.z}},
                           {"you", id == stewardId}});
        }
        return McpResult::value({{"entities", std::move(out)}, {"matched", matched}});
    };
    mcp->addTool(std::move(entities));

    McpTool blocks;
    blocks.name = "client_blocks";
    blocks.description =
        "A region of the world as this client knows it, as server_blocks shows the real one: "
        "unexplored cells are air, and it can lag the server by a second. Limit 32768 cells.";
    blocks.inputSchema = game::mcpBlocksSchema();
    blocks.side = McpSide::Frame;
    blocks.player = true;
    blocks.run = [this](const json& args) {
        if (!session) return McpResult::error("no game yet");
        return game::mcpBlocks(args, session->world().world());
    };
    mcp->addTool(std::move(blocks));

    McpTool command;
    command.name = "client_command";
    command.description =
        "Send a game command, exactly as the UI's send_command does. This is how a player acts: "
        "the game's commands are whatever its `on :command` handler takes.";
    command.inputSchema = game::mcpCommandSchema();
    command.side = McpSide::Frame;
    command.player = true;
    command.run = [this](const json& args) {
        if (!session) return McpResult::error("no game yet");
        session->sendCommand(game::mcpCommandValue(args, *uiEngine));
        return McpResult::text("sent; the game thread takes it up between ticks");
    };
    mcp->addTool(std::move(command));

    McpTool screenshot;
    screenshot.name = "client_screenshot";
    screenshot.description =
        "The window as it looks now. A JPEG, 800 pixels on its longest side unless you ask "
        "otherwise. `region` picks out part of it, in framebuffer pixels: viewport times the "
        "display's scale, so 2x on a Retina screen. Images stay in the conversation and are "
        "sent again with every request, so ask for no more than you need.";
    screenshot.inputSchema = {
        {"type", "object"},
        {"properties",
         {{"max_size", {{"type", "integer"}, {"description", "Longest side in pixels (default 800)"}}},
          {"quality", {{"type", "integer"}, {"description", "JPEG quality, 1 to 100 (default 80)"}}},
          {"region", {{"type", "array"}, {"description", "[x, y, width, height] in framebuffer pixels"}}}}}};
    screenshot.side = McpSide::AfterFrame;
    screenshot.player = true;
    screenshot.run = [this](const json& args) {
        finevk::SwapChain* swapChain = engine->renderer().swapChain();
        if (!swapChain) return McpResult::error("no swap chain");

        render::CaptureRect rect;
        if (args.contains("region")) {
            const json& region = args["region"];
            if (region.size() != 4) return McpResult::error("region must be [x, y, width, height]");
            rect = {region[0].get<uint32_t>(), region[1].get<uint32_t>(), region[2].get<uint32_t>(),
                    region[3].get<uint32_t>()};
        }
        std::vector<uint8_t> bytes;
        uint32_t width = 0, height = 0;
        bool ok = render::FrameCapture::captureJPEG(
            swapChain->image(engine->window().currentImageIndex()), *stagingPool, rect,
            static_cast<uint32_t>(std::max(0, args.value("max_size", 800))),
            std::clamp(args.value("quality", 80), 1, 100), bytes, width, height,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        if (!ok) return McpResult::error("capture failed: is the region inside the window?");

        McpResult result = McpResult::image(bytes, "image/jpeg");
        result.content.push_back({{"type", "text"},
                                  {"text", std::to_string(width) + "x" + std::to_string(height) + ", " +
                                               std::to_string(bytes.size()) + " bytes"}});
        return result;
    };
    mcp->addTool(std::move(screenshot));
}

void GameClient::Impl::loadUiScript(const std::string& relative) {
    fs::path path = contentRoot / relative;
    if (!fs::is_regular_file(path)) {
        std::cerr << "[ui script] not found: " << path << "\n";
        return;
    }
    auto* script = uiEngine->loadScript(path);
    auto result = uiEngine->execute(*script, *uiCtx);
    if (!result.success) {
        std::cerr << "[ui script] " << path.string() << ":" << result.errorLine << ": "
                  << result.error << "\n";
    }
}

GameClient::GameClient(ClientOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {
    impl_->setupShell();
}

GameClient::~GameClient() = default;

bool GameClient::newGameRequested() const {
    return impl_->newGameRequested;
}

bool GameClient::restartRequested() const {
    return impl_->restartRequested;
}

int GameClient::run() {
    impl_->engine->run();
    impl_->shutdown();
    return 0;
}

}  // namespace finevox::client
