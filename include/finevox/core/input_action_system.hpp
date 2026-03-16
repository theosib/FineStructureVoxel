#pragma once

/**
 * @file input_action_system.hpp
 * @brief Context-aware input binding system mapping key/mouse events to named actions
 *
 * Loads bindings from .bindings config files. Each binding maps an event spec
 * (key_e, click_left, mod_shift+click_left, etc.) to an action name with
 * optional finescript argument expression.
 *
 * The system is config-driven and context-aware: different bindings are active
 * depending on the current InputContext (Gameplay, Menu, Chat, Inventory, etc.).
 */

#include "finevox/core/input_context.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace finevox {

// ============================================================================
// RawInputEvent - Lightweight event description (no finevk dependency)
// ============================================================================

/// Event types that the input action system understands
enum class RawEventType : uint8_t {
    KeyPress,
    KeyRelease,
    MouseButtonPress,
    MouseButtonRelease,
    MouseScroll,
};

/// Modifier flags (matches finevk::Modifier layout)
enum class InputModifier : uint32_t {
    None    = 0,
    Shift   = 1 << 0,
    Control = 1 << 1,
    Alt     = 1 << 2,
    Super   = 1 << 3,
};

inline InputModifier operator|(InputModifier a, InputModifier b) {
    return static_cast<InputModifier>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline InputModifier operator&(InputModifier a, InputModifier b) {
    return static_cast<InputModifier>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasModifier(InputModifier mods, InputModifier flag) {
    return (static_cast<uint32_t>(mods) & static_cast<uint32_t>(flag)) != 0;
}

/// Lightweight input event for the action system (no finevk dependency)
struct RawInputEvent {
    RawEventType type;
    int keyOrButton = 0;      ///< Key code or mouse button number
    InputModifier modifiers = InputModifier::None;
    float scrollDelta = 0.0f; ///< For MouseScroll events
};

// ============================================================================
// ActionResult - What processEvent returns
// ============================================================================

/// Result of processing an input event through the binding system
struct ActionResult {
    std::string actionName;         ///< Empty if no binding matched
    std::string argExpression;      ///< Finescript expression for args (may be empty)
    float scrollDelta = 0.0f;       ///< Passed through for scroll events

    bool matched() const { return !actionName.empty(); }
};

// ============================================================================
// Event spec types (internal but exposed for testing)
// ============================================================================

/// What kind of input event a binding matches
enum class EventSpecKind : uint8_t {
    Key,            ///< key_<name>: matches press then release (simple key typed)
    KeyPress,       ///< keypress_<name>: matches key press only
    KeyRelease,     ///< keyrelease_<name>: matches key release only
    Click,          ///< click_<name>: matches mouse press then release
    MousePress,     ///< mousepress_<name>: matches mouse press only
    MouseRelease,   ///< mouserelease_<name>: matches mouse release only
    Scroll,         ///< scroll: matches mouse scroll, delta as argument
};

/// Parsed event specification from a binding config line
struct EventSpec {
    EventSpecKind kind;
    int keyOrButton = 0;              ///< GLFW key code or mouse button number
    InputModifier requiredModifiers = InputModifier::None;
};

// ============================================================================
// InputActionSystem
// ============================================================================

class InputActionSystem {
public:
    InputActionSystem();
    ~InputActionSystem();

    /// Load bindings from a config file (filesystem path)
    void loadBindings(const std::string& path);

    /// Load bindings from a string (for testing)
    void loadBindingsFromString(const std::string& content);

    /// Set active input context (changes which bindings are live)
    void setContext(InputContext ctx);

    /// Get current input context
    [[nodiscard]] InputContext context() const { return currentContext_; }

    /// Process a raw input event. Returns an ActionResult (empty name if no match).
    [[nodiscard]] ActionResult processEvent(const RawInputEvent& event);

    /// Register a custom context name (maps to an internal ID for extensibility)
    /// Built-in contexts: "gameplay", "menu", "chat", "inventory"
    void registerContext(const std::string& name);

    /// Query binding for display/settings
    struct BindingInfo {
        std::string eventSpec;
        std::string actionName;
        std::string argExpression;
    };
    [[nodiscard]] std::vector<BindingInfo> getBindingsForContext(InputContext ctx) const;

    /// Translate a key name to a GLFW key code (e.g., "w" → 87, "space" → 32)
    [[nodiscard]] static int keyNameToCode(const std::string& name);

    /// Translate a GLFW key code to a key name (for display)
    [[nodiscard]] static std::string keyCodeToName(int code);

    // ========================================================================
    // Begin/end action tracking (for auto-cancel on context switch)
    // ========================================================================

    /// Called after dispatch to track active begin_* actions.
    /// Pass the action name; if it starts with "begin_", it's tracked.
    void trackBeginAction(const std::string& actionName);

    /// Called when an end_* action fires. Removes from tracking.
    void trackEndAction(const std::string& actionName);

    /// Get all active begin actions that need cancellation.
    /// Called on context switch to generate end_* actions.
    [[nodiscard]] std::vector<std::string> getActiveBeginActions() const;

    /// Clear all active begin actions (after sending cancellations).
    void clearActiveBeginActions();

private:
    struct CompiledBinding {
        EventSpec spec;
        std::string actionName;
        std::string argExpression;
        std::string rawEventSpec; // For display
    };

    void parseBindings(const std::string& content);
    static EventSpec parseEventSpec(const std::string& specStr);
    static int parseMouseButtonName(const std::string& name);
    bool eventMatchesSpec(const RawInputEvent& event, const EventSpec& spec) const;

    std::unordered_map<InputContext, std::vector<CompiledBinding>> bindings_;
    InputContext currentContext_ = InputContext::Gameplay;

    // Extended context name → InputContext mapping
    std::unordered_map<std::string, InputContext> contextNames_;

    // Active begin_* actions for auto-cancel
    std::unordered_set<std::string> activeBeginActions_;
};

// ============================================================================
// Key name lookup table
// ============================================================================

/// Build the key name → GLFW code lookup table (called once internally)
const std::unordered_map<std::string, int>& getKeyNameTable();

}  // namespace finevox
