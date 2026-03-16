#include <gtest/gtest.h>
#include <filesystem>

#include "finevox/core/input_action_system.hpp"
#include "finevox/core/action_dispatch.hpp"

using namespace finevox;

// ============================================================================
// Key name table
// ============================================================================

TEST(InputActionSystemTest, KeyNameToCode) {
    EXPECT_EQ(InputActionSystem::keyNameToCode("w"), 87);
    EXPECT_EQ(InputActionSystem::keyNameToCode("space"), 32);
    EXPECT_EQ(InputActionSystem::keyNameToCode("escape"), 256);
    EXPECT_EQ(InputActionSystem::keyNameToCode("1"), 49);
    EXPECT_EQ(InputActionSystem::keyNameToCode("f1"), 290);
    EXPECT_EQ(InputActionSystem::keyNameToCode("unknown_key"), -1);
}

TEST(InputActionSystemTest, KeyCodeToName) {
    EXPECT_EQ(InputActionSystem::keyCodeToName(87), "w");
    EXPECT_EQ(InputActionSystem::keyCodeToName(32), "space");
    EXPECT_EQ(InputActionSystem::keyCodeToName(256), "escape");
}

// ============================================================================
// Event spec parsing
// ============================================================================

TEST(InputActionSystemTest, ParseSimpleKeyBinding) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
)");

    auto bindings = sys.getBindingsForContext(InputContext::Gameplay);
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].eventSpec, "key_e");
    EXPECT_EQ(bindings[0].actionName, "open_inventory");
    EXPECT_TRUE(bindings[0].argExpression.empty());
}

TEST(InputActionSystemTest, ParseBindingWithArgs) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_1: select_hotbar_slot 0
  key_2: select_hotbar_slot 1
)");

    auto bindings = sys.getBindingsForContext(InputContext::Gameplay);
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings[0].actionName, "select_hotbar_slot");
    EXPECT_EQ(bindings[0].argExpression, "0");
    EXPECT_EQ(bindings[1].argExpression, "1");
}

TEST(InputActionSystemTest, ParseModifierBinding) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  mod_shift+click_left: secondary_action {=shift true}
)");

    auto bindings = sys.getBindingsForContext(InputContext::Gameplay);
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].actionName, "secondary_action");
    EXPECT_EQ(bindings[0].argExpression, "{=shift true}");
}

TEST(InputActionSystemTest, ParseMultipleContexts) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
  key_escape: open_menu

context: menu
  key_escape: close_menu

context: chat
  key_escape: close_chat
  key_enter: submit_chat
)");

    EXPECT_EQ(sys.getBindingsForContext(InputContext::Gameplay).size(), 2u);
    EXPECT_EQ(sys.getBindingsForContext(InputContext::Menu).size(), 1u);
    EXPECT_EQ(sys.getBindingsForContext(InputContext::Chat).size(), 2u);
}

TEST(InputActionSystemTest, CommentsAndBlankLinesIgnored) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
# This is a comment
context: gameplay
  # Another comment
  key_w: move_forward

  key_s: move_back
)");

    auto bindings = sys.getBindingsForContext(InputContext::Gameplay);
    ASSERT_EQ(bindings.size(), 2u);
}

// ============================================================================
// Event matching
// ============================================================================

TEST(InputActionSystemTest, MatchKeyPress) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
)");

    RawInputEvent event;
    event.type = RawEventType::KeyPress;
    event.keyOrButton = InputActionSystem::keyNameToCode("e");

    auto result = sys.processEvent(event);
    EXPECT_TRUE(result.matched());
    EXPECT_EQ(result.actionName, "open_inventory");
}

TEST(InputActionSystemTest, NoMatchWrongKey) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
)");

    RawInputEvent event;
    event.type = RawEventType::KeyPress;
    event.keyOrButton = InputActionSystem::keyNameToCode("w");

    auto result = sys.processEvent(event);
    EXPECT_FALSE(result.matched());
}

TEST(InputActionSystemTest, NoMatchKeyRelease_ForKeyBinding) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
)");

    // key_ bindings fire on press, not release
    RawInputEvent event;
    event.type = RawEventType::KeyRelease;
    event.keyOrButton = InputActionSystem::keyNameToCode("e");

    auto result = sys.processEvent(event);
    EXPECT_FALSE(result.matched());
}

TEST(InputActionSystemTest, MatchKeyPressAndRelease) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  keypress_w: begin_move_forward
  keyrelease_w: end_move_forward
)");

    int wCode = InputActionSystem::keyNameToCode("w");

    RawInputEvent press;
    press.type = RawEventType::KeyPress;
    press.keyOrButton = wCode;

    auto r1 = sys.processEvent(press);
    EXPECT_TRUE(r1.matched());
    EXPECT_EQ(r1.actionName, "begin_move_forward");

    RawInputEvent release;
    release.type = RawEventType::KeyRelease;
    release.keyOrButton = wCode;

    auto r2 = sys.processEvent(release);
    EXPECT_TRUE(r2.matched());
    EXPECT_EQ(r2.actionName, "end_move_forward");
}

TEST(InputActionSystemTest, MatchMouseClick) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  click_left: primary_action
  click_right: secondary_action
)");

    RawInputEvent leftPress;
    leftPress.type = RawEventType::MouseButtonPress;
    leftPress.keyOrButton = 0; // left

    auto r1 = sys.processEvent(leftPress);
    EXPECT_TRUE(r1.matched());
    EXPECT_EQ(r1.actionName, "primary_action");

    RawInputEvent rightPress;
    rightPress.type = RawEventType::MouseButtonPress;
    rightPress.keyOrButton = 1; // right

    auto r2 = sys.processEvent(rightPress);
    EXPECT_TRUE(r2.matched());
    EXPECT_EQ(r2.actionName, "secondary_action");
}

TEST(InputActionSystemTest, MatchModifier) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  click_left: primary_action
  mod_shift+click_left: quick_action
)");

    // Without shift → primary_action (first match wins)
    RawInputEvent noMod;
    noMod.type = RawEventType::MouseButtonPress;
    noMod.keyOrButton = 0;
    noMod.modifiers = InputModifier::None;

    auto r1 = sys.processEvent(noMod);
    EXPECT_TRUE(r1.matched());
    EXPECT_EQ(r1.actionName, "primary_action");

    // With shift → quick_action (modifier binding should be checked first)
    // Note: bindings are checked in order. Put modifier bindings before
    // non-modifier ones in the config to ensure correct priority.
    InputActionSystem sys2;
    sys2.loadBindingsFromString(R"(
context: gameplay
  mod_shift+click_left: quick_action
  click_left: primary_action
)");

    RawInputEvent withShift;
    withShift.type = RawEventType::MouseButtonPress;
    withShift.keyOrButton = 0;
    withShift.modifiers = InputModifier::Shift;

    auto r2 = sys2.processEvent(withShift);
    EXPECT_TRUE(r2.matched());
    EXPECT_EQ(r2.actionName, "quick_action");
}

TEST(InputActionSystemTest, MatchScroll) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  scroll: scroll_hotbar
)");

    RawInputEvent scroll;
    scroll.type = RawEventType::MouseScroll;
    scroll.scrollDelta = 1.0f;

    auto result = sys.processEvent(scroll);
    EXPECT_TRUE(result.matched());
    EXPECT_EQ(result.actionName, "scroll_hotbar");
    EXPECT_FLOAT_EQ(result.scrollDelta, 1.0f);
}

// ============================================================================
// Context switching
// ============================================================================

TEST(InputActionSystemTest, ContextSwitchChangesBindings) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
  key_escape: open_menu

context: menu
  key_escape: close_menu
)");

    int escCode = InputActionSystem::keyNameToCode("escape");
    RawInputEvent esc;
    esc.type = RawEventType::KeyPress;
    esc.keyOrButton = escCode;

    // In gameplay context
    auto r1 = sys.processEvent(esc);
    EXPECT_EQ(r1.actionName, "open_menu");

    // Switch to menu
    sys.setContext(InputContext::Menu);
    auto r2 = sys.processEvent(esc);
    EXPECT_EQ(r2.actionName, "close_menu");
}

TEST(InputActionSystemTest, NoBindingsForContext) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
)");

    sys.setContext(InputContext::Chat);

    RawInputEvent event;
    event.type = RawEventType::KeyPress;
    event.keyOrButton = InputActionSystem::keyNameToCode("e");

    auto result = sys.processEvent(event);
    EXPECT_FALSE(result.matched());
}

// ============================================================================
// Begin/end action tracking
// ============================================================================

TEST(InputActionSystemTest, TrackBeginEndActions) {
    InputActionSystem sys;

    sys.trackBeginAction("begin_move_forward");
    sys.trackBeginAction("begin_move_left");
    EXPECT_EQ(sys.getActiveBeginActions().size(), 2u);

    sys.trackEndAction("end_move_forward");
    EXPECT_EQ(sys.getActiveBeginActions().size(), 1u);

    sys.clearActiveBeginActions();
    EXPECT_TRUE(sys.getActiveBeginActions().empty());
}

TEST(InputActionSystemTest, NonBeginActionsNotTracked) {
    InputActionSystem sys;

    sys.trackBeginAction("open_inventory");
    EXPECT_TRUE(sys.getActiveBeginActions().empty());
}

// ============================================================================
// ActionDispatch
// ============================================================================

TEST(ActionDispatchTest, RegisterAndDispatch) {
    ActionDispatch dispatch;
    std::string received;

    dispatch.registerAction("test_action", [&](const ActionArgs& args) {
        received = "dispatched:" + args.argExpression;
    });

    ActionArgs args;
    args.argExpression = "42";
    EXPECT_TRUE(dispatch.dispatch("test_action", args));
    EXPECT_EQ(received, "dispatched:42");
}

TEST(ActionDispatchTest, DispatchUnknownAction) {
    ActionDispatch dispatch;
    EXPECT_FALSE(dispatch.dispatch("nonexistent"));
}

TEST(ActionDispatchTest, UnregisterAction) {
    ActionDispatch dispatch;
    dispatch.registerAction("temp", [](const ActionArgs&) {});
    EXPECT_TRUE(dispatch.hasAction("temp"));

    dispatch.unregisterAction("temp");
    EXPECT_FALSE(dispatch.hasAction("temp"));
    EXPECT_FALSE(dispatch.dispatch("temp"));
}

TEST(ActionDispatchTest, DispatchNoArgs) {
    ActionDispatch dispatch;
    bool called = false;

    dispatch.registerAction("simple", [&](const ActionArgs& args) {
        called = true;
        EXPECT_TRUE(args.argExpression.empty());
    });

    EXPECT_TRUE(dispatch.dispatch("simple"));
    EXPECT_TRUE(called);
}

TEST(ActionDispatchTest, ScrollDeltaPassedThrough) {
    ActionDispatch dispatch;
    float receivedDelta = 0.0f;

    dispatch.registerAction("scroll_test", [&](const ActionArgs& args) {
        receivedDelta = args.scrollDelta;
    });

    ActionArgs args;
    args.scrollDelta = -3.5f;
    dispatch.dispatch("scroll_test", args);
    EXPECT_FLOAT_EQ(receivedDelta, -3.5f);
}

// ============================================================================
// Integration: process event → dispatch
// ============================================================================

TEST(InputActionIntegrationTest, EventToDispatch) {
    InputActionSystem sys;
    sys.loadBindingsFromString(R"(
context: gameplay
  key_e: open_inventory
  key_1: select_slot 0
)");

    ActionDispatch dispatch;
    std::string lastAction;
    std::string lastArgs;

    dispatch.registerAction("open_inventory", [&](const ActionArgs& args) {
        lastAction = "open_inventory";
        lastArgs = args.argExpression;
    });
    dispatch.registerAction("select_slot", [&](const ActionArgs& args) {
        lastAction = "select_slot";
        lastArgs = args.argExpression;
    });

    // Simulate pressing 'e'
    RawInputEvent event;
    event.type = RawEventType::KeyPress;
    event.keyOrButton = InputActionSystem::keyNameToCode("e");

    auto result = sys.processEvent(event);
    ASSERT_TRUE(result.matched());

    ActionArgs dArgs;
    dArgs.argExpression = result.argExpression;
    dispatch.dispatch(result.actionName, dArgs);

    EXPECT_EQ(lastAction, "open_inventory");
    EXPECT_TRUE(lastArgs.empty());

    // Simulate pressing '1'
    event.keyOrButton = InputActionSystem::keyNameToCode("1");
    result = sys.processEvent(event);
    ASSERT_TRUE(result.matched());

    dArgs.argExpression = result.argExpression;
    dispatch.dispatch(result.actionName, dArgs);

    EXPECT_EQ(lastAction, "select_slot");
    EXPECT_EQ(lastArgs, "0");
}

// ============================================================================
// Loading from default.bindings file
// ============================================================================

TEST(InputActionSystemTest, LoadDefaultBindingsFile) {
    // Construct absolute path from test source file location
    std::filesystem::path testDir = std::filesystem::path(__FILE__).parent_path();
    std::filesystem::path projectRoot = testDir.parent_path();
    std::filesystem::path bindingsPath = projectRoot / "resources" / "input" / "default.bindings";

    InputActionSystem sys;
    sys.loadBindings(bindingsPath.string());

    auto bindings = sys.getBindingsForContext(InputContext::Gameplay);
    // Should have loaded many bindings
    EXPECT_GT(bindings.size(), 10u);

    // Check a specific one
    bool found = false;
    for (auto& b : bindings) {
        if (b.actionName == "open_inventory" && b.eventSpec == "key_e") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// Inventory context (mapped to Menu)
// ============================================================================

TEST(InputActionSystemTest, InventoryContextMappedToMenu) {
    InputActionSystem sys;
    sys.registerContext("inventory");
    sys.loadBindingsFromString(R"(
context: inventory
  key_e: close_inventory
  click_left: inventory_interact :left
)");

    // Inventory context maps to Menu
    sys.setContext(InputContext::Menu);

    RawInputEvent event;
    event.type = RawEventType::KeyPress;
    event.keyOrButton = InputActionSystem::keyNameToCode("e");

    auto result = sys.processEvent(event);
    EXPECT_TRUE(result.matched());
    EXPECT_EQ(result.actionName, "close_inventory");
}
