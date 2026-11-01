#include "finevox/net/protocol.hpp"

#include "finevox/core/string_interner.hpp"
#include "finevox/script/finevox_interner.hpp"

#include <finescript/cbor.h>
#include <finescript/map_data.h>

namespace finevox::net {

using finescript::Value;

namespace {

script::FineVoxInterner& interner() {
    static script::FineVoxInterner instance;
    return instance;
}

}  // namespace

finenet::ConnectionConfig connectionConfig() {
    using finenet::ChannelConfig;
    using finenet::Priority;

    auto channel = [](finenet::ChannelId id, std::string_view name, Priority priority) {
        ChannelConfig config;
        config.id = id;
        config.name = name;
        config.defaultPriority = priority;
        return config;
    };

    finenet::ConnectionConfig config;
    config.transport = finenet::TransportMode::TcpOnly;
    config.keepAliveIntervalMs = 1000;
    config.connectionTimeoutMs = 10000;
    config.channels = {
        channel(CH_CONTROL, "control", Priority::Critical),
        channel(CH_INPUT, "input", Priority::High),
        channel(CH_ACTIONS, "actions", Priority::High),
        channel(CH_ENTITIES, "entities", Priority::Normal),
        channel(CH_WORLD, "world", Priority::Low),
        channel(CH_UI, "ui", Priority::High),
        channel(CH_ASSETS, "assets", Priority::Bulk),
        channel(CH_CHAT, "chat", Priority::Normal),
    };
    return config;
}

std::vector<uint8_t> encodeBody(const Value& value) {
    return finescript::cborEncode(value, interner());
}

Value decodeBody(std::span<const uint8_t> body) {
    try {
        return finescript::cborDecode(body.data(), body.size(), interner());
    } catch (const std::exception&) {
        return Value::nil();
    }
}

uint32_t key(std::string_view name) {
    return StringInterner::global().intern(name);
}

Value field(const Value& map, std::string_view name) {
    if (!map.isMap()) return Value::nil();
    return const_cast<Value&>(map).asMap().get(key(name));
}

std::string textField(const Value& map, std::string_view name) {
    Value value = field(map, name);
    if (value.isString()) return std::string(value.asString());
    if (value.isSymbol()) return std::string(StringInterner::global().lookup(value.asSymbol()));
    return {};
}

int64_t intField(const Value& map, std::string_view name, int64_t fallback) {
    Value value = field(map, name);
    return value.isNumeric() ? static_cast<int64_t>(value.asNumber()) : fallback;
}

double numberField(const Value& map, std::string_view name, double fallback) {
    Value value = field(map, name);
    return value.isNumeric() ? value.asNumber() : fallback;
}

bool boolField(const Value& map, std::string_view name, bool fallback) {
    Value value = field(map, name);
    return value.isNil() ? fallback : value.truthy();
}

void setField(Value& map, std::string_view name, Value value) {
    map.asMap().set(key(name), std::move(value));
}

Value symbol(std::string_view name) {
    return Value::symbol(key(name));
}

}  // namespace finevox::net
