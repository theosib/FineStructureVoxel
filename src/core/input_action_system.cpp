#include "finevox/core/input_action_system.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace finevox {

// ============================================================================
// GLFW key code constants (core can't include GLFW headers)
// ============================================================================

namespace glfw_keys {
    // Printable keys
    constexpr int SPACE = 32;
    constexpr int APOSTROPHE = 39;
    constexpr int COMMA = 44;
    constexpr int MINUS = 45;
    constexpr int PERIOD = 46;
    constexpr int SLASH = 47;
    constexpr int KEY_0 = 48;
    constexpr int KEY_1 = 49;
    constexpr int KEY_2 = 50;
    constexpr int KEY_3 = 51;
    constexpr int KEY_4 = 52;
    constexpr int KEY_5 = 53;
    constexpr int KEY_6 = 54;
    constexpr int KEY_7 = 55;
    constexpr int KEY_8 = 56;
    constexpr int KEY_9 = 57;
    constexpr int SEMICOLON = 59;
    constexpr int EQUAL = 61;
    constexpr int A = 65;
    constexpr int B = 66;
    constexpr int C = 67;
    constexpr int D = 68;
    constexpr int E = 69;
    constexpr int F = 70;
    constexpr int G = 71;
    constexpr int H = 72;
    constexpr int I = 73;
    constexpr int J = 74;
    constexpr int K = 75;
    constexpr int L = 76;
    constexpr int M = 77;
    constexpr int N = 78;
    constexpr int O = 79;
    constexpr int P = 80;
    constexpr int Q = 81;
    constexpr int R = 82;
    constexpr int S = 83;
    constexpr int T = 84;
    constexpr int U = 85;
    constexpr int V = 86;
    constexpr int W = 87;
    constexpr int X = 88;
    constexpr int Y = 89;
    constexpr int Z = 90;
    constexpr int LEFT_BRACKET = 91;
    constexpr int BACKSLASH = 92;
    constexpr int RIGHT_BRACKET = 93;
    constexpr int GRAVE_ACCENT = 96;

    // Function keys
    constexpr int ESCAPE = 256;
    constexpr int ENTER = 257;
    constexpr int TAB = 258;
    constexpr int BACKSPACE = 259;
    constexpr int INSERT = 260;
    constexpr int DELETE_KEY = 261;
    constexpr int RIGHT = 262;
    constexpr int LEFT = 263;
    constexpr int DOWN = 264;
    constexpr int UP = 265;
    constexpr int PAGE_UP = 266;
    constexpr int PAGE_DOWN = 267;
    constexpr int HOME = 268;
    constexpr int END = 269;
    constexpr int CAPS_LOCK = 280;
    constexpr int F1 = 290;
    constexpr int F2 = 291;
    constexpr int F3 = 292;
    constexpr int F4 = 293;
    constexpr int F5 = 294;
    constexpr int F6 = 295;
    constexpr int F7 = 296;
    constexpr int F8 = 297;
    constexpr int F9 = 298;
    constexpr int F10 = 299;
    constexpr int F11 = 300;
    constexpr int F12 = 301;

    // Modifier keys
    constexpr int LEFT_SHIFT = 340;
    constexpr int LEFT_CONTROL = 341;
    constexpr int LEFT_ALT = 342;
    constexpr int RIGHT_SHIFT = 344;
    constexpr int RIGHT_CONTROL = 345;
    constexpr int RIGHT_ALT = 346;

    // Mouse buttons
    constexpr int MOUSE_LEFT = 0;
    constexpr int MOUSE_RIGHT = 1;
    constexpr int MOUSE_MIDDLE = 2;
}

// ============================================================================
// Key name table
// ============================================================================

const std::unordered_map<std::string, int>& getKeyNameTable() {
    static const std::unordered_map<std::string, int> table = {
        // Letters
        {"a", glfw_keys::A}, {"b", glfw_keys::B}, {"c", glfw_keys::C},
        {"d", glfw_keys::D}, {"e", glfw_keys::E}, {"f", glfw_keys::F},
        {"g", glfw_keys::G}, {"h", glfw_keys::H}, {"i", glfw_keys::I},
        {"j", glfw_keys::J}, {"k", glfw_keys::K}, {"l", glfw_keys::L},
        {"m", glfw_keys::M}, {"n", glfw_keys::N}, {"o", glfw_keys::O},
        {"p", glfw_keys::P}, {"q", glfw_keys::Q}, {"r", glfw_keys::R},
        {"s", glfw_keys::S}, {"t", glfw_keys::T}, {"u", glfw_keys::U},
        {"v", glfw_keys::V}, {"w", glfw_keys::W}, {"x", glfw_keys::X},
        {"y", glfw_keys::Y}, {"z", glfw_keys::Z},
        // Numbers
        {"0", glfw_keys::KEY_0}, {"1", glfw_keys::KEY_1}, {"2", glfw_keys::KEY_2},
        {"3", glfw_keys::KEY_3}, {"4", glfw_keys::KEY_4}, {"5", glfw_keys::KEY_5},
        {"6", glfw_keys::KEY_6}, {"7", glfw_keys::KEY_7}, {"8", glfw_keys::KEY_8},
        {"9", glfw_keys::KEY_9},
        // Special keys
        {"space", glfw_keys::SPACE},
        {"escape", glfw_keys::ESCAPE},
        {"enter", glfw_keys::ENTER},
        {"tab", glfw_keys::TAB},
        {"backspace", glfw_keys::BACKSPACE},
        {"insert", glfw_keys::INSERT},
        {"delete", glfw_keys::DELETE_KEY},
        {"arrow_right", glfw_keys::RIGHT},
        {"arrow_left", glfw_keys::LEFT},
        {"arrow_down", glfw_keys::DOWN},
        {"arrow_up", glfw_keys::UP},
        {"page_up", glfw_keys::PAGE_UP},
        {"page_down", glfw_keys::PAGE_DOWN},
        {"home", glfw_keys::HOME},
        {"end", glfw_keys::END},
        {"caps_lock", glfw_keys::CAPS_LOCK},
        {"grave", glfw_keys::GRAVE_ACCENT},
        {"backtick", glfw_keys::GRAVE_ACCENT},
        // Punctuation
        {"comma", glfw_keys::COMMA},
        {"period", glfw_keys::PERIOD},
        {"slash", glfw_keys::SLASH},
        {"semicolon", glfw_keys::SEMICOLON},
        {"apostrophe", glfw_keys::APOSTROPHE},
        {"minus", glfw_keys::MINUS},
        {"equal", glfw_keys::EQUAL},
        {"left_bracket", glfw_keys::LEFT_BRACKET},
        {"right_bracket", glfw_keys::RIGHT_BRACKET},
        {"backslash", glfw_keys::BACKSLASH},
        // Function keys
        {"f1", glfw_keys::F1}, {"f2", glfw_keys::F2}, {"f3", glfw_keys::F3},
        {"f4", glfw_keys::F4}, {"f5", glfw_keys::F5}, {"f6", glfw_keys::F6},
        {"f7", glfw_keys::F7}, {"f8", glfw_keys::F8}, {"f9", glfw_keys::F9},
        {"f10", glfw_keys::F10}, {"f11", glfw_keys::F11}, {"f12", glfw_keys::F12},
        // Modifier keys (as bindable keys)
        {"left_shift", glfw_keys::LEFT_SHIFT},
        {"right_shift", glfw_keys::RIGHT_SHIFT},
        {"left_control", glfw_keys::LEFT_CONTROL},
        {"right_control", glfw_keys::RIGHT_CONTROL},
        {"left_alt", glfw_keys::LEFT_ALT},
        {"right_alt", glfw_keys::RIGHT_ALT},
    };
    return table;
}

// Reverse lookup (lazy-built)
static const std::unordered_map<int, std::string>& getCodeToNameTable() {
    static std::unordered_map<int, std::string> table;
    static bool built = false;
    if (!built) {
        for (auto& [name, code] : getKeyNameTable()) {
            // Prefer shorter names for duplicates (e.g., "grave" over "backtick")
            if (table.find(code) == table.end() || name.size() < table[code].size()) {
                table[code] = name;
            }
        }
        built = true;
    }
    return table;
}

int InputActionSystem::keyNameToCode(const std::string& name) {
    auto& table = getKeyNameTable();
    auto it = table.find(name);
    return it != table.end() ? it->second : -1;
}

std::string InputActionSystem::keyCodeToName(int code) {
    auto& table = getCodeToNameTable();
    auto it = table.find(code);
    return it != table.end() ? it->second : "unknown";
}

// ============================================================================
// Mouse button name parsing
// ============================================================================

int InputActionSystem::parseMouseButtonName(const std::string& name) {
    if (name == "left") return glfw_keys::MOUSE_LEFT;
    if (name == "right") return glfw_keys::MOUSE_RIGHT;
    if (name == "middle") return glfw_keys::MOUSE_MIDDLE;
    // Try numeric
    try { return std::stoi(name); } catch (...) {}
    return -1;
}

// ============================================================================
// Event spec parsing
// ============================================================================

static InputModifier parseModifierName(const std::string& name) {
    if (name == "shift") return InputModifier::Shift;
    if (name == "ctrl" || name == "control") return InputModifier::Control;
    if (name == "alt") return InputModifier::Alt;
    if (name == "super") return InputModifier::Super;
    return InputModifier::None;
}

// Split "mod_shift+ctrl+click_left" into modifiers and base event
static std::pair<InputModifier, std::string> splitModifiers(const std::string& spec) {
    InputModifier mods = InputModifier::None;

    // Check for mod_ prefix
    if (spec.substr(0, 4) != "mod_") {
        return {mods, spec};
    }

    // Everything after "mod_" up to the last known event prefix
    std::string rest = spec.substr(4);

    // Split on '+', last part is the base event
    std::vector<std::string> parts;
    std::istringstream ss(rest);
    std::string part;
    while (std::getline(ss, part, '+')) {
        parts.push_back(part);
    }

    if (parts.empty()) return {mods, spec};

    // Last part is the base event spec
    std::string baseEvent = parts.back();
    parts.pop_back();

    // Remaining parts are modifier names
    for (auto& modName : parts) {
        mods = mods | parseModifierName(modName);
    }

    return {mods, baseEvent};
}

EventSpec InputActionSystem::parseEventSpec(const std::string& specStr) {
    auto [mods, baseSpec] = splitModifiers(specStr);
    EventSpec spec;
    spec.requiredModifiers = mods;

    // Parse base event spec
    if (baseSpec == "scroll") {
        spec.kind = EventSpecKind::Scroll;
        spec.keyOrButton = 0;
        return spec;
    }

    // key_<name>
    if (baseSpec.substr(0, 4) == "key_") {
        spec.kind = EventSpecKind::Key;
        spec.keyOrButton = keyNameToCode(baseSpec.substr(4));
        return spec;
    }

    // keypress_<name>
    if (baseSpec.substr(0, 9) == "keypress_") {
        spec.kind = EventSpecKind::KeyPress;
        spec.keyOrButton = keyNameToCode(baseSpec.substr(9));
        return spec;
    }

    // keyrelease_<name>
    if (baseSpec.substr(0, 11) == "keyrelease_") {
        spec.kind = EventSpecKind::KeyRelease;
        spec.keyOrButton = keyNameToCode(baseSpec.substr(11));
        return spec;
    }

    // click_<name>
    if (baseSpec.substr(0, 6) == "click_") {
        spec.kind = EventSpecKind::Click;
        spec.keyOrButton = parseMouseButtonName(baseSpec.substr(6));
        return spec;
    }

    // mousepress_<name>
    if (baseSpec.substr(0, 11) == "mousepress_") {
        spec.kind = EventSpecKind::MousePress;
        spec.keyOrButton = parseMouseButtonName(baseSpec.substr(11));
        return spec;
    }

    // mouserelease_<name>
    if (baseSpec.substr(0, 13) == "mouserelease_") {
        spec.kind = EventSpecKind::MouseRelease;
        spec.keyOrButton = parseMouseButtonName(baseSpec.substr(13));
        return spec;
    }

    // Unknown — treat as key
    spec.kind = EventSpecKind::Key;
    spec.keyOrButton = -1;
    return spec;
}

// ============================================================================
// Event matching
// ============================================================================

bool InputActionSystem::eventMatchesSpec(const RawInputEvent& event, const EventSpec& spec) const {
    // Check modifiers
    if (spec.requiredModifiers != InputModifier::None) {
        if ((event.modifiers & spec.requiredModifiers) != spec.requiredModifiers) {
            return false;
        }
    }

    switch (spec.kind) {
        case EventSpecKind::Key:
            // Key typed = press event (we fire on press for responsiveness)
            return event.type == RawEventType::KeyPress &&
                   event.keyOrButton == spec.keyOrButton;

        case EventSpecKind::KeyPress:
            return event.type == RawEventType::KeyPress &&
                   event.keyOrButton == spec.keyOrButton;

        case EventSpecKind::KeyRelease:
            return event.type == RawEventType::KeyRelease &&
                   event.keyOrButton == spec.keyOrButton;

        case EventSpecKind::Click:
            // Click = press event (fire on press for responsiveness)
            return event.type == RawEventType::MouseButtonPress &&
                   event.keyOrButton == spec.keyOrButton;

        case EventSpecKind::MousePress:
            return event.type == RawEventType::MouseButtonPress &&
                   event.keyOrButton == spec.keyOrButton;

        case EventSpecKind::MouseRelease:
            return event.type == RawEventType::MouseButtonRelease &&
                   event.keyOrButton == spec.keyOrButton;

        case EventSpecKind::Scroll:
            return event.type == RawEventType::MouseScroll;
    }

    return false;
}

// ============================================================================
// Binding file parsing
// ============================================================================

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool isIndented(const std::string& line) {
    return !line.empty() && (line[0] == ' ' || line[0] == '\t');
}

void InputActionSystem::parseBindings(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    InputContext currentCtx = InputContext::Gameplay;
    bool inContext = false;

    while (std::getline(stream, line)) {
        // Strip comments
        auto commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // Context declaration
        if (trimmed.substr(0, 8) == "context:") {
            std::string ctxName = trim(trimmed.substr(8));
            auto it = contextNames_.find(ctxName);
            if (it != contextNames_.end()) {
                currentCtx = it->second;
            }
            inContext = true;
            continue;
        }

        // Binding line (must be indented if inside a context block)
        if (inContext && isIndented(line)) {
            // Parse "event_spec: action_name [args...]"
            auto colonPos = trimmed.find(':');
            if (colonPos == std::string::npos) continue;

            std::string eventSpecStr = trim(trimmed.substr(0, colonPos));
            std::string remainder = trim(trimmed.substr(colonPos + 1));

            if (eventSpecStr.empty() || remainder.empty()) continue;

            // Split remainder into action name and arg expression
            std::string actionName;
            std::string argExpr;

            auto spacePos = remainder.find(' ');
            if (spacePos == std::string::npos) {
                actionName = remainder;
            } else {
                actionName = remainder.substr(0, spacePos);
                argExpr = trim(remainder.substr(spacePos + 1));
            }

            CompiledBinding binding;
            binding.spec = parseEventSpec(eventSpecStr);
            binding.actionName = actionName;
            binding.argExpression = argExpr;
            binding.rawEventSpec = eventSpecStr;

            bindings_[currentCtx].push_back(std::move(binding));
        }
    }
}

// ============================================================================
// InputActionSystem implementation
// ============================================================================

InputActionSystem::InputActionSystem() {
    // Register built-in context names
    contextNames_["gameplay"] = InputContext::Gameplay;
    contextNames_["menu"] = InputContext::Menu;
    contextNames_["chat"] = InputContext::Chat;
}

InputActionSystem::~InputActionSystem() = default;

void InputActionSystem::loadBindings(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return; // Silently fail — bindings are optional
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    loadBindingsFromString(content);
}

void InputActionSystem::loadBindingsFromString(const std::string& content) {
    bindings_.clear();
    parseBindings(content);
}

void InputActionSystem::setContext(InputContext ctx) {
    if (ctx == currentContext_) return;
    currentContext_ = ctx;
    // Note: active begin actions are NOT cleared here automatically.
    // The caller should call getActiveBeginActions() and dispatch end_* actions.
}

ActionResult InputActionSystem::processEvent(const RawInputEvent& event) {
    auto it = bindings_.find(currentContext_);
    if (it == bindings_.end()) return {};

    for (const auto& binding : it->second) {
        if (eventMatchesSpec(event, binding.spec)) {
            ActionResult result;
            result.actionName = binding.actionName;
            result.argExpression = binding.argExpression;
            if (event.type == RawEventType::MouseScroll) {
                result.scrollDelta = event.scrollDelta;
            }
            return result;
        }
    }

    return {};
}

void InputActionSystem::registerContext(const std::string& name) {
    if (contextNames_.find(name) != contextNames_.end()) return;

    // Assign the next available InputContext value
    // We reuse the existing enum values for built-ins and map extended names
    // to one of them. For now, extended contexts map to Menu (most common use case
    // for new contexts like "inventory").
    // TODO: If more than 3 contexts are needed, extend InputContext enum.
    if (name == "inventory") {
        contextNames_[name] = InputContext::Menu; // Inventory uses Menu context
    } else {
        // Default unknown contexts to Gameplay
        contextNames_[name] = InputContext::Gameplay;
    }
}

std::vector<InputActionSystem::BindingInfo>
InputActionSystem::getBindingsForContext(InputContext ctx) const {
    std::vector<BindingInfo> result;
    auto it = bindings_.find(ctx);
    if (it == bindings_.end()) return result;

    for (const auto& binding : it->second) {
        result.push_back({binding.rawEventSpec, binding.actionName, binding.argExpression});
    }
    return result;
}

// ============================================================================
// Begin/end action tracking
// ============================================================================

void InputActionSystem::trackBeginAction(const std::string& actionName) {
    if (actionName.substr(0, 6) == "begin_") {
        activeBeginActions_.insert(actionName);
    }
}

void InputActionSystem::trackEndAction(const std::string& actionName) {
    if (actionName.substr(0, 4) == "end_") {
        // Convert end_move_forward → begin_move_forward
        std::string beginName = "begin_" + actionName.substr(4);
        activeBeginActions_.erase(beginName);
    }
}

std::vector<std::string> InputActionSystem::getActiveBeginActions() const {
    return {activeBeginActions_.begin(), activeBeginActions_.end()};
}

void InputActionSystem::clearActiveBeginActions() {
    activeBeginActions_.clear();
}

}  // namespace finevox
