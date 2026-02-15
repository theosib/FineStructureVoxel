#include "finevox/core/entity_manager.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/item_drop_entity.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/event_queue.hpp"

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
    entity->setCurrentChunk(ChunkPos::fromBlock(toBlockPos(position)));

    // Publish spawn event to graphics
    graphicsQueue_.push(GraphicsEvent::entitySpawn(
        id, type, position, entity->yaw(), entity->pitch()));

    entities_[id] = std::move(entity);
    return id;
}

EntityId EntityManager::spawnEntity(std::unique_ptr<Entity> entity) {
    EntityId id = entity->id();
    entity->setCurrentChunk(ChunkPos::fromBlock(toBlockPos(entity->position())));

    // Publish spawn event to graphics
    graphicsQueue_.push(GraphicsEvent::entitySpawn(
        id, entity->type(), entity->position(), entity->yaw(), entity->pitch()));

    entities_[id] = std::move(entity);
    return id;
}

bool EntityManager::despawnEntity(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
        return false;
    }

    // Publish despawn event to graphics
    graphicsQueue_.push(GraphicsEvent::entityDespawn(id));

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
    EntityId id = spawnEntity(EntityType::Player, position);

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
    for (auto& [id, entity] : entities_) {
        if (!entity->isAlive()) continue;

        // Apply gravity and movement
        if (entity->hasGravity()) {
            physics_.applyGravity(*entity, tickDt);
        }

        // Move with collision
        Vec3 velocity = entity->velocity();
        Vec3 movement = velocity * tickDt;
        physics_.moveBody(*entity, movement);

        // Update ground state
        entity->setOnGround(physics_.checkOnGround(*entity));
    }
}

void EntityManager::processEntityTransfers() {
    for (auto& [id, entity] : entities_) {
        ChunkPos newChunk = ChunkPos::fromBlock(toBlockPos(entity->position()));
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
            graphicsQueue_.push(GraphicsEvent::playerCorrection(
                playerId,
                player->position(),
                player->velocity(),
                player->isOnGround(),
                auth.lastInputSequence,
                CorrectionReason::PhysicsDivergence
            ));
        }
    }
}

void EntityManager::publishSnapshots() {
    std::vector<GraphicsEvent> batch;
    batch.reserve(entities_.size());

    for (const auto& [id, entity] : entities_) {
        if (entity->isAlive()) {
            batch.push_back(GraphicsEvent::entitySnapshot(*entity, currentTick_));
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
            entity->setHalfExtents(Vec3(0.3f, 0.9f, 0.3f));  // 0.6 x 1.8 x 0.6
            entity->setEyeHeight(1.62f);
            entity->setMaxStepHeight(0.625f);
            break;

        case EntityType::Pig:
        case EntityType::Cow:
        case EntityType::Sheep:
            entity->setHalfExtents(Vec3(0.45f, 0.45f, 0.45f));  // ~0.9 cube
            entity->setEyeHeight(0.7f);
            break;

        case EntityType::Chicken:
            entity->setHalfExtents(Vec3(0.2f, 0.35f, 0.2f));  // Small
            entity->setEyeHeight(0.5f);
            break;

        case EntityType::Zombie:
        case EntityType::Skeleton:
            entity->setHalfExtents(Vec3(0.3f, 0.95f, 0.3f));  // Slightly taller than player
            entity->setEyeHeight(1.7f);
            break;

        case EntityType::Creeper:
            entity->setHalfExtents(Vec3(0.3f, 0.85f, 0.3f));
            entity->setEyeHeight(1.4f);
            break;

        case EntityType::Spider:
            entity->setHalfExtents(Vec3(0.7f, 0.45f, 0.7f));  // Wide and flat
            entity->setEyeHeight(0.65f);
            break;

        case EntityType::Arrow:
        case EntityType::Fireball:
            entity->setHalfExtents(Vec3(0.125f, 0.125f, 0.125f));
            entity->setHasGravity(type == EntityType::Arrow);  // Arrows have gravity
            break;

        case EntityType::Minecart:
            entity->setHalfExtents(Vec3(0.49f, 0.35f, 0.49f));
            entity->setEyeHeight(0.5f);
            break;

        case EntityType::Boat:
            entity->setHalfExtents(Vec3(0.7f, 0.225f, 0.7f));
            entity->setEyeHeight(0.3f);
            break;

        default:
            // Default entity size
            entity->setHalfExtents(Vec3(0.3f, 0.5f, 0.3f));
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
