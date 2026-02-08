#pragma once

/**
 * @file key_bindings.hpp
 * @brief Key binding persistence via ConfigManager
 *
 * Stores action->keycode mappings in ConfigManager as integer values.
 * Key codes are GLFW key codes (platform-neutral integers).
 * Core cannot include GLFW headers, so constants are stored as raw ints.
 */

#include <string>
#include <vector>

namespace finevox {

/// A single key binding: action name -> key code
struct KeyBinding {
    std::string action;
    int keyCode;          ///< GLFW key code (e.g., 87 = GLFW_KEY_W)
    bool isMouse = false; ///< True if this is a mouse button binding
};

/// Default key bindings (hardcoded fallback)
std::vector<KeyBinding> getDefaultKeyBindings();

/// Load key bindings from ConfigManager.
/// Returns defaults for any action not found in config.
std::vector<KeyBinding> loadKeyBindings();

/// Save key bindings to ConfigManager.
void saveKeyBindings(const std::vector<KeyBinding>& bindings);

/// Config key for an action (e.g., "forward" -> "input.bind.forward")
std::string bindingConfigKey(const std::string& action);

}  // namespace finevox
