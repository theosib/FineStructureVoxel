#include <gtest/gtest.h>
#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <finescript/native_function.h>
#include <finescript/value.h>
#include <finescript/map_data.h>
#include <filesystem>

using finescript::Value;
using finescript::ExecutionContext;
using finescript::ScriptEngine;
using finescript::SimpleLambdaFunction;

static Value makeFn(std::function<Value(ExecutionContext&, const std::vector<Value>&)> fn) {
    return Value::nativeFunction(std::make_shared<SimpleLambdaFunction>(std::move(fn)));
}

// Register minimal stub UI widget builders so hotbar.fs can execute
// without the full finegui library. Each stub creates a map with :type set.
static void registerStubUiBindings(ScriptEngine& engine) {
    // Helper: create a widget map with :type field + merge keyword args
    auto makeWidget = [&](const char* typeName,
                          const std::vector<Value>& args,
                          int skipPositional) -> Value {
        auto w = Value::map();
        auto& m = w.asMap();
        m.set(engine.intern("type"), Value::string(typeName));
        for (size_t i = static_cast<size_t>(skipPositional); i < args.size(); i++) {
            if (args[i].isMap()) {
                auto& src = args[i].asMap();
                for (auto key : src.keys()) {
                    m.set(key, src.get(key));
                }
            }
        }
        return w;
    };

    auto ui = Value::map();
    auto& uiMap = ui.asMap();

    // ui.window "title" =children [...] =window_flags [...]
    uiMap.set(engine.intern("window"), makeFn(
        [&engine, makeWidget](ExecutionContext&, const std::vector<Value>& args) -> Value {
            auto w = makeWidget("window", args, 1);
            if (!args.empty() && args[0].isString()) {
                w.asMap().set(engine.intern("title"), args[0]);
            }
            return w;
        }));

    // ui.button "label" =width N =height N
    uiMap.set(engine.intern("button"), makeFn(
        [&engine, makeWidget](ExecutionContext&, const std::vector<Value>& args) -> Value {
            auto w = makeWidget("button", args, 1);
            if (!args.empty() && args[0].isString()) {
                w.asMap().set(engine.intern("label"), args[0]);
            }
            return w;
        }));

    // ui.push_color :symbol [r g b a]
    uiMap.set(engine.intern("push_color"), makeFn(
        [&engine, makeWidget](ExecutionContext&, const std::vector<Value>& args) -> Value {
            auto w = makeWidget("push_color", args, 2);
            if (args.size() > 1 && args[1].isArray()) {
                w.asMap().set(engine.intern("color"), args[1]);
            }
            return w;
        }));

    // ui.pop_color [count]
    uiMap.set(engine.intern("pop_color"), makeFn(
        [&engine, makeWidget](ExecutionContext&, const std::vector<Value>& args) -> Value {
            auto w = makeWidget("pop_color", args, 1);
            if (!args.empty() && args[0].isNumeric()) {
                w.asMap().set(engine.intern("count"), args[0]);
            }
            return w;
        }));

    // ui.same_line [offset]
    uiMap.set(engine.intern("same_line"), makeFn(
        [&engine, makeWidget](ExecutionContext&, const std::vector<Value>& args) -> Value {
            auto w = makeWidget("same_line", args, 1);
            if (!args.empty() && args[0].isNumeric()) {
                w.asMap().set(engine.intern("offset"), args[0]);
            }
            return w;
        }));

    engine.registerConstant("ui", std::move(ui));
}

static std::string findHotbarScript() {
    auto cwd = std::filesystem::current_path();
    for (auto p = cwd; p != p.parent_path(); p = p.parent_path()) {
        auto candidate = p / "resources" / "ui" / "hotbar.fs";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return "";
}

TEST(HotbarBridgeTest, ScriptDefinesHotbarOverlay) {
    auto scriptPath = findHotbarScript();
    if (scriptPath.empty()) {
        GTEST_SKIP() << "Could not find resources/ui/hotbar.fs";
    }

    ScriptEngine engine;
    registerStubUiBindings(engine);

    auto* compiled = engine.loadScript(scriptPath);
    ASSERT_NE(compiled, nullptr) << "Failed to compile hotbar.fs";

    ExecutionContext ctx(engine);
    auto result = engine.execute(*compiled, ctx);
    ASSERT_TRUE(result.success) << "Execution failed: " << result.error;

    auto hotbar = ctx.get("hotbar_overlay");
    ASSERT_TRUE(hotbar.isMap()) << "hotbar_overlay should be a map";

    uint32_t childrenSym = engine.intern("children");
    auto children = hotbar.asMap().get(childrenSym);
    ASSERT_TRUE(children.isArray()) << "hotbar_overlay should have children array";

    // Each slot = push_color + push_color + button + pop_color = 4 widgets
    // 7 same_line spacers between slots
    // Total: 8*4 + 7 = 39
    const auto& arr = children.asArray();
    EXPECT_EQ(arr.size(), 39u) << "Expected 39 child widgets (8 slots with style + 7 spacers)";
}

TEST(HotbarBridgeTest, SlotButtonsHaveIds) {
    auto scriptPath = findHotbarScript();
    if (scriptPath.empty()) {
        GTEST_SKIP() << "Could not find resources/ui/hotbar.fs";
    }

    ScriptEngine engine;
    registerStubUiBindings(engine);

    auto* compiled = engine.loadScript(scriptPath);
    ASSERT_NE(compiled, nullptr);

    ExecutionContext ctx(engine);
    engine.execute(*compiled, ctx);

    auto hotbar = ctx.get("hotbar_overlay");
    ASSERT_TRUE(hotbar.isMap());

    uint32_t childrenSym = engine.intern("children");
    uint32_t idSym = engine.intern("id");
    auto children = hotbar.asMap().get(childrenSym);
    ASSERT_TRUE(children.isArray());

    std::vector<std::string> ids;
    for (const auto& child : children.asArray()) {
        if (child.isMap()) {
            auto idVal = child.asMap().get(idSym);
            if (idVal.isString()) {
                ids.push_back(std::string(idVal.asString()));
            }
        }
    }

    for (int i = 0; i < 8; i++) {
        std::string expected = "slot_" + std::to_string(i);
        EXPECT_NE(std::find(ids.begin(), ids.end(), expected), ids.end())
            << "Missing button ID: " << expected;
    }

    for (int i = 0; i < 8; i++) {
        std::string btnCol = "s" + std::to_string(i) + "_btn_col";
        EXPECT_NE(std::find(ids.begin(), ids.end(), btnCol), ids.end())
            << "Missing color ID: " << btnCol;
    }
}

TEST(HotbarBridgeTest, NativeFunctionPattern) {
    ScriptEngine engine;

    int selectedSlot = 0;
    std::vector<std::string> palette = {"stone", "dirt", "grass"};

    engine.registerFunction("get_hotbar_slots",
        [&](ExecutionContext&, const std::vector<Value>&) -> Value {
            std::vector<Value> slots;
            for (const auto& name : palette) {
                slots.push_back(Value::string(name));
            }
            return Value::array(std::move(slots));
        });

    engine.registerFunction("get_selected_slot",
        [&](ExecutionContext&, const std::vector<Value>&) -> Value {
            return Value::integer(selectedSlot);
        });

    engine.registerFunction("set_selected_slot",
        [&](ExecutionContext&, const std::vector<Value>& args) -> Value {
            if (!args.empty() && args[0].isNumeric()) {
                selectedSlot = static_cast<int>(args[0].asNumber());
            }
            return Value::nil();
        });

    ExecutionContext ctx(engine);

    auto slotsResult = engine.executeCommand("get_hotbar_slots", ctx);
    ASSERT_TRUE(slotsResult.success) << slotsResult.error;
    ASSERT_TRUE(slotsResult.returnValue.isArray());
    EXPECT_EQ(slotsResult.returnValue.asArray().size(), 3u);
    EXPECT_EQ(slotsResult.returnValue.asArray()[0].asString(), "stone");

    auto getResult = engine.executeCommand("get_selected_slot", ctx);
    ASSERT_TRUE(getResult.success);
    EXPECT_EQ(static_cast<int>(getResult.returnValue.asNumber()), 0);

    auto setResult = engine.executeCommand("set_selected_slot 2", ctx);
    ASSERT_TRUE(setResult.success);
    EXPECT_EQ(selectedSlot, 2);

    auto getResult2 = engine.executeCommand("get_selected_slot", ctx);
    ASSERT_TRUE(getResult2.success);
    EXPECT_EQ(static_cast<int>(getResult2.returnValue.asNumber()), 2);
}
