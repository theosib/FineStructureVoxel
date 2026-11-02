#include "finevox/game/replication_server.hpp"

#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/projectile_system.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world_replica.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/game/game_runtime.hpp"
#include "finevox/net/protocol.hpp"
#include "finevox/net/subchunk_codec.hpp"
#include "finevox/script/event_value.hpp"
#include "finevox/worldgen/blueprint.hpp"

#include <finescript/map_data.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace finevox::game {

namespace net = finevox::net;
using finenet::Message;
using net::ByteWriter;
using finescript::Value;

namespace {

/// What may wait on a connection before this stops adding to it. Enough to fill a LAN's pipe,
/// and little enough that what's sent is still true when it arrives.
constexpr size_t WORLD_BACKLOG = 512 * 1024;
constexpr size_t ENTITY_BACKLOG = 64 * 1024;
/// A single pass's share of a big sync, so that the tick it lands on still finishes on time
constexpr int SUBCHUNKS_PER_PASS = 256;

constexpr uint64_t VISION_PASSES = 15;
constexpr uint64_t TIME_PASSES = 30;
constexpr uint64_t NETWORK_ENTITY_PASSES = 2;  // A local client gets every tick

uint64_t chunkKey(const ChunkPos& pos) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(pos.x) & 0xFFFFF) << 40) |
           (static_cast<uint64_t>(static_cast<uint32_t>(pos.z) & 0xFFFFF) << 20) |
           static_cast<uint64_t>(static_cast<uint32_t>(pos.y) & 0xFFFFF);
}

void markAllDirty(ClientView& /*view*/, World& world, std::unordered_map<uint64_t, ChunkPos>& dirty) {
    world.forEachColumn([&dirty](ColumnPos pos, ChunkColumn& column) {
        column.forEachSubChunk([&](int32_t chunkY, SubChunk&) {
            ChunkPos chunk(pos.x, chunkY, pos.z);
            dirty[chunkKey(chunk)] = chunk;
        });
    });
}

/// Palette of a subchunk's blocks, in first-seen order. Each id is the wire id.
net::SubchunkData gather(const SubChunk& subchunk, ChunkPos pos) {
    net::SubchunkData data;
    data.pos = pos;

    std::unordered_map<uint32_t, uint16_t> seen;
    std::vector<uint16_t> indices(net::SubchunkData::CELLS);
    for (int32_t i = 0; i < static_cast<int32_t>(net::SubchunkData::CELLS); ++i) {
        uint32_t id = subchunk.getBlock(i).id;
        auto [it, inserted] = seen.try_emplace(id, static_cast<uint16_t>(data.palette.size()));
        if (inserted) data.palette.push_back(id);
        indices[i] = it->second;
    }
    if (data.palette.size() > 1) data.indices = std::move(indices);
    data.light = subchunk.lightData();

    if (subchunk.fluidLayer()) {
        for (int32_t i = 0; i < static_cast<int32_t>(net::SubchunkData::CELLS); ++i) {
            FluidTypeId fluid = subchunk.getFluid(i);
            if (fluid.isEmpty()) continue;
            data.fluids.push_back({static_cast<uint16_t>(i), fluid.id, subchunk.getFluidLevel(i)});
        }
    }
    return data;
}

}  // namespace

ClientView::ClientView(int64_t player, std::shared_ptr<finenet::Connection> connection)
    : player_(player), connection_(std::move(connection)) {}

ReplicationServer::ReplicationServer(GameRuntime& runtime) : runtime_(runtime) {}

void ReplicationServer::addView(std::shared_ptr<ClientView> view) {
    std::lock_guard lock(mutex_);
    arriving_.push_back(std::move(view));
}

void ReplicationServer::removeView(int64_t player) {
    std::lock_guard lock(mutex_);
    leaving_.push_back(player);
}

void ReplicationServer::post(std::function<void()> work) {
    std::lock_guard lock(mutex_);
    posted_.push_back(std::move(work));
}

void ReplicationServer::publish(std::vector<uint8_t> message, int64_t player, int64_t faction) {
    for (auto& view : views_) {
        if (player != 0 && view->player() != player) continue;
        if (faction != 0 && (!view->bound_ || view->faction_ != faction)) continue;
        send(*view, net::CH_UI, net::msg::UiMessage, message);
    }
}

void ReplicationServer::tick(float dt) {
    runtime_.tickReplicas(dt);
    pump();
}

void ReplicationServer::runPosted() {
    std::vector<std::function<void()>> work;
    std::vector<std::shared_ptr<ClientView>> arriving;
    std::vector<int64_t> leaving;
    {
        std::lock_guard lock(mutex_);
        work = std::exchange(posted_, {});
        arriving = std::exchange(arriving_, {});
        leaving = std::exchange(leaving_, {});
    }

    for (auto& view : arriving) views_.push_back(std::move(view));
    for (int64_t player : leaving) {
        std::erase_if(views_, [player](const auto& view) { return view->player() == player; });
    }
    for (auto& task : work) task();
}

void ReplicationServer::adoptChanges() {
    for (const auto& [faction, replica] : runtime_.replicas()) {
        auto changes = replica->takeChanges();
        if (changes.empty()) continue;
        for (auto& view : views_) {
            if (!view->bound_ || view->faction_ != faction) continue;
            for (const ChunkPos& chunk : changes) view->dirty_[chunkKey(chunk)] = chunk;
        }
    }
}

void ReplicationServer::collectDeaths() {
    died_.clear();
    for (auto& message : runtime_.session().graphicsEvents().drainAll()) {
        if (message.kind != GraphicsMessage::Kind::Event) continue;
        if (script::readEventType(message.event) != script::EVT_ENTITY_DESPAWN) continue;
        if (script::readBool(message.event, script::EventSymbols::instance().died)) {
            died_.insert(script::readEntityId(message.event));
        }
    }
}

void ReplicationServer::pump() {
    runPosted();
    collectDeaths();
    adoptChanges();
    ++pass_;

    for (auto& pointer : views_) {
        ClientView& view = *pointer;
        if (!view.connection_->isConnected()) continue;
        sendBinding(view);
        if (!view.bound_) continue;

        sendWorld(view);
        uint64_t every = view.connection_->isLocal() ? 1 : NETWORK_ENTITY_PASSES;
        if (pass_ % every == 0) {
            sendEntities(view);
            sendProjectiles(view);
        }
        if (pass_ % VISION_PASSES == 0 || view.lastVision_.empty()) sendVision(view);
        // A pause can't wait: the client has to stop when the game does
        if (pass_ % TIME_PASSES == 0 || view.sentPaused_ != runtime_.session().isPaused()) sendTime(view);
        sendGhosts(view);
    }
}

void ReplicationServer::send(ClientView& view, finenet::ChannelId channel, uint32_t type, std::vector<uint8_t> body) {
    bytesSent_.fetch_add(body.size(), std::memory_order_relaxed);
    view.connection_->send(channel, Message(type, std::move(body)));
}

void ReplicationServer::sendBinding(ClientView& view) {
    auto seat = runtime_.seat(view.player());
    if (!seat || seat->faction == 0) return;
    if (view.bound_ && view.faction_ == seat->faction && view.body_ == seat->body) return;

    bool newFaction = !view.bound_ || view.faction_ != seat->faction;
    view.faction_ = seat->faction;
    view.body_ = seat->body;
    view.bound_ = true;

    Value body = Value::map();
    net::setField(body, "faction", Value::integer(seat->faction));
    if (seat->body != INVALID_ENTITY_ID) {
        net::setField(body, "body", Value::integer(static_cast<int64_t>(seat->body)));
        net::setField(body, "spawn", Value::array(std::vector<Value>{Value::number(seat->spawn.x),
                                                                     Value::number(seat->spawn.y),
                                                                     Value::number(seat->spawn.z)}));
    }
    send(view, net::CH_WORLD, net::msg::Bound, net::encodeBody(body));

    if (newFaction) {
        view.dirty_.clear();
        markAllDirty(view, runtime_.replicaFor(seat->faction).world(), view.dirty_);
        view.lastVision_.clear();
        view.lastGhosts_ = nullptr;
    }
}

void ReplicationServer::sendSubchunk(ClientView& view, ChunkPos pos) {
    const World& world = runtime_.replicaFor(view.faction_).world();
    const SubChunk* subchunk = world.getSubChunk(pos);
    if (!subchunk) return;

    net::SubchunkData data = gather(*subchunk, pos);

    for (uint32_t id : data.palette) {
        if (id == 0 || !view.blockNames_.insert(id).second) continue;
        std::vector<uint8_t> body;
        ByteWriter w(body);
        w.var(id);
        w.str(BlockTypeId(id).name());
        send(view, net::CH_WORLD, net::msg::BlockName, std::move(body));
    }
    for (const auto& fluid : data.fluids) {
        if (!view.fluidNames_.insert(fluid.id).second) continue;
        std::vector<uint8_t> body;
        ByteWriter w(body);
        w.var(fluid.id);
        w.str(StringInterner::global().lookup(fluid.id));
        send(view, net::CH_WORLD, net::msg::FluidName, std::move(body));
    }

    std::vector<uint8_t> body;
    net::encodeSubchunk(data, body, !view.connection_->isLocal());
    send(view, net::CH_WORLD, net::msg::Subchunk, std::move(body));
}

void ReplicationServer::sendWorld(ClientView& view) {
    if (!view.dirty_.empty() && view.connection_->pendingBytes(net::CH_WORLD) < WORLD_BACKLOG) {
        std::vector<ChunkPos> order;
        order.reserve(view.dirty_.size());
        for (const auto& [key, chunk] : view.dirty_) order.push_back(chunk);

        // What the player's looking at first, when there's more than can go at once
        size_t take = std::min<size_t>(order.size(), SUBCHUNKS_PER_PASS);
        if (take < order.size()) {
            float x = view.hintX_.load() / 16.0f, z = view.hintZ_.load() / 16.0f;
            auto closer = [x, z](const ChunkPos& a, const ChunkPos& b) {
                float ax = float(a.x) - x, az = float(a.z) - z;
                float bx = float(b.x) - x, bz = float(b.z) - z;
                return ax * ax + az * az < bx * bx + bz * bz;
            };
            std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(take), order.end(), closer);
        }

        for (size_t i = 0; i < take; ++i) {
            if (view.connection_->pendingBytes(net::CH_WORLD) >= WORLD_BACKLOG) break;
            sendSubchunk(view, order[i]);
            view.dirty_.erase(chunkKey(order[i]));
        }
    }

    if (view.dirty_.empty() && !view.syncSent_) {
        // Vision and the time before this, so that a first frame has them
        sendVision(view);
        sendTime(view);
        view.syncSent_ = true;
        send(view, net::CH_WORLD, net::msg::SyncDone, net::encodeBody(Value::map()));
        view.synced_ = true;
    }
}

bool ReplicationServer::factionSees(const ClientView& view, const Vec3& position) const {
    if (!runtime_.manifest().fogOfWar) return true;
    if (runtime_.replicaFor(view.faction_).revealAll()) return true;
    return runtime_.vision().isVisible(view.faction_, static_cast<int32_t>(std::floor(position.x)),
                                       static_cast<int32_t>(std::floor(position.z)));
}

void ReplicationServer::sendEntities(ClientView& view) {
    if (view.connection_->pendingBytes(net::CH_ENTITIES) >= ENTITY_BACKLOG) return;

    std::unordered_set<EntityId> present;
    std::vector<std::pair<EntityId, EntityState>> moved;
    auto& entities = runtime_.session().entities();

    for (const auto& [id, entity] : entities.entities()) {
        if (!entity->isAlive()) continue;
        Vec3 position = entity->position();
        if (entity->faction() != view.faction_ && !factionSees(view, position)) continue;
        present.insert(id);

        EntityState state = EntityState::fromEntity(*entity);
        ClientView::SentEntity now;
        now.position = position;
        now.yaw = state.yaw;
        now.pitch = state.pitch;
        now.animation = state.animationId;
        now.onGround = state.onGround;

        auto known = view.known_.find(id);
        if (known == view.known_.end()) {
            std::string type(entity->typeName());
            uint32_t wireType = type.empty() ? 0 : StringInterner::global().intern(type);
            if (wireType != 0 && view.typeNames_.insert(wireType).second) {
                std::vector<uint8_t> body;
                ByteWriter w(body);
                w.var(wireType);
                w.str(type);
                send(view, net::CH_ENTITIES, net::msg::EntityTypeName, std::move(body));
            }

            std::vector<uint8_t> body;
            ByteWriter w(body);
            w.u64(id);
            w.var(wireType);
            w.u16(state.faction);
            w.f64(state.position.x);
            w.f64(state.position.y);
            w.f64(state.position.z);
            w.f32(state.yaw);
            w.f32(state.pitch);
            send(view, net::CH_ENTITIES, net::msg::EntitySpawn, std::move(body));
            view.known_.emplace(id, now);
            continue;
        }

        const auto& sent = known->second;
        constexpr float EPSILON = 0.002f;
        bool same = std::abs(sent.position.x - now.position.x) < EPSILON &&
                    std::abs(sent.position.y - now.position.y) < EPSILON &&
                    std::abs(sent.position.z - now.position.z) < EPSILON &&
                    std::abs(sent.yaw - now.yaw) < EPSILON && std::abs(sent.pitch - now.pitch) < EPSILON &&
                    sent.animation == now.animation && sent.onGround == now.onGround;
        if (same) continue;
        known->second = now;
        moved.emplace_back(id, std::move(state));
    }

    for (auto it = view.known_.begin(); it != view.known_.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        std::vector<uint8_t> body;
        ByteWriter w(body);
        w.u64(it->first);
        w.u8(died_.count(it->first) ? 1 : 0);
        send(view, net::CH_ENTITIES, net::msg::EntityGone, std::move(body));
        it = view.known_.erase(it);
    }

    if (moved.empty()) return;
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.u64(runtime_.gameScript().tickCount());
    w.var(moved.size());
    for (const auto& [id, state] : moved) {
        w.u64(id);
        w.f32(static_cast<float>(state.position.x));
        w.f32(static_cast<float>(state.position.y));
        w.f32(static_cast<float>(state.position.z));
        w.f32(static_cast<float>(state.velocity.x));
        w.f32(static_cast<float>(state.velocity.y));
        w.f32(static_cast<float>(state.velocity.z));
        w.f32(state.yaw);
        w.f32(state.pitch);
        w.u8(state.animationId);
        w.u8(state.onGround ? 1 : 0);
    }
    send(view, net::CH_ENTITIES, net::msg::EntityBatch, std::move(body));
}

void ReplicationServer::sendVision(ClientView& view) {
    if (!runtime_.manifest().fogOfWar) return;
    auto cells = runtime_.vision().snapshot(view.faction_);
    if (cells.empty() || cells == view.lastVision_) return;

    std::vector<uint8_t> compressed;
    net::encodeCompressed(cells, compressed);
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.bytes(compressed);
    send(view, net::CH_WORLD, net::msg::Vision, std::move(body));
    view.lastVision_ = std::move(cells);
}

void ReplicationServer::sendTime(ClientView& view) {
    const WorldTime& time = runtime_.session().worldTime();
    std::vector<uint8_t> body;
    ByteWriter w(body);
    view.sentPaused_ = runtime_.session().isPaused();
    w.u64(static_cast<uint64_t>(time.totalTicks()));
    w.u8((time.isFrozen() ? net::TIME_FROZEN : 0) | (view.sentPaused_ ? net::TIME_PAUSED : 0));
    send(view, net::CH_WORLD, net::msg::Time, std::move(body));
}

void ReplicationServer::sendProjectiles(ClientView& view) {
    std::vector<ProjectileView> visible;
    for (const ProjectileView& shot : runtime_.projectiles().snapshot()) {
        if (factionSees(view, Vec3(shot.position))) visible.push_back(shot);
    }
    if (visible.empty() && !view.hadProjectiles_) return;
    view.hadProjectiles_ = !visible.empty();

    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.var(visible.size());
    for (const ProjectileView& shot : visible) {
        w.f32(static_cast<float>(shot.position.x));
        w.f32(static_cast<float>(shot.position.y));
        w.f32(static_cast<float>(shot.position.z));
        w.f32(shot.velocity.x);
        w.f32(shot.velocity.y);
        w.f32(shot.velocity.z);
    }
    send(view, net::CH_ENTITIES, net::msg::Projectiles, std::move(body));
}

void ReplicationServer::sendGhosts(ClientView& view) {
    auto ghosts = runtime_.siteGhosts(view.faction_);
    if (ghosts.get() == view.lastGhosts_) return;
    view.lastGhosts_ = ghosts.get();

    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.var(ghosts ? ghosts->size() : 0);
    if (ghosts) {
        for (const auto& ghost : *ghosts) {
            for (const auto& cell : ghost.cells) {
                uint32_t id = cell.block.id;
                if (id == 0 || !view.blockNames_.insert(id).second) continue;
                std::vector<uint8_t> name;
                ByteWriter nameWriter(name);
                nameWriter.var(id);
                nameWriter.str(cell.block.name());
                send(view, net::CH_WORLD, net::msg::BlockName, std::move(name));
            }
            w.f32(ghost.built);
            w.var(ghost.cells.size());
            for (const auto& cell : ghost.cells) {
                w.svar(cell.pos.x);
                w.svar(cell.pos.y);
                w.svar(cell.pos.z);
                w.var(cell.block.id);
            }
        }
    }
    send(view, net::CH_WORLD, net::msg::Ghosts, std::move(body));
}

}  // namespace finevox::game
