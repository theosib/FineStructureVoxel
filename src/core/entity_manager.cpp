#include "finevox/core/entity_manager.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/item_drop_entity.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/mob_event_hooks.hpp"
#include "finevox/core/ai_driver.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_serializer.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/script/event_value.hpp"

namespace finevox {

EntityManager::EntityManager(World& world, GraphicsEventQueue& graphicsQueue)
    : world_(world)
    , graphicsQueue_(graphicsQueue)
    , physics_(createBlockShapeProvider(world))
{
}

EntityManager::~EntityManager() = default;

// ============================================================================
// Entity Lifecycle
// ============================================================================

EntityId EntityManager::spawnEntity(EntityType type, Vec3 position) {
    EntityId id = nextEntityId_++;
    auto entity = createEntity(type, id);
    entity->setPosition(position);
    entity->setCurrentChunk(ChunkPos::fromBlock(toBlockCoord(position)));

    // Publish spawn event to graphics
    graphicsQueue_.push(GraphicsMessage::fromEvent(
        script::makeEntitySpawnValue(id, static_cast<uint16_t>(type),
                                      position, entity->yaw(), entity->pitch())));

    spatialIndex_.insert(id, position);
    entities_[id] = std::move(entity);
    return id;
}

EntityId EntityManager::spawnEntity(std::unique_ptr<Entity> entity) {
    EntityId id = entity->id();
    if (id == INVALID_ENTITY_ID) {
        id = nextEntityId_++;
        entity->setId(id);
    }
    entity->setCurrentChunk(ChunkPos::fromBlock(toBlockCoord(entity->position())));

    // Publish spawn event to graphics
    graphicsQueue_.push(GraphicsMessage::fromEvent(
        script::makeEntitySpawnValue(id, static_cast<uint16_t>(entity->type()),
                                      entity->position(), entity->yaw(), entity->pitch())));

    Entity* rawPtr = entity.get();
    spatialIndex_.insert(id, rawPtr->position());
    entities_[id] = std::move(entity);

    // Wire up MobEntity-specific hooks and AI presets
    if (auto* mob = dynamic_cast<MobEntity*>(rawPtr)) {
        mob->setEntityManager(this);

        // Auto-configure AI preset if brain is empty
        if (const auto* def = mob->typeDef(); def && mob->brain().goalCount() == 0) {
            configureAIPreset(*mob, def->aiType);
        }

        // Attach script hooks
        if (hooksProvider_) {
            if (auto* hooks = hooksProvider_(mob->typeName())) {
                mob->setEventHooks(hooks);
            }
        }

        // Fire onSpawn
        if (mob->eventHooks()) {
            mob->eventHooks()->onSpawn(*mob);
        }
    }

    return id;
}

bool EntityManager::despawnEntity(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return false;
    }

    // Publish despawn event to graphics
    graphicsQueue_.push(GraphicsMessage::fromEvent(script::makeEntityDespawnValue(id)));

    // Remove from spatial index
    spatialIndex_.remove(id);

    // Remove from player authorities if applicable
    playerAuthorities_.erase(id);

    // Clear local player if this was it
    if (id == localPlayerId_) {
        localPlayerId_ = INVALID_ENTITY_ID;
    }

    entities_.erase(it);
    return true;
}

Entity* EntityManager::getEntity(EntityId id) {
    auto it = entities_.find(id);
    return it != entities_.end() ? it->second.get() : nullptr;
}

const Entity* EntityManager::getEntity(EntityId id) const {
    auto it = entities_.find(id);
    return it != entities_.end() ? it->second.get() : nullptr;
}

bool EntityManager::hasEntity(EntityId id) const {
    return entities_.find(id) != entities_.end();
}

// ============================================================================
// Player Management
// ============================================================================

EntityId EntityManager::spawnPlayer(Vec3 position) {
    // Try to use a registered player entity type; fall back to defaults
    auto playerTypeId = EntityTypeId::fromName("finevox:player");
    std::unique_ptr<MobEntity> mob;

    if (!playerTypeId.isEmpty() && EntityTypeRegistry::global().hasType(playerTypeId)) {
        mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, playerTypeId);
    } else {
        // No registered player type — create with defaults
        mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId{});
        mob->setMaxHealth(20.0f);
        mob->setHealth(20.0f);
    }

    mob->setPosition(position);
    mob->setHalfExtents(Vec3(0.35f, 0.925f, 0.35f));
    mob->setEyeHeight(1.65f);
    mob->setMaxStepHeight(0.6f);
    mob->setIsPlayer(true);
    mob->setDriver(std::make_unique<PlayerInputDriver>());

    EntityId id = spawnEntity(std::move(mob));

    // Set up player authority tracking
    auto& auth = getPlayerAuthority(id);
    auth.lastReceivedPosition = position;
    auth.correctionThreshold = correctionThreshold_;

    return id;
}

Entity* EntityManager::getLocalPlayer() {
    return getEntity(localPlayerId_);
}

const Entity* EntityManager::getLocalPlayer() const {
    return getEntity(localPlayerId_);
}

// ============================================================================
// Tick Processing
// ============================================================================

void EntityManager::tick(float tickDt) {
    ++currentTick_;

    // 1. Update all entities (AI, animations, timers)
    for (auto& [id, entity] : entities_) {
        if (entity->isAlive()) {
            entity->tick(tickDt, world_);
            entity->advanceAnimation(tickDt);

            // Fire script onTick after built-in AI
            if (auto* mob = dynamic_cast<MobEntity*>(entity.get()); mob && mob->eventHooks()) {
                mob->eventHooks()->onTick(*mob, tickDt);
            }
        }
    }

    // 2. Run physics for all entities
    physicsPass(tickDt);

    // 3. Handle chunk transfers
    processEntityTransfers();

    // 4. Validate player predictions, generate corrections
    if (validationEnabled_) {
        validatePlayerPredictions();
    }

    // 5. Publish snapshots to graphics thread
    publishSnapshots();

    // 6. Process pending removals
    processPendingRemovals();
}

void EntityManager::physicsPass(float tickDt) {
    // Create fluid query provider from world
    FluidQueryProvider fluidQuery = [this](const BlockCoord& pos) -> std::pair<FluidTypeId, uint8_t> {
        FluidTypeId fid = world_.getFluid(pos);
        uint8_t level = world_.getFluidLevel(pos);
        return {fid, level};
    };

    for (auto& [id, entity] : entities_) {
        if (!entity->isAlive()) continue;

        // 1. Compute fluid contact
        FluidContactInfo contact;
        if (entity->isAffectedByFluids()) {
            bool wasInFluid = entity->isInFluid();
            contact = computeFluidContact(*entity, entity->eyeHeight(), fluidQuery);
            entity->setFluidState(contact.inFluid, contact.submerged,
                                  contact.submersion, contact.fluidType);

            // Splash sound on dry→wet transition
            if (!wasInFluid && contact.inFluid && soundQueue_) {
                const FluidType* ft = FluidRegistry::global().getType(contact.fluidType);
                if (ft && ft->soundSet.isValid()) {
                    auto pos = entity->position();
                    soundQueue_->push(script::makeSoundEventValue(
                        ft->soundSet, "splash", "effects", pos.x, pos.y, pos.z));
                }
            }
        }

        // 2. Apply gravity
        if (entity->hasGravity()) {
            physics_.applyGravity(*entity, tickDt);
        }

        // 3. Apply fluid forces (buoyancy, drag, flow)
        if (contact.inFluid && entity->isAffectedByFluids()) {
            Vec3 vel = entity->velocity();
            applyBuoyancy(vel, contact, physics_.gravity(), tickDt);
            applyFluidDrag(vel, contact, tickDt);
            applyFlowForce(vel, contact, tickDt);
            entity->setVelocity(vel);
        }

        // 4. Move with collision
        Vec3 velocity = entity->velocity();
        Vec3 movement = velocity * tickDt;
        physics_.moveBody(*entity, movement);

        // 5. Update ground state
        entity->setOnGround(physics_.checkOnGround(*entity));
    }
}

void EntityManager::processEntityTransfers() {
    for (auto& [id, entity] : entities_) {
        // Update spatial index with current position
        spatialIndex_.update(id, entity->position());

        ChunkPos newChunk = ChunkPos::fromBlock(toBlockCoord(entity->position()));
        if (newChunk != entity->currentChunk()) {
            // Entity moved to a new chunk
            // TODO: Notify chunk system for entity tracking
            entity->setCurrentChunk(newChunk);
        }
    }
}

void EntityManager::validatePlayerPredictions() {
    for (auto& [playerId, auth] : playerAuthorities_) {
        Entity* player = getEntity(playerId);
        if (!player) continue;

        Vec3 posError = player->position() - auth.lastReceivedPosition;
        float errorMagnitude = glm::length(posError);

        if (errorMagnitude > auth.correctionThreshold) {
            graphicsQueue_.push(GraphicsMessage::fromEvent(
                script::makePlayerCorrectionValue(
                    playerId,
                    player->position(),
                    player->velocity(),
                    player->isOnGround(),
                    auth.lastInputSequence,
                    "physics_divergence"
                )));
        }
    }
}

void EntityManager::publishSnapshots() {
    std::vector<GraphicsMessage> batch;
    batch.reserve(entities_.size());

    for (const auto& [id, entity] : entities_) {
        if (entity->isAlive()) {
            batch.push_back(GraphicsMessage::fromSnapshot(
                EntitySnapshot::fromEntity(*entity, currentTick_)));
        }
    }

    if (!batch.empty()) {
        graphicsQueue_.pushBatch(std::move(batch));
    }
}

void EntityManager::processPendingRemovals() {
    // Collect entities marked for removal
    for (const auto& [id, entity] : entities_) {
        if (entity->isMarkedForRemoval()) {
            pendingRemovals_.push_back(id);
        }
    }

    // Remove them
    for (EntityId id : pendingRemovals_) {
        despawnEntity(id);
    }
    pendingRemovals_.clear();
}

// ============================================================================
// Player Event Handlers
// ============================================================================

void EntityManager::handlePlayerPosition(const BlockEvent& event) {
    EntityId playerId = event.entityId;
    Entity* player = getEntity(playerId);
    if (!player) return;

    Vec3 pos = Vec3(event.entityState.position);
    Vec3 vel = Vec3(event.entityState.velocity);
    bool ground = event.entityState.onGround;
    uint64_t seq = event.entityState.inputSequence;

    // Update authority tracking
    auto& auth = getPlayerAuthority(playerId);
    auth.lastReceivedPosition = pos;
    auth.lastReceivedVelocity = vel;
    auth.lastReceivedOnGround = ground;
    auth.lastInputSequence = seq;

    // Update player entity
    player->setPosition(pos);
    player->setVelocity(vel);
    player->setOnGround(ground);

    // Also update look direction if provided via sendPlayerState
    player->setLook(event.entityState.yaw, event.entityState.pitch);
}

void EntityManager::handlePlayerLook(const BlockEvent& event) {
    Entity* player = getEntity(event.entityId);
    if (!player) return;

    player->setLook(event.entityState.yaw, event.entityState.pitch);
}

void EntityManager::handlePlayerJump(const BlockEvent& event) {
    Entity* player = getEntity(event.entityId);
    if (!player) return;

    // Jump is handled by graphics thread prediction
    // Here we just acknowledge the intent
    // The position update will come via PlayerPosition event
}

void EntityManager::handlePlayerSprint(const BlockEvent& event, bool starting) {
    Entity* player = getEntity(event.entityId);
    if (!player) return;

    // Sprint affects movement speed - could be tracked per-entity
    // For now, this is informational
}

void EntityManager::handlePlayerSneak(const BlockEvent& event, bool starting) {
    Entity* player = getEntity(event.entityId);
    if (!player) return;

    // Sneak affects movement speed and collision behavior
    // For now, this is informational
}

// ============================================================================
// Internal Methods
// ============================================================================

std::unique_ptr<Entity> EntityManager::createEntity(EntityType type, EntityId id) {
    // ItemDrop uses a specialized subclass — callers with an actual item
    // should use spawnEntity(unique_ptr<Entity>) with a pre-built ItemDropEntity
    if (type == EntityType::ItemDrop) {
        return std::make_unique<ItemDropEntity>(id, ItemStack{});
    }

    auto entity = std::make_unique<Entity>(id, type);

    // Configure based on type
    switch (type) {
        case EntityType::Player:
            entity->setHalfExtents(Vec3(0.35f, 0.925f, 0.35f));  // 0.7 x 1.85 x 0.7
            entity->setEyeHeight(1.65f);
            entity->setMaxStepHeight(0.6f);
            break;

        case EntityType::Pig:
        case EntityType::Cow:
        case EntityType::Sheep:
            entity->setHalfExtents(Vec3(0.5f, 0.5f, 0.5f));  // ~1.0 cube
            entity->setEyeHeight(0.7f);
            break;

        case EntityType::Chicken:
            entity->setHalfExtents(Vec3(0.25f, 0.4f, 0.25f));  // Small bird
            entity->setEyeHeight(0.5f);
            break;

        case EntityType::Zombie:
        case EntityType::Skeleton:
            entity->setHalfExtents(Vec3(0.35f, 0.95f, 0.35f));  // Slightly taller than player
            entity->setEyeHeight(1.7f);
            break;

        case EntityType::Spider:
            entity->setHalfExtents(Vec3(0.75f, 0.5f, 0.75f));  // Wide and flat
            entity->setEyeHeight(0.65f);
            break;

        case EntityType::Arrow:
        case EntityType::Fireball:
            entity->setHalfExtents(Vec3(0.125f, 0.125f, 0.125f));
            entity->setHasGravity(type == EntityType::Arrow);  // Arrows have gravity
            break;

        case EntityType::Minecart:
            entity->setHalfExtents(Vec3(0.5f, 0.4f, 0.5f));
            entity->setEyeHeight(0.5f);
            break;

        case EntityType::Boat:
            entity->setHalfExtents(Vec3(0.75f, 0.25f, 0.75f));
            entity->setEyeHeight(0.3f);
            break;

        default:
            // Default entity size
            entity->setHalfExtents(Vec3(0.35f, 0.5f, 0.35f));
            entity->setEyeHeight(0.8f);
            break;
    }

    return entity;
}

PlayerAuthority& EntityManager::getPlayerAuthority(EntityId playerId) {
    auto it = playerAuthorities_.find(playerId);
    if (it == playerAuthorities_.end()) {
        auto& auth = playerAuthorities_[playerId];
        auth.playerId = playerId;
        auth.correctionThreshold = correctionThreshold_;
        return auth;
    }
    return it->second;
}

// ============================================================================
// Persistence (Column-based save/load)
// ============================================================================

std::vector<Entity*> EntityManager::getEntitiesInColumn(ColumnPos colPos) {
    std::vector<Entity*> result;
    for (auto& [id, entity] : entities_) {
        auto eColPos = ColumnPos::fromChunk(entity->currentChunk());
        if (eColPos.x == colPos.x && eColPos.z == colPos.z) {
            result.push_back(entity.get());
        }
    }
    return result;
}

std::vector<const Entity*> EntityManager::getEntitiesInColumn(ColumnPos colPos) const {
    std::vector<const Entity*> result;
    for (const auto& [id, entity] : entities_) {
        auto eColPos = ColumnPos::fromChunk(entity->currentChunk());
        if (eColPos.x == colPos.x && eColPos.z == colPos.z) {
            result.push_back(entity.get());
        }
    }
    return result;
}

void EntityManager::saveColumnEntities(ChunkColumn& column) {
    auto colPos = column.position();

    // Gather non-player entities in this column
    std::vector<const Entity*> toSave;
    for (const auto& [id, entity] : entities_) {
        // Skip players — they are managed separately
        if (isPlayer(*entity)) continue;

        auto eColPos = ColumnPos::fromChunk(entity->currentChunk());
        if (eColPos.x == colPos.x && eColPos.z == colPos.z) {
            toSave.push_back(entity.get());
        }
    }

    if (toSave.empty()) {
        // Remove stale entity data if present
        if (column.hasData()) {
            column.data()->remove(StringInterner::global().intern("entity_data"));
        }
        return;
    }

    auto bytes = EntitySerializer::serialize(toSave);
    column.getOrCreateData().set<std::vector<uint8_t>>("entity_data", std::move(bytes));
}

size_t EntityManager::loadColumnEntities(ChunkColumn& column) {
    if (!column.hasData()) return 0;

    auto entityBytes = column.data()->get<std::vector<uint8_t>>("entity_data");
    if (entityBytes.empty()) return 0;

    auto entities = EntitySerializer::deserialize(entityBytes);
    size_t count = entities.size();

    for (auto& entity : entities) {
        spawnEntity(std::move(entity));
    }

    // Clear the entity data from the column to avoid re-loading
    column.data()->remove(StringInterner::global().intern("entity_data"));

    return count;
}

// ============================================================================
// Event Handler Registration
// ============================================================================

void registerEntityEventHandlers(UpdateScheduler& scheduler, EntityManager& manager) {
    // This function will be implemented when we add event handler dispatch
    // to UpdateScheduler. For now, the render_demo will call handlers directly.
    //
    // Future implementation:
    // scheduler.registerHandler(EventType::PlayerPosition,
    //     [&manager](const BlockEvent& e) { manager.handlePlayerPosition(e); });
    // scheduler.registerHandler(EventType::PlayerLook,
    //     [&manager](const BlockEvent& e) { manager.handlePlayerLook(e); });
    // etc.
}

}  // namespace finevox
