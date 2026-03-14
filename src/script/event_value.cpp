#include "finevox/script/event_value.hpp"
#include "finevox/core/sound_event.hpp"
#include <finescript/map_data.h>

namespace finevox::script {

// ============================================================================
// EventSymbols singleton
// ============================================================================

const EventSymbols& EventSymbols::instance() {
    static EventSymbols syms = [] {
        auto& si = StringInterner::global();
        EventSymbols s{};
        s.type            = si.intern("type");
        s.pos_x           = si.intern("pos_x");
        s.pos_y           = si.intern("pos_y");
        s.pos_z           = si.intern("pos_z");
        s.chunk_x         = si.intern("chunk_x");
        s.chunk_y         = si.intern("chunk_y");
        s.chunk_z         = si.intern("chunk_z");
        s.local_x         = si.intern("local_x");
        s.local_y         = si.intern("local_y");
        s.local_z         = si.intern("local_z");
        s.block_type      = si.intern("block_type");
        s.previous_type   = si.intern("previous_type");
        s.rotation        = si.intern("rotation");
        s.face            = si.intern("face");
        s.neighbor_face_mask = si.intern("neighbor_face_mask");
        s.entity_id       = si.intern("entity_id");
        s.entity_type     = si.intern("entity_type");
        s.vel_x           = si.intern("vel_x");
        s.vel_y           = si.intern("vel_y");
        s.vel_z           = si.intern("vel_z");
        s.on_ground       = si.intern("on_ground");
        s.yaw             = si.intern("yaw");
        s.pitch           = si.intern("pitch");
        s.animation_id    = si.intern("animation_id");
        s.animation_time  = si.intern("animation_time");
        s.input_sequence  = si.intern("input_sequence");
        s.fluid_type      = si.intern("fluid_type");
        s.fluid_level     = si.intern("fluid_level");
        s.recipe_id       = si.intern("recipe_id");
        s.station_pos_x   = si.intern("station_pos_x");
        s.station_pos_y   = si.intern("station_pos_y");
        s.station_pos_z   = si.intern("station_pos_z");
        s.timestamp       = si.intern("timestamp");
        s.tick_number     = si.intern("tick_number");
        s.sound_set       = si.intern("sound_set");
        s.action          = si.intern("action");
        s.category        = si.intern("category");
        s.volume          = si.intern("volume");
        s.positional      = si.intern("positional");
        s.correction_reason = si.intern("correction_reason");
        s.correct_block_type = si.intern("correct_block_type");
        s.expected_block_type = si.intern("expected_block_type");
        s.block_x         = si.intern("block_x");
        s.block_y         = si.intern("block_y");
        s.block_z         = si.intern("block_z");
        s.ticks           = si.intern("ticks");
        s.starting        = si.intern("starting");
        return s;
    }();
    return syms;
}

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

finescript::Value makeMap() {
    return finescript::Value::map();
}

void setStr(finescript::MapData& m, uint32_t key, std::string_view sv) {
    m.set(key, finescript::Value::string(std::string(sv)));
}

void setInt(finescript::MapData& m, uint32_t key, int64_t v) {
    m.set(key, finescript::Value::integer(v));
}

void setFloat(finescript::MapData& m, uint32_t key, double v) {
    m.set(key, finescript::Value::number(v));
}

void setBool(finescript::MapData& m, uint32_t key, bool v) {
    m.set(key, finescript::Value::boolean(v));
}

void setType(finescript::MapData& m, const char* typeName) {
    const auto& s = EventSymbols::instance();
    setStr(m, s.type, typeName);
}

void setPos(finescript::MapData& m, BlockCoord pos) {
    const auto& s = EventSymbols::instance();
    setInt(m, s.pos_x, pos.x);
    setInt(m, s.pos_y, pos.y);
    setInt(m, s.pos_z, pos.z);
}

void setBlockType(finescript::MapData& m, uint32_t key, BlockTypeId bt) {
    setStr(m, key, bt.name());
}

}  // anonymous namespace

// ============================================================================
// Command/Action Event Builders
// ============================================================================

finescript::Value makeBlockPlacedValue(BlockCoord pos, BlockTypeId newType,
                                       BlockTypeId oldType, Rotation rot) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_BLOCK_PLACED);
    setPos(m, pos);
    setBlockType(m, s.block_type, newType);
    setBlockType(m, s.previous_type, oldType);
    setInt(m, s.rotation, rot.index());
    return v;
}

finescript::Value makeBlockBrokenValue(BlockCoord pos, BlockTypeId oldType) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_BLOCK_BROKEN);
    setPos(m, pos);
    setBlockType(m, s.previous_type, oldType);
    return v;
}

finescript::Value makeBlockChangedValue(BlockCoord pos, BlockTypeId oldType, BlockTypeId newType) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_BLOCK_CHANGED);
    setPos(m, pos);
    setBlockType(m, s.previous_type, oldType);
    setBlockType(m, s.block_type, newType);
    return v;
}

finescript::Value makePlayerUseValue(BlockCoord pos, Face face) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_USE);
    setPos(m, pos);
    setInt(m, s.face, static_cast<int64_t>(face));
    return v;
}

finescript::Value makePlayerHitValue(BlockCoord pos, Face face) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_HIT);
    setPos(m, pos);
    setInt(m, s.face, static_cast<int64_t>(face));
    return v;
}

finescript::Value makePlayerPositionValue(EntityId id, glm::dvec3 position, glm::dvec3 velocity,
                                          bool onGround, uint64_t inputSequence) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_POSITION);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setFloat(m, s.pos_x, position.x);
    setFloat(m, s.pos_y, position.y);
    setFloat(m, s.pos_z, position.z);
    setFloat(m, s.vel_x, velocity.x);
    setFloat(m, s.vel_y, velocity.y);
    setFloat(m, s.vel_z, velocity.z);
    setBool(m, s.on_ground, onGround);
    setInt(m, s.input_sequence, static_cast<int64_t>(inputSequence));
    return v;
}

finescript::Value makePlayerLookValue(EntityId id, float yaw, float pitch) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_LOOK);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setFloat(m, s.yaw, yaw);
    setFloat(m, s.pitch, pitch);
    return v;
}

finescript::Value makePlayerJumpValue(EntityId id) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_JUMP);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    return v;
}

finescript::Value makePlayerSprintValue(EntityId id, bool starting) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_SPRINT);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setBool(m, s.starting, starting);
    return v;
}

finescript::Value makePlayerSneakValue(EntityId id, bool starting) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_SNEAK);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setBool(m, s.starting, starting);
    return v;
}

finescript::Value makeFluidPlacedValue(BlockCoord pos, FluidTypeId type, uint8_t level) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_FLUID_PLACED);
    setPos(m, pos);
    setStr(m, s.fluid_type, type.name());
    setInt(m, s.fluid_level, level);
    return v;
}

finescript::Value makeFluidRemovedValue(BlockCoord pos, FluidTypeId previousFluid) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_FLUID_REMOVED);
    setPos(m, pos);
    setStr(m, s.fluid_type, previousFluid.name());
    return v;
}

finescript::Value makeSetWorldTimeValue(int64_t ticks) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_SET_WORLD_TIME);
    setInt(m, s.ticks, ticks);
    return v;
}

finescript::Value makeCraftItemValue(BlockCoord stationPos, RecipeId recipe) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_CRAFT_ITEM);
    setInt(m, s.station_pos_x, stationPos.x);
    setInt(m, s.station_pos_y, stationPos.y);
    setInt(m, s.station_pos_z, stationPos.z);
    setInt(m, s.recipe_id, static_cast<int64_t>(recipe.id));
    return v;
}

// ============================================================================
// Sound Event Builders
// ============================================================================

finescript::Value makeSoundEventValue(SoundSetId soundSet, std::string_view action,
                                      std::string_view category,
                                      float posX, float posY, float posZ,
                                      float volume, float pitch, bool positional) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAY_SOUND);
    setStr(m, s.sound_set, soundSet.name());
    setStr(m, s.action, action);
    setStr(m, s.category, category);
    setFloat(m, s.pos_x, posX);
    setFloat(m, s.pos_y, posY);
    setFloat(m, s.pos_z, posZ);
    setFloat(m, s.volume, volume);
    setFloat(m, s.pitch, pitch);
    setBool(m, s.positional, positional);
    return v;
}

// ============================================================================
// Graphics Event Builders
// ============================================================================

finescript::Value makeEntitySpawnValue(EntityId id, uint16_t entityType,
                                       glm::dvec3 pos, float yaw, float pitch) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_ENTITY_SPAWN);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setInt(m, s.entity_type, entityType);
    setFloat(m, s.pos_x, pos.x);
    setFloat(m, s.pos_y, pos.y);
    setFloat(m, s.pos_z, pos.z);
    setFloat(m, s.yaw, yaw);
    setFloat(m, s.pitch, pitch);
    return v;
}

finescript::Value makeEntityDespawnValue(EntityId id) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_ENTITY_DESPAWN);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    return v;
}

finescript::Value makePlayerCorrectionValue(EntityId id, glm::dvec3 pos, glm::dvec3 vel,
                                            bool onGround, uint64_t inputSequence,
                                            std::string_view reason) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_PLAYER_CORRECTION);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setFloat(m, s.pos_x, pos.x);
    setFloat(m, s.pos_y, pos.y);
    setFloat(m, s.pos_z, pos.z);
    setFloat(m, s.vel_x, vel.x);
    setFloat(m, s.vel_y, vel.y);
    setFloat(m, s.vel_z, vel.z);
    setBool(m, s.on_ground, onGround);
    setInt(m, s.input_sequence, static_cast<int64_t>(inputSequence));
    setStr(m, s.correction_reason, reason);
    return v;
}

finescript::Value makeBlockCorrectionValue(BlockCoord pos, BlockTypeId correct, BlockTypeId expected) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_BLOCK_CORRECTION);
    setPos(m, pos);
    setBlockType(m, s.correct_block_type, correct);
    setBlockType(m, s.expected_block_type, expected);
    return v;
}

finescript::Value makeEntityAnimationValue(EntityId id, uint8_t animId, float time) {
    const auto& s = EventSymbols::instance();
    auto v = makeMap();
    auto& m = v.asMap();
    setType(m, EVT_ENTITY_ANIMATION);
    setInt(m, s.entity_id, static_cast<int64_t>(id));
    setInt(m, s.animation_id, animId);
    setFloat(m, s.animation_time, time);
    return v;
}

// ============================================================================
// Reader Helpers
// ============================================================================

std::string_view readEventType(const finescript::Value& event) {
    const auto& s = EventSymbols::instance();
    auto typeVal = event.asMap().get(s.type);
    if (typeVal.isString()) {
        return typeVal.asString();
    }
    return "";
}

BlockCoord readBlockCoord(const finescript::Value& event) {
    const auto& s = EventSymbols::instance();
    const auto& m = event.asMap();
    return BlockCoord(
        static_cast<int32_t>(m.get(s.pos_x).asInt()),
        static_cast<int32_t>(m.get(s.pos_y).asInt()),
        static_cast<int32_t>(m.get(s.pos_z).asInt())
    );
}

EntityId readEntityId(const finescript::Value& event) {
    const auto& s = EventSymbols::instance();
    auto v = event.asMap().get(s.entity_id);
    if (v.isInt()) {
        return static_cast<EntityId>(v.asInt());
    }
    return INVALID_ENTITY_ID;
}

Face readFace(const finescript::Value& event) {
    const auto& s = EventSymbols::instance();
    auto v = event.asMap().get(s.face);
    if (v.isInt()) {
        return static_cast<Face>(v.asInt());
    }
    return Face::PosY;
}

BlockTypeId readBlockTypeId(const finescript::Value& event, uint32_t fieldSymbol) {
    auto v = event.asMap().get(fieldSymbol);
    if (v.isString()) {
        return BlockTypeId::fromName(v.asString());
    }
    return AIR_BLOCK_TYPE;
}

FluidTypeId readFluidTypeId(const finescript::Value& event) {
    const auto& s = EventSymbols::instance();
    auto v = event.asMap().get(s.fluid_type);
    if (v.isString()) {
        return FluidTypeId::fromName(v.asString());
    }
    return FluidTypeId{};
}

glm::dvec3 readDVec3(const finescript::Value& event, uint32_t xSym, uint32_t ySym, uint32_t zSym) {
    const auto& m = event.asMap();
    return glm::dvec3(
        m.get(xSym).asNumber(),
        m.get(ySym).asNumber(),
        m.get(zSym).asNumber()
    );
}

float readFloat(const finescript::Value& event, uint32_t fieldSymbol, float defaultVal) {
    auto v = event.asMap().get(fieldSymbol);
    if (v.isNumeric()) {
        return static_cast<float>(v.asNumber());
    }
    return defaultVal;
}

int64_t readInt(const finescript::Value& event, uint32_t fieldSymbol, int64_t defaultVal) {
    auto v = event.asMap().get(fieldSymbol);
    if (v.isInt()) {
        return v.asInt();
    }
    return defaultVal;
}

bool readBool(const finescript::Value& event, uint32_t fieldSymbol, bool defaultVal) {
    auto v = event.asMap().get(fieldSymbol);
    if (v.isBool()) {
        return v.asBool();
    }
    return defaultVal;
}

std::string_view readString(const finescript::Value& event, uint32_t fieldSymbol,
                             std::string_view defaultVal) {
    auto v = event.asMap().get(fieldSymbol);
    if (v.isString()) {
        return v.asString();
    }
    return defaultVal;
}

// ============================================================================
// BlockCoord convenience overload for sound events
// ============================================================================

finescript::Value makeSoundEventValue(SoundSetId soundSet, std::string_view action,
                                      std::string_view category, BlockCoord pos,
                                      float volume, float pitch, bool positional) {
    float px = static_cast<float>(pos.x) + 0.5f;
    float py = static_cast<float>(pos.y) + 0.5f;
    float pz = static_cast<float>(pos.z) + 0.5f;
    return makeSoundEventValue(soundSet, action, category, px, py, pz, volume, pitch, positional);
}

}  // namespace finevox::script
