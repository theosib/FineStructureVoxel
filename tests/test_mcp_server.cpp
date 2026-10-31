#include "finevox/game/mcp_server.hpp"
#include "finevox/game/mcp_tools.hpp"

#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace finevox::game;
using json = nlohmann::json;
using finescript::Value;

namespace {

McpTool echoTool(std::string name, McpSide side, bool player = false) {
    McpTool tool;
    tool.name = std::move(name);
    tool.description = "Echo";
    tool.inputSchema = {{"type", "object"},
                        {"properties", {{"text", {{"type", "string"}}}, {"times", {{"type", "integer"}}}}},
                        {"required", {"text"}}};
    tool.side = side;
    tool.player = player;
    tool.run = [](const json& args) { return McpResult::text(args.at("text").get<std::string>()); };
    return tool;
}

json call(McpServer& server, const std::string& method, json params = json::object()) {
    json request = {{"jsonrpc", "2.0"}, {"id", 7}, {"method", method}, {"params", std::move(params)}};
    return json::parse(server.handleMessage(request.dump()));
}

json callTool(McpServer& server, const std::string& name, json args = json::object()) {
    return call(server, "tools/call", {{"name", name}, {"arguments", std::move(args)}});
}

std::string firstText(const json& reply) {
    return reply.at("result").at("content").at(0).at("text").get<std::string>();
}

std::vector<std::string> toolNames(McpServer& server) {
    std::vector<std::string> names;
    json reply = call(server, "tools/list");
    for (const json& tool : reply["result"]["tools"]) {
        names.push_back(tool["name"]);
    }
    return names;
}

/// A raw HTTP exchange on 127.0.0.1, as a client makes
std::string httpExchange(int port, const std::string& request) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));
    EXPECT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    EXPECT_EQ(::send(fd, request.data(), request.size(), 0), static_cast<ssize_t>(request.size()));
    std::string reply;
    char buffer[4096];
    ssize_t got;
    while ((got = ::recv(fd, buffer, sizeof(buffer), 0)) > 0) reply.append(buffer, static_cast<size_t>(got));
    ::close(fd);
    return reply;
}

std::string postRequest(const std::string& body, const std::string& extraHeader = "") {
    return "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n" + extraHeader +
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

TEST(McpServerTest, ShakesHands) {
    McpServer server(0, false);
    json reply = call(server, "initialize", {{"protocolVersion", "2025-03-26"}});
    EXPECT_EQ(reply["id"], 7);
    EXPECT_EQ(reply["result"]["protocolVersion"], "2025-03-26");
    EXPECT_TRUE(reply["result"]["capabilities"].contains("tools"));

    // One it doesn't know gets the newest it does
    reply = call(server, "initialize", {{"protocolVersion", "1999-01-01"}});
    EXPECT_EQ(reply["result"]["protocolVersion"], "2025-06-18");

    EXPECT_TRUE(call(server, "ping")["result"].is_object());
}

TEST(McpServerTest, ANotificationGetsNoReply) {
    McpServer server(0, false);
    EXPECT_EQ(server.handleMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), "");
}

TEST(McpServerTest, BadRequestsGetProperErrors) {
    McpServer server(0, false);
    EXPECT_EQ(json::parse(server.handleMessage("{nonsense"))["error"]["code"], -32700);
    EXPECT_EQ(json::parse(server.handleMessage("[1]"))["error"]["code"], -32600);
    EXPECT_EQ(call(server, "no/such/method")["error"]["code"], -32601);
    EXPECT_EQ(callTool(server, "no_such_tool")["error"]["code"], -32602);
}

TEST(McpServerTest, ArgumentsAreCheckedStrictly) {
    McpServer server(0, false);
    server.addTool(echoTool("echo", McpSide::Any));

    EXPECT_EQ(firstText(callTool(server, "echo", {{"text", "hi"}})), "hi");

    json missing = callTool(server, "echo");
    EXPECT_EQ(missing["error"]["code"], -32602);
    EXPECT_NE(missing["error"]["message"].get<std::string>().find("text"), std::string::npos);

    json unknown = callTool(server, "echo", {{"text", "hi"}, {"tiems", 2}});
    EXPECT_EQ(unknown["error"]["code"], -32602);
    EXPECT_NE(unknown["error"]["message"].get<std::string>().find("tiems"), std::string::npos);

    EXPECT_EQ(callTool(server, "echo", {{"text", 3}})["error"]["code"], -32602);
    EXPECT_EQ(callTool(server, "echo", {{"text", "hi"}, {"times", 1.5}})["error"]["code"], -32602);
}

TEST(McpServerTest, AToolThatThrowsIsAToolError) {
    McpServer server(0, false);
    McpTool tool = echoTool("bad", McpSide::Any);
    tool.run = [](const json&) -> McpResult { throw std::runtime_error("nope"); };
    server.addTool(std::move(tool));

    json reply = callTool(server, "bad", {{"text", "x"}});
    EXPECT_TRUE(reply["result"]["isError"].get<bool>());
    EXPECT_EQ(firstText(reply), "nope");
}

TEST(McpServerTest, PlayerModeOffersOnlyPlayerTools) {
    McpServer server(0, true);
    server.addTool(echoTool("seen", McpSide::Any, true));
    server.addTool(echoTool("hidden", McpSide::Any, false));

    auto names = toolNames(server);
    EXPECT_NE(std::find(names.begin(), names.end(), "seen"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "hidden"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "logs"), names.end());

    // Hidden means uncallable, and not merely unlisted
    EXPECT_EQ(firstText(callTool(server, "seen", {{"text", "x"}})), "x");
    EXPECT_EQ(callTool(server, "hidden", {{"text", "x"}})["error"]["code"], -32602);
}

TEST(McpServerTest, ToolsRunOnTheirOwnThread) {
    McpServer server(0, false);
    std::thread::id ranOn;
    McpTool tool = echoTool("game", McpSide::Game);
    tool.run = [&ranOn](const json&) {
        ranOn = std::this_thread::get_id();
        return McpResult::text("done");
    };
    server.addTool(std::move(tool));

    std::atomic<bool> finished{false};
    std::thread gameThread([&] {
        while (!finished.load()) {
            server.pump(McpSide::Game);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    json reply = callTool(server, "game", {{"text", "x"}});
    std::thread::id gameId = gameThread.get_id();
    finished.store(true);
    gameThread.join();

    EXPECT_EQ(firstText(reply), "done");
    EXPECT_EQ(ranOn, gameId);
    EXPECT_NE(ranOn, std::this_thread::get_id());
}

TEST(McpServerTest, SidesHaveSeparateQueues) {
    McpServer server(0, false);
    server.addTool(echoTool("after", McpSide::AfterFrame));

    std::atomic<bool> finished{false};
    std::thread frameThread([&] {
        // Frame and Game mustn't pick up an AfterFrame tool
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
        while (std::chrono::steady_clock::now() < until) {
            server.pump(McpSide::Frame);
            server.pump(McpSide::Game);
        }
        while (!finished.load()) {
            server.pump(McpSide::AfterFrame);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    auto started = std::chrono::steady_clock::now();
    json reply = callTool(server, "after", {{"text", "x"}});
    auto took = std::chrono::steady_clock::now() - started;
    finished.store(true);
    frameThread.join();

    EXPECT_EQ(firstText(reply), "x");
    EXPECT_GE(took, std::chrono::milliseconds(25));
}

TEST(McpServerTest, StoppingReleasesACallerWaitingOnAThreadThatNeverComes) {
    McpServer server(0, false);
    server.addTool(echoTool("game", McpSide::Game));
    std::string error;
    ASSERT_TRUE(server.start(error)) << error;

    json request = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                    {"params", {{"name", "game"}, {"arguments", {{"text", "x"}}}}}};
    std::string reply;
    std::thread caller([&] { reply = httpExchange(server.port(), postRequest(request.dump())); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto started = std::chrono::steady_clock::now();
    server.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    caller.join();
    EXPECT_NE(reply.find("shutting down"), std::string::npos);
}

TEST(McpServerTest, StatusAnswersWithNoThreadRunning) {
    McpServer server(0, false);
    json report = json::parse(firstText(callTool(server, "status")));
    EXPECT_EQ(report["ticks"]["count"], 0);
    EXPECT_TRUE(report["ticks"]["last_ms_ago"].is_null());

    server.noteTickStart();
    server.noteTickEnd();
    server.noteFrame();
    report = json::parse(firstText(callTool(server, "status")));
    EXPECT_EQ(report["ticks"]["count"], 1);
    EXPECT_EQ(report["frames"]["count"], 1);
    EXPECT_TRUE(report["ticks"]["last_ms_ago"].is_number());
}

TEST(McpServerTest, WaitTimesOutSayingWhy) {
    McpServer server(0, false);
    json reply = callTool(server, "wait", {{"ticks", 5}, {"seconds", 0.05}});
    EXPECT_TRUE(reply["result"]["isError"].get<bool>());
    EXPECT_NE(firstText(reply).find("timed out"), std::string::npos);

    std::thread ticker([&] {
        for (int i = 0; i < 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            server.noteTickEnd();
        }
    });
    reply = callTool(server, "wait", {{"ticks", 3}, {"seconds", 5.0}});
    ticker.join();
    EXPECT_FALSE(reply["result"]["isError"].get<bool>());
}

TEST(McpServerTest, LogsKeepWhatTheProgramPrints) {
    McpServer server(0, false);
    EXPECT_TRUE(callTool(server, "logs")["result"]["isError"].get<bool>());

    server.captureOutput();
    std::cout << "first line\nsecond ";
    std::cout << "line\n";
    std::cerr << "third line" << std::endl;
    std::cout << "unfinished" << std::flush;

    json all = json::parse(firstText(callTool(server, "logs")));
    EXPECT_EQ(all["lines"], json({"first line", "second line", "third line"}));
    EXPECT_EQ(all["next"], 3);

    json last = json::parse(firstText(callTool(server, "logs", {{"tail", 1}})));
    EXPECT_EQ(last["lines"], json({"third line"}));

    json found = json::parse(firstText(callTool(server, "logs", {{"contains", "second"}})));
    EXPECT_EQ(found["lines"], json({"second line"}));

    json since = json::parse(firstText(callTool(server, "logs", {{"since", 2}})));
    EXPECT_EQ(since["lines"], json({"third line"}));

    std::cout << std::endl;
}

TEST(McpServerTest, ServesOverHttp) {
    McpServer server(0, false);
    server.addTool(echoTool("echo", McpSide::Any));
    std::string error;
    ASSERT_TRUE(server.start(error)) << error;
    ASSERT_GT(server.port(), 0);

    json request = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                    {"params", {{"name", "echo"}, {"arguments", {{"text", "over the wire"}}}}}};
    std::string reply = httpExchange(server.port(), postRequest(request.dump()));
    EXPECT_NE(reply.find("200 OK"), std::string::npos);
    EXPECT_NE(reply.find("over the wire"), std::string::npos);

    reply = httpExchange(server.port(),
                         postRequest(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
    EXPECT_NE(reply.find("202 Accepted"), std::string::npos);

    reply = httpExchange(server.port(), "GET /mcp HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_NE(reply.find("405"), std::string::npos);
}

TEST(McpServerTest, WebPagesCantReachIt) {
    McpServer server(0, false);
    std::string error;
    ASSERT_TRUE(server.start(error)) << error;
    std::string body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";

    std::string reply = httpExchange(server.port(), postRequest(body, "Origin: https://example.com\r\n"));
    EXPECT_NE(reply.find("403"), std::string::npos);
    // A name that merely starts with localhost
    reply = httpExchange(server.port(), postRequest(body, "Origin: http://localhost.example.com\r\n"));
    EXPECT_NE(reply.find("403"), std::string::npos);

    reply = httpExchange(server.port(), postRequest(body, "Origin: http://localhost:3000\r\n"));
    EXPECT_NE(reply.find("200 OK"), std::string::npos);
}

TEST(McpServerTest, AFailedBindReportsIt) {
    McpServer first(0, false);
    std::string error;
    ASSERT_TRUE(first.start(error)) << error;

    McpServer second(first.port(), false);
    EXPECT_FALSE(second.start(error));
    EXPECT_NE(error.find(std::to_string(first.port())), std::string::npos);
}

TEST(McpServerTest, ACallerThatHangsUpDoesNoHarm) {
    McpServer server(0, false);
    McpTool big = echoTool("big", McpSide::Any);
    big.run = [](const json&) { return McpResult::text(std::string(4 << 20, 'x')); };
    server.addTool(std::move(big));
    std::string error;
    ASSERT_TRUE(server.start(error)) << error;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(server.port()));
    ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    json request = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                    {"params", {{"name", "big"}, {"arguments", {{"text", "x"}}}}}};
    std::string text = postRequest(request.dump());
    ASSERT_EQ(::send(fd, text.data(), text.size(), 0), static_cast<ssize_t>(text.size()));
    ::close(fd);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::string reply = httpExchange(server.port(), postRequest(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
    EXPECT_NE(reply.find("200 OK"), std::string::npos);
}

TEST(McpValueTest, ScriptValuesBecomeJson) {
    finescript::ScriptEngine engine;
    finescript::ExecutionContext context(engine);
    json args = {{"code", R"({=a 1 =b 2.5 =c "text" =d :sym =e [nil true] =f fn [x] x})"}};
    McpResult result = mcpEval(args, engine, context);
    ASSERT_FALSE(result.isError) << result.content.dump();

    json value = json::parse(result.content[0]["text"].get<std::string>());
    EXPECT_FALSE(value["truncated"].get<bool>());
    EXPECT_EQ(value["value"]["a"], 1);
    EXPECT_EQ(value["value"]["b"], 2.5);
    EXPECT_EQ(value["value"]["c"], "text");
    EXPECT_EQ(value["value"]["d"], ":sym");
    EXPECT_TRUE(value["value"]["e"][0].is_null());
    EXPECT_EQ(value["value"]["f"], "<function>");
}

TEST(McpValueTest, ScriptErrorsComeBackAsToolErrors) {
    finescript::ScriptEngine engine;
    finescript::ExecutionContext context(engine);
    EXPECT_TRUE(mcpEval({{"code", "(1 +"}}, engine, context).isError);
    EXPECT_TRUE(mcpEval({{"code", "{no_such_function 1}"}}, engine, context).isError);
}

TEST(McpValueTest, EvalSeesTheContextsGlobals) {
    finescript::ScriptEngine engine;
    finescript::ExecutionContext context(engine);
    context.set("score", Value::integer(41));
    McpResult result = mcpEval({{"code", "(score + 1)"}}, engine, context);
    json value = json::parse(result.content[0]["text"].get<std::string>());
    EXPECT_EQ(value["value"], 42);
}

TEST(McpValueTest, ACycleIsCutAtTheLimit) {
    finescript::ScriptEngine engine;
    finescript::ExecutionContext context(engine);
    auto map = std::make_shared<finescript::MapData>();
    Value self = Value::map(map);
    map->set(engine.intern("self"), self);

    bool truncated = false;
    McpValueLimits limits;
    json out = mcpValueToJson(self, engine, limits, truncated);
    EXPECT_TRUE(truncated);
    json walk = out;
    for (int i = 0; i <= limits.maxDepth; ++i) walk = json(walk["self"]);
    EXPECT_EQ(walk, "...");

    map->remove(engine.intern("self"));  // Or the map keeps itself alive
}

TEST(McpValueTest, NodeLimitBoundsTheAnswer) {
    finescript::ScriptEngine engine;
    bool truncated = false;
    McpValueLimits limits;
    limits.maxNodes = 10;
    std::vector<Value> many(1000, Value::integer(1));
    json out = mcpValueToJson(Value::array(std::move(many)), engine, limits, truncated);
    EXPECT_TRUE(truncated);
    EXPECT_LE(std::count_if(out.begin(), out.end(), [](const json& v) { return v.is_number(); }), 10);
}

TEST(McpValueTest, JsonBecomesScriptValues) {
    finescript::ScriptEngine engine;
    finescript::ExecutionContext context(engine);
    Value value = mcpJsonToValue({{"n", 3}, {"f", 1.5}, {"s", "text"}, {"y", ":sym"}, {"list", {1, 2}}}, engine);
    ASSERT_TRUE(value.isMap());
    const auto& map = value.asMap();
    EXPECT_EQ(map.get(engine.intern("n")).asInt(), 3);
    EXPECT_EQ(map.get(engine.intern("f")).asNumber(), 1.5);
    EXPECT_EQ(map.get(engine.intern("s")).asString(), "text");
    EXPECT_EQ(map.get(engine.intern("y")).asSymbol(), engine.intern("sym"));
    EXPECT_EQ(map.get(engine.intern("list")).asArray().size(), 2u);
}

TEST(McpValueTest, ACommandNameIsASymbol) {
    finescript::ScriptEngine engine;
    Value plain = mcpCommandValue({{"name", "train"}}, engine);
    EXPECT_EQ(plain.asMap().get(engine.intern("name")).asSymbol(), engine.intern("train"));

    Value withColon = mcpCommandValue({{"name", ":move"}, {"args", {{"x", 4}}}}, engine);
    EXPECT_EQ(withColon.asMap().get(engine.intern("name")).asSymbol(), engine.intern("move"));
    EXPECT_EQ(withColon.asMap().get(engine.intern("x")).asInt(), 4);
}

TEST(McpFilterTest, EntitiesMatchEveryFilterGiven) {
    EXPECT_TRUE(mcpEntityMatches(json::object(), "hand", 1, 0, 0, 0));
    EXPECT_TRUE(mcpEntityMatches({{"type", "hand"}}, "hand", 1, 0, 0, 0));
    EXPECT_FALSE(mcpEntityMatches({{"type", "militia"}}, "hand", 1, 0, 0, 0));
    EXPECT_FALSE(mcpEntityMatches({{"faction", 2}}, "hand", 1, 0, 0, 0));
    json near = {{"near", {10, 0, 0}}, {"radius", 5.0}};
    EXPECT_TRUE(mcpEntityMatches(near, "hand", 1, 6, 0, 0));
    EXPECT_FALSE(mcpEntityMatches(near, "hand", 1, 4, 0, 0));
    EXPECT_THROW((void)mcpEntityMatches({{"near", {1, 2}}}, "hand", 1, 0, 0, 0), std::runtime_error);
}
