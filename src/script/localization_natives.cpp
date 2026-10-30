#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/label_registry.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/map_data.h>
#include <finescript/script_engine.h>

namespace finevox::script {

void registerLocalizationNatives(finescript::ScriptEngine& engine) {
    using finescript::ExecutionContext;
    using finescript::Value;
    const uint32_t keyCount = engine.intern("count");

    // loc "ui.key" [=count n] [=name value ...] -> localized string
    engine.registerFunction("loc",
        [keyCount](ExecutionContext&, const std::vector<Value>& args) -> Value {
            if (args.empty() || !args[0].isString()) return Value::nil();

            std::vector<LabelRegistry::Param> params;
            int64_t count = 0;
            bool hasCount = false;
            if (args.size() > 1 && args.back().isMap()) {
                auto& named = const_cast<Value&>(args.back()).asMap();
                for (uint32_t key : named.keys()) {
                    Value value = named.get(key);
                    if (key == keyCount && value.isNumeric()) {
                        count = static_cast<int64_t>(value.asNumber());
                        hasCount = true;
                        continue;
                    }
                    params.push_back({std::string(StringInterner::global().lookup(key)),
                                      value.isString() ? value.asString() : value.toString()});
                }
            }
            return Value::string(LabelRegistry::global().formatLocalized(
                args[0].asString(), params, hasCount ? &count : nullptr));
        });

    engine.registerFunction("loc_set_locale",
        [](ExecutionContext&, const std::vector<Value>& args) -> Value {
            if (!args.empty() && args[0].isString()) {
                LabelRegistry::global().setLocale(args[0].asString());
            }
            return Value::nil();
        });

    engine.registerFunction("loc_locale", [](ExecutionContext&, const std::vector<Value>&) -> Value {
        return Value::string(LabelRegistry::global().locale());
    });
}

}  // namespace finevox::script
