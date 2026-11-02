#include "finevox/net/client_session.hpp"

#include "finevox/script/event_value.hpp"

#include <finenet/endpoint.hpp>
#include <finescript/map_data.h>

#include <algorithm>
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
}

void ClientSession::attach(std::unique_ptr<finenet::Connection> connection) {
    std::lock_guard lock(mutex_);
    local_ = connection->isLocal();
    connection_ = std::move(connection);
    helloSent_ = false;
    registerHandlers();
    phase_ = ClientPhase::Connecting;
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

void ClientSession::setSetting(std::string_view name, const Value& value) {
    Value body = Value::map();
    setField(body, "key", Value::string(std::string(name)));
    setField(body, "value", value);
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
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.f32(static_cast<float>(state.position.x));
    w.f32(static_cast<float>(state.position.y));
    w.f32(static_cast<float>(state.position.z));
    w.f32(static_cast<float>(state.velocity.x));
    w.f32(static_cast<float>(state.velocity.y));
    w.f32(static_cast<float>(state.velocity.z));
    w.f32(state.yaw);
    w.f32(state.pitch);
    w.u8(state.onGround ? 1 : 0);
    send(CH_INPUT, msg::PlayerState, std::move(body));
}

void ClientSession::sendViewHint(double x, double z) {
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.f32(static_cast<float>(x));
    w.f32(static_cast<float>(z));
    send(CH_INPUT, msg::ViewHint, std::move(body));
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
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.svar(pos.x);
    w.svar(pos.y);
    w.svar(pos.z);
    send(CH_ACTIONS, msg::ExpectChange, std::move(body));
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
        ByteReader r(m.payload());
        uint32_t id = static_cast<uint32_t>(r.var());
        std::string_view name = r.str();
        if (r.ok()) world_.nameBlock(id, name);
        if (r.ok()) ghostBlocks_[id] = BlockTypeId::fromName(name);
    });
    c.onMessage(msg::FluidName, [this](const Message& m) {
        ByteReader r(m.payload());
        uint32_t id = static_cast<uint32_t>(r.var());
        std::string_view name = r.str();
        if (r.ok()) world_.nameFluid(id, name);
    });
    c.onMessage(msg::EntityTypeName, [this](const Message& m) {
        ByteReader r(m.payload());
        uint32_t id = static_cast<uint32_t>(r.var());
        std::string name(r.str());
        if (r.ok()) entityTypeNames_[id] = std::move(name);
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
        std::this_thread::sleep_for(LOOP_WAIT);
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
        ByteReader r(message.payload());
        auto hashBytes = r.bytes();
        uint64_t offset = r.var();
        uint64_t total = r.var();
        auto data = r.bytes();
        Sha256 hash{};
        if (!r.ok() || hashBytes.size() != hash.size()) {
            fail("the server sent bad content");
            return;
        }
        std::copy(hashBytes.begin(), hashBytes.end(), hash.begin());
        if (!contentSync_->receive(hash, offset, total, data)) {
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
}

void ClientSession::onEntitySpawn(const Message& message) {
    ByteReader r(message.payload());
    EntityState state;
    state.id = r.u64();
    uint32_t type = static_cast<uint32_t>(r.var());
    state.faction = r.u16();
    state.position.x = r.f64();
    state.position.y = r.f64();
    state.position.z = r.f64();
    state.yaw = r.f32();
    state.pitch = r.f32();
    if (!r.ok()) return;

    auto name = entityTypeNames_.find(type);
    graphics_.push(GraphicsMessage::fromEvent(script::makeEntitySpawnValue(
        state.id, 0, state.position, state.yaw, state.pitch, name == entityTypeNames_.end() ? "" : name->second)));

    // A first snapshot, so that it's drawn before anything's moved
    EntitySnapshot snapshot;
    snapshot.entity = state;
    graphics_.push(GraphicsMessage::fromSnapshot(std::move(snapshot)));

    std::lock_guard lock(mutex_);
    entities_[state.id] = std::move(state);
}

void ClientSession::onEntityGone(const Message& message) {
    ByteReader r(message.payload());
    EntityId id = r.u64();
    bool died = r.u8() != 0;
    if (!r.ok()) return;
    graphics_.push(GraphicsMessage::fromEvent(script::makeEntityDespawnValue(id, died)));
    std::lock_guard lock(mutex_);
    entities_.erase(id);
}

void ClientSession::onEntityBatch(const Message& message) {
    ByteReader r(message.payload());
    uint64_t tick = r.u64();
    uint64_t count = r.var();
    constexpr uint64_t ENTRY_BYTES = 8 + 8 * 4 + 2;
    if (!r.ok() || count > r.remaining() / ENTRY_BYTES) return;

    std::vector<GraphicsMessage> batch;
    batch.reserve(count);
    std::lock_guard lock(mutex_);
    for (uint64_t i = 0; i < count; ++i) {
        EntityId id = r.u64();
        auto known = entities_.find(id);
        EntityState parsed;
        parsed.id = id;
        parsed.position.x = r.f32();
        parsed.position.y = r.f32();
        parsed.position.z = r.f32();
        parsed.velocity.x = r.f32();
        parsed.velocity.y = r.f32();
        parsed.velocity.z = r.f32();
        parsed.yaw = r.f32();
        parsed.pitch = r.f32();
        parsed.animationId = r.u8();
        parsed.onGround = (r.u8() & 1) != 0;
        if (known == entities_.end()) continue;  // It's since gone
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
    ByteReader r(message.payload());
    uint64_t count = r.var();
    constexpr uint64_t ENTRY_BYTES = 4 * 6;
    if (!r.ok() || count > r.remaining() / ENTRY_BYTES) return;

    std::vector<ProjectileView> views(count);
    for (auto& view : views) {
        view.position.x = r.f32();
        view.position.y = r.f32();
        view.position.z = r.f32();
        view.velocity.x = r.f32();
        view.velocity.y = r.f32();
        view.velocity.z = r.f32();
        view.faction = 0;
    }
    std::lock_guard lock(mutex_);
    projectiles_ = std::move(views);
}

void ClientSession::onVision(const Message& message) {
    ByteReader r(message.payload());
    std::vector<uint8_t> cells;
    constexpr size_t LIMIT = 16 * 1024 * 1024;
    if (!decodeCompressed(r.bytes(), cells, LIMIT)) return;
    std::lock_guard lock(mutex_);
    vision_ = std::move(cells);
}

void ClientSession::onTime(const Message& message) {
    ByteReader r(message.payload());
    uint64_t ticks = r.u64();
    uint8_t flags = r.u8();
    if (!r.ok()) return;
    paused_ = (flags & TIME_PAUSED) != 0;
    std::lock_guard lock(mutex_);
    time_.setTime(static_cast<int64_t>(ticks));
    time_.setFrozen(flags != 0);
    timeStamp_ = Clock::now();
}

void ClientSession::onGhosts(const Message& message) {
    ByteReader r(message.payload());
    uint64_t groups = r.var();
    if (!r.ok() || groups > r.remaining()) return;

    auto ghosts = std::make_shared<std::vector<worldgen::SiteGhost>>();
    ghosts->resize(groups);
    for (auto& ghost : *ghosts) {
        ghost.built = r.f32();
        uint64_t cells = r.var();
        if (!r.ok() || cells > r.remaining()) return;
        ghost.cells.resize(cells);
        for (auto& cell : ghost.cells) {
            cell.pos.x = static_cast<int32_t>(r.svar());
            cell.pos.y = static_cast<int32_t>(r.svar());
            cell.pos.z = static_cast<int32_t>(r.svar());
            auto name = ghostBlocks_.find(static_cast<uint32_t>(r.var()));
            cell.block = name == ghostBlocks_.end() ? BlockTypeId() : name->second;
        }
    }
    if (!r.ok()) return;
    std::lock_guard lock(mutex_);
    ghosts_ = std::move(ghosts);
}

}  // namespace finevox::net
