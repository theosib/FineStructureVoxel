#pragma once

/**
 * @file event_value.hpp
 * @brief Flexible event builders using finescript Values
 *
 * Provides functions that construct finescript Value maps for each event type,
 * replacing fixed C++ structs on non-hot paths. These maps are:
 * - Inherently serializable (CBOR via finescript)
 * - Extensible (scripts/mods can add fields)
 * - Network-ready for multiplayer
 *
 * Symbol IDs for field names are pre-interned and cached for fast access.
 */

#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/rotation.hpp"
#include "finevox/core/recipe.hpp"
#include "finevox/core/item_type.hpp"
#include <finescript/value.h>
#include <cstdint>
#include <limits>
#include <string_view>

namespace finevox {

// Forward declarations
struct SoundSetId;

namespace script {

// ============================================================================
// EventSymbols - Pre-interned symbol IDs for event field names
// ============================================================================

/**
 * Lazily initialized cache of interned symbol IDs used as map keys in event
 * Values. Avoids per-event intern() calls.
 */
struct EventSymbols {
    // Event identification
    uint32_t type;

    // Position fields
    uint32_t pos_x, pos_y, pos_z;
    uint32_t chunk_x, chunk_y, chunk_z;
    uint32_t local_x, local_y, local_z;

    // Block fields
    uint32_t block_type, previous_type;
    uint32_t rotation;

    // Interaction
    uint32_t face;
    uint32_t neighbor_face_mask;

    // Entity fields
    uint32_t entity_id, player_id, entity_type, entity_type_name;
    uint32_t vel_x, vel_y, vel_z;
    uint32_t on_ground;
    uint32_t yaw, pitch;
    uint32_t animation_id, animation_time;
    uint32_t input_sequence;

    // Fluid fields
    uint32_t fluid_type, fluid_level;

    // Crafting
    uint32_t recipe_id;
    uint32_t station_pos_x, station_pos_y, station_pos_z;

    // Timestamp
    uint32_t timestamp;
    uint32_t tick_number;

    // Sound fields
    uint32_t sound_set, action, category;
    uint32_t volume, positional;

    // Graphics-specific
    uint32_t correction_reason;
    uint32_t correct_block_type, expected_block_type;
    uint32_t block_x, block_y, block_z;

    // World time
    uint32_t ticks;

    // Sprint/sneak state
    uint32_t starting;

    // Combat fields
    uint32_t attacker_id, target_id, damage_info;

    // Block-break tool context
    uint32_t tool_type, precise_break;

    // Particle fields
    uint32_t preset, emitter_id, duration, count;

    /// Get the singleton instance (lazily initialized).
    static const EventSymbols& instance();
};

// ============================================================================
// Event type name constants
// ============================================================================

// Block lifecycle
constexpr const char* EVT_BLOCK_PLACED = "block_placed";
constexpr const char* EVT_BLOCK_BROKEN = "block_broken";
constexpr const char* EVT_BLOCK_CHANGED = "block_changed";

// Block interactions
constexpr const char* EVT_PLAYER_USE = "player_use";
constexpr const char* EVT_PLAYER_HIT = "player_hit";

// Player state
constexpr const char* EVT_PLAYER_POSITION = "player_position";
constexpr const char* EVT_PLAYER_LOOK = "player_look";
constexpr const char* EVT_PLAYER_JUMP = "player_jump";
constexpr const char* EVT_PLAYER_SPRINT = "player_sprint";
constexpr const char* EVT_PLAYER_SNEAK = "player_sneak";

// Fluid
constexpr const char* EVT_FLUID_PLACED = "fluid_placed";
constexpr const char* EVT_FLUID_REMOVED = "fluid_removed";

// Admin/system
constexpr const char* EVT_SET_WORLD_TIME = "set_world_time";

// Combat
constexpr const char* EVT_ATTACK_ENTITY = "attack_entity";

// Crafting
constexpr const char* EVT_CRAFT_ITEM = "craft_item";

// Sound
constexpr const char* EVT_PLAY_SOUND = "play_sound";

// Graphics
constexpr const char* EVT_ENTITY_SPAWN = "entity_spawn";
constexpr const char* EVT_ENTITY_DESPAWN = "entity_despawn";
constexpr const char* EVT_PLAYER_CORRECTION = "player_correction";
constexpr const char* EVT_BLOCK_CORRECTION = "block_correction";
constexpr const char* EVT_ENTITY_ANIMATION = "entity_animation";

// Particles
constexpr const char* EVT_PARTICLE_SPAWN = "particle_spawn";
constexpr const char* EVT_PARTICLE_DESPAWN = "particle_despawn";
constexpr const char* EVT_PARTICLE_BURST = "particle_burst";

// Game-defined command, opaque to the engine
constexpr const char* EVT_GAME_COMMAND = "game_command";

// ============================================================================
// Command/Action Event Builders (Graphics thread → Game thread)
// ============================================================================

/** Build a block-placed action. */
finescript::Value makeBlockPlacedValue(BlockCoord pos, BlockTypeId newType,
                                       BlockTypeId oldType, Rotation rot = Rotation::IDENTITY,
                                       EntityId actor = INVALID_ENTITY_ID);

/** Build a block-broken action.
 *  @param tool Item type held by the breaker (empty = bare hand).
 *  @param preciseBreak True if the tool performs a "precise" break (silk-touch equivalent).
 *  @param actor Entity ID of the actor breaking the block. */
finescript::Value makeBlockBrokenValue(BlockCoord pos, BlockTypeId oldType,
                                        ItemTypeId tool = {},
                                        bool preciseBreak = false,
                                        EntityId actor = INVALID_ENTITY_ID);

/** Build a block-changed event. */
finescript::Value makeBlockChangedValue(BlockCoord pos, BlockTypeId oldType, BlockTypeId newType);

/** Build a player-use (right-click) action. */
finescript::Value makePlayerUseValue(BlockCoord pos, Face face,
                                     EntityId actor = INVALID_ENTITY_ID);

/** Build a player-hit (left-click) action. */
finescript::Value makePlayerHitValue(BlockCoord pos, Face face,
                                     EntityId actor = INVALID_ENTITY_ID);

/** Build a player position update. */
finescript::Value makePlayerPositionValue(EntityId id, glm::dvec3 position, glm::dvec3 velocity,
                                          bool onGround, uint64_t inputSequence,
                                          float yaw = 0.0f, float pitch = 0.0f);

/** Build a player look update. */
finescript::Value makePlayerLookValue(EntityId id, float yaw, float pitch);

/** Build a player jump event. */
finescript::Value makePlayerJumpValue(EntityId id);

/** Build a player sprint start/stop event. */
finescript::Value makePlayerSprintValue(EntityId id, bool starting);

/** Build a player sneak start/stop event. */
finescript::Value makePlayerSneakValue(EntityId id, bool starting);

/** Build a fluid-placed action. */
finescript::Value makeFluidPlacedValue(BlockCoord pos, FluidTypeId type, uint8_t level = 15);

/** Build a fluid-removed action. */
finescript::Value makeFluidRemovedValue(BlockCoord pos, FluidTypeId previousFluid);

/** Build a set-world-time command. */
finescript::Value makeSetWorldTimeValue(int64_t ticks);

/** Build a craft-item action. */
finescript::Value makeCraftItemValue(BlockCoord stationPos, RecipeId recipe);

/** Build an attack-entity action. */
finescript::Value makeAttackEntityValue(EntityId attacker, EntityId target,
                                        finescript::Value damageInfo);

// ============================================================================
// Sound Event Builders
// ============================================================================

/** Build a sound event value. */
finescript::Value makeSoundEventValue(SoundSetId soundSet, std::string_view action,
                                      std::string_view category,
                                      float posX, float posY, float posZ,
                                      float volume = 1.0f, float pitch = 1.0f,
                                      bool positional = true);

// ============================================================================
// Graphics Event Builders (Game thread → Graphics thread)
// ============================================================================

/** Build an entity-spawn event. */
finescript::Value makeEntitySpawnValue(EntityId id, uint16_t entityType,
                                       glm::dvec3 pos, float yaw, float pitch,
                                       std::string_view entityTypeName = "");

/** Build an entity-despawn event. */
finescript::Value makeEntityDespawnValue(EntityId id);

/** Build a player-correction event. */
finescript::Value makePlayerCorrectionValue(EntityId id, glm::dvec3 pos, glm::dvec3 vel,
                                            bool onGround, uint64_t inputSequence,
                                            std::string_view reason);

/** Build a block-correction event. */
finescript::Value makeBlockCorrectionValue(BlockCoord pos, BlockTypeId correct, BlockTypeId expected);

/** Build an entity-animation event. */
finescript::Value makeEntityAnimationValue(EntityId id, uint8_t animId, float time);

// ============================================================================
// Particle Event Builders (command + notification)
// ============================================================================

/** Build a particle-emitter spawn command.
 *  @param preset  Emitter preset name (e.g. "finevox:block_break").
 *  @param pos     World position (ignored when attached != INVALID_ENTITY_ID).
 *  @param attached Attach emitter to this entity (position follows). Pass
 *                  INVALID_ENTITY_ID for a world-space emitter.
 *  @param duration Override preset duration. Pass NaN to use preset default. */
finescript::Value makeParticleSpawnValue(std::string_view preset,
                                          glm::dvec3 pos,
                                          EntityId attached = INVALID_ENTITY_ID,
                                          float duration = std::numeric_limits<float>::quiet_NaN());

/** Build a particle-emitter despawn command. */
finescript::Value makeParticleDespawnValue(uint32_t emitterId);

/** Build a one-shot particle burst command at a world position. */
finescript::Value makeParticleBurstValue(std::string_view preset, glm::dvec3 pos);

/// Wrap a game-defined command for transport; read back with readGameCommandBody
finescript::Value makeGameCommandValue(finescript::Value body);
finescript::Value readGameCommandBody(const finescript::Value& event);

// ============================================================================
// BlockCoord convenience overload for sound events
// ============================================================================

/** Build a sound event value with BlockCoord position (centered at +0.5). */
finescript::Value makeSoundEventValue(SoundSetId soundSet, std::string_view action,
                                      std::string_view category, BlockCoord pos,
                                      float volume = 1.0f, float pitch = 1.0f,
                                      bool positional = true);

// ============================================================================
// Reader Helpers — Extract typed fields from event Value maps
// ============================================================================

/** Read the event type string from an event Value. */
std::string_view readEventType(const finescript::Value& event);

/** Read a BlockCoord from pos_x/pos_y/pos_z fields. */
BlockCoord readBlockCoord(const finescript::Value& event);

/** Read entity ID from an event. */
EntityId readEntityId(const finescript::Value& event);

/** Read a Face value from an event. */
Face readFace(const finescript::Value& event);

/** Read a BlockTypeId from a named field. */
BlockTypeId readBlockTypeId(const finescript::Value& event, uint32_t fieldSymbol);

/** Read an ItemTypeId from a named field (empty if field missing or not a string). */
ItemTypeId readItemTypeId(const finescript::Value& event, uint32_t fieldSymbol);

/** Read a FluidTypeId from the fluid_type field. */
FluidTypeId readFluidTypeId(const finescript::Value& event);

/** Read a double-precision vec3 from named fields. */
glm::dvec3 readDVec3(const finescript::Value& event, uint32_t xSym, uint32_t ySym, uint32_t zSym);

/** Read a float from a named field, with default. */
float readFloat(const finescript::Value& event, uint32_t fieldSymbol, float defaultVal = 0.0f);

/** Read an int64 from a named field, with default. */
int64_t readInt(const finescript::Value& event, uint32_t fieldSymbol, int64_t defaultVal = 0);

/** Read a bool from a named field, with default. */
bool readBool(const finescript::Value& event, uint32_t fieldSymbol, bool defaultVal = false);

/** Read a string from a named field, with default. */
std::string_view readString(const finescript::Value& event, uint32_t fieldSymbol,
                             std::string_view defaultVal = "");

}  // namespace script
}  // namespace finevox
