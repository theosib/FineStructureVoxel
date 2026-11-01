#include "finevox/game/mcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <streambuf>

using json = nlohmann::json;

namespace finevox::game {

namespace {

constexpr std::string_view PROTOCOL_VERSIONS[] = {"2025-06-18", "2025-03-26", "2024-11-05"};
constexpr size_t MAX_REQUEST_BYTES = 1 << 20;
constexpr auto SAFE_POINT_TIMEOUT = std::chrono::seconds(10);
constexpr int RECEIVE_TIMEOUT_SECONDS = 5;
constexpr size_t LOG_LINES = 5000;

// JSON-RPC
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;

int64_t nowNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string dump(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

json rpcResult(const json& id, json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

json rpcError(const json& id, int code, std::string message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

bool hasType(const json& value, const std::string& type) {
    if (type == "integer") return value.is_number_integer();
    if (type == "number") return value.is_number();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "array") return value.is_array();
    if (type == "object") return value.is_object();
    return true;
}

/// What's wrong with these arguments, or empty. Strict, so that a misspelt option fails rather
/// than being ignored and answering a different question.
std::string validate(const json& schema, const json& args) {
    if (!args.is_object()) return "arguments must be an object";
    const json properties = schema.value("properties", json::object());
    for (const auto& name : schema.value("required", json::array())) {
        if (!args.contains(name.get<std::string>())) return "missing argument: " + name.get<std::string>();
    }
    for (const auto& [name, value] : args.items()) {
        if (!properties.contains(name)) return "unknown argument: " + name;
        std::string type = properties[name].value("type", "");
        if (!hasType(value, type)) return "argument " + name + " must be of type " + type;
    }
    return "";
}

std::string base64(const std::vector<uint8_t>& data) {
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t chunk = uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) chunk |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < data.size()) chunk |= uint32_t(data[i + 2]);
        out += ALPHABET[(chunk >> 18) & 63];
        out += ALPHABET[(chunk >> 12) & 63];
        out += i + 1 < data.size() ? ALPHABET[(chunk >> 6) & 63] : '=';
        out += i + 2 < data.size() ? ALPHABET[chunk & 63] : '=';
    }
    return out;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS: see SO_NOSIGPIPE in serve()
#endif

/// A socket write that goes all the way. A caller that's gone must not take the game with it,
/// which is what the default SIGPIPE would do.
bool sendAll(int socket, std::string_view data) {
    while (!data.empty()) {
        ssize_t sent = ::send(socket, data.data(), data.size(), MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data.remove_prefix(static_cast<size_t>(sent));
    }
    return true;
}

void respond(int socket, std::string_view status, std::string_view body) {
    std::string head = "HTTP/1.1 " + std::string(status) + "\r\nContent-Type: application/json\r\n" +
                       "Content-Length: " + std::to_string(body.size()) +
                       "\r\nConnection: close\r\n\r\n";
    if (sendAll(socket, head)) sendAll(socket, body);
}

std::string headerValue(const std::string& head, std::string name) {
    std::string lower = head;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    name = "\r\n" + name + ":";
    size_t at = lower.find(name);
    if (at == std::string::npos) return "";
    size_t start = at + name.size();
    size_t end = head.find("\r\n", start);
    std::string value = head.substr(start, end - start);
    size_t first = value.find_first_not_of(" \t");
    return first == std::string::npos ? "" : value.substr(first);
}

/// A web page can reach 127.0.0.1 too, so one must not get to run scripts here
bool originAllowed(const std::string& origin) {
    if (origin.empty()) return true;
    for (std::string_view local : {"http://127.0.0.1", "http://localhost", "https://127.0.0.1", "https://localhost"}) {
        if (origin.rfind(local, 0) != 0) continue;
        if (origin.size() == local.size() || origin[local.size()] == ':') return true;
    }
    return false;
}

}  // namespace

// ============================================================================
// McpResult
// ============================================================================

McpResult McpResult::text(std::string text) {
    McpResult result;
    result.content.push_back({{"type", "text"}, {"text", std::move(text)}});
    return result;
}

McpResult McpResult::value(const json& value) {
    return text(dump(value));
}

McpResult McpResult::error(std::string message) {
    McpResult result = text(std::move(message));
    result.isError = true;
    return result;
}

McpResult McpResult::image(const std::vector<uint8_t>& data, std::string_view mimeType) {
    McpResult result;
    result.content.push_back({{"type", "image"}, {"data", base64(data)}, {"mimeType", mimeType}});
    return result;
}

// ============================================================================
// McpOutput: what the game prints, kept as well as printed
// ============================================================================

class McpOutput {
public:
    McpOutput() : coutTee_(*this, std::cout.rdbuf()), cerrTee_(*this, std::cerr.rdbuf()) {
        std::cout.rdbuf(&coutTee_);
        std::cerr.rdbuf(&cerrTee_);
    }

    ~McpOutput() {
        std::cout.rdbuf(coutTee_.target);
        std::cerr.rdbuf(cerrTee_.target);
    }

    [[nodiscard]] json read(int64_t since, size_t tail, const std::string& contains) {
        std::lock_guard lock(mutex_);
        std::vector<std::string> lines;
        int64_t first = next_ - static_cast<int64_t>(lines_.size());
        for (size_t i = lines_.size(); i-- > 0 && lines.size() < tail;) {
            if (first + static_cast<int64_t>(i) < since) break;
            if (contains.empty() || lines_[i].find(contains) != std::string::npos) {
                lines.push_back(lines_[i]);
            }
        }
        std::reverse(lines.begin(), lines.end());
        return {{"lines", std::move(lines)}, {"next", next_}};
    }

private:
    class Tee : public std::streambuf {
    public:
        Tee(McpOutput& owner, std::streambuf* target_) : target(target_), owner_(owner) {}
        std::streambuf* target;

    protected:
        std::streamsize xsputn(const char* data, std::streamsize size) override {
            owner_.append(partial_, data, size);
            return target->sputn(data, size);
        }
        int_type overflow(int_type c) override {
            if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
            char ch = traits_type::to_char_type(c);
            owner_.append(partial_, &ch, 1);
            return target->sputc(ch);
        }
        int sync() override { return target->pubsync(); }

    private:
        McpOutput& owner_;
        std::string partial_;
    };

    void append(std::string& partial, const char* data, std::streamsize size) {
        std::lock_guard lock(mutex_);
        for (std::streamsize i = 0; i < size; ++i) {
            if (data[i] != '\n') {
                partial += data[i];
                continue;
            }
            lines_.push_back(std::move(partial));
            partial.clear();
            ++next_;
            if (lines_.size() > LOG_LINES) lines_.pop_front();
        }
    }

    std::mutex mutex_;
    std::deque<std::string> lines_;
    int64_t next_ = 0;
    Tee coutTee_;
    Tee cerrTee_;
};

// ============================================================================
// McpServer
// ============================================================================

struct McpServer::Job {
    enum State { Queued, Running, Abandoned };

    const McpTool* tool = nullptr;
    json args;
    std::atomic<int> state{Queued};
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    McpResult result;
};

McpServer::McpServer(int port, bool playerOnly) : port_(port), playerOnly_(playerOnly) {
    addBuiltinTools();
}

McpServer::~McpServer() {
    stop();
}

void McpServer::addTool(McpTool tool) {
    std::unique_lock lock(toolMutex_);
    toolIndex_[tool.name] = tools_.size();
    tools_.push_back(std::move(tool));
}

void McpServer::captureOutput() {
    if (!output_) output_ = std::make_unique<McpOutput>();
}

void McpServer::addBuiltinTools() {
    McpTool status;
    status.name = "status";
    status.description =
        "Is the game alive? Tick and frame counts, rates, and how long ago each last happened, "
        "so a hang shows up as a growing age. Answers even if the game has hung. Tick times "
        "cover the last second.";
    status.inputSchema = {{"type", "object"}, {"properties", json::object()}};
    status.player = true;
    status.run = [this](const json&) { return McpResult::value(statusReport()); };
    addTool(std::move(status));

    McpTool logs;
    logs.name = "logs";
    logs.description =
        "What the game has printed to stdout and stderr, including script print and log calls "
        "and script errors. The last 5000 lines are kept. Pass `next` from one call as `since` "
        "in the next to read only what's new.";
    logs.inputSchema = {{"type", "object"},
                        {"properties",
                         {{"tail", {{"type", "integer"}, {"description", "At most this many lines, the newest (default 100)"}}},
                          {"since", {{"type", "integer"}, {"description", "Only lines from this position on"}}},
                          {"contains", {{"type", "string"}, {"description", "Only lines containing this text"}}}}}};
    logs.run = [this](const json& args) {
        if (!output_) return McpResult::error("output isn't being captured");
        return McpResult::value(output_->read(args.value("since", int64_t(0)),
                                              static_cast<size_t>(std::max(0, args.value("tail", 100))),
                                              args.value("contains", std::string())));
    };
    addTool(std::move(logs));

    McpTool wait;
    wait.name = "wait";
    wait.description =
        "Let the game run: returns after this many more ticks or frames, or after `seconds` "
        "if they don't come. Use it between acting and looking at the result: other tools "
        "only see the game as it is now.";
    wait.inputSchema = {{"type", "object"},
                        {"properties",
                         {{"ticks", {{"type", "integer"}, {"description", "Game ticks to wait for"}}},
                          {"frames", {{"type", "integer"}, {"description", "Rendered frames to wait for"}}},
                          {"seconds", {{"type", "number"}, {"description", "Give up after this long (default and limit 30)"}}}}}};
    wait.player = true;
    wait.run = [this](const json& args) {
        uint64_t ticks = ticks_.count.load() + std::max(0, args.value("ticks", 0));
        uint64_t frames = frames_.count.load() + std::max(0, args.value("frames", 0));
        double seconds = std::clamp(args.value("seconds", 30.0), 0.0, 30.0);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (ticks_.count.load() < ticks || frames_.count.load() < frames) {
            if (stopping_.load()) return McpResult::error("the game is shutting down");
            if (std::chrono::steady_clock::now() >= deadline) {
                return McpResult::error("timed out: " + dump(statusReport()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return McpResult::value(statusReport());
    };
    addTool(std::move(wait));
}

json McpServer::statusReport() const {
    int64_t now = nowNanos();
    return {{"ticks", ticks_.report(now)},
            {"frames", frames_.report(now)},
            {"tick_ms_average", tickMsAverage_.load()},
            {"tick_ms_worst", tickMsWorst_.load()}};
}

void McpServer::Rate::note(int64_t nanos) {
    count.fetch_add(1, std::memory_order_relaxed);
    lastNanos.store(nanos, std::memory_order_relaxed);
    ++windowCount;
    if (windowStart == 0) windowStart = nanos;
    int64_t elapsed = nanos - windowStart;
    if (elapsed >= 1'000'000'000) {
        perSecond.store(static_cast<float>(windowCount * 1e9 / elapsed), std::memory_order_relaxed);
        windowCount = 0;
        windowStart = nanos;
    }
}

json McpServer::Rate::report(int64_t nanos) const {
    int64_t last = lastNanos.load();
    json report = {{"count", count.load()}, {"per_second", perSecond.load()}};
    report["last_ms_ago"] = last == 0 ? json(nullptr) : json((nanos - last) / 1'000'000);
    return report;
}

void McpServer::noteTickStart() {
    tickStartNanos_.store(nowNanos(), std::memory_order_relaxed);
}

void McpServer::noteTickEnd() {
    int64_t now = nowNanos();
    int64_t start = tickStartNanos_.load(std::memory_order_relaxed);
    if (start != 0) {
        float ms = static_cast<float>(now - start) / 1e6f;
        tickMsSum_ += ms;
        tickMsPeak_ = std::max(tickMsPeak_, ms);
        ++tickMsCount_;
    }
    uint64_t before = ticks_.windowCount;
    ticks_.note(now);
    if (ticks_.windowCount < before && tickMsCount_ > 0) {
        tickMsAverage_.store(static_cast<float>(tickMsSum_ / tickMsCount_), std::memory_order_relaxed);
        tickMsWorst_.store(tickMsPeak_, std::memory_order_relaxed);
        tickMsSum_ = 0.0;
        tickMsPeak_ = 0.0f;
        tickMsCount_ = 0;
    }
}

void McpServer::noteFrame() {
    frames_.note(nowNanos());
}

McpServer::Mailbox& McpServer::mailbox(McpSide side) {
    switch (side) {
        case McpSide::Frame: return frameBox_;
        case McpSide::AfterFrame: return afterFrameBox_;
        default: return gameBox_;
    }
}

void McpServer::pump(McpSide side) {
    Mailbox& box = mailbox(side);
    if (!box.waiting.load(std::memory_order_acquire)) return;

    std::deque<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard lock(box.mutex);
        jobs.swap(box.jobs);
        box.waiting.store(false, std::memory_order_release);
    }
    for (auto& job : jobs) {
        int expected = Job::Queued;
        if (!job->state.compare_exchange_strong(expected, Job::Running)) continue;
        McpResult result;
        try {
            result = job->tool->run(job->args);
        } catch (const std::exception& e) {
            result = McpResult::error(e.what());
        }
        {
            std::lock_guard lock(job->mutex);
            job->result = std::move(result);
            job->done = true;
        }
        job->finished.notify_all();
    }
}

json McpServer::callTool(const McpTool& tool, const json& args) {
    McpResult result;
    if (tool.side == McpSide::Any) {
        try {
            result = tool.run(args);
        } catch (const std::exception& e) {
            result = McpResult::error(e.what());
        }
    } else {
        auto job = std::make_shared<Job>();
        job->tool = &tool;
        job->args = args;
        Mailbox& box = mailbox(tool.side);
        {
            std::lock_guard lock(box.mutex);
            box.jobs.push_back(job);
            box.waiting.store(true, std::memory_order_release);
        }

        // Once a tool is running it finishes, since it can't be stopped from here. Until then a
        // thread that never comes round must not hold the caller forever.
        auto deadline = std::chrono::steady_clock::now() + SAFE_POINT_TIMEOUT;
        std::unique_lock lock(job->mutex);
        while (!job->done) {
            job->finished.wait_for(lock, std::chrono::milliseconds(50));
            if (job->done || job->state.load() == Job::Running) continue;
            bool late = std::chrono::steady_clock::now() >= deadline;
            if (!late && !stopping_.load()) continue;
            int expected = Job::Queued;
            if (job->state.compare_exchange_strong(expected, Job::Abandoned)) {
                job->result = McpResult::error(
                    stopping_.load() ? "the game is shutting down"
                                     : "the " + tool.name + " tool's thread never reached a safe point: " +
                                           dump(statusReport()));
                job->done = true;
            }
        }
        result = std::move(job->result);
    }
    return {{"content", std::move(result.content)}, {"isError", result.isError}};
}

const McpTool* McpServer::findTool(const std::string& name) const {
    std::shared_lock lock(toolMutex_);
    auto found = toolIndex_.find(name);
    if (found == toolIndex_.end()) return nullptr;
    const McpTool& tool = tools_[found->second];
    return playerOnly_ && !tool.player ? nullptr : &tool;
}

json McpServer::listTools() const {
    json tools = json::array();
    std::shared_lock lock(toolMutex_);
    for (const McpTool& tool : tools_) {
        if (playerOnly_ && !tool.player) continue;
        tools.push_back({{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
    }
    return {{"tools", std::move(tools)}};
}

json McpServer::initializeResult(const json& params) const {
    std::string version(PROTOCOL_VERSIONS[0]);
    std::string asked = params.value("protocolVersion", std::string());
    for (std::string_view known : PROTOCOL_VERSIONS) {
        if (known == asked) version = asked;
    }
    return {{"protocolVersion", version},
            {"capabilities", {{"tools", json::object()}}},
            {"serverInfo", {{"name", "finevox"}, {"version", "1"}}},
            {"instructions",
             "A live game. Start with `status`. server_* tools see the whole simulation; client_* "
             "tools see what the player sees, fog of war included. *_eval runs finescript."}};
}

std::string McpServer::handleMessage(std::string_view body) {
    json message = json::parse(body, nullptr, false);
    if (message.is_discarded()) return dump(rpcError(nullptr, PARSE_ERROR, "not JSON"));
    if (!message.is_object() || !message.contains("method") || !message["method"].is_string()) {
        return dump(rpcError(nullptr, INVALID_REQUEST, "expected a JSON-RPC request"));
    }

    std::string method = message["method"];
    if (!message.contains("id")) return "";
    const json& id = message["id"];
    json params = message.value("params", json::object());

    if (method == "initialize") return dump(rpcResult(id, initializeResult(params)));
    if (method == "ping") return dump(rpcResult(id, json::object()));
    if (method == "tools/list") return dump(rpcResult(id, listTools()));
    if (method != "tools/call") return dump(rpcError(id, METHOD_NOT_FOUND, "no such method: " + method));

    std::string name = params.value("name", std::string());
    const McpTool* tool = findTool(name);
    if (!tool) return dump(rpcError(id, INVALID_PARAMS, "no such tool: " + name));
    json args = params.value("arguments", json::object());
    std::string problem = validate(tool->inputSchema, args);
    if (!problem.empty()) return dump(rpcError(id, INVALID_PARAMS, problem));
    return dump(rpcResult(id, callTool(*tool, args)));
}

bool McpServer::start(std::string& error) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = "can't open a socket";
        return false;
    }
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, 16) != 0) {
        error = "can't listen on 127.0.0.1:" + std::to_string(port_) + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    socklen_t length = sizeof(address);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    port_ = ntohs(address.sin_port);

    listenSocket_ = fd;
    stopping_.store(false);
    listener_ = std::thread([this] { listen(); });
    return true;
}

void McpServer::stop() {
    if (listenSocket_ < 0) return;
    stopping_.store(true);
    listener_.join();
    ::close(listenSocket_);
    listenSocket_ = -1;
    while (connections_.load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void McpServer::listen() {
    while (!stopping_.load()) {
        pollfd waiting{listenSocket_, POLLIN, 0};
        if (::poll(&waiting, 1, 100) <= 0) continue;
        int socket = ::accept(listenSocket_, nullptr, nullptr);
        if (socket < 0) continue;

        // A request of its own for each caller: a tool waiting on the game mustn't hold up
        // `status`, which is how the caller finds out why
        connections_.fetch_add(1);
        std::thread([this, socket] {
            serve(socket);
            ::close(socket);
            connections_.fetch_sub(1);
        }).detach();
    }
}

void McpServer::serve(int socket) {
    timeval timeout{RECEIVE_TIMEOUT_SECONDS, 0};
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    std::string request;
    char buffer[8192];
    size_t bodyStart = std::string::npos;
    while (bodyStart == std::string::npos) {
        ssize_t got = ::recv(socket, buffer, sizeof(buffer), 0);
        if (got <= 0 || request.size() > MAX_REQUEST_BYTES) return;
        request.append(buffer, static_cast<size_t>(got));
        bodyStart = request.find("\r\n\r\n");
    }
    std::string head = request.substr(0, bodyStart);
    bodyStart += 4;

    if (!originAllowed(headerValue(head, "origin"))) {
        respond(socket, "403 Forbidden", R"({"error":"origin not allowed"})");
        return;
    }
    if (head.rfind("POST /mcp ", 0) != 0) {
        // No server-sent events, which a client takes as this being request and response alone
        respond(socket, "405 Method Not Allowed", R"({"error":"POST to /mcp"})");
        return;
    }

    std::string lengthHeader = headerValue(head, "content-length");
    size_t length = lengthHeader.empty() ? 0 : std::strtoul(lengthHeader.c_str(), nullptr, 10);
    if (length > MAX_REQUEST_BYTES) {
        respond(socket, "413 Content Too Large", R"({"error":"too long"})");
        return;
    }
    while (request.size() < bodyStart + length) {
        ssize_t got = ::recv(socket, buffer, sizeof(buffer), 0);
        if (got <= 0) return;
        request.append(buffer, static_cast<size_t>(got));
    }

    std::string reply = handleMessage(std::string_view(request).substr(bodyStart, length));
    if (reply.empty()) {
        respond(socket, "202 Accepted", "");
    } else {
        respond(socket, "200 OK", reply);
    }
}

}  // namespace finevox::game
