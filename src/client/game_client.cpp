#include "finevox/client/game_client.hpp"

#include "finevox/core/action_dispatch.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_model.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/input_action_system.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/physics.hpp"
#include "finevox/core/player_controller.hpp"
#include "finevox/core/sky.hpp"
#include "finevox/core/tactical_camera.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/render/block_atlas.hpp"
#include "finevox/render/engine.hpp"
#include "finevox/render/entity_renderer.hpp"
#include "finevox/render/frame_capture.hpp"
#include "finevox/render/particle_atlas.hpp"
#include "finevox/render/particle_renderer.hpp"
#include "finevox/render/scene_renderer.hpp"
#include "finevox/render/world_render_layer.hpp"
#include "finevox/render/world_renderer.hpp"
#include "finevox/script/event_value.hpp"
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

#include <cmath>
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

class GameRootFinder : public finescript::ResourceFinder {
public:
    explicit GameRootFinder(fs::path root) : root_(std::move(root)) {}

    fs::path resolve(std::string_view name) override {
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

/// The server stops sending entities the viewer can't see, so silence means hidden
bool isCurrent(const EntityRenderState& state) {
    constexpr float STALE_SECONDS = 0.5f;
    return state.visible && state.active && state.timeSinceSnapshot < STALE_SECONDS;
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
    Impl(game::GameRuntime& rt, ClientOptions opts) : runtime(rt), options(std::move(opts)) {}

    ~Impl() {
        // Members below hold GPU resources that in-flight frames may still use
        if (engine) engine->device().waitIdle();
        finegui::setActiveSkin(nullptr);
    }

    game::GameRuntime& runtime;
    ClientOptions options;

    std::unique_ptr<Engine> engine;
    std::unique_ptr<finegui::GuiSystem> gui;
    std::unique_ptr<finescript::ScriptEngine> uiEngine;
    std::unique_ptr<finegui::MapRenderer> mapRenderer;
    std::unique_ptr<finegui::ScriptGuiManager> guiManager;
    std::unique_ptr<finegui::TextureRegistry> textureRegistry;
    std::vector<std::unique_ptr<finegui::Skin>> skins;
    std::unique_ptr<ExecutionContext> uiCtx;
    std::unique_ptr<GameRootFinder> finder;
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
    glm::vec2 viewport{1600.0f, 900.0f};
    SkyParameters sky;
    std::unordered_map<uint16_t, glm::vec3> factionColors;

    InputActionSystem inputActions;
    bool panLeft = false, panRight = false, panForward = false, panBack = false;
    float groundCheckIn = 0.0f;     ///< Seconds until the camera next looks at the ground
    int grabHeld = 0;               ///< Bindings holding the world in hand: it takes either
    glm::vec2 grabCursor{0.0f};
    bool orbitLeft = false, orbitRight = false;
    glm::vec2 mouse{0.0f};

    struct FogQuad {
        glm::dvec3 center;
        float alpha;
    };
    std::vector<uint8_t> fogCells;
    std::vector<FogQuad> fogQuads;

    finevk::StagingPoolPtr stagingPool;
    uint64_t frameCount = 0;
    bool screenshotRequested = false;
    std::string screenshotPath;
    int fps = 0;
    int framesThisSecond = 0;
    float fpsTimer = 0.0f;

    void setup();
    void registerNatives();
    void loadUiScript();
    void fireUi(uint32_t symbol);
    void bindInput();
    void updateCamera(float dt);
    void buildScene();
    void drainUiMessages();
    void buildProjectiles();
    void buildFogOverlay();
    void setEmbodied(bool on);
    void updateStewardView(float dt);
    glm::vec3 factionColor(uint16_t faction) const;
    [[nodiscard]] RaycastResult pickBlock(glm::vec2 cursor) const;
    bool handleAction(const ActionResult& result);
    [[nodiscard]] std::vector<int32_t> groundHeightsAround(const glm::dvec3& centre) const;

    uint32_t symFrame = 0, symMouse = 0, symAction = 0, symState = 0, symKey = 0;
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

void GameClient::Impl::setup() {
    const game::GameManifest& manifest = runtime.manifest();
    GameSession& session = runtime.session();
    // Everything drawn comes from the replica, which holds only what the viewer saw
    World& world = runtime.replica().world();

    EngineConfig engineConfig;
    engineConfig.windowTitle = manifest.windowTitle;
    engineConfig.windowWidth = options.width;
    engineConfig.windowHeight = options.height;
    engineConfig.enableValidation = options.validation;
    engine = std::make_unique<Engine>(engineConfig);

    auto& window = engine->window();
    auto& device = engine->device();
    auto& renderer = engine->renderer();

    stagingPool = finevk::StagingPool::create(&device).initialSize(4 * 1024 * 1024).build();

    finegui::GuiConfig guiConfig;
    guiConfig.fontSize = 16.0f;
    guiConfig.msaaSamples = renderer.msaaSamples();
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
    finder = std::make_unique<GameRootFinder>(runtime.gameRoot());
    uiEngine->setResourceFinder(finder.get());
    uiCtx = std::make_unique<ExecutionContext>(*uiEngine);

    symFrame = uiEngine->intern("frame");
    symMouse = uiEngine->intern("mouse");
    symAction = uiEngine->intern("action");
    symState = uiEngine->intern("state");
    symKey = uiEngine->intern("key");

    render::WorldRendererConfig worldConfig;
    worldConfig.viewDistance = manifest.viewDistance;
    worldRenderer = std::make_unique<render::WorldRenderer>(&device, &renderer, world, worldConfig);
    worldRenderer->setLODEnabled(manifest.lod);

    std::string shaderDir = options.shaderDir.string() + "/";
    worldRenderer->loadShaders(shaderDir + "chunk.vert.spv", shaderDir + "chunk.frag.spv");
    worldRenderer->loadFluidShader(shaderDir + "fluid.frag.spv");

    atlas = std::make_unique<render::BlockAtlas>();
    atlas->buildFromTextures(&device, renderer.commandPool(),
                             (runtime.gameRoot() / "textures").string(),
                             runtime.content().blockTextures());
    worldRenderer->setBlockAtlas(atlas->texture());
    worldRenderer->setTextureProvider(atlas->createProvider());
    worldRenderer->setGeometryProvider(runtime.content().geometryProvider());
    worldRenderer->setFaceOccludesProvider(runtime.content().faceOccludesProvider(world));
    worldRenderer->initialize();

    particleAtlas = std::make_unique<render::ParticleAtlas>();
    fs::path particleDir = runtime.gameRoot() / "textures" / "particles";
    if (particleAtlas->loadFromDirectory(&device, renderer.commandPool(), particleDir.string()) == 0) {
        particleAtlas->createPlaceholder(&device, renderer.commandPool());
    }
    particleAtlas->applyToRegistry(ParticleTypeRegistry::global(), nullptr);
    particleRenderer = std::make_unique<render::ParticleRenderer>(&device, &renderer, session.particles());
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
        [this](finevk::CommandBuffer& cmd) { sceneRenderer->drawTranslucent(cmd); });

    models.loadDirectory(runtime.gameRoot() / "models");
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

    LightEngine& light = session.lightEngine();
    worldRenderer->setLightProvider([&light](const BlockCoord& pos) -> uint8_t {
        return static_cast<uint8_t>((light.getSkyLight(pos) << 4) | light.getBlockLight(pos));
    });
    worldRenderer->setSmoothLighting(true);
    light.setMeshRebuildQueue(worldRenderer->meshRebuildQueue());
    world.setMeshRebuildQueue(worldRenderer->meshRebuildQueue());
    world.setAlwaysDeferMeshRebuild(false);
    glm::dvec3 start(0.0, double(manifest.seaLevel + 10), 0.0);
    worldRenderer->rebuildAllMeshesNear(start);

    int32_t radius = manifest.worldRadius * 16;
    tactical.setBounds(glm::vec2(float(-radius)), glm::vec2(float(radius)));
    camera.setPerspective(FOV_DEGREES, float(window.width()) / float(window.height()), NEAR_PLANE,
                          FAR_PLANE);
    tactical.setPivot(start);

    std::string bindings = (runtime.gameRoot() / "input" / "default.bindings").string();
    if (fs::exists(bindings)) inputActions.loadBindings(bindings);

    stewardShapes = createBlockShapeProvider(world);
    stewardPhysics = std::make_unique<PhysicsSystem>(stewardShapes);
    stewardBody = std::make_unique<SimplePhysicsBody>(
        Vec3(0.0f, float(manifest.seaLevel + 16), 0.0f), Vec3(0.35f, 0.9f, 0.35f));
    steward.setPhysics(stewardBody.get(), stewardPhysics.get());
    steward.setFlyMode(false);
    stewardId = session.entities().spawnPlayer(stewardBody->position());
    session.entities().setLocalPlayerId(stewardId);
    if (MobEntity* mob = session.entities().getMob(stewardId)) {
        mob->setFaction(manifest.viewerFaction);
    }

    registerNatives();
    bindInput();
    uiCtx->set("view_mode", Value::symbol(uiEngine->intern("tactical")));
    loadUiScript();

    auto worldLayer = std::make_shared<render::WorldRenderLayer>(*worldRenderer);
    engine->addRenderLayer(worldLayer);

    auto guiLayer = std::make_shared<GuiLayer>();
    guiLayer->renderFn = [this](const RenderContext& ctx) {
        gui->beginFrame();
        fireUi(symFrame);
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

        auto framePeriod = worldRenderer->recordFrameStart();

        GameSession& s = runtime.session();
        sky = computeSkyParameters(s.worldTime().timeOfDay(), runtime.content().skyConfig());
        worldRenderer->setSkyParameters(sky);
        if (worldRenderer->fogDynamicColor()) worldRenderer->setFogColor(sky.fogColor);

        entityRenderer.processEvents(s.graphicsEvents());
        entityRenderer.update(dt, camera.positionD());
        drainUiMessages();

        updateCamera(dt);
        worldRenderer->updateCamera(camera.state(), camera.positionD());
        sceneRenderer->updateCamera(camera.state(), camera.positionD());
        sceneRenderer->setSky(sky.sunDirection, glm::vec3(sky.fogColor), 0.0f, 0.0f,
                              sky.skyBrightness, sky.ambientLevel);
        buildScene();
        buildProjectiles();
        buildFogOverlay();

        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::microseconds(framePeriod.count() / 2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!worldRenderer->waitForMeshUploads(deadline)) break;
            worldRenderer->updateMeshes(0);
        }

        ++framesThisSecond;
        fpsTimer += dt;
        if (fpsTimer >= 1.0f) {
            fps = framesThisSecond;
            framesThisSecond = 0;
            fpsTimer -= 1.0f;
            if (options.printStats) {
                std::cout << "[client] fps=" << fps << " chunks=" << worldRenderer->renderedChunkCount()
                          << "/" << worldRenderer->loadedChunkCount()
                          << " culled=" << worldRenderer->culledChunkCount()
                          << " tris=" << worldRenderer->renderedTriangleCount()
                          << " entities=" << entityRenderer.entityCount()
                          << " cam=" << camera.positionD().x << "," << camera.positionD().y << ","
                          << camera.positionD().z << std::endl;
            }
        }
    };
    callbacks.onResize = [this](uint32_t width, uint32_t height) {
        if (height > 0) {
            camera.setPerspective(FOV_DEGREES, float(width) / float(height), NEAR_PLANE, FAR_PLANE);
        }
    };
    callbacks.getClearColor = [this]() -> glm::vec4 {
        // With fog on, the edge of the known world should read as darkness
        return runtime.manifest().fogOfWar ? glm::vec4(0.015f, 0.02f, 0.04f, 1.0f) : sky.skyColor;
    };
    callbacks.onPostRender = [this]() {
        ++frameCount;
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
                bool ok = render::FrameCapture::captureImage(swapChain->image(index), *stagingPool,
                                                             screenshotPath,
                                                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
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
    engine->setFenceWakeSignal(worldRenderer->wakeSignal());
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
    else if (action == "begin_jump") {
        jumpHeld = true;
        if (embodied) steward.requestJump();
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

void GameClient::Impl::setEmbodied(bool on) {
    embodied = on;
    auto& input = engine->inputManager();
    panLeft = panRight = panForward = panBack = false;
    grabHeld = 0;
    steward.clearInput();

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
    steward.update(dt);

    Vec3 pos = stewardBody->position();
    Vec3 eye(pos.x, pos.y + steward.eyeHeight(), pos.z);
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
    runtime.session().actions().sendPlayerState(stewardId, state);
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

    const World& world = runtime.replica().world();
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
    LightEngine& light = runtime.session().lightEngine();
    glm::dvec3 cameraPos = camera.positionD();
    float tickRate = static_cast<float>(runtime.manifest().tickRate);
    auto& opaque = sceneRenderer->opaque();

    for (const auto& [id, state] : entityRenderer.entities()) {
        if (!isCurrent(state)) continue;

        // The camera is inside this one, or just past it: its drawn position trails the
        // live body by a tick, which on walking backward puts it in front of the eye
        if (embodied && id == stewardId) continue;

        const EntityTypeDef* def = EntityTypeRegistry::global().getType(state.typeId);
        float alpha = state.interpolationAlpha(tickRate);
        glm::dvec3 pos = state.interpolatedPosition(alpha);

        BlockCoord cell(static_cast<int32_t>(std::floor(pos.x)),
                        static_cast<int32_t>(std::floor(pos.y)) + 1,
                        static_cast<int32_t>(std::floor(pos.z)));
        float skyLight = light.getSkyLight(cell) / 15.0f * sky.skyBrightness;
        float blockLight = light.getBlockLight(cell) / 15.0f;
        float brightness = std::max({skyLight, blockLight, 0.3f}) * 1.6f;

        glm::vec3 team = factionColor(state.faction);
        const EntityModel* model =
            def && def->model != 0 ? models.find(StringInterner::global().lookup(def->model)) : nullptr;
        if (model) {
            render::appendEntity(opaque, *model, state, cameraPos, team, brightness, tickRate,
                                 poseScratch);
        } else {
            glm::vec3 half = def ? def->halfExtents : glm::vec3(0.3f, 0.9f, 0.3f);
            glm::vec3 center(pos - cameraPos);
            center.y += half.y;
            render::appendBox(opaque, center, half, glm::vec4(team * brightness, 1.0f));
        }
    }
}

void GameClient::Impl::buildProjectiles() {
    glm::dvec3 cameraPos = camera.positionD();
    auto& opaque = sceneRenderer->opaque();
    for (const ProjectileView& p : runtime.projectiles().snapshot()) {
        glm::vec3 center(p.position - cameraPos);
        glm::vec3 dir = glm::length(p.velocity) > 0.01f ? glm::normalize(p.velocity) : glm::vec3(0, 1, 0);
        for (float along : {-0.3f, -0.1f, 0.1f, 0.3f}) {
            render::appendBox(opaque, center + dir * along, glm::vec3(0.05f),
                              glm::vec4(0.25f, 0.18f, 0.1f, 1.0f));
        }
    }
}

void GameClient::Impl::buildFogOverlay() {
    if (!runtime.manifest().fogOfWar) return;

    // Vision and terrain change far slower than frames, and finding each cell's top
    // means walking down a column of blocks. Do that a few times a second.
    if (frameCount % FOG_REFRESH_FRAMES == 0 || fogCells.empty()) {
        fogCells = runtime.vision().snapshot(runtime.manifest().viewerFaction);
        fogQuads.clear();

        const FactionVision& vision = runtime.vision();
        const VisionBounds& bounds = vision.bounds();
        int32_t cell = FactionVision::CELL_SIZE;
        World& world = runtime.replica().world();
        double range = tactical.distance() * 2.5 + 64.0;
        glm::dvec3 pivot = tactical.pivot();

        int32_t minCx = std::max(0, static_cast<int32_t>((pivot.x - range - bounds.minX) / cell));
        int32_t maxCx = std::min(vision.cellsX() - 1, static_cast<int32_t>((pivot.x + range - bounds.minX) / cell));
        int32_t minCz = std::max(0, static_cast<int32_t>((pivot.z - range - bounds.minZ) / cell));
        int32_t maxCz = std::min(vision.cellsZ() - 1, static_cast<int32_t>((pivot.z + range - bounds.minZ) / cell));

        for (int32_t cz = minCz; cz <= maxCz; ++cz) {
            for (int32_t cx = minCx; cx <= maxCx; ++cx) {
                uint8_t state = fogCells[size_t(cz) * vision.cellsX() + cx];
                if (state == static_cast<uint8_t>(VisionState::Visible)) continue;

                int32_t x = bounds.minX + cx * cell;
                int32_t z = bounds.minZ + cz * cell;
                int32_t top = static_cast<int32_t>(pivot.y) + 48;
                while (top > 0 && world.getBlock(x + cell / 2, top, z + cell / 2).isAir()) --top;
                if (top <= 0) continue;

                float alpha = state == static_cast<uint8_t>(VisionState::Explored) ? 0.42f : 0.97f;
                fogQuads.push_back({glm::dvec3(x + cell * 0.5, top + 1.05, z + cell * 0.5), alpha});
            }
        }
    }

    glm::dvec3 cameraPos = camera.positionD();
    float half = FactionVision::CELL_SIZE * 0.5f;
    auto& overlay = sceneRenderer->translucent();
    overlay.reserve(overlay.size() + fogQuads.size() * 6);
    for (const FogQuad& quad : fogQuads) {
        render::appendBillboard(overlay, glm::vec3(quad.center - cameraPos), glm::vec3(1, 0, 0),
                                glm::vec3(0, 0, 1), half, half,
                                glm::vec4(0.02f, 0.03f, 0.06f, quad.alpha));
    }
}

void GameClient::Impl::drainUiMessages() {
    for (auto& bytes : runtime.uiOutbox().drainAll()) {
        Value message = finescript::cborDecode(bytes, interner);
        uiCtx->set("msg", std::move(message));
        fireUi(symState);
    }
}

RaycastResult GameClient::Impl::pickBlock(glm::vec2 cursor) const {
    WorldRay ray = screenRayToWorld(view, cursor, viewport);
    BlockShapeProvider shapes = createBlockShapeProvider(runtime.replica().world());
    Vec3 origin(static_cast<float>(ray.origin.x), static_cast<float>(ray.origin.y),
                static_cast<float>(ray.origin.z));
    return raycastBlocks(origin, Vec3(ray.direction), PICK_RANGE, RaycastMode::Interaction, shapes);
}

void GameClient::Impl::registerNatives() {
    auto& e = *uiEngine;
    GameSession& session = runtime.session();

    e.registerFunction("send_command", [this, &session](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        // Through CBOR so the game thread gets its own copy and nothing is shared
        auto bytes = finescript::cborEncode(args[0], interner);
        session.actions().sendGameCommand(finescript::cborDecode(bytes, interner));
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
        fs::path path = runtime.gameRoot() / std::string(args[0].asString());
        auto skin = finegui::loadSkin(*gui, path, *uiEngine, *uiCtx, error);
        if (!skin) {
            std::cerr << "[ui script] skin " << path.string() << ": " << error << "\n";
            return Value::boolean(false);
        }
        skins.push_back(std::move(skin));
        finegui::setActiveSkin(skins.back().get());
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
        const auto* schematic = runtime.blueprints().find(name);
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
        RaycastResult hit = pickBlock(viewport * 0.5f);
        if (!hit.hit) return Value::nil();
        auto result = Value::map();
        auto& map = result.asMap();
        map.set(uiEngine->intern("x"), Value::integer(hit.blockPos.x));
        map.set(uiEngine->intern("y"), Value::integer(hit.blockPos.y));
        map.set(uiEngine->intern("z"), Value::integer(hit.blockPos.z));
        return result;
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

    e.registerFunction("cam_set", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() >= 3) {
            tactical.setDistance(static_cast<float>(number(args[0])));
            tactical.orbit(static_cast<float>(number(args[1])) - tactical.yaw(),
                           static_cast<float>(number(args[2])) - tactical.pitch());
        }
        return Value::nil();
    });

    // pick_block mx my -> {=x =y =z =nx =ny =nz} or nil
    e.registerFunction("pick_block", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::nil();
        RaycastResult hit = pickBlock(glm::vec2(number(args[0]), number(args[1])));
        if (!hit.hit) return Value::nil();
        auto result = Value::map();
        auto& map = result.asMap();
        BlockCoord n = BlockCoord(hit.blockPos.x, hit.blockPos.y, hit.blockPos.z);
        map.set(uiEngine->intern("x"), Value::integer(n.x));
        map.set(uiEngine->intern("y"), Value::integer(n.y));
        map.set(uiEngine->intern("z"), Value::integer(n.z));
        return result;
    });

    // pick_entities x0 y0 x1 y1 -> ids whose bodies project into the rectangle
    e.registerFunction("pick_entities", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 4) return Value::array(std::vector<Value>{});
        float x0 = static_cast<float>(number(args[0])), y0 = static_cast<float>(number(args[1]));
        float x1 = static_cast<float>(number(args[2])), y1 = static_cast<float>(number(args[3]));
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);

        // Small rectangles are clicks: a click anywhere on a body should hit it
        constexpr float SLOP = 6.0f;
        x0 -= SLOP; y0 -= SLOP; x1 += SLOP; y1 += SLOP;

        std::vector<Value> found;
        for (const auto& [id, state] : entityRenderer.entities()) {
            if (!isCurrent(state)) continue;
            glm::dvec3 feet = state.currentPosition;
            auto top = worldToScreen(view, feet + glm::dvec3(0.0, 1.7, 0.0), viewport);
            auto bottom = worldToScreen(view, feet, viewport);
            if (!top || !bottom) continue;
            float minY = std::min(top->y, bottom->y), maxY = std::max(top->y, bottom->y);
            float centerX = (top->x + bottom->x) * 0.5f;
            if (centerX >= x0 && centerX <= x1 && maxY >= y0 && minY <= y1) {
                found.push_back(Value::integer(static_cast<int64_t>(id)));
            }
        }
        return Value::array(std::move(found));
    });

    // entity_pos id -> [x y z] (as last received; nil if unknown or hidden)
    e.registerFunction("entity_pos", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        const auto* state = entityRenderer.getEntity(static_cast<EntityId>(number(args[0])));
        if (!state || !isCurrent(*state)) return Value::nil();
        return Value::array(std::vector<Value>{Value::number(state->currentPosition.x),
                                               Value::number(state->currentPosition.y),
                                               Value::number(state->currentPosition.z)});
    });

    e.registerFunction("entity_faction", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        const auto* state = entityRenderer.getEntity(static_cast<EntityId>(number(args[0])));
        return state ? Value::integer(state->faction) : Value::nil();
    });

    // marker_ring x y z radius r g b [a]
    e.registerFunction("marker_ring", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 7) return Value::nil();
        glm::vec3 center(glm::dvec3(number(args[0]), number(args[1]), number(args[2])) -
                         camera.positionD());
        float radius = static_cast<float>(number(args[3]));
        render::appendRing(sceneRenderer->translucent(), center, radius, 0.12f, colorArg(args, 4));
        return Value::nil();
    });

    // marker_box x y z sx sy sz r g b [a]: min corner and size, unlit
    e.registerFunction("marker_box", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 9) return Value::nil();
        glm::vec3 size(number(args[3]), number(args[4]), number(args[5]));
        glm::vec3 center(glm::dvec3(number(args[0]), number(args[1]), number(args[2])) -
                         camera.positionD());
        render::appendBox(sceneRenderer->translucent(), center + size * 0.5f, size * 0.5f,
                          colorArg(args, 6), false);
        return Value::nil();
    });

    // marker_bar x y z width fraction: health bar facing the camera
    e.registerFunction("marker_bar", [this](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 5) return Value::nil();
        glm::vec3 center(glm::dvec3(number(args[0]), number(args[1]), number(args[2])) -
                         camera.positionD());
        float halfWidth = static_cast<float>(number(args[3])) * 0.5f;
        float fraction = std::clamp(static_cast<float>(number(args[4])), 0.0f, 1.0f);
        auto& out = sceneRenderer->translucent();
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
        screenshotPath = freshPath(args.empty() ? "screenshots/shot.png" : args[0].toString()).string();
        screenshotRequested = true;
        return Value::nil();
    });

    e.registerFunction("quit_game", [this](ExecutionContext&, const Args&) -> Value {
        engine->requestStop();
        return Value::nil();
    });
}

void GameClient::Impl::loadUiScript() {
    fs::path path = runtime.gameRoot() / runtime.manifest().uiScript;
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

GameClient::GameClient(game::GameRuntime& runtime, ClientOptions options)
    : impl_(std::make_unique<Impl>(runtime, std::move(options))) {
    impl_->setup();
}

GameClient::~GameClient() {
    impl_->runtime.stopThreads();
}

int GameClient::run() {
    impl_->runtime.startThreads();
    impl_->engine->run();
    impl_->runtime.stopThreads();
    return 0;
}

}  // namespace finevox::client
