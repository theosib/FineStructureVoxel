#include "finevox/core/key_bindings.hpp"
#include "finevox/core/config.hpp"

namespace finevox {

// GLFW key code constants (stable across versions, core can't include GLFW)
static constexpr int KEY_W = 87;
static constexpr int KEY_S = 83;
static constexpr int KEY_A = 65;
static constexpr int KEY_D = 68;
static constexpr int KEY_SPACE = 32;
static constexpr int KEY_LEFT_SHIFT = 340;
static constexpr int MOUSE_BUTTON_LEFT = 0;
static constexpr int MOUSE_BUTTON_RIGHT = 1;

std::vector<KeyBinding> getDefaultKeyBindings() {
    return {
        {"forward", KEY_W,           false},
        {"back",    KEY_S,           false},
        {"left",    KEY_A,           false},
        {"right",   KEY_D,           false},
        {"up",      KEY_SPACE,       false},
        {"down",    KEY_LEFT_SHIFT,  false},
        {"break",   MOUSE_BUTTON_LEFT,  true},
        {"place",   MOUSE_BUTTON_RIGHT, true},
    };
}

std::string bindingConfigKey(const std::string& action) {
    return "input.bind." + action;
}

std::vector<KeyBinding> loadKeyBindings() {
    auto bindings = getDefaultKeyBindings();
    auto& config = ConfigManager::instance();

    if (!config.isInitialized()) {
        return bindings;
    }

    for (auto& binding : bindings) {
        auto key = bindingConfigKey(binding.action);
        auto val = config.get<int64_t>(key);
        if (val.has_value()) {
            binding.keyCode = static_cast<int>(*val);
        }
        auto mouseVal = config.get<bool>(key + ".mouse");
        if (mouseVal.has_value()) {
            binding.isMouse = *mouseVal;
        }
    }
    return bindings;
}

void saveKeyBindings(const std::vector<KeyBinding>& bindings) {
    auto& config = ConfigManager::instance();
    for (const auto& b : bindings) {
        config.set(bindingConfigKey(b.action), static_cast<int64_t>(b.keyCode));
        config.set(bindingConfigKey(b.action) + ".mouse", b.isMouse);
    }
}

}  // namespace finevox
