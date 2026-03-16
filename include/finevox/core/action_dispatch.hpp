#pragma once

/**
 * @file action_dispatch.hpp
 * @brief Named action dispatch system for input actions
 *
 * Maps action names to handler functions. Same code path for single-player
 * and multiplayer: in single-player, handlers run locally; in multiplayer,
 * the client serializes the action and sends it over the network.
 */

#include <functional>
#include <string>
#include <unordered_map>

namespace finescript { class Value; }

namespace finevox {

/// Arguments passed to action handlers
struct ActionArgs {
    std::string argExpression;  ///< Finescript expression string (may be empty)
    float scrollDelta = 0.0f;   ///< For scroll actions
};

class ActionDispatch {
public:
    using ActionHandler = std::function<void(const ActionArgs& args)>;

    /// Register a named action handler
    void registerAction(const std::string& name, ActionHandler handler);

    /// Unregister an action handler
    void unregisterAction(const std::string& name);

    /// Check if an action is registered
    [[nodiscard]] bool hasAction(const std::string& name) const;

    /// Dispatch an action by name with args.
    /// Returns true if a handler was found and called.
    bool dispatch(const std::string& actionName, const ActionArgs& args);

    /// Convenience: dispatch with just a name (no args)
    bool dispatch(const std::string& actionName);

    /// Clear all registered actions
    void clear();

private:
    std::unordered_map<std::string, ActionHandler> handlers_;
};

}  // namespace finevox
