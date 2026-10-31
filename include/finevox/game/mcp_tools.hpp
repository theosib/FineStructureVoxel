#pragma once

/**
 * @file mcp_tools.hpp
 * @brief The game's MCP tools, and what the client's share with the server's
 */

#include "finevox/game/mcp_server.hpp"

#include <finescript/value.h>

#include <string_view>

namespace finescript {
class ExecutionContext;
class ScriptEngine;
}  // namespace finescript

namespace finevox {
class Entity;
class World;
}  // namespace finevox

namespace finevox::game {

class GameRuntime;

/// How much of a script value to show. A game's state can be huge, and full of cycles.
struct McpValueLimits {
    int maxDepth = 6;
    int maxNodes = 2000;
};

/// A script value as JSON. Symbols are ":name" strings, and what won't fit comes out as "...",
/// which sets `truncated`.
[[nodiscard]] nlohmann::json mcpValueToJson(const finescript::Value& value,
                                            finescript::ScriptEngine& engine,
                                            const McpValueLimits& limits, bool& truncated);

/// JSON as a script value. Strings of the form ":name" become symbols.
[[nodiscard]] finescript::Value mcpJsonToValue(const nlohmann::json& value,
                                               finescript::ScriptEngine& engine);

/// A command for `on :command` handlers, from {"name": "...", ...}. `name` is always a symbol.
[[nodiscard]] finescript::Value mcpCommandValue(const nlohmann::json& args,
                                                finescript::ScriptEngine& engine);

/// Run `code` in a context and report its value. Script errors come back as tool errors.
[[nodiscard]] McpResult mcpEval(const nlohmann::json& args, finescript::ScriptEngine& engine,
                                finescript::ExecutionContext& context);

/// Schema for the *_eval tools
[[nodiscard]] nlohmann::json mcpEvalSchema();

/// Schema for the *_entities tools, and the test they share. `position` is x y z.
[[nodiscard]] nlohmann::json mcpEntityFilterSchema();
[[nodiscard]] bool mcpEntityMatches(const nlohmann::json& args, std::string_view type,
                                    int faction, double x, double y, double z);

/// Schema for the *_blocks tools, and a block region of a world as text: a palette and, for
/// each y, a row of characters for each z, one character for each x
[[nodiscard]] nlohmann::json mcpBlocksSchema();
[[nodiscard]] McpResult mcpBlocks(const nlohmann::json& args, const World& world);

/// Schema for the tools that send a game command
[[nodiscard]] nlohmann::json mcpCommandSchema();

/// server_* tools, for a process that has a game thread
void addServerMcpTools(McpServer& server, GameRuntime& runtime);

}  // namespace finevox::game
