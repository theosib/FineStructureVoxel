#pragma once

/**
 * @file input_context.hpp
 * @brief Input routing context for switching between gameplay, menu, and chat
 *
 * Design: [10-input.md] §10.1 InputManager
 */

#include <cstdint>

namespace finevox {

/// Input routing context — determines which systems receive input
enum class InputContext : uint8_t {
    Gameplay,  ///< Normal gameplay: movement, camera, block interaction
    Menu,      ///< Menu/inventory open: mouse visible, no camera movement
    Chat       ///< Chat open: keyboard goes to text entry, no movement
};

}  // namespace finevox
