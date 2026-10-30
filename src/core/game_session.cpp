#include "finevox/core/game_session.hpp"
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/event_bus.hpp"
#include "finevox/core/block_events.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/column_manager.hpp"
#include "finevox/core/io_manager.hpp"
#include "finevox/core/tick_journal.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_event.hpp"
#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/item_drop_entity.hpp"
#include "finevox/core/loot_registry.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/crafting_helper.hpp"
#include "finevox/core/inventory.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/particle_subsystem.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/script/event_value.hpp"
#include "finevox/script/finevox_interner.hpp"

#include <finenet/finenet.hpp>
#include <finescript/cbor.h>
#include <finescript/value.h>
#include <finescript/map_data.h>

#include <thread>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <random>

namespace finevox {

using namespace finevox::script;

// ============================================================================
// GameActions convenience methods — build Values and call sendAction()
// ============================================================================

bool GameActions::breakBlock(BlockCoord pos, ItemTypeId tool,
                             bool preciseBreak, EntityId actor) {
    auto oldType = BlockTypeId::fromName("finevox:air"); // actual type checked server-side
    sendAction(makeBlockBrokenValue(pos, oldType, tool, preciseBreak, actor));
    return true;
}

bool GameActions::placeBlock(BlockCoord pos, BlockTypeId type,
                             Rotation rotation, EntityId actor) {
    auto oldType = BlockTypeId::fromName("finevox:air");
    sendAction(makeBlockPlacedValue(pos, type, oldType, rotation, actor));
    return true;
}

bool GameActions::useBlock(BlockCoord pos, Face face, EntityId actor) {
    sendAction(makePlayerUseValue(pos, face, actor));
    return true;
}

bool GameActions::hitBlock(BlockCoord pos, Face face, EntityId actor) {
    sendAction(makePlayerHitValue(pos, face, actor));
    return true;
}

bool GameActions::placeFluid(BlockCoord pos, FluidTypeId type, uint8_t level) {
    sendAction(makeFluidPlacedValue(pos, type, level));
    return true;
}

bool GameActions::removeFluid(BlockCoord pos) {
    auto emptyFluid = FluidTypeId{}; // empty
    sendAction(makeFluidRemovedValue(pos, emptyFluid));
    return true;
}

void GameActions::sendPlayerState(EntityId id, const EntityState& state) {
    sendAction(makePlayerPositionValue(id, state.position, state.velocity,
                                        state.onGround, state.inputSequence,
                                        state.yaw, state.pitch));
}

void GameActions::setWorldTime(int64_t ticks) {
    sendAction(makeSetWorldTimeValue(ticks));
}

bool GameActions::craftItem(BlockCoord stationPos, RecipeId recipe) {
    sendAction(makeCraftItemValue(stationPos, recipe));
    return true;
}

void GameActions::attackEntity(EntityId attacker, EntityId target, finescript::Value damageInfo) {
    sendAction(makeAttackEntityValue(attacker, target, std::move(damageInfo)));
}

void GameActions::spawnParticleEmitter(std::string_view preset, glm::dvec3 pos,
                                        EntityId attached, float duration) {
    sendAction(makeParticleSpawnValue(preset, pos, attached, duration));
}

void GameActions::despawnParticleEmitter(uint32_t emitterId) {
    sendAction(makeParticleDespawnValue(emitterId));
}

void GameActions::particleBurst(std::string_view preset, glm::dvec3 pos) {
    sendAction(makeParticleBurstValue(preset, pos));
}

void GameActions::sendGameCommand(finescript::Value command) {
    sendAction(makeGameCommandValue(std::move(command)));
}

// ============================================================================
// LocalGameActions — single-player implementation
//
// Sound events are pushed eagerly on the calling thread (instant audio).
// Block mutations and player state are deferred to the game thread via
// the command queue.
// ============================================================================

class LocalGameActions : public GameActions {
public:
    LocalGameActions(World& world, SoundEventQueue& soundQueue, GameCommandQueue& commandQueue,
                     finenet::Connection* clientConn, finenet::Connection* serverConn,
                     bool enableSound = true)
        : world_(world), soundQueue_(soundQueue), commandQueue_(commandQueue),
          clientConnection_(clientConn), serverConnection_(serverConn),
          enableSound_(enableSound) {}

    void sendAction(finescript::Value action) override {
        if (action.isNil()) return;

        // Route across the in-process finenet::Connection pair when connected
        if (clientConnection_ && serverConnection_ &&
            clientConnection_->isConnected() && serverConnection_->isConnected()) {
            try {
                FineVoxInterner interner;
                auto cborBytes = finescript::cborEncode(action, interner);

                auto typeStr = readEventType(action);
                finenet::ChannelId ch = finenet::Channels::Actions.id;
                finenet::MessageType msgType = finenet::MessageType::PlayerAction;
                if (typeStr == EVT_PLAYER_POSITION || typeStr == EVT_PLAYER_LOOK ||
                    typeStr == EVT_PLAYER_JUMP || typeStr == EVT_PLAYER_SPRINT ||
                    typeStr == EVT_PLAYER_SNEAK) {
                    ch = finenet::Channels::Input.id;
                    msgType = finenet::MessageType::PlayerInput;
                }

                auto msg = finenet::MessageBuilder(msgType).writeCBOR(cborBytes).build();
                if (clientConnection_->send(ch, std::move(msg)) == finenet::SendResult::Ok) {
                    serverConnection_->update();
                    return;
                }
            } catch (...) {
                // Fall back to direct queue push if action contains non-CBOR values
            }
        }

        // Push the raw Value into the command queue for the game thread
        commandQueue_.push(std::move(action));
    }

    // Override typed methods to add eager sound feedback before queuing
    bool breakBlock(BlockCoord pos, ItemTypeId tool = {},
                    bool preciseBreak = false,
                    EntityId actor = INVALID_ENTITY_ID) override {
        BlockTypeId oldType = world_.getBlock(pos);
        if (oldType.isAir()) return false;

        // Sound eagerly (instant audio feedback on calling thread)
        if (enableSound_) {
            auto soundSet = BlockRegistry::global().getType(oldType).soundSet();
            if (soundSet.isValid()) {
                soundQueue_.push(makeSoundEventValue(soundSet, "break", "effects", pos));
            }
        }

        // Defer mutation to game thread
        sendAction(makeBlockBrokenValue(pos, oldType, tool, preciseBreak, actor));
        return true;
    }

    bool placeBlock(BlockCoord pos, BlockTypeId type,
                    Rotation rotation = Rotation::IDENTITY,
                    EntityId actor = INVALID_ENTITY_ID) override {
        // Sound eagerly
        if (enableSound_) {
            auto soundSet = BlockRegistry::global().getType(type).soundSet();
            if (soundSet.isValid()) {
                soundQueue_.push(makeSoundEventValue(soundSet, "place", "effects", pos));
            }
        }

        // Defer mutation to game thread
        sendAction(makeBlockPlacedValue(pos, type, world_.getBlock(pos), rotation, actor));
        return true;
    }

    bool placeFluid(BlockCoord pos, FluidTypeId type, uint8_t level) override {
        // Sound eagerly
        if (enableSound_) {
            const FluidType* ft = FluidRegistry::global().getType(type);
            if (ft && ft->soundSet.isValid()) {
                soundQueue_.push(makeSoundEventValue(ft->soundSet, "place", "effects", pos));
            }
        }

        // Defer mutation to game thread
        sendAction(makeFluidPlacedValue(pos, type, level));
        return true;
    }

    bool removeFluid(BlockCoord pos) override {
        FluidTypeId oldFluid = world_.getFluid(pos);
        if (oldFluid.isEmpty()) return false;

        // Sound eagerly
        if (enableSound_) {
            const FluidType* ft = FluidRegistry::global().getType(oldFluid);
            if (ft && ft->soundSet.isValid()) {
                soundQueue_.push(makeSoundEventValue(ft->soundSet, "break", "effects", pos));
            }
        }

        // Defer mutation to game thread
        sendAction(makeFluidRemovedValue(pos, oldFluid));
        return true;
    }

    bool useBlock(BlockCoord pos, Face face,
                  EntityId actor = INVALID_ENTITY_ID) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        sendAction(makePlayerUseValue(pos, face, actor));
        return true;
    }

    bool hitBlock(BlockCoord pos, Face face,
                  EntityId actor = INVALID_ENTITY_ID) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        sendAction(makePlayerHitValue(pos, face, actor));
        return true;
    }

private:
    World& world_;
    SoundEventQueue& soundQueue_;
    GameCommandQueue& commandQueue_;
    finenet::Connection* clientConnection_ = nullptr;
    finenet::Connection* serverConnection_ = nullptr;
    bool enableSound_ = true;
};

// ============================================================================
// Command execution — reads finescript Value commands, dispatches to subsystems
// ============================================================================

static void executeCommand(World& world, UpdateScheduler& scheduler,
                           EntityManager& entityManager, WorldTime& worldTime,
                           FluidTickManager* fluidTickManager,
                           ParticleSystem* particles,
                           EventBus* eventBus,
                           const std::function<void(const finescript::Value&)>& gameCommandHandler,
                           const finescript::Value& cmd) {
    auto typeStr = readEventType(cmd);

    if (typeStr == EVT_BLOCK_BROKEN) {
        auto pos = readBlockCoord(cmd);

        // Roll loot table while block is still present.
        // The subsequent world.breakBlock() → onDestroy handler may depend on
        // block-state; we observe loot here so default-drop behavior is
        // engine-mechanism (data/.loot files are the policy).
        BlockTypeId oldType = world.getBlock(pos);
        if (!oldType.isAir()) {
            const BlockType& bt = BlockRegistry::global().getType(oldType);
            LootTableId tableId = bt.lootTable();
            if (tableId.isValid()) {
                const auto& s = EventSymbols::instance();
                LootContext lootCtx;
                lootCtx.brokenBlock = oldType;
                lootCtx.position = pos;
                lootCtx.toolUsed = readItemTypeId(cmd, s.tool_type);
                lootCtx.preciseBreak = readBool(cmd, s.precise_break, false);

                auto drops = LootRegistry::global().roll(tableId, lootCtx);
                if (!drops.empty()) {
                    // Deterministic-ish scatter: use world tick as seed bias so
                    // back-to-back breaks of identical blocks don't stack perfectly.
                    std::mt19937 scatterRng(
                        static_cast<uint32_t>(pos.x * 73856093u)
                        ^ static_cast<uint32_t>(pos.y * 19349663u)
                        ^ static_cast<uint32_t>(pos.z * 83492791u));
                    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
                    std::uniform_real_distribution<float> speedDist(0.8f, 1.6f);

                    for (auto& stack : drops) {
                        if (stack.isEmpty()) continue;
                        auto drop = std::make_unique<ItemDropEntity>(
                            INVALID_ENTITY_ID, std::move(stack));
                        drop->setPosition(Vec3(
                            static_cast<float>(pos.x) + 0.5f,
                            static_cast<float>(pos.y) + 0.2f,
                            static_cast<float>(pos.z) + 0.5f));
                        float theta = angleDist(scatterRng);
                        float horiz = speedDist(scatterRng);
                        drop->setVelocity(Vec3(
                            std::cos(theta) * horiz,
                            2.5f,
                            std::sin(theta) * horiz));
                        entityManager.spawnEntity(std::move(drop));
                    }
                }
            }
        }

        // Particles: per-block-type emitter override, else the generic
        // default. Policy lives in .model files; the engine only checks
        // whether a preset is registered.
        if (particles && !oldType.isAir()) {
            const BlockType& bt = BlockRegistry::global().getType(oldType);
            EmitterPresetId presetId = bt.particleEmitter();
            std::string_view presetName =
                presetId.isValid() ? presetId.name()
                                   : std::string_view("finevox:block_break");
            if (EmitterPresetRegistry::global().has(presetName)) {
                glm::dvec3 center(pos.x + 0.5, pos.y + 0.5, pos.z + 0.5);
                particles->burst(presetName, center);
            }
        }

        EntityId actor = readEntityId(cmd);
        world.breakBlock(pos, actor);
        if (fluidTickManager) fluidTickManager->notifyBlockChanged(pos);
        if (eventBus) eventBus->publish(BlockBrokenEvent{pos, oldType});
    }
    else if (typeStr == EVT_BLOCK_PLACED) {
        auto pos = readBlockCoord(cmd);
        const auto& s = EventSymbols::instance();
        BlockTypeId oldType = world.getBlock(pos);
        auto blockType = readBlockTypeId(cmd, s.block_type);
        uint8_t rotIdx = static_cast<uint8_t>(readInt(cmd, s.rotation, 0));
        Rotation rot = Rotation::byIndex(rotIdx);
        EntityId actor = readEntityId(cmd);
        world.placeBlock(pos, blockType, rot, actor);
        if (fluidTickManager) fluidTickManager->notifyBlockChanged(pos);
        if (eventBus) eventBus->publish(BlockPlacedEvent{pos, blockType, oldType, rot});
    }
    else if (typeStr == EVT_FLUID_PLACED) {
        auto pos = readBlockCoord(cmd);
        auto fluidType = readFluidTypeId(cmd);
        auto level = static_cast<uint8_t>(readInt(cmd, EventSymbols::instance().fluid_level, 15));
        world.setFluid(pos, fluidType, level);
        if (fluidTickManager) fluidTickManager->notifyFluidChanged(pos);
        if (eventBus) eventBus->publish(FluidPlacedEvent{pos, fluidType, level});
    }
    else if (typeStr == EVT_FLUID_REMOVED) {
        auto pos = readBlockCoord(cmd);
        FluidTypeId prevFluid = world.getFluid(pos);
        if (prevFluid.isEmpty()) {
            prevFluid = readFluidTypeId(cmd);
        }
        world.removeFluid(pos);
        if (fluidTickManager) fluidTickManager->notifyFluidChanged(pos);
        if (eventBus) eventBus->publish(FluidRemovedEvent{pos, prevFluid});
    }
    else if (typeStr == EVT_PLAYER_USE) {
        auto pos = readBlockCoord(cmd);
        auto face = readFace(cmd);
        EntityId actor = readEntityId(cmd);
        scheduler.pushExternalEvent(BlockEvent::playerUse(pos, face, actor));
        if (eventBus) eventBus->publish(PlayerUseEvent{pos, face, actor});
    }
    else if (typeStr == EVT_PLAYER_HIT) {
        auto pos = readBlockCoord(cmd);
        auto face = readFace(cmd);
        EntityId actor = readEntityId(cmd);
        scheduler.pushExternalEvent(BlockEvent::playerHit(pos, face, actor));
        if (eventBus) eventBus->publish(PlayerHitEvent{pos, face, actor});
    }
    else if (typeStr == EVT_PLAYER_POSITION) {
        auto id = readEntityId(cmd);
        const auto& s = EventSymbols::instance();
        auto pos = readDVec3(cmd, s.pos_x, s.pos_y, s.pos_z);
        auto vel = readDVec3(cmd, s.vel_x, s.vel_y, s.vel_z);
        bool onGround = readBool(cmd, s.on_ground);
        uint64_t seq = static_cast<uint64_t>(readInt(cmd, s.input_sequence));

        BlockEvent event = BlockEvent::playerPosition(id, pos, vel, onGround, seq);
        event.entityState.yaw = readFloat(cmd, s.yaw, 0.0f);
        event.entityState.pitch = readFloat(cmd, s.pitch, 0.0f);
        entityManager.handlePlayerPosition(event);
        if (eventBus) eventBus->publish(PlayerPositionEvent{id, event.entityState});
    }
    else if (typeStr == EVT_PLAYER_LOOK) {
        auto id = readEntityId(cmd);
        const auto& s = EventSymbols::instance();
        float yaw = readFloat(cmd, s.yaw);
        float pitch = readFloat(cmd, s.pitch);

        BlockEvent event = BlockEvent::playerLook(id, yaw, pitch);
        entityManager.handlePlayerLook(event);
        if (eventBus) eventBus->publish(PlayerLookEvent{id, event.entityState});
    }
    else if (typeStr == EVT_PLAYER_JUMP) {
        auto id = readEntityId(cmd);
        BlockEvent event = BlockEvent::playerJump(id);
        entityManager.handlePlayerJump(event);
        if (eventBus) eventBus->publish(PlayerJumpEvent{id});
    }
    else if (typeStr == EVT_PLAYER_SPRINT) {
        auto id = readEntityId(cmd);
        bool starting = readBool(cmd, EventSymbols::instance().starting);
        BlockEvent event = BlockEvent::playerSprint(id, starting);
        entityManager.handlePlayerSprint(event, starting);
        if (eventBus) eventBus->publish(PlayerSprintEvent{id, starting});
    }
    else if (typeStr == EVT_PLAYER_SNEAK) {
        auto id = readEntityId(cmd);
        bool starting = readBool(cmd, EventSymbols::instance().starting);
        BlockEvent event = BlockEvent::playerSneak(id, starting);
        entityManager.handlePlayerSneak(event, starting);
        if (eventBus) eventBus->publish(PlayerSneakEvent{id, starting});
    }
    else if (typeStr == EVT_SET_WORLD_TIME) {
        int64_t ticks = readInt(cmd, EventSymbols::instance().ticks);
        worldTime.setTime(ticks);
        if (eventBus) eventBus->publish(SetWorldTimeEvent{ticks});
    }
    else if (typeStr == EVT_CRAFT_ITEM) {
        const auto& s = EventSymbols::instance();
        BlockCoord stationPos{
            static_cast<int32_t>(readInt(cmd, s.station_pos_x, 0)),
            static_cast<int32_t>(readInt(cmd, s.station_pos_y, 0)),
            static_cast<int32_t>(readInt(cmd, s.station_pos_z, 0))
        };
        RecipeId recipeId{static_cast<InternedId>(readInt(cmd, s.recipe_id, 0))};
        const Recipe* recipe = RecipeRegistry::global().getRecipe(recipeId);
        bool crafted = false;

        if (recipe && !recipe->isSmelting()) {
            Entity* player = entityManager.getEntity(entityManager.localPlayerId());
            if (player && player->entityData()) {
                DataContainer* dc = player->entityData();
                DataContainer* gridChild = dc->getChild("crafting_grid");
                if (!gridChild) {
                    gridChild = dc->getChild("craft_grid");
                }
                if (gridChild) {
                    int32_t gridW = gridChild->get<int32_t>("width", recipe->width > 0 ? recipe->width : 3);
                    int32_t gridH = gridChild->get<int32_t>("height", recipe->height > 0 ? recipe->height : 3);
                    InventoryView gridView(*gridChild, world.nameRegistry());
                    std::vector<ItemTypeId> slots(static_cast<size_t>(gridW * gridH));
                    for (int32_t i = 0; i < gridW * gridH; ++i) {
                        auto stack = gridView.getSlot(i);
                        slots[static_cast<size_t>(i)] = stack.isEmpty() ? EMPTY_ITEM_TYPE : stack.type;
                    }
                    if (CraftingHelper::canCraft(*recipe, slots.data(), gridW, gridH)) {
                        CraftingHelper::MatchResult match;
                        if (recipe->isShaped()) {
                            match = CraftingHelper::matchShaped(*recipe, slots.data(), gridW, gridH);
                        } else {
                            match.recipe = recipe;
                        }
                        if (match.recipe && CraftingHelper::executeCraft(match, gridView, gridW, gridH)) {
                            crafted = true;
                            if (DataContainer* bagChild = dc->getChild("bag")) {
                                InventoryView bagView(*bagChild, world.nameRegistry());
                                bagView.addItem(recipe->outputItem, recipe->outputCount);
                            }
                        }
                    }
                } else {
                    crafted = true;
                }
            } else {
                crafted = true;
            }
        }

        if (eventBus) {
            eventBus->publish(CraftItemEvent{stationPos, recipeId, crafted});
        }
    }
    else if (typeStr == EVT_ATTACK_ENTITY) {
        const auto& s = EventSymbols::instance();
        EntityId attackerId = static_cast<EntityId>(readInt(cmd, s.attacker_id));
        EntityId targetId = static_cast<EntityId>(readInt(cmd, s.target_id));

        // Look up target mob and apply damage
        if (auto* mob = dynamic_cast<MobEntity*>(entityManager.getEntity(targetId))) {
            // Read damage amount from the damage_info sub-map
            auto damageInfoVal = cmd.asMap().get(s.damage_info);
            float amount = 0.0f;
            if (damageInfoVal.isMap()) {
                auto amountSym = StringInterner::global().intern("amount");
                auto amtVal = damageInfoVal.asMap().get(amountSym);
                if (amtVal.isFloat()) amount = static_cast<float>(amtVal.asFloat());
                else if (amtVal.isInt()) amount = static_cast<float>(amtVal.asInt());
            }

            if (amount > 0.0f) {
                mob->damage(amount, attackerId);
            }
        }
    }
    else if (typeStr == EVT_PARTICLE_SPAWN) {
        if (!particles) return;
        const auto& s = EventSymbols::instance();
        std::string preset(readString(cmd, s.preset));
        if (preset.empty()) return;

        glm::dvec3 position = readDVec3(cmd, s.pos_x, s.pos_y, s.pos_z);
        EntityId attached = static_cast<EntityId>(
            readInt(cmd, s.entity_id, static_cast<int64_t>(INVALID_ENTITY_ID)));

        // Duration: if field missing, pass sentinel (-2) to use preset default
        float duration = -2.0f;
        if (cmd.asMap().get(s.duration).isNumeric()) {
            duration = readFloat(cmd, s.duration, -2.0f);
        }

        if (attached != INVALID_ENTITY_ID) {
            particles->spawnEmitterAttached(preset, attached, duration);
        } else {
            particles->spawnEmitter(preset, position, duration);
        }
    }
    else if (typeStr == EVT_PARTICLE_DESPAWN) {
        if (!particles) return;
        const auto& s = EventSymbols::instance();
        auto id = static_cast<EmitterId>(readInt(cmd, s.emitter_id, 0));
        if (id != INVALID_EMITTER_ID) particles->despawnEmitter(id);
    }
    else if (typeStr == EVT_PARTICLE_BURST) {
        if (!particles) return;
        const auto& s = EventSymbols::instance();
        std::string preset(readString(cmd, s.preset));
        if (preset.empty()) return;
        glm::dvec3 position = readDVec3(cmd, s.pos_x, s.pos_y, s.pos_z);
        particles->burst(preset, position);
    }
    else if (typeStr == EVT_GAME_COMMAND) {
        if (gameCommandHandler) gameCommandHandler(readGameCommandBody(cmd));
    }
}

// ============================================================================
// Built-in subsystem wrappers
// ============================================================================

class WorldTimeSubsystem : public GameSubsystem {
public:
    explicit WorldTimeSubsystem(WorldTime& wt) : worldTime_(wt) {}
    std::string_view name() const override { return "WorldTime"; }
    TickPhase phase() const override { return TickPhase::PreTick; }
    int32_t priority() const override { return 0; }
    void tick(float dt) override { worldTime_.advance(dt); }
private:
    WorldTime& worldTime_;
};

class BlockEventSubsystem : public GameSubsystem {
public:
    explicit BlockEventSubsystem(UpdateScheduler& sched) : scheduler_(sched) {}
    std::string_view name() const override { return "BlockEvents"; }
    TickPhase phase() const override { return TickPhase::Tick; }
    int32_t priority() const override { return 0; }
    void tick(float /*dt*/) override {
        scheduler_.advanceGameTick();
        scheduler_.processEvents();
    }
private:
    UpdateScheduler& scheduler_;
};

class FluidSubsystem : public GameSubsystem {
public:
    explicit FluidSubsystem(FluidTickManager& ftm) : fluidTicks_(ftm) {}
    std::string_view name() const override { return "FluidSim"; }
    TickPhase phase() const override { return TickPhase::Tick; }
    int32_t priority() const override { return 100; }
    void tick(float /*dt*/) override { fluidTicks_.tick(); }
private:
    FluidTickManager& fluidTicks_;
};

class EntitySubsystem : public GameSubsystem {
public:
    explicit EntitySubsystem(EntityManager& em) : entities_(em) {}
    std::string_view name() const override { return "Entities"; }
    TickPhase phase() const override { return TickPhase::PostTick; }
    int32_t priority() const override { return 0; }
    void tick(float dt) override { entities_.tick(dt); }
private:
    EntityManager& entities_;
};

class ColumnLifecycleSubsystem : public GameSubsystem {
public:
    explicit ColumnLifecycleSubsystem(ColumnManager& cm) : columnManager_(cm) {}
    std::string_view name() const override { return "ColumnLifecycle"; }
    TickPhase phase() const override { return TickPhase::PostTick; }
    int32_t priority() const override { return 100; }
    void tick(float /*dt*/) override {
        columnManager_.tick();
        columnManager_.processSaveQueue();
    }
private:
    ColumnManager& columnManager_;
};

// ============================================================================
// GameSession::Impl
// ============================================================================

struct GameSession::Impl {
    // Owned subsystems (order matters for destruction)
    std::unique_ptr<World> world;
    std::unique_ptr<ColumnManager> columnManager;
    std::unique_ptr<TickJournal> tickJournal;
    std::unique_ptr<IOManager> ioManager;
    IOManager* activeIOManager = nullptr;
    std::unique_ptr<UpdateScheduler> scheduler;
    std::unique_ptr<LightEngine> lightEngine;
    std::unique_ptr<SoundEventQueue> soundQueue;
    std::unique_ptr<GraphicsEventQueue> graphicsQueue;
    std::unique_ptr<EntityManager> entityManager;
    std::unique_ptr<WorldTime> worldTime;
    std::unique_ptr<FluidTickManager> fluidTickManager;
    std::unique_ptr<ParticleSystem> particleSystem;

    // EventBus
    std::shared_ptr<EventBus> eventBus;
    std::function<void(const finescript::Value&)> gameCommandHandler;

    // Registered subsystems (sorted by phase/priority)
    std::vector<std::shared_ptr<GameSubsystem>> subsystems;

    // Command queue (graphics thread -> game thread)
    std::unique_ptr<GameCommandQueue> commandQueue;

    // In-process finenet::Connection local pair
    std::unique_ptr<finenet::Connection> clientConnection;
    std::unique_ptr<finenet::Connection> serverConnection;

    // Command interface
    std::unique_ptr<LocalGameActions> actions;

    // Game thread
    std::thread gameThread;
    std::atomic<bool> gameThreadRunning{false};

    // Config
    GameSessionConfig config;

    // Sort subsystems by (phase, priority)
    void sortSubsystems() {
        std::sort(subsystems.begin(), subsystems.end(),
            [](const std::shared_ptr<GameSubsystem>& a,
               const std::shared_ptr<GameSubsystem>& b) {
                if (a->phase() != b->phase())
                    return static_cast<uint8_t>(a->phase()) < static_cast<uint8_t>(b->phase());
                return a->priority() < b->priority();
            });
    }

    // Drain and execute all pending commands, then process scheduler events
    void drainAndExecuteCommands() {
        if (serverConnection) {
            serverConnection->update();
        }
        if (clientConnection) {
            clientConnection->update();
        }

        auto commands = commandQueue->drainAll();
        if (commands.empty()) return;

        for (const auto& cmd : commands) {
            executeCommand(*world, *scheduler, *entityManager, *worldTime,
                           fluidTickManager.get(), particleSystem.get(),
                           eventBus.get(), gameCommandHandler, cmd);
        }
        scheduler->processEvents();
    }

    // Tick all subsystems in order
    void tickSubsystems(float dt) {
        for (auto& sub : subsystems) {
            sub->tick(dt);
        }
    }

    // Game thread main loop
    void gameThreadLoop() {
        using Clock = std::chrono::steady_clock;

        const float tickDt = 1.0f / static_cast<float>(config.tickRate);
        const auto tickInterval = std::chrono::microseconds(
            static_cast<int64_t>(1000000.0 / config.tickRate));

        auto nextTickTime = Clock::now() + tickInterval;
        commandQueue->setAlarm(nextTickTime);

        while (commandQueue->waitForWork()) {
            // 1. Drain and execute all pending commands
            drainAndExecuteCommands();

            // 2. Dispatch cross-thread events
            if (eventBus) eventBus->dispatchQueued();

            // 3. Process ticks that are due
            auto now = Clock::now();
            int catchup = 0;
            while (now >= nextTickTime && catchup < 10) {
                tickSubsystems(tickDt);

                nextTickTime += tickInterval;
                ++catchup;
            }

            // If we fell behind too much, skip ahead
            if (catchup >= 10 && Clock::now() >= nextTickTime) {
                nextTickTime = Clock::now() + tickInterval;
            }

            // 4. Set next tick alarm
            commandQueue->setAlarm(nextTickTime);
        }
    }
};

// ============================================================================
// GameSession
// ============================================================================

GameSession::GameSession() : impl_(std::make_unique<Impl>()) {}

GameSession::~GameSession() {
    // Stop game thread first
    stopGameThread();

    // Detach subsystems in reverse order
    for (auto it = impl_->subsystems.rbegin(); it != impl_->subsystems.rend(); ++it) {
        (*it)->onDetach();
    }

    // Stop lighting thread before destroying world
    if (impl_->lightEngine && impl_->lightEngine->isRunning()) {
        impl_->lightEngine->stop();
    }

    // Flush and stop persistence I/O before destroying ColumnManager and World
    if (impl_->columnManager) {
        impl_->columnManager->processSaveQueue();
        impl_->columnManager->unbindIOManager();
    }
    if (impl_->ioManager) {
        impl_->ioManager->flush();
        impl_->ioManager->stop();
    }
}

std::unique_ptr<GameSession> GameSession::createLocal(const GameSessionConfig& config) {
    auto session = std::unique_ptr<GameSession>(new GameSession());
    auto& impl = *session->impl_;
    impl.config = config;

    // Create EventBus
    impl.eventBus = std::make_shared<EventBus>();

    // Create core systems
    impl.world = std::make_unique<World>();
    impl.columnManager = std::make_unique<ColumnManager>(config.columnCacheCapacity);
    impl.world->setColumnManager(impl.columnManager.get());

    impl.scheduler = std::make_unique<UpdateScheduler>(*impl.world);
    {
        TickConfig tickCfg = impl.scheduler->tickConfig();
        tickCfg.randomTicksPerSubchunk = config.randomTicksPerChunk;
        tickCfg.randomTicksEnabled = (config.randomTicksPerChunk > 0);
        if (config.tickRate > 0) {
            tickCfg.gameTickIntervalMs = 1000u / config.tickRate;
        }
        impl.scheduler->setTickConfig(tickCfg);
    }

    // Lighting
    impl.lightEngine = std::make_unique<LightEngine>(*impl.world);
    impl.lightEngine->setMaxPropagationDistance(10000);

    // Wire lighting and scheduler to world
    if (config.enableLighting) {
        impl.world->setLightEngine(impl.lightEngine.get());
    }
    impl.world->setUpdateScheduler(impl.scheduler.get());

    // Event queues
    impl.soundQueue = std::make_unique<SoundEventQueue>();
    impl.graphicsQueue = std::make_unique<GraphicsEventQueue>();
    impl.commandQueue = std::make_unique<GameCommandQueue>();

    // In-process finenet::Connection local pair
    {
        finenet::ConnectionConfig connCfg;
        connCfg.debugSerialization = config.debugNetSerialization;
        for (auto& ch : connCfg.channels) {
            ch.maxMessageSize = 0;
        }
        auto [clientConn, serverConn] = finenet::Connection::createLocalPair(connCfg);
        impl.clientConnection = std::move(clientConn);
        impl.serverConnection = std::move(serverConn);

        // Server receives actions & inputs from clientConnection and pushes to commandQueue
        auto* cmdQueuePtr = impl.commandQueue.get();
        auto serverActionHandler = [cmdQueuePtr](const finenet::Message& msg) {
            finenet::MessageReader reader(msg);
            auto cborSpan = reader.readCBOR();
            if (reader.valid() && !cborSpan.empty()) {
                FineVoxInterner interner;
                auto val = finescript::cborDecode(cborSpan.data(), cborSpan.size(), interner);
                cmdQueuePtr->push(std::move(val));
            }
        };
        impl.serverConnection->onMessage(finenet::MessageType::PlayerAction, serverActionHandler);
        impl.serverConnection->onMessage(finenet::MessageType::PlayerInput, serverActionHandler);
        impl.serverConnection->onChannel(finenet::Channels::Actions, serverActionHandler);
        impl.serverConnection->onChannel(finenet::Channels::Input, serverActionHandler);

        // Client receives sound events (Effects channel) and entity/visual events (Entities channel)
        auto* sndQueuePtr = impl.soundQueue.get();
        impl.clientConnection->onMessage(finenet::MessageType::SoundEvent,
            [sndQueuePtr](const finenet::Message& msg) {
                finenet::MessageReader reader(msg);
                auto cborSpan = reader.readCBOR();
                if (reader.valid() && !cborSpan.empty()) {
                    FineVoxInterner interner;
                    auto val = finescript::cborDecode(cborSpan.data(), cborSpan.size(), interner);
                    sndQueuePtr->push(std::move(val));
                }
            });

        auto* gfxQueuePtr = impl.graphicsQueue.get();
        auto clientGfxEventHandler = [gfxQueuePtr](const finenet::Message& msg) {
            finenet::MessageReader reader(msg);
            auto cborSpan = reader.readCBOR();
            if (reader.valid() && !cborSpan.empty()) {
                FineVoxInterner interner;
                auto val = finescript::cborDecode(cborSpan.data(), cborSpan.size(), interner);
                gfxQueuePtr->push(GraphicsMessage::fromEvent(std::move(val)));
            }
        };
        impl.clientConnection->onMessage(finenet::MessageType::EntitySpawn, clientGfxEventHandler);
        impl.clientConnection->onMessage(finenet::MessageType::EntityDespawn, clientGfxEventHandler);
        impl.clientConnection->onMessage(finenet::MessageType::EntityAnimation, clientGfxEventHandler);
        impl.clientConnection->onMessage(finenet::MessageType::PlayerCorrection, clientGfxEventHandler);
        impl.clientConnection->onMessage(finenet::MessageType::BlockCorrection, clientGfxEventHandler);
        impl.clientConnection->onMessage(finenet::MessageType::EntityPosition,
            [gfxQueuePtr](const finenet::Message& msg) {
                finenet::MessageReader reader(msg);
                uint64_t tickNum = reader.readVarInt();
                auto cborSpan = reader.readCBOR();
                if (reader.valid() && !cborSpan.empty()) {
                    std::vector<uint8_t> bytes(cborSpan.begin(), cborSpan.end());
                    EntitySnapshot snap;
                    snap.entity = EntityState::fromCBOR(bytes);
                    snap.tickNumber = tickNum;
                    gfxQueuePtr->push(GraphicsMessage::fromSnapshot(std::move(snap)));
                }
            });
    }

    // Entity system
    impl.entityManager = std::make_unique<EntityManager>(*impl.world, *impl.graphicsQueue);
    impl.entityManager->physics().setGravity(config.gravity);
    if (config.enableSound) {
        impl.entityManager->setSoundQueue(impl.soundQueue.get());
    }

    // Wire ColumnManager entity & tick persistence callbacks
    auto* emPtr = impl.entityManager.get();
    auto* schedPtr = impl.scheduler.get();
    impl.columnManager->setBeforeSaveCallback([emPtr](ChunkColumn& col) {
        emPtr->saveColumnEntities(col);
    });
    impl.columnManager->setColumnLoadedCallback([emPtr](ChunkColumn& col) {
        emPtr->loadColumnEntities(col);
    });
    impl.columnManager->setTickExtractCallback([schedPtr](ColumnPos pos) {
        return schedPtr->extractTicksForColumn(pos);
    });
    impl.columnManager->setPreEvictionCallback([&impl](ColumnPos pos) {
        auto ticks = impl.scheduler->extractTicksForColumn(pos);
        if (!ticks.empty() && impl.activeIOManager) {
            impl.activeIOManager->queueTickSave(pos, std::move(ticks));
        }
    });

    // Optional disk persistence via IOManager
    if (!config.worldSaveDir.empty()) {
        std::filesystem::path saveDir(config.worldSaveDir);
        impl.tickJournal = std::make_unique<TickJournal>();
        impl.tickJournal->setDirectory(saveDir / "tick_journals");
        impl.scheduler->setTickJournal(impl.tickJournal.get());

        impl.ioManager = std::make_unique<IOManager>(saveDir);
        impl.ioManager->setTickJournal(impl.tickJournal.get());
        impl.ioManager->setTickLoadCallback([schedPtr](ColumnPos, std::vector<ScheduledTick> ticks) {
            schedPtr->pushPendingTicks(std::move(ticks));
        });
        impl.ioManager->start();
        session->bindIOManager(impl.ioManager.get());
    }

    // World time
    impl.worldTime = std::make_unique<WorldTime>();
    impl.worldTime->setTicksPerSecond(static_cast<float>(config.tickRate));

    // Fluid simulation
    if (config.enableFluidSimulation) {
        impl.fluidTickManager = std::make_unique<FluidTickManager>(*impl.world);
        impl.world->setFluidTickManager(impl.fluidTickManager.get());
        if (config.enableLighting) {
            impl.fluidTickManager->simulator().setLightEngine(impl.lightEngine.get());
        }
    }

    // Particle simulation (CPU, game-thread authoritative)
    impl.particleSystem = std::make_unique<ParticleSystem>(*impl.world);
    impl.entityManager->setParticleSystem(impl.particleSystem.get());

    // Command interface
    impl.actions = std::make_unique<LocalGameActions>(
        *impl.world, *impl.soundQueue, *impl.commandQueue,
        impl.clientConnection.get(), impl.serverConnection.get(),
        config.enableSound);

    // Register built-in subsystems
    session->addSubsystem(std::make_shared<WorldTimeSubsystem>(*impl.worldTime));
    session->addSubsystem(std::make_shared<BlockEventSubsystem>(*impl.scheduler));
    session->addSubsystem(std::make_shared<EntitySubsystem>(*impl.entityManager));
    session->addSubsystem(std::make_shared<ColumnLifecycleSubsystem>(*impl.columnManager));

    if (impl.fluidTickManager) {
        session->addSubsystem(std::make_shared<FluidSubsystem>(*impl.fluidTickManager));
    }

    // Particles run in LateTick so attached emitters see post-physics positions.
    session->addSubsystem(std::make_shared<ParticleSubsystem>(
        *impl.particleSystem, impl.entityManager.get()));

    return session;
}

GameActions& GameSession::actions() { return *impl_->actions; }

World& GameSession::world() { return *impl_->world; }
const World& GameSession::world() const { return *impl_->world; }

UpdateScheduler& GameSession::scheduler() { return *impl_->scheduler; }
LightEngine& GameSession::lightEngine() { return *impl_->lightEngine; }
EntityManager& GameSession::entities() { return *impl_->entityManager; }
WorldTime& GameSession::worldTime() { return *impl_->worldTime; }
FluidTickManager& GameSession::fluidTicks() { return *impl_->fluidTickManager; }
ParticleSystem& GameSession::particles() { return *impl_->particleSystem; }
ColumnManager& GameSession::columnManager() { return *impl_->columnManager; }
IOManager* GameSession::ioManager() { return impl_->activeIOManager; }

void GameSession::bindIOManager(IOManager* io) {
    impl_->activeIOManager = io;
    if (impl_->columnManager) {
        if (io) {
            impl_->columnManager->bindIOManager(io);
        } else {
            impl_->columnManager->unbindIOManager();
        }
    }
}

SoundEventQueue& GameSession::soundEvents() { return *impl_->soundQueue; }
GraphicsEventQueue& GameSession::graphicsEvents() { return *impl_->graphicsQueue; }

EventBus& GameSession::eventBus() { return *impl_->eventBus; }

void GameSession::setGameCommandHandler(std::function<void(const finescript::Value&)> handler) {
    impl_->gameCommandHandler = std::move(handler);
}

finenet::Connection& GameSession::clientConnection() { return *impl_->clientConnection; }
const finenet::Connection& GameSession::clientConnection() const { return *impl_->clientConnection; }
finenet::Connection& GameSession::serverConnection() { return *impl_->serverConnection; }
const finenet::Connection& GameSession::serverConnection() const { return *impl_->serverConnection; }

// ============================================================================
// Subsystem Management
// ============================================================================

void GameSession::addSubsystem(std::shared_ptr<GameSubsystem> subsystem) {
    subsystem->onAttach(*this, *impl_->eventBus);
    impl_->subsystems.push_back(std::move(subsystem));
    impl_->sortSubsystems();
}

void GameSession::removeSubsystem(const std::shared_ptr<GameSubsystem>& subsystem) {
    auto it = std::find(impl_->subsystems.begin(), impl_->subsystems.end(), subsystem);
    if (it != impl_->subsystems.end()) {
        (*it)->onDetach();
        impl_->subsystems.erase(it);
    }
}

const std::vector<std::shared_ptr<GameSubsystem>>& GameSession::subsystems() const {
    return impl_->subsystems;
}

// ============================================================================
// Game Thread Lifecycle
// ============================================================================

void GameSession::startGameThread() {
    if (impl_->gameThreadRunning.load(std::memory_order_acquire)) {
        return;  // Already running
    }

    impl_->commandQueue->resetShutdown();
    impl_->gameThreadRunning.store(true, std::memory_order_release);
    impl_->gameThread = std::thread([this]() {
        impl_->gameThreadLoop();
        impl_->gameThreadRunning.store(false, std::memory_order_release);
    });
}

void GameSession::stopGameThread() {
    if (!impl_->gameThreadRunning.load(std::memory_order_acquire)) {
        return;  // Not running
    }

    impl_->commandQueue->shutdown();
    if (impl_->gameThread.joinable()) {
        impl_->gameThread.join();
    }
}

bool GameSession::isGameThreadRunning() const {
    return impl_->gameThreadRunning.load(std::memory_order_acquire);
}

// ============================================================================
// Synchronous Tick (for tests)
// ============================================================================

void GameSession::tick(float dt) {
    assert(!isGameThreadRunning() && "tick() must not be called while game thread is running");

    // Drain pending commands from the queue (actions pushed from calling thread)
    impl_->drainAndExecuteCommands();

    // Dispatch cross-thread events
    if (impl_->eventBus) impl_->eventBus->dispatchQueued();

    // Tick all subsystems in order
    impl_->tickSubsystems(dt);
}

// ============================================================================
// GameSessionConfig serialization
// ============================================================================

GameSessionConfig GameSessionConfig::fromDataContainer(const DataContainer& dc) {
    GameSessionConfig c;
    c.enableLighting = dc.get<bool>("enable_lighting", true);
    c.enableSound = dc.get<bool>("enable_sound", true);
    c.enableFluidSimulation = dc.get<bool>("enable_fluid_simulation", true);
    c.gravity = dc.get<float>("gravity", -14.0f);
    c.tickRate = dc.get<uint32_t>("tick_rate", 30);
    c.randomTicksPerChunk = dc.get<uint32_t>("random_ticks_per_chunk", 4);
    c.columnCacheCapacity = static_cast<size_t>(dc.get<int64_t>("column_cache_capacity", 64));
    c.worldSaveDir = dc.get<std::string>("world_save_dir", "");
    return c;
}

DataContainer GameSessionConfig::toDataContainer() const {
    DataContainer dc;
    dc.set("enable_lighting", enableLighting);
    dc.set("enable_sound", enableSound);
    dc.set("enable_fluid_simulation", enableFluidSimulation);
    dc.set("gravity", gravity);
    dc.set("tick_rate", tickRate);
    dc.set("random_ticks_per_chunk", randomTicksPerChunk);
    dc.set("column_cache_capacity", static_cast<int64_t>(columnCacheCapacity));
    if (!worldSaveDir.empty()) {
        dc.set("world_save_dir", worldSaveDir);
    }
    return dc;
}

}  // namespace finevox
