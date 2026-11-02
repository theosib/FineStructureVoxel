#include "finevox/game/game_host.hpp"

#include "finevox/core/entity_state.hpp"
#include "finevox/core/game_actions.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/world_replica.hpp"
#include "finevox/game/replication_server.hpp"
#include "finevox/script/finevox_interner.hpp"

#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <algorithm>
#include <cmath>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace finevox::game {

namespace fs = std::filesystem;
using finenet::Connection;
using finenet::ConnectionState;
using finenet::Message;
using finescript::Value;
using Clock = std::chrono::steady_clock;
using namespace finevox::net;

namespace {

constexpr auto LOOP_WAIT = std::chrono::milliseconds(5);
constexpr size_t ASSET_BACKLOG = 1024 * 1024;
constexpr size_t MAX_SETTING_TEXT = 64;

std::string freshToken() {
    std::random_device random;
    Sha256 bytes{};
    for (auto& byte : bytes) byte = static_cast<uint8_t>(random());
    return toHex(bytes).substr(0, 32);
}

std::string trim(std::string text) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && isSpace(text.front())) text.erase(text.begin());
    while (!text.empty() && isSpace(text.back())) text.pop_back();
    return text;
}

std::vector<std::string> words(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> out;
    for (std::string word; in >> word;) out.push_back(word);
    return out;
}

/// A setting's value as text, so that a symbol in a schema matches a string from a config file
std::string valueText(const Value& value) {
    if (value.isSymbol()) return std::string(StringInterner::global().lookup(value.asSymbol()));
    if (value.isString()) return std::string(value.asString());
    return value.toString();
}

/// Control characters have no place in a name or a chat line, and could fool a log or a terminal
std::string printable(std::string_view text, size_t limit) {
    std::string out;
    for (char c : text.substr(0, limit)) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) out.push_back(c);
    }
    return out;
}

}  // namespace

std::optional<ServerConfig> ServerConfig::load(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;

    ServerConfig config;
    for (std::string line; std::getline(in, line);) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (key == "title") {
            config.title = value;
        } else if (key == "port") {
            config.port = static_cast<uint16_t>(std::stoi(value));
        } else if (key == "password") {
            config.password = value;
        } else if (key == "max_players") {
            config.maxPlayers = std::stoi(value);
        } else if (key == "admins") {
            config.admins = words(value);
        } else if (key == "lock") {
            config.locked = words(value);
        } else if (key == "load_timeout") {
            config.loadTimeoutSeconds = std::stof(value);
        } else if (key == "saves") {
            config.saves = value == "true";
        } else if (key.rfind("setting.", 0) == 0) {
            config.settings[key.substr(8)] = value;
        } else {
            std::cerr << "[server] unknown key in " << path.string() << ": " << key << "\n";
        }
    }
    return config;
}

namespace {

std::atomic<bool> stopSignalled{false};
std::atomic<bool> stoppedCleanly{false};

void onStopSignal(int) {
    stopSignalled = true;
}

#ifdef _WIN32
/// Windows has no SIGTERM to send. A console's Ctrl+C or Ctrl+Break, its window closing, logging
/// off and shutting down all come here instead.
BOOL WINAPI onConsoleEvent(DWORD event) {
    stopSignalled = true;
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) return TRUE;
    // For the rest the process ends when this returns, and Windows allows about five seconds, so
    // wait here for the save
    for (int i = 0; i < 90 && !stoppedCleanly.load(); ++i) Sleep(50);
    return TRUE;
}
#endif

}  // namespace

int serve(RuntimeOptions options, const fs::path& configPath, std::optional<int> port) {
    HostConfig config;
    if (!configPath.empty()) {
        auto loaded = ServerConfig::load(configPath);
        if (!loaded) {
            std::cerr << "error: can't read " << configPath.string() << "\n";
            return 1;
        }
        config.server = std::move(*loaded);
    } else if (auto manifest = GameManifest::load(options.gameRoot)) {
        config.server.port = manifest->netPort;
    }
    if (port) config.server.port = static_cast<uint16_t>(*port);

    config.runtime = std::move(options);
    if (config.server.saves) {
        config.runtime.savePath = config.runtime.saveRoot() / "server";
    } else {
        config.runtime.noSaves = true;
    }
    config.listen = true;

    GameHost host(std::move(config));
    std::string error;
    if (!host.start(error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    // A clean stop saves the game, so a server that's killed politely loses nothing
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
#endif
    while (!stopSignalled.load() && host.phase() != GameHost::Phase::Ended) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    host.stop();
    int code = host.exitCode();
    stoppedCleanly = true;
    return code;
}

GameHost::GameHost(HostConfig config) : config_(std::move(config)) {}

GameHost::~GameHost() {
    stop();
}

std::string GameHost::failure() const {
    std::lock_guard lock(mutex_);
    return failure_;
}

void GameHost::noteError(std::string text) {
    std::cerr << "[host] " << text << "\n";
    std::lock_guard lock(mutex_);
    failure_ = std::move(text);
}

GameRuntime* GameHost::runtime() {
    std::lock_guard lock(runtimeMutex_);
    return runtime_.get();
}

std::string GameHost::title() const {
    if (!config_.server.title.empty()) return config_.server.title;
    auto it = settings_.find("title");
    if (it != settings_.end() && it->second.isString() && !it->second.asString().empty()) {
        return std::string(it->second.asString());
    }
    return manifest_.name;
}

int GameHost::maxPlayers() const {
    return config_.server.maxPlayers > 0 ? std::min(config_.server.maxPlayers, declaredMaxPlayers_)
                                         : declaredMaxPlayers_;
}

bool GameHost::isLateJoinOpen() const {
    return lateJoin_ == "open" || openedLive_.load();
}

int GameHost::connectedPlayers() const {
    int count = 0;
    for (const auto& [id, player] : players_) count += player.connected ? 1 : 0;
    return count;
}

GameHost::Player* GameHost::findPlayer(int64_t id) {
    auto it = players_.find(id);
    return it == players_.end() ? nullptr : &it->second;
}

GameHost::Peer* GameHost::findPeer(int64_t player) {
    for (auto& peer : peers_) {
        if (peer->player == player) return peer.get();
    }
    return nullptr;
}

std::string GameHost::uniqueName(std::string wanted) const {
    wanted = printable(trim(std::move(wanted)), MAX_NAME_BYTES);
    if (wanted.empty()) wanted = "Player";
    auto taken = [this](const std::string& name) {
        for (const auto& [id, player] : players_) {
            if (player.name == name) return true;
        }
        return false;
    };
    if (!taken(wanted)) return wanted;
    for (int n = 2;; ++n) {
        std::string candidate = wanted + " " + std::to_string(n);
        if (!taken(candidate)) return candidate;
    }
}

void GameHost::loadDeclaration() {
    if (manifest_.lobbyScript.empty()) return;
    fs::path path = config_.runtime.gameRoot / manifest_.lobbyScript;
    if (!fs::is_regular_file(path)) {
        throw std::runtime_error("lobby script not found: " + path.string());
    }

    static script::FineVoxInterner interner;
    finescript::ScriptEngine engine;
    engine.setInterner(&interner);
    finescript::ExecutionContext context(engine);
    auto* script = engine.loadScript(path);
    auto result = engine.execute(*script, context);
    if (!result.success) {
        throw std::runtime_error(path.string() + ":" + std::to_string(result.errorLine) + ": " + result.error);
    }
    if (!result.returnValue.isMap()) {
        throw std::runtime_error(path.string() + " must return a map");
    }

    // A copy with nothing of that engine in it, which is about to go away
    declaration_ = decodeBody(encodeBody(result.returnValue));
    startPolicy_ = textField(declaration_, "start");
    launchPolicy_ = textField(declaration_, "launch");
    lateJoin_ = textField(declaration_, "late_join");
    if (startPolicy_.empty()) startPolicy_ = "lobby";
    if (launchPolicy_.empty()) launchPolicy_ = "host";
    if (lateJoin_.empty()) lateJoin_ = startPolicy_ == "immediate" ? "open" : "never";
    minPlayers_ = static_cast<int>(intField(declaration_, "min_players", 1));
    declaredMaxPlayers_ = static_cast<int>(intField(declaration_, "max_players", 16));

    Value schema = field(declaration_, "settings");
    if (schema.isArray()) {
        for (const Value& entry : schema.asArray()) {
            settingSchema_.push_back(entry);
            settings_[textField(entry, "key")] = field(entry, "default");
        }
    }
}

bool GameHost::settingAllowed(const std::string& key, const Value& value, Value& normalised) const {
    const Value* schema = nullptr;
    for (const Value& entry : settingSchema_) {
        if (textField(entry, "key") == key) schema = &entry;
    }
    if (!schema) return false;

    std::string type = textField(*schema, "type");
    if (type == "choice") {
        Value choices = field(*schema, "choices");
        if (!choices.isArray()) return false;
        for (const Value& choice : choices.asArray()) {
            Value candidate = field(choice, "value");
            if (valueText(candidate) == valueText(value)) {
                normalised = candidate;
                return true;
            }
        }
        return false;
    }
    if (type == "int") {
        double number = 0.0;
        if (value.isNumeric()) {
            number = value.asNumber();
        } else if (value.isString()) {
            char* end = nullptr;
            std::string text(value.asString());
            number = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0') return false;
        } else {
            return false;
        }
        int64_t whole = static_cast<int64_t>(std::llround(number));
        if (!field(*schema, "min").isNil() && whole < intField(*schema, "min")) return false;
        if (!field(*schema, "max").isNil() && whole > intField(*schema, "max")) return false;
        normalised = Value::integer(whole);
        return true;
    }
    if (type == "bool") {
        std::string text = valueText(value);
        if (value.isBool()) {
            normalised = value;
        } else if (text == "true" || text == "false") {
            normalised = Value::boolean(text == "true");
        } else {
            return false;
        }
        return true;
    }
    if (type == "text") {
        if (!value.isString()) return false;
        normalised = Value::string(printable(value.asString(), MAX_SETTING_TEXT));
        return true;
    }
    return false;
}

void GameHost::applyPresets() {
    for (const auto& [key, text] : config_.server.settings) {
        Value normalised;
        if (!settingAllowed(key, Value::string(text), normalised)) {
            std::cerr << "[host] setting " << key << ": " << text << " isn't allowed, ignoring it\n";
            continue;
        }
        settings_[key] = normalised;
    }
}

bool GameHost::start(std::string& error) {
    auto manifest = GameManifest::load(config_.runtime.gameRoot);
    if (!manifest) {
        error = "no readable game.conf in " + config_.runtime.gameRoot.string();
        return false;
    }
    manifest_ = *manifest;

    try {
        loadDeclaration();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    applyPresets();

    if (config_.listen && !openToNetwork(config_.server.port, error)) return false;

    thread_ = std::thread([this] { loop(); });
    return true;
}

std::unique_ptr<Connection> GameHost::connectLocal() {
    auto [client, server] = Connection::createLocalPair(connectionConfig());
    server->setWakeSignal(&signal_);
    {
        std::lock_guard lock(mutex_);
        arrivals_.push_back(std::shared_ptr<Connection>(std::move(server)));
    }
    signal_.signal();
    return std::move(client);
}

bool GameHost::openToNetwork(uint16_t port, std::string& error) {
    finenet::ListenerConfig config;
    config.connection = connectionConfig();
    auto listener = finenet::Listener::listen(port, config, &error);
    if (!listener) return false;

    listener->setWakeSignal(&signal_);
    uint16_t bound = listener->port();
    {
        std::lock_guard lock(mutex_);
        listener_ = std::move(listener);
        beacon_ = std::make_unique<finenet::DiscoveryBeacon>(manifest_.contentNamespace, bound,
                                                             config_.discoveryPort);
        beaconDirty_ = true;
    }
    networkPort_ = bound;
    // Opening a game that's under way is an invitation: whoever comes, the game finds them a place
    if (phase_.load() != Phase::Lobby) openedLive_ = true;
    std::cout << "[host] open on port " << bound << std::endl;
    return true;
}

void GameHost::closeNetwork() {
    std::lock_guard lock(mutex_);
    listener_.reset();
    beacon_.reset();
    networkPort_ = 0;
}

void GameHost::loop() {
    // A game that has no lobby exists from the start, for whoever turns up
    if (startPolicy_ == "immediate") beginLoading();

    while (!stopping_.load()) {
        signal_.waitFor(LOOP_WAIT);

        acceptConnections();
        for (size_t i = 0; i < peers_.size(); ++i) {
            peers_[i]->connection->update();
            pumpContent(*peers_[i]);
        }
        dropClosedPeers();
        updatePhase();
        if (lobbyDirty_) sendLobby();
        updateBeacon();
    }
}

void GameHost::acceptConnections() {
    std::vector<std::shared_ptr<Connection>> arrived;
    {
        std::lock_guard lock(mutex_);
        arrived = std::exchange(arrivals_, {});
        if (listener_) {
            while (auto connection = listener_->accept()) {
                connection->setWakeSignal(&signal_);
                arrived.push_back(std::shared_ptr<Connection>(std::move(connection)));
            }
        }
    }
    for (auto& connection : arrived) addPeer(std::move(connection));
}

void GameHost::addPeer(std::shared_ptr<Connection> connection) {
    auto peer = std::make_unique<Peer>();
    peer->connection = std::move(connection);
    peer->secondStart = Clock::now();
    registerHandlers(*peer);
    peers_.push_back(std::move(peer));
}

void GameHost::registerHandlers(Peer& peer) {
    Connection& c = *peer.connection;
    Peer* p = &peer;  // Peers are owned by unique_ptr, and these handlers go with the connection
    c.onMessage(msg::Hello, [this, p](const Message& m) { onHello(*p, m); });
    c.onMessage(msg::SetSetting, [this, p](const Message& m) { onSetSetting(*p, m); });
    c.onMessage(msg::SetReady, [this, p](const Message& m) { onSetReady(*p, m); });
    c.onMessage(msg::Launch, [this, p](const Message& m) { onLaunch(*p, m); });
    c.onMessage(msg::Chat, [this, p](const Message& m) { onChat(*p, m); });
    c.onMessage(msg::Kick, [this, p](const Message& m) { onKick(*p, m); });
    c.onMessage(msg::Leave, [this, p](const Message&) {
        if (Player* player = findPlayer(p->player)) player->token.clear();
    });
    c.onMessage(msg::ContentRequest, [this, p](const Message& m) { onContentRequest(*p, m); });
    c.onMessage(msg::ContentReady, [this, p](const Message&) { onContentReady(*p); });
    c.onMessage(msg::Loaded, [this, p](const Message&) { onLoaded(*p); });
    c.onMessage(msg::Command, [this, p](const Message& m) { onCommand(*p, m); });
    c.onMessage(msg::PlayerState, [this, p](const Message& m) { onPlayerState(*p, m); });
    c.onMessage(msg::ExpectChange, [this, p](const Message& m) { onExpectChange(*p, m); });
    c.onMessage(msg::ViewHint, [this, p](const Message& m) { onViewHint(*p, m); });
    c.onMessage(msg::ClientState, [this, p](const Message& m) { onClientState(*p, m); });
}

void GameHost::reject(Peer& peer, std::string_view reason, std::string_view text) {
    Value body = Value::map();
    setField(body, "reason", symbol(reason));
    if (!text.empty()) setField(body, "text", Value::string(std::string(text)));
    peer.connection->send(CH_CONTROL, Message(msg::Reject, encodeBody(body)));
    peer.connection->disconnect(text.empty() ? reason : text);
}

void GameHost::onHello(Peer& peer, const Message& message) {
    if (peer.player != 0) return;
    Value body = decodeBody(message.payload());

    if (intField(body, "protocol") != PROTOCOL_VERSION) {
        reject(peer, "protocol", "this server speaks a different version");
        return;
    }
    if (textField(body, "game") != manifest_.contentNamespace) {
        reject(peer, "game", "this server is running " + manifest_.name);
        return;
    }
    bool local = peer.connection->isLocal();
    if (!local && !config_.server.password.empty() && textField(body, "password") != config_.server.password) {
        reject(peer, "password", "wrong password");
        return;
    }

    // Someone coming back gets their own seat, whatever the game's up to
    std::string token = textField(body, "token");
    if (!token.empty()) {
        for (auto& [id, player] : players_) {
            if (player.token != token || player.connected) continue;
            player.connected = true;
            peer.player = id;
            lobbyDirty_ = true;
            systemChat(player.name + " is back");
            sendWelcome(peer, player);
            if (phase_.load() != Phase::Lobby) joinRunningGame(peer);
            return;
        }
    }

    Phase phase = phase_.load();
    if (phase == Phase::Ended) {
        reject(peer, "started", "this game has ended");
        return;
    }
    if (phase != Phase::Lobby && !isLateJoinOpen() && !local) {
        reject(peer, "started", "this game has started");
        return;
    }
    if (connectedPlayers() >= maxPlayers()) {
        reject(peer, "full", "this game is full");
        return;
    }

    Player player;
    player.id = nextPlayerId_++;
    player.name = uniqueName(textField(body, "name"));
    player.token = freshToken();
    player.local = local;
    player.contentReady = local;
    bool firstHere = connectedPlayers() == 0;
    player.admin = local || (firstHere && config_.server.admins.empty()) ||
                   std::find(config_.server.admins.begin(), config_.server.admins.end(), player.name) !=
                       config_.server.admins.end();

    peer.player = player.id;
    auto [it, inserted] = players_.emplace(player.id, std::move(player));
    lobbyDirty_ = true;
    systemChat(it->second.name + " joined");

    sendWelcome(peer, it->second);
    if (!local) sendManifest(peer);
    if (phase != Phase::Lobby) joinRunningGame(peer);
}

void GameHost::sendWelcome(Peer& peer, const Player& player) {
    Phase phase = phase_.load();
    Value body = Value::map();
    setField(body, "player", Value::integer(player.id));
    setField(body, "token", Value::string(player.token));
    setField(body, "game", Value::string(manifest_.contentNamespace));
    setField(body, "title", Value::string(title()));
    setField(body, "phase", symbol(phase == Phase::Lobby     ? "lobby"
                                   : phase == Phase::Loading ? "loading"
                                   : phase == Phase::Running ? "running"
                                                             : "ended"));
    Value lobby = declaration_.isMap() ? declaration_ : Value::map();
    if (!declaration_.isMap()) {
        setField(lobby, "start", symbol(startPolicy_));
        setField(lobby, "launch", symbol(launchPolicy_));
        setField(lobby, "late_join", symbol(lateJoin_));
        setField(lobby, "min_players", Value::integer(minPlayers_));
        setField(lobby, "max_players", Value::integer(declaredMaxPlayers_));
        setField(lobby, "settings", Value::array(std::vector<Value>{}));
    }
    setField(body, "lobby", lobby);
    setField(body, "host", Value::boolean(player.admin));
    setField(body, "content", Value::boolean(!player.contentReady));
    peer.connection->send(CH_CONTROL, Message(msg::Welcome, encodeBody(body)));
}

void GameHost::sendManifest(Peer& peer) {
    if (!manifestFiles_) {
        manifestFiles_ = buildManifest(config_.runtime.gameRoot, manifest_.clientContent.empty()
                                                                     ? defaultClientContent()
                                                                     : manifest_.clientContent);
    }

    std::vector<Value> files;
    files.reserve(manifestFiles_->size());
    for (const ContentFile& file : *manifestFiles_) {
        Value entry = Value::map();
        setField(entry, "path", Value::string(file.path));
        setField(entry, "size", Value::integer(static_cast<int64_t>(file.size)));
        setField(entry, "sha256", Value::string(toHex(file.hash)));
        files.push_back(std::move(entry));
    }
    Value body = Value::map();
    setField(body, "files", Value::array(std::move(files)));
    peer.connection->send(CH_ASSETS, Message(msg::ContentManifest, encodeBody(body)));
}

void GameHost::onContentRequest(Peer& peer, const Message& message) {
    if (!manifestFiles_) return;
    Value body = decodeBody(message.payload());
    Value wanted = field(body, "files");
    if (!wanted.isArray()) return;

    // Only what's on the list: a client doesn't get to ask for any file it can name
    for (const Value& entry : wanted.asArray()) {
        if (!entry.isString()) continue;
        auto hash = fromHex(entry.asString());
        if (!hash) continue;
        auto file = std::find_if(manifestFiles_->begin(), manifestFiles_->end(),
                                 [&](const ContentFile& f) { return f.hash == *hash; });
        if (file != manifestFiles_->end()) peer.sending.push_back({*file, 0});
    }
}

void GameHost::pumpContent(Peer& peer) {
    while (!peer.sending.empty() && peer.connection->pendingBytes(CH_ASSETS) < ASSET_BACKLOG) {
        ContentSending& current = peer.sending.front();
        std::ifstream in(config_.runtime.gameRoot / current.file.path, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(current.offset));
        std::vector<uint8_t> chunk(std::min<uint64_t>(MAX_CONTENT_CHUNK, current.file.size - current.offset));
        in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));

        std::vector<uint8_t> body;
        ByteWriter w(body);
        w.bytes(current.file.hash);
        w.var(current.offset);
        w.var(current.file.size);
        w.bytes(chunk);
        peer.connection->send(CH_ASSETS, Message(msg::ContentChunk, std::move(body)));

        current.offset += chunk.size();
        if (current.offset >= current.file.size) peer.sending.pop_front();
    }
}

void GameHost::onContentReady(Peer& peer) {
    if (Player* player = findPlayer(peer.player)) {
        player->contentReady = true;
        lobbyDirty_ = true;
    }
}

void GameHost::onSetSetting(Peer& peer, const Message& message) {
    Player* player = findPlayer(peer.player);
    if (!player || !player->admin || phase_.load() != Phase::Lobby) return;

    Value body = decodeBody(message.payload());
    std::string key = textField(body, "key");
    const auto& locked = config_.server.locked;
    if (std::find(locked.begin(), locked.end(), key) != locked.end()) return;

    Value normalised;
    if (!settingAllowed(key, field(body, "value"), normalised)) return;
    settings_[key] = std::move(normalised);

    // New terms, so nobody's agreed to them yet
    for (auto& [id, other] : players_) {
        if (launchPolicy_ == "vote") other.voted = false;
        if (launchPolicy_ == "auto") other.ready = false;
    }
    lobbyDirty_ = true;
    {
        std::lock_guard lock(mutex_);
        beaconDirty_ = true;
    }
}

void GameHost::onSetReady(Peer& peer, const Message& message) {
    Player* player = findPlayer(peer.player);
    if (!player || phase_.load() != Phase::Lobby) return;
    player->ready = boolField(decodeBody(message.payload()), "ready");
    lobbyDirty_ = true;
}

void GameHost::onLaunch(Peer& peer, const Message& message) {
    Player* player = findPlayer(peer.player);
    if (!player || phase_.load() != Phase::Lobby) return;
    bool wants = boolField(decodeBody(message.payload()), "launch", true);

    if (launchPolicy_ == "vote") {
        player->voted = wants;
        lobbyDirty_ = true;
    } else if (launchPolicy_ == "host") {
        if (player->admin && wants && launchAllowed()) beginLoading();
    }
}

bool GameHost::launchAllowed() const {
    if (connectedPlayers() < minPlayers_) return false;
    for (const auto& [id, player] : players_) {
        if (player.connected && !player.contentReady) return false;
    }
    return true;
}

void GameHost::launchIfDue() {
    if (phase_.load() != Phase::Lobby) return;

    if (config_.autoLaunch) {
        if (connectedPlayers() > 0 && launchAllowed()) beginLoading();
        return;
    }
    if (launchPolicy_ == "host" || !launchAllowed()) return;

    for (const auto& [id, player] : players_) {
        if (!player.connected) continue;
        bool agreed = launchPolicy_ == "vote" ? player.voted : player.ready;
        if (!agreed) return;
    }
    beginLoading();
}

void GameHost::onChat(Peer& peer, const Message& message) {
    Player* player = findPlayer(peer.player);
    if (!player) return;
    if (++peer.chatsThisSecond > MAX_CHATS_PER_SECOND) return;

    Value body = decodeBody(message.payload());
    std::string text = printable(textField(body, "text"), MAX_CHAT_BYTES);
    if (text.empty()) return;

    uint16_t team = 0;
    if (textField(body, "to") == "team") {
        if (GameRuntime* game = runtime()) {
            if (auto seat = game->seat(player->id)) team = seat->faction;
        }
    }
    sendChatLine(player->id, player->name, text, team);
}

void GameHost::sendChatLine(int64_t from, std::string_view name, std::string_view text, uint16_t onlyFaction) {
    Value body = Value::map();
    setField(body, "from", Value::integer(from));
    setField(body, "name", Value::string(std::string(name)));
    setField(body, "text", Value::string(std::string(text)));
    if (onlyFaction != 0) setField(body, "to", symbol("team"));
    auto bytes = encodeBody(body);

    GameRuntime* game = runtime();
    for (auto& peer : peers_) {
        if (peer->player == 0) continue;
        if (onlyFaction != 0) {
            auto seat = game ? game->seat(peer->player) : std::nullopt;
            if (!seat || seat->faction != onlyFaction) continue;
        }
        peer->connection->send(CH_CHAT, Message(msg::Chat, bytes));
    }
}

void GameHost::systemChat(std::string_view text) {
    sendChatLine(0, "", text);
}

void GameHost::onKick(Peer& peer, const Message& message) {
    Player* player = findPlayer(peer.player);
    if (!player || !player->admin) return;
    int64_t target = intField(decodeBody(message.payload()), "player");
    Player* victim = findPlayer(target);
    if (!victim || victim->local) return;

    victim->token.clear();
    if (Peer* theirs = findPeer(target)) reject(*theirs, "banned", "you were removed from the game");
}

void GameHost::onCommand(Peer& peer, const Message& message) {
    GameRuntime* game = runtime();
    if (!game || peer.player == 0 || phase_.load() != Phase::Running) return;
    if (message.payloadSize() > MAX_COMMAND_BYTES) return;
    if (++peer.commandsThisSecond > MAX_COMMANDS_PER_SECOND) return;

    Value command = decodeBody(message.payload());
    if (command.isNil()) return;
    game->session().actions().sendPlayerCommand(peer.player, std::move(command));
}

void GameHost::onPlayerState(Peer& peer, const Message& message) {
    GameRuntime* game = runtime();
    if (!game || peer.player == 0) return;
    auto seat = game->seat(peer.player);
    if (!seat || seat->body == INVALID_ENTITY_ID) return;

    ByteReader r(message.payload());
    EntityState state;
    state.position.x = r.f32();
    state.position.y = r.f32();
    state.position.z = r.f32();
    state.velocity.x = r.f32();
    state.velocity.y = r.f32();
    state.velocity.z = r.f32();
    state.yaw = r.f32();
    state.pitch = r.f32();
    state.onGround = r.u8() != 0;
    if (!r.ok() || !std::isfinite(state.position.x) || !std::isfinite(state.position.y) ||
        !std::isfinite(state.position.z)) {
        return;
    }
    game->session().actions().sendPlayerState(seat->body, state);
}

void GameHost::onExpectChange(Peer& peer, const Message& message) {
    GameRuntime* game = runtime();
    if (!game || !replication_ || peer.player == 0) return;
    ByteReader r(message.payload());
    BlockCoord pos(static_cast<int32_t>(r.svar()), static_cast<int32_t>(r.svar()), static_cast<int32_t>(r.svar()));
    if (!r.ok()) return;
    auto seat = game->seat(peer.player);
    if (!seat || seat->faction == 0) return;
    uint16_t faction = seat->faction;
    replication_->post([game, faction, pos] { game->replicaFor(faction).expectChange(pos); });
}

void GameHost::onViewHint(Peer& peer, const Message& message) {
    ByteReader r(message.payload());
    float x = r.f32(), z = r.f32();
    if (r.ok() && peer.view && std::isfinite(x) && std::isfinite(z)) peer.view->setViewHint(x, z);
}

void GameHost::onClientState(Peer& peer, const Message& message) {
    GameRuntime* game = runtime();
    Player* player = findPlayer(peer.player);
    if (!game || !player || !player->local) return;
    game->setClientState(decodeBody(message.payload()));
}

Value GameHost::lobbyStateValue() const {
    GameRuntime* game = const_cast<GameHost*>(this)->runtime();
    std::vector<Value> players;
    int votes = 0;
    for (const auto& [id, player] : players_) {
        Value entry = Value::map();
        setField(entry, "id", Value::integer(player.id));
        setField(entry, "name", Value::string(player.name));
        setField(entry, "connected", Value::boolean(player.connected));
        setField(entry, "ready", Value::boolean(player.ready));
        setField(entry, "host", Value::boolean(player.admin));
        setField(entry, "local", Value::boolean(player.local));
        setField(entry, "content", Value::boolean(player.contentReady));
        setField(entry, "voted", Value::boolean(player.voted));
        if (game) {
            if (auto seat = game->seat(id); seat && seat->faction != 0) {
                setField(entry, "faction", Value::integer(seat->faction));
            }
        }
        if (auto* peer = const_cast<GameHost*>(this)->findPeer(id); peer && !peer->connection->isLocal()) {
            setField(entry, "ping", Value::integer(static_cast<int64_t>(peer->connection->stats().rttMs)));
        }
        votes += player.voted ? 1 : 0;
        players.push_back(std::move(entry));
    }

    Value settings = Value::map();
    for (const auto& [key, value] : settings_) setField(settings, key, value);

    std::vector<Value> locked;
    for (const std::string& key : config_.server.locked) locked.push_back(Value::string(key));

    Phase phase = phase_.load();
    Value body = Value::map();
    setField(body, "phase", symbol(phase == Phase::Lobby     ? "lobby"
                                   : phase == Phase::Loading ? "loading"
                                   : phase == Phase::Running ? "running"
                                                             : "ended"));
    setField(body, "players", Value::array(std::move(players)));
    setField(body, "settings", std::move(settings));
    setField(body, "locked", Value::array(std::move(locked)));
    setField(body, "votes", Value::integer(votes));
    setField(body, "can_launch", Value::boolean(phase == Phase::Lobby && launchAllowed()));
    setField(body, "title", Value::string(title()));
    setField(body, "open", Value::boolean(networkPort_.load() != 0));
    setField(body, "port", Value::integer(networkPort_.load()));
    return body;
}

void GameHost::sendLobby() {
    lobbyDirty_ = false;
    auto bytes = encodeBody(lobbyStateValue());
    for (auto& peer : peers_) {
        if (peer->player != 0) peer->connection->send(CH_UI, Message(msg::LobbyState, bytes));
    }
    std::lock_guard lock(mutex_);
    beaconDirty_ = true;
}

void GameHost::updateBeacon() {
    bool dirty;
    {
        std::lock_guard lock(mutex_);
        dirty = beaconDirty_ && beacon_;
        beaconDirty_ = false;
    }
    if (!dirty) return;

    Phase phase = phase_.load();
    Value listing = Value::map();
    setField(listing, "title", Value::string(title()));
    setField(listing, "game", Value::string(manifest_.contentNamespace));
    setField(listing, "game_name", Value::string(manifest_.name));
    setField(listing, "players", Value::integer(connectedPlayers()));
    setField(listing, "max_players", Value::integer(maxPlayers()));
    setField(listing, "phase", symbol(phase == Phase::Lobby     ? "lobby"
                                      : phase == Phase::Loading ? "loading"
                                      : phase == Phase::Running ? "running"
                                                                : "ended"));
    setField(listing, "joinable", Value::boolean(phase == Phase::Lobby || (phase == Phase::Running && isLateJoinOpen())));
    setField(listing, "password", Value::boolean(!config_.server.password.empty()));

    Value summary = Value::map();
    for (const auto& [key, value] : settings_) {
        if (key != "title") setField(summary, key, value);
    }
    setField(listing, "settings", std::move(summary));

    std::lock_guard lock(mutex_);
    if (beacon_) beacon_->setListing(encodeBody(listing));
}

void GameHost::dropClosedPeers() {
    for (size_t i = 0; i < peers_.size();) {
        Peer& peer = *peers_[i];
        if (peer.connection->state() != ConnectionState::Disconnected) {
            auto now = Clock::now();
            if (now - peer.secondStart >= std::chrono::seconds(1)) {
                peer.secondStart = now;
                peer.commandsThisSecond = peer.chatsThisSecond = 0;
            }
            ++i;
            continue;
        }

        peer.connection->update();  // Anything it said on the way out
        if (Player* player = findPlayer(peer.player)) {
            player->connected = false;
            std::string name = player->name;
            if (peer.view && replication_) replication_->removeView(peer.player);

            Phase phase = phase_.load();
            if (phase == Phase::Lobby || player->token.empty()) {
                bool wasAdmin = player->admin;
                if (phase != Phase::Lobby && runtime()) runtime()->postPlayerLeft(peer.player);
                players_.erase(peer.player);
                if (wasAdmin && config_.server.admins.empty()) {
                    // Someone has to be able to start it
                    for (auto& [id, other] : players_) {
                        if (other.connected) {
                            other.admin = true;
                            break;
                        }
                    }
                }
            } else if (phase == Phase::Running && runtime()) {
                runtime()->postPlayerLeft(peer.player);
            }
            systemChat(name + " left");
            lobbyDirty_ = true;
        }
        peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void GameHost::updatePhase() {
    switch (phase_.load()) {
        case Phase::Lobby:
            launchIfDue();
            break;
        case Phase::Loading:
            if (loadFailed_.load()) {
                endGame(failure());
            } else if (worldLoaded_.load() && !replication_) {
                onGameLoaded();
            } else if (replication_) {
                replication_->pump();
                startGameIfReady();
            }
            break;
        case Phase::Running:
            if (GameRuntime* game = runtime(); game && game->exitRequested()) endGame("");
            break;
        case Phase::Ended:
            break;
    }
}

void GameHost::beginLoading() {
    phase_ = Phase::Loading;
    loadingStarted_ = Clock::now();
    lobbyDirty_ = true;

    RuntimeOptions options = config_.runtime;
    for (const auto& [id, player] : players_) {
        if (player.connected) options.players.push_back({id, player.name, player.local});
    }

    Value settings = Value::map();
    for (const auto& [key, value] : settings_) setField(settings, key, value);
    options.settings = encodeBody(settings);

    // A setting can reach into game.conf, for what has to be known before the world exists
    for (const Value& schema : settingSchema_) {
        std::string key = textField(schema, "key");
        const Value& chosen = settings_[key];
        std::string target = textField(schema, "manifest");
        if (target == "world_seed" && chosen.isNumeric() && chosen.asNumber() == 0.0) {
            // A seed of 0 means surprise me
            options.manifestOverrides[target] = std::to_string(std::random_device{}());
        } else if (!target.empty()) {
            options.manifestOverrides[target] = valueText(chosen);
        }

        Value choices = field(schema, "choices");
        if (!choices.isArray()) continue;
        for (const Value& choice : choices.asArray()) {
            if (valueText(field(choice, "value")) != valueText(chosen)) continue;
            Value overrides = field(choice, "manifest");
            if (!overrides.isMap()) continue;
            auto& map = overrides.asMap();
            for (uint32_t name : map.keys()) {
                options.manifestOverrides[std::string(StringInterner::global().lookup(name))] =
                    valueText(map.get(name));
            }
        }
    }

    loader_ = std::thread([this, options = std::move(options)]() mutable {
        try {
            auto game = std::make_unique<GameRuntime>(std::move(options));
            game->load();
            game->settleLight();
            {
                std::lock_guard lock(runtimeMutex_);
                runtime_ = std::move(game);
            }
            worldLoaded_ = true;
        } catch (const std::exception& e) {
            noteError(e.what());
            loadFailed_ = true;
        }
        signal_.signal();
    });
}

Value GameHost::worldInfoValue(const Peer& peer) const {
    GameRuntime* game = const_cast<GameHost*>(this)->runtime();
    const GameManifest& manifest = game->manifest();
    const FactionVision& vision = game->vision();
    const VisionBounds& bounds = vision.bounds();
    bool local = peer.connection->isLocal();

    Value world = Value::map();
    setField(world, "radius", Value::integer(manifest.worldRadius));
    setField(world, "sea_level", Value::integer(manifest.seaLevel));
    setField(world, "tick_rate", Value::integer(manifest.tickRate));
    setField(world, "entity_rate", Value::integer(local ? manifest.tickRate : manifest.tickRate / 2));
    setField(world, "fog", Value::boolean(manifest.fogOfWar));
    setField(world, "view_distance", Value::number(manifest.viewDistance));
    setField(world, "lod", Value::boolean(manifest.lod));
    setField(world, "step_height", Value::number(manifest.stepHeight));
    setField(world, "player_entity", Value::string(manifest.playerEntity));
    setField(world, "vision_cell", Value::integer(FactionVision::CELL_SIZE));
    setField(world, "vision_min",
             Value::array(std::vector<Value>{Value::integer(bounds.minX), Value::integer(bounds.minZ)}));
    setField(world, "vision_cells",
             Value::array(std::vector<Value>{Value::integer(vision.cellsX()), Value::integer(vision.cellsZ())}));
    setField(world, "resumed", Value::boolean(game->resumed()));
    if (local && game->resumed()) setField(world, "client_state", game->clientState());

    Value body = Value::map();
    setField(body, "world", std::move(world));
    return body;
}

void GameHost::sendGameStarting(Peer& peer) {
    peer.connection->send(CH_CONTROL, Message(msg::GameStarting, encodeBody(worldInfoValue(peer))));

    peer.view = std::make_shared<ClientView>(peer.player, peer.connection);
    if (replication_) replication_->addView(peer.view);
    peer.loaded = false;
    peer.gameStartSent = false;
}

void GameHost::joinRunningGame(Peer& peer) {
    // In the middle of loading, the rest happens when it's done
    if (!worldLoaded_.load() || !replication_) return;

    GameRuntime* game = runtime();
    Player* player = findPlayer(peer.player);
    if (!game || !player) return;

    if (!game->seat(peer.player)) {
        std::string name = player->name;
        bool local = player->local;
        int64_t id = peer.player;
        replication_->post([game, id, name, local] { game->playerJoined(id, name, local); });
    }
    sendGameStarting(peer);
}

void GameHost::onGameLoaded() {
    GameRuntime* game = runtime();
    replication_ = game->replication();

    // Whoever came while it was loading isn't in the game's list of players yet
    for (auto& peer : peers_) {
        if (peer->player != 0) joinRunningGame(*peer);
    }
    lobbyDirty_ = true;

    // Nobody to wait for, on a server with no one on it yet
    if (startPolicy_ == "immediate") startGame();
}

void GameHost::onLoaded(Peer& peer) {
    peer.loaded = true;
    if (gameStarted_ && !peer.gameStartSent) {
        peer.gameStartSent = true;
        Value body = Value::map();
        setField(body, "tick", Value::integer(static_cast<int64_t>(runtime()->gameScript().tickCount())));
        peer.connection->send(CH_CONTROL, Message(msg::GameStart, encodeBody(body)));
    }
}

void GameHost::startGameIfReady() {
    bool everyone = true;
    int waiting = 0;
    for (auto& peer : peers_) {
        if (peer->player != 0 && peer->view && !peer->loaded) {
            everyone = false;
            ++waiting;
        }
    }

    auto limit = std::chrono::duration<float>(config_.server.loadTimeoutSeconds);
    if (!everyone && Clock::now() - loadingStarted_ > limit) {
        for (auto& peer : peers_) {
            if (peer->player != 0 && peer->view && !peer->loaded) {
                reject(*peer, "other", "took too long to load");
            }
        }
        std::cerr << "[host] dropped " << waiting << " player(s) who hadn't loaded\n";
        everyone = true;
    }
    if (everyone) startGame();
}

void GameHost::startGame() {
    gameStarted_ = true;
    replication_->pump();
    runtime()->startThreads();
    phase_ = Phase::Running;
    lobbyDirty_ = true;

    Value body = Value::map();
    setField(body, "tick", Value::integer(static_cast<int64_t>(runtime()->gameScript().tickCount())));
    auto bytes = encodeBody(body);
    for (auto& peer : peers_) {
        if (peer->player != 0 && peer->view && peer->connection->isConnected()) {
            peer->gameStartSent = true;
            peer->connection->send(CH_CONTROL, Message(msg::GameStart, bytes));
        }
    }
}

void GameHost::endGame(std::string_view text) {
    phase_ = Phase::Ended;
    Value body = Value::map();
    if (!text.empty()) setField(body, "text", Value::string(std::string(text)));
    auto bytes = encodeBody(body);
    for (auto& peer : peers_) {
        if (peer->player != 0) peer->connection->send(CH_CONTROL, Message(msg::GameEnded, bytes));
    }
    lobbyDirty_ = true;
}

void GameHost::stop() {
    if (stopping_.exchange(true)) return;

    if (thread_.joinable()) thread_.join();
    if (loader_.joinable()) loader_.join();

    closeNetwork();

    // A client's last words are its camera, for the save
    for (auto& peer : peers_) peer->connection->update();

    GameRuntime* game = runtime();
    if (game) game->stopThreads();

    if (phase_.load() != Phase::Ended) endGame("");
    for (auto& peer : peers_) peer->connection->disconnect("the host left");
    peers_.clear();

    if (game) {
        std::string error;
        if (!game->saveGame(error)) std::cerr << "[save] couldn't save: " << error << "\n";
    }
}

int GameHost::exitCode() {
    GameRuntime* game = runtime();
    return game ? game->exitCode() : (failure().empty() ? 0 : 1);
}

}  // namespace finevox::game
