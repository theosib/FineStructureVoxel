#pragma once

/**
 * @file mcp_server.hpp
 * @brief A Model Context Protocol server inside a running game
 *
 * Lets a tool or an LLM look at a live game, and act in it, over HTTP on 127.0.0.1. It is off
 * unless the game is started with --mcp, and adds nothing to a run that doesn't ask for it: the
 * tick hook is a subsystem that only exists when the server does.
 *
 * Tools run where their data lives. A tool names an McpSide, the HTTP thread queues the call,
 * and that thread runs it at its next safe point, so a tool can read what the thread it's on
 * can read, with no locks. McpSide::Any tools run on the HTTP thread and answer even if the
 * game has hung, so they may only touch atomics.
 *
 * Client and server tools are registered by the client and the runtime, so a process offers
 * those of the sides it has: finevox_headless is server tools alone.
 */

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace finevox::game {

class McpOutput;

constexpr int MCP_DEFAULT_PORT = 7801;

enum class McpSide : uint8_t {
    Any,         ///< On the HTTP thread, right away. May only touch atomics.
    Game,        ///< On the game thread, between ticks
    Frame,       ///< On the render thread, where UI scripts run
    AfterFrame,  ///< On the render thread, with the finished frame in the swap chain
};

struct McpResult {
    nlohmann::json content = nlohmann::json::array();
    bool isError = false;

    [[nodiscard]] static McpResult text(std::string text);
    [[nodiscard]] static McpResult value(const nlohmann::json& value);
    [[nodiscard]] static McpResult error(std::string message);
    /// `data` is the encoded file, which this base64-encodes
    [[nodiscard]] static McpResult image(const std::vector<uint8_t>& data, std::string_view mimeType);
};

struct McpTool {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;  ///< Properties, required, and basic types are enforced
    McpSide side = McpSide::Any;
    /// Shows no more than a player could see, so an LLM player may be given it
    bool player = false;
    std::function<McpResult(const nlohmann::json& args)> run;
};

class McpServer {
public:
    /// `playerOnly` offers only the tools a player could fairly use. Port 0 picks a free one.
    McpServer(int port, bool playerOnly);
    ~McpServer();

    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    /// Before start()
    void addTool(McpTool tool);

    /// Listen on 127.0.0.1. A failure leaves the game running without a server.
    bool start(std::string& error);
    void stop();
    [[nodiscard]] int port() const { return port_; }

    /// Run what's queued for this side, from that side's thread. Nothing queued is one load.
    void pump(McpSide side);

    /// Frame and tick counters, for telling a slow game from a hung one
    void noteTickStart();
    void noteTickEnd();
    void noteFrame();

    /// Keep what the game prints, for the `logs` tool. Until this object goes away.
    void captureOutput();

    /// Answer one JSON-RPC message. Empty for a notification, which gets no reply.
    [[nodiscard]] std::string handleMessage(std::string_view body);

private:
    struct Job;
    struct Mailbox {
        std::mutex mutex;
        std::deque<std::shared_ptr<Job>> jobs;
        std::atomic<bool> waiting{false};
    };
    struct Rate {
        std::atomic<uint64_t> count{0};
        std::atomic<int64_t> lastNanos{0};
        std::atomic<float> perSecond{0.0f};
        uint64_t windowCount = 0;
        int64_t windowStart = 0;
        void note(int64_t nanos);
        [[nodiscard]] nlohmann::json report(int64_t nanos) const;
    };

    [[nodiscard]] nlohmann::json initializeResult(const nlohmann::json& params) const;
    [[nodiscard]] nlohmann::json listTools() const;
    [[nodiscard]] nlohmann::json callTool(const McpTool& tool, const nlohmann::json& args);
    [[nodiscard]] const McpTool* findTool(const std::string& name) const;
    [[nodiscard]] nlohmann::json statusReport() const;
    void addBuiltinTools();
    void listen();
    void serve(int socket);
    [[nodiscard]] Mailbox& mailbox(McpSide side);

    int port_;
    bool playerOnly_;
    std::vector<McpTool> tools_;
    std::unordered_map<std::string, size_t> toolIndex_;
    Mailbox gameBox_;
    Mailbox frameBox_;
    Mailbox afterFrameBox_;

    int listenSocket_ = -1;
    std::thread listener_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> connections_{0};

    Rate ticks_;
    Rate frames_;
    std::atomic<int64_t> tickStartNanos_{0};
    std::atomic<float> tickMsAverage_{0.0f};
    std::atomic<float> tickMsWorst_{0.0f};
    double tickMsSum_ = 0.0;
    float tickMsPeak_ = 0.0f;
    uint32_t tickMsCount_ = 0;

    std::unique_ptr<McpOutput> output_;
};

}  // namespace finevox::game
