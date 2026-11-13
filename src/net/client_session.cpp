#include "finevox/net/client_session.hpp"
#include "finevox/net/messages.hpp"

#include "finevox/script/event_value.hpp"

#include <finenet/endpoint.hpp>
#include <finescript/map_data.h>

#include <algorithm>
#include <utility>
#include <iostream>

namespace finevox::net {

using finenet::ConnectionState;
using finenet::Message;
using finescript::Value;
using Clock = std::chrono::steady_clock;

namespace {

/// How long to keep trying to get back to a game after losing it
constexpr auto RECONNECT_FOR = std::chrono::seconds(45);
constexpr auto RECONNECT_EVERY = std::chrono::seconds(1);
constexpr auto LOOP_WAIT = std::chrono::milliseconds(5);

/// A prediction stands for this long plus two round trips
constexpr int PREDICTION_BASE_MS = 150;

}  // namespace

std::string_view phaseName(ClientPhase phase) {
    switch (phase) {
        case ClientPhase::Idle: return "idle";
        case ClientPhase::Connecting: return "connecting";
        case ClientPhase::Joining: return "joining";
        case ClientPhase::Content: return "content";
        case ClientPhase::Lobby: return "lobby";
        case ClientPhase::Loading: return "loading";
        case ClientPhase::Running: return "running";
        case ClientPhase::Reconnecting: return "reconnecting";
        case ClientPhase::Ended: return "ended";
        case ClientPhase::Failed: return "failed";
    }
    return "idle";
}

ClientSession::ClientSession(Options options) : options_(std::move(options)) {
    if (options_.cacheDir.empty()) options_.cacheDir = defaultCacheDir();
    if (options_.name.size() > MAX_NAME_BYTES) options_.name.resize(MAX_NAME_BYTES);
    token_ = options_.token;
    contentRoot_ = options_.gameRoot;
    thread_ = std::thread([this] { run(); });
}

ClientSession::~ClientSession() {
    stopping_ = true;
    signal_.signal();
    if (thread_.joinable()) thread_.join();
    std::shared_ptr<finenet::Connection> connection;
    {
        std::lock_guard lock(mutex_);
        connection = std::move(connection_);
    }
}

void ClientSession::connect(std::vector<std::string> addresses, uint16_t defaultPort) {
    std::lock_guard lock(mutex_);
    addresses_ = std::move(addresses);
    defaultPort_ = defaultPort;
    addressIndex_ = 0;
    nextDial_ = Clock::now();
    phase_ = ClientPhase::Connecting;
    events_.push_back({SessionEvent::Kind::Phase, std::string(phaseName(ClientPhase::Connecting)), {}});
    signal_.signal();
}

void ClientSession::attach(std::unique_ptr<finenet::Connection> connection) {
    std::lock_guard lock(mutex_);
    local_ = connection->isLocal();
    connection->setWakeSignal(&signal_);
    connection_ = std::move(connection);
    helloSent_ = false;
    registerHandlers();
    phase_ = ClientPhase::Connecting;
    signal_.signal();
}

void ClientSession::leave() {
    leaving_ = true;
    std::shared_ptr<finenet::Connection> connection;
    {
        std::lock_guard lock(mutex_);
        connection = connection_;
        addresses_.clear();
    }
    if (connection && connection->isConnected()) {
        connection->send(CH_CONTROL, Message(msg::Leave, encodeBody(Value::map())));
        connection->disconnect("left");
    }
    setPhase(ClientPhase::Ended);
}

std::string ClientSession::failure() const {
    std::lock_guard lock(mutex_);
    return failure_;
}

std::vector<SessionEvent> ClientSession::takeEvents() {
    std::lock_guard lock(mutex_);
    return std::exchange(events_, {});
}

std::string ClientSession::token() const {
    std::lock_guard lock(mutex_);
    return token_;
}

std::string ClientSession::serverTitle() const {
    std::lock_guard lock(mutex_);
    return title_;
}

int ClientSession::serverRevision() const {
    std::lock_guard lock(mutex_);
    return serverRevision_;
}

float ClientSession::rttMs() const {
    std::lock_guard lock(mutex_);
    return connection_ ? connection_->stats().rttMs : 0.0f;
}

std::filesystem::path ClientSession::contentRoot() const {
    std::lock_guard lock(mutex_);
    return contentRoot_;
}

std::vector<uint8_t> ClientSession::lobbyDeclaration() const {
    std::lock_guard lock(mutex_);
    return lobbyDeclaration_;
}

float ClientSession::contentProgress() const {
    return contentProgress_.load();
}

WorldInfo ClientSession::worldInfo() const {
    std::lock_guard lock(mutex_);
    return worldInfo_.value_or(WorldInfo{});
}

std::optional<Binding> ClientSession::binding() const {
    std::lock_guard lock(mutex_);
    return binding_;
}

std::vector<uint8_t> ClientSession::vision() const {
    std::lock_guard lock(mutex_);
    return vision_;
}

std::vector<ProjectileView> ClientSession::projectiles() const {
    std::lock_guard lock(mutex_);
    return projectiles_;
}

std::shared_ptr<const std::vector<worldgen::SiteGhost>> ClientSession::ghosts() const {
    std::lock_guard lock(mutex_);
    return ghosts_;
}

float ClientSession::timeOfDay() const {
    std::lock_guard lock(mutex_);
    WorldTime now;
    float seconds = std::chrono::duration<float>(Clock::now() - timeStamp_).count();
    int64_t ticks = time_.totalTicks();
    if (!time_.isFrozen() && timeStamp_ != Clock::time_point{}) {
        ticks += static_cast<int64_t>(seconds * (worldInfo_ ? worldInfo_->tickRate : 30));
    }
    now.setTime(ticks);
    return now.timeOfDay();
}

int64_t ClientSession::day() const {
    std::lock_guard lock(mutex_);
    return time_.totalTicks() / WorldTime::TICKS_PER_DAY;
}

size_t ClientSession::entitiesKnown() const {
    std::lock_guard lock(mutex_);
    return entities_.size();
}

std::vector<EntityState> ClientSession::entities() const {
    std::lock_guard lock(mutex_);
    std::vector<EntityState> out;
    out.reserve(entities_.size());
    for (const auto& [id, state] : entities_) out.push_back(state);
    return out;
}

float ClientSession::entityHealth(EntityId id) const {
    std::lock_guard lock(mutex_);
    auto it = health_.find(id);
    return it == health_.end() ? 1.0f : static_cast<float>(it->second) / FULL_HEALTH;
}

void ClientSession::send(finenet::ChannelId channel, uint32_t type, std::vector<uint8_t> body) {
    std::shared_ptr<finenet::Connection> connection;
    {
        std::lock_guard lock(mutex_);
        connection = connection_;
    }
    if (connection) connection->send(channel, Message(type, std::move(body)));
}

void ClientSession::sendMap(finenet::ChannelId channel, uint32_t type, const Value& map) {
    send(channel, type, encodeBody(map));
}

void ClientSession::setSetting(std::string_view name, const Value& value, int64_t player) {
    Value body = Value::map();
    setField(body, "key", Value::string(std::string(name)));
    setField(body, "value", value);
    if (player != 0) setField(body, "player", Value::integer(player));
    sendMap(CH_CONTROL, msg::SetSetting, body);
}

void ClientSession::setReady(bool ready) {
    Value body = Value::map();
    setField(body, "ready", Value::boolean(ready));
    sendMap(CH_CONTROL, msg::SetReady, body);
}

void ClientSession::launch(bool launch) {
    Value body = Value::map();
    setField(body, "launch", Value::boolean(launch));
    sendMap(CH_CONTROL, msg::Launch, body);
}

void ClientSession::chat(std::string_view text, bool teamOnly) {
    Value body = Value::map();
    setField(body, "text", Value::string(std::string(text.substr(0, MAX_CHAT_BYTES))));
    if (teamOnly) setField(body, "to", symbol("team"));
    sendMap(CH_CHAT, msg::Chat, body);
}

void ClientSession::kick(int64_t player) {
    Value body = Value::map();
    setField(body, "player", Value::integer(player));
    sendMap(CH_CONTROL, msg::Kick, body);
}

void ClientSession::sendLoaded() {
    sendMap(CH_CONTROL, msg::Loaded, Value::map());
}

void ClientSession::sendCommand(const Value& command) {
    auto body = encodeBody(command);
    if (body.size() > MAX_COMMAND_BYTES) {
        std::cerr << "[net] dropped a command of " << body.size() << " bytes\n";
        return;
    }
    send(CH_ACTIONS, msg::Command, std::move(body));
}

void ClientSession::sendPlayerState(const EntityState& state) {
    PlayerStateWire wire;
    wire.position = glm::vec3(state.position);
    wire.velocity = glm::vec3(state.velocity);
    wire.yaw = state.yaw;
    wire.pitch = state.pitch;
    wire.onGround = state.onGround;
    send(CH_INPUT, msg::PlayerState, encodePlayerState(wire));
}

void ClientSession::sendViewHint(double x, double z) {
    send(CH_INPUT, msg::ViewHint, encodeViewHint({static_cast<float>(x), static_cast<float>(z)}));
}

void ClientSession::sendClientState(std::span<const uint8_t> state) {
    send(CH_ACTIONS, msg::ClientState, std::vector<uint8_t>(state.begin(), state.end()));
}

void ClientSession::predictBlock(BlockCoord pos, BlockTypeId block) {
    int hold = PREDICTION_BASE_MS + static_cast<int>(rttMs() * 2.0f);
    world_.predict(pos, block, std::chrono::milliseconds(hold));
    expectChange(pos);
}

void ClientSession::expectChange(BlockCoord pos) {
    send(CH_ACTIONS, msg::ExpectChange, encodeExpectChange({pos.x, pos.y, pos.z}));
}

void ClientSession::setPhase(ClientPhase phase) {
    phase_ = phase;
    push(SessionEvent::Kind::Phase, std::string(phaseName(phase)));
}

void ClientSession::push(SessionEvent::Kind kind, std::string text, std::vector<uint8_t> body) {
    std::lock_guard lock(mutex_);
    events_.push_back({kind, std::move(text), std::move(body)});
}

void ClientSession::fail(std::string reason) {
    {
        std::lock_guard lock(mutex_);
        failure_ = reason;
        addresses_.clear();
    }
    phase_ = ClientPhase::Failed;
    push(SessionEvent::Kind::Failed, std::move(reason));
}

void ClientSession::registerHandlers() {
    // mutex_ is held
    auto& c = *connection_;
    auto lobbyEvent = [this](SessionEvent::Kind kind) {
        return [this, kind](const Message& message) {
            auto payload = message.payload();
            push(kind, {}, std::vector<uint8_t>(payload.begin(), payload.end()));
        };
    };

    c.onMessage(msg::Welcome, [this](const Message& m) { onWelcome(m); });
    c.onMessage(msg::Reject, [this](const Message& m) { onReject(m); });
    c.onMessage(msg::LobbyState, [this, lobbyEvent](const Message& m) {
        ClientPhase now = phase_.load();
        if (now == ClientPhase::Joining) setPhase(ClientPhase::Lobby);
        lobbyEvent(SessionEvent::Kind::Lobby)(m);
    });
    c.onMessage(msg::Chat, lobbyEvent(SessionEvent::Kind::Chat));
    c.onMessage(msg::UiMessage, lobbyEvent(SessionEvent::Kind::Ui));
    c.onMessage(msg::ContentManifest, [this](const Message& m) { onContentManifest(m); });
    c.onMessage(msg::ContentChunk, [this](const Message& m) { onContentChunk(m); });
    c.onMessage(msg::GameStarting, [this](const Message& m) { onGameStarting(m); });
    c.onMessage(msg::GameStart, [this](const Message&) { setPhase(ClientPhase::Running); });
    c.onMessage(msg::GameEnded, [this](const Message&) {
        {
            std::lock_guard lock(mutex_);
            addresses_.clear();
        }
        leaving_ = true;
        setPhase(ClientPhase::Ended);
    });
    c.onMessage(msg::SyncDone, [this](const Message&) {
        worldReady_ = true;
        push(SessionEvent::Kind::WorldReady);
    });
    c.onMessage(msg::Bound, [this](const Message& m) { onBound(m); });
    c.onMessage(msg::BlockName, [this](const Message& m) {
        auto name = decodeName(m.payload());
        if (!name) return;
        world_.nameBlock(name->id, name->name);
        ghostBlocks_[name->id] = BlockTypeId::fromName(name->name);
    });
    c.onMessage(msg::FluidName, [this](const Message& m) {
        if (auto name = decodeName(m.payload())) world_.nameFluid(name->id, name->name);
    });
    c.onMessage(msg::EntityTypeName, [this](const Message& m) {
        if (auto name = decodeName(m.payload())) entityTypeNames_[name->id] = std::move(name->name);
    });
    c.onMessage(msg::Subchunk, [this](const Message& m) {
        SubchunkData data;
        if (decodeSubchunk(m.payload(), data)) {
            world_.apply(data);
        } else {
            std::cerr << "[net] a bad subchunk from the server\n";
        }
    });
    c.onMessage(msg::Vision, [this](const Message& m) { onVision(m); });
    c.onMessage(msg::Time, [this](const Message& m) { onTime(m); });
    c.onMessage(msg::Ghosts, [this](const Message& m) { onGhosts(m); });
    c.onMessage(msg::EntitySpawn, [this](const Message& m) { onEntitySpawn(m); });
    c.onMessage(msg::EntityGone, [this](const Message& m) { onEntityGone(m); });
    c.onMessage(msg::EntityBatch, [this](const Message& m) { onEntityBatch(m); });
    c.onMessage(msg::Projectiles, [this](const Message& m) { onProjectiles(m); });
}

void ClientSession::sayHello() {
    Value body = Value::map();
    setField(body, "protocol", Value::integer(PROTOCOL_VERSION));
    setField(body, "revision", Value::integer(PROTOCOL_REVISION));
    setField(body, "game", Value::string(options_.game));
    setField(body, "name", Value::string(options_.name));
    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
    }
    if (!token.empty()) setField(body, "token", Value::string(token));
    if (!options_.password.empty()) setField(body, "password", Value::string(options_.password));

    std::vector<Value> caps;
    for (const std::string& cap : options_.caps) caps.push_back(Value::string(cap));
    setField(body, "caps", Value::array(std::move(caps)));
    sendMap(CH_CONTROL, msg::Hello, body);
}

void ClientSession::dial() {
    std::string address;
    {
        std::lock_guard lock(mutex_);
        if (addressIndex_ >= addresses_.size()) return;
        address = addresses_[addressIndex_];
    }

    auto endpoint = finenet::parseEndpoint(address, defaultPort_);
    if (!endpoint || endpoint->port == 0) {
        fail("not an address: " + address);
        return;
    }
    auto connection = finenet::Connection::connect(endpoint->host, endpoint->port, connectionConfig());

    std::lock_guard lock(mutex_);
    connection->setWakeSignal(&signal_);
    connection_ = std::move(connection);
    helloSent_ = false;
    registerHandlers();
}

void ClientSession::onConnectionLost() {
    std::string reason;
    bool hadWelcome;
    size_t addressCount;
    {
        std::lock_guard lock(mutex_);
        reason = connection_ ? connection_->disconnectReason() : "";
        hadWelcome = hadWelcome_;
        addressCount = addresses_.size();
        connection_.reset();
        helloSent_ = false;
    }

    ClientPhase now = phase_.load();
    if (leaving_ || now == ClientPhase::Ended || now == ClientPhase::Failed) return;

    bool inGame = now == ClientPhase::Running || now == ClientPhase::Loading || now == ClientPhase::Reconnecting;
    if (hadWelcome && inGame && addressCount > 0) {
        std::lock_guard lock(mutex_);
        if (now != ClientPhase::Reconnecting) {
            reconnectUntil_ = Clock::now() + RECONNECT_FOR;
            phase_ = ClientPhase::Reconnecting;
            events_.push_back({SessionEvent::Kind::Phase, std::string(phaseName(ClientPhase::Reconnecting)), {}});
        }
        if (Clock::now() < reconnectUntil_) {
            nextDial_ = Clock::now() + RECONNECT_EVERY;
            return;
        }
    } else if (!hadWelcome) {
        // On to the server's next address
        std::lock_guard lock(mutex_);
        if (++addressIndex_ < addresses_.size()) {
            nextDial_ = Clock::now();
            return;
        }
    }

    if (now == ClientPhase::Lobby || now == ClientPhase::Running || now == ClientPhase::Reconnecting) {
        fail(reason.empty() ? "lost the server" : reason);
    } else {
        fail(reason.empty() ? "couldn't reach the server" : reason);
    }
}

void ClientSession::run() {
    auto lastSettle = Clock::now();
    while (!stopping_) {
        std::shared_ptr<finenet::Connection> connection;
        bool wantDial = false;
        bool hello = false;
        {
            std::lock_guard lock(mutex_);
            connection = connection_;
            wantDial = !connection_ && addressIndex_ < addresses_.size() && !addresses_.empty() &&
                       Clock::now() >= nextDial_;
            hello = connection_ && !helloSent_ && connection_->isConnected();
            if (hello) helloSent_ = true;
        }

        if (wantDial && !leaving_) {
            dial();
            continue;
        }

        if (connection) {
            if (hello) {
                if (phase_.load() != ClientPhase::Reconnecting) setPhase(ClientPhase::Joining);
                sayHello();
            }
            connection->update();
            if (connection->state() == ConnectionState::Disconnected) {
                connection->update();  // Anything that arrived in time, such as a Reject
                onConnectionLost();
                continue;
            }
        }

        auto now = Clock::now();
        if (now - lastSettle > std::chrono::milliseconds(20)) {
            world_.settle(now);
            lastSettle = now;
        }
        signal_.waitFor(LOOP_WAIT);
    }
}

void ClientSession::onWelcome(const Message& message) {
    Value body = decodeBody(message.payload());
    playerId_ = intField(body, "player");

    std::string phase = textField(body, "phase");
    {
        std::lock_guard lock(mutex_);
        token_ = textField(body, "token");
        title_ = textField(body, "title");
        serverRevision_ = static_cast<int>(intField(body, "revision"));
        lobbyDeclaration_ = encodeBody(field(body, "lobby"));
        hadWelcome_ = true;
    }

    bool returning = phase_.load() == ClientPhase::Reconnecting;
    if (returning) {
        // The server's about to tell us everything again, and it may have changed
        forgetEntities();
        worldReady_ = false;
    }

    if (!boolField(body, "content", true)) {
        contentProgress_ = 1.0f;
    } else if (!returning) {
        setPhase(ClientPhase::Content);
    }
}

void ClientSession::forgetEntities() {
    std::vector<EntityId> ids;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, state] : entities_) ids.push_back(id);
        entities_.clear();
        health_.clear();
    }
    for (EntityId id : ids) graphics_.push(GraphicsMessage::fromEvent(script::makeEntityDespawnValue(id, false)));
}

void ClientSession::onReject(const Message& message) {
    Value body = decodeBody(message.payload());
    std::string reason = textField(body, "reason");
    std::string text = textField(body, "text");
    fail(text.empty() ? reason : text);
}

void ClientSession::onContentManifest(const Message& message) {
    Value body = decodeBody(message.payload());
    Value list = field(body, "files");
    if (!list.isArray()) {
        fail("the server sent a bad content list");
        return;
    }

    std::vector<ContentFile> files;
    for (const Value& entry : list.asArray()) {
        ContentFile file;
        file.path = textField(entry, "path");
        file.size = static_cast<uint64_t>(intField(entry, "size"));
        auto hash = fromHex(textField(entry, "sha256"));
        if (!hash) {
            fail("the server sent a bad content list");
            return;
        }
        file.hash = *hash;
        files.push_back(std::move(file));
    }

    contentSync_ = std::make_unique<ContentSync>(options_.gameRoot, options_.cacheDir);
    if (!contentSync_->begin(std::move(files))) {
        fail("the server named a file outside the game");
        return;
    }

    if (contentSync_->done()) {
        onContentChunk(Message(msg::ContentChunk));
        return;
    }

    std::vector<Value> wanted;
    for (const Sha256& hash : contentSync_->missing()) {
        wanted.push_back(Value::string(toHex(hash)));
    }
    Value request = Value::map();
    setField(request, "files", Value::array(std::move(wanted)));
    sendMap(CH_ASSETS, msg::ContentRequest, request);
}

void ClientSession::onContentChunk(const Message& message) {
    if (!contentSync_) return;

    if (!contentSync_->done()) {
        auto chunk = decodeContentChunk(message.payload());
        if (!chunk) {
            fail("the server sent bad content");
            return;
        }
        Sha256 hash{};
        std::copy(chunk->sha256.begin(), chunk->sha256.end(), hash.begin());
        if (!contentSync_->receive(hash, chunk->offset, chunk->total, chunk->data)) {
            fail("the server sent content that doesn't match its list");
            return;
        }
        uint64_t expected = contentSync_->bytesExpected();
        contentProgress_ = expected == 0 ? 1.0f
                                         : static_cast<float>(contentSync_->bytesReceived()) /
                                               static_cast<float>(expected);
    }

    if (contentSync_->done()) {
        std::filesystem::path root = contentSync_->finish();
        {
            std::lock_guard lock(mutex_);
            contentRoot_ = std::move(root);
        }
        contentProgress_ = 1.0f;
        contentSync_.reset();
        sendMap(CH_ASSETS, msg::ContentReady, Value::map());
        if (phase_.load() == ClientPhase::Content) setPhase(ClientPhase::Lobby);
    }
}

void ClientSession::onGameStarting(const Message& message) {
    Value body = decodeBody(message.payload());
    Value world = field(body, "world");

    WorldInfo info;
    info.radius = static_cast<int32_t>(intField(world, "radius"));
    info.seaLevel = static_cast<int32_t>(intField(world, "sea_level"));
    info.tickRate = static_cast<uint32_t>(intField(world, "tick_rate", 30));
    info.entityRate = static_cast<uint32_t>(intField(world, "entity_rate", 30));
    info.fog = boolField(world, "fog");
    info.viewDistance = static_cast<float>(numberField(world, "view_distance", 256.0));
    info.lod = boolField(world, "lod", true);
    info.stepHeight = static_cast<float>(numberField(world, "step_height", 0.6));
    info.playerEntity = textField(world, "player_entity");
    info.visionCell = static_cast<int32_t>(intField(world, "vision_cell", 4));
    Value minimum = field(world, "vision_min");
    Value cells = field(world, "vision_cells");
    if (minimum.isArray() && minimum.asArray().size() == 2) {
        info.visionMinX = static_cast<int32_t>(minimum.asArray()[0].asNumber());
        info.visionMinZ = static_cast<int32_t>(minimum.asArray()[1].asNumber());
    }
    if (cells.isArray() && cells.asArray().size() == 2) {
        info.visionCellsX = static_cast<int32_t>(cells.asArray()[0].asNumber());
        info.visionCellsZ = static_cast<int32_t>(cells.asArray()[1].asNumber());
    }
    info.resumed = boolField(world, "resumed");
    Value state = field(world, "client_state");
    if (!state.isNil()) info.clientState = encodeBody(state);

    {
        std::lock_guard lock(mutex_);
        worldInfo_ = std::move(info);
    }
    if (phase_.load() != ClientPhase::Reconnecting) {
        setPhase(ClientPhase::Loading);
    }
}

void ClientSession::onBound(const Message& message) {
    Value body = decodeBody(message.payload());
    Binding bound;
    bound.faction = static_cast<uint16_t>(intField(body, "faction"));
    bound.body = static_cast<EntityId>(intField(body, "body"));
    Value spawn = field(body, "spawn");
    if (spawn.isArray() && spawn.asArray().size() == 3) {
        const auto& v = spawn.asArray();
        bound.spawn = glm::dvec3(v[0].asNumber(), v[1].asNumber(), v[2].asNumber());
    }
    std::lock_guard lock(mutex_);
    binding_ = bound;
    if (field(body, "moved").truthy()) pendingMove_ = bound.spawn;
}

std::optional<glm::dvec3> ClientSession::takeMove() {
    std::lock_guard lock(mutex_);
    return std::exchange(pendingMove_, std::nullopt);
}

void ClientSession::onEntitySpawn(const Message& message) {
    auto spawn = decodeEntitySpawn(message.payload());
    if (!spawn) return;
    EntityState state;
    state.id = spawn->id;
    uint32_t type = spawn->type;
    state.faction = spawn->faction;
    state.position = spawn->position;
    state.yaw = spawn->yaw;
    state.pitch = spawn->pitch;
    uint8_t health = spawn->health;

    auto name = entityTypeNames_.find(type);
    graphics_.push(GraphicsMessage::fromEvent(script::makeEntitySpawnValue(
        state.id, 0, state.position, state.yaw, state.pitch, name == entityTypeNames_.end() ? "" : name->second)));

    // A first snapshot, so that it's drawn before anything's moved
    EntitySnapshot snapshot;
    snapshot.entity = state;
    graphics_.push(GraphicsMessage::fromSnapshot(std::move(snapshot)));

    std::lock_guard lock(mutex_);
    health_[state.id] = health;
    entities_[state.id] = std::move(state);
}

void ClientSession::onEntityGone(const Message& message) {
    auto gone = decodeEntityGone(message.payload());
    if (!gone) return;
    EntityId id = gone->id;
    bool died = gone->died;
    graphics_.push(GraphicsMessage::fromEvent(script::makeEntityDespawnValue(id, died)));
    std::lock_guard lock(mutex_);
    entities_.erase(id);
    health_.erase(id);
}

void ClientSession::onEntityBatch(const Message& message) {
    auto decoded = decodeEntityBatch(message.payload());
    if (!decoded) return;
    uint64_t tick = decoded->tick;

    std::vector<GraphicsMessage> batch;
    batch.reserve(decoded->entities.size());
    std::lock_guard lock(mutex_);
    for (const EntityMoveWire& move : decoded->entities) {
        EntityId id = move.id;
        auto known = entities_.find(id);
        EntityState parsed;
        parsed.id = id;
        parsed.position = glm::dvec3(move.position);
        parsed.velocity = glm::dvec3(move.velocity);
        parsed.yaw = move.yaw;
        parsed.pitch = move.pitch;
        parsed.animationId = move.animation;
        parsed.onGround = move.onGround;
        if (known == entities_.end()) continue;  // It's since gone
        health_[id] = move.health;
        parsed.faction = known->second.faction;
        known->second = parsed;

        EntitySnapshot snapshot;
        snapshot.entity = std::move(parsed);
        snapshot.tickNumber = tick;
        batch.push_back(GraphicsMessage::fromSnapshot(std::move(snapshot)));
    }
    graphics_.pushBatch(std::move(batch));
}

void ClientSession::onProjectiles(const Message& message) {
    auto shots = decodeProjectiles(message.payload());
    if (!shots) return;
    std::vector<ProjectileView> views;
    views.reserve(shots->size());
    for (const ProjectileWire& shot : *shots) {
        views.push_back({glm::dvec3(shot.position), shot.velocity, 0});
    }
    std::lock_guard lock(mutex_);
    projectiles_ = std::move(views);
}

void ClientSession::onVision(const Message& message) {
    constexpr size_t LIMIT = 16 * 1024 * 1024;
    auto vision = decodeVision(message.payload(), LIMIT);
    if (!vision) return;
    std::lock_guard lock(mutex_);
    vision_ = std::move(vision->cells);
}

void ClientSession::onTime(const Message& message) {
    auto time = decodeTime(message.payload());
    if (!time) return;
    paused_ = time->paused;
    std::lock_guard lock(mutex_);
    time_.setTime(static_cast<int64_t>(time->ticks));
    time_.setFrozen(time->held || time->paused);
    timeStamp_ = Clock::now();
}

void ClientSession::onGhosts(const Message& message) {
    auto wire = decodeGhosts(message.payload());
    if (!wire) return;

    auto ghosts = std::make_shared<std::vector<worldgen::SiteGhost>>();
    ghosts->resize(wire->size());
    for (size_t i = 0; i < wire->size(); ++i) {
        const GhostWire& group = (*wire)[i];
        auto& ghost = (*ghosts)[i];
        ghost.built = group.built;
        ghost.cells.resize(group.cells.size());
        for (size_t c = 0; c < group.cells.size(); ++c) {
            const GhostCellWire& from = group.cells[c];
            auto& cell = ghost.cells[c];
            cell.pos = BlockCoord(from.x, from.y, from.z);
            auto name = ghostBlocks_.find(from.block);
            cell.block = name == ghostBlocks_.end() ? BlockTypeId() : name->second;
        }
    }
    std::lock_guard lock(mutex_);
    ghosts_ = std::move(ghosts);
}

}  // namespace finevox::net
