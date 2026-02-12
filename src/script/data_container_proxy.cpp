#include "finevox/script/data_container_proxy.hpp"

namespace finevox::script {

DataContainerProxy::DataContainerProxy(DataContainer& container)
    : container_(container) {}

finescript::Value DataContainerProxy::get(uint32_t key) const {
    const DataValue* raw = container_.getRaw(key);
    if (!raw) {
        return finescript::Value::nil();
    }

    return std::visit([](const auto& val) -> finescript::Value {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return finescript::Value::nil();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return finescript::Value::integer(val);
        } else if constexpr (std::is_same_v<T, double>) {
            return finescript::Value::number(val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return finescript::Value::string(val);
        } else if constexpr (std::is_same_v<T, InternedString>) {
            return finescript::Value::symbol(val.id);
        } else {
            // Binary blobs, nested containers, typed arrays — not directly
            // representable in finescript. Return nil for now.
            return finescript::Value::nil();
        }
    }, *raw);
}

void DataContainerProxy::set(uint32_t key, finescript::Value value) {
    switch (value.type()) {
        case finescript::Value::Type::Nil:
            container_.remove(key);
            break;
        case finescript::Value::Type::Bool:
            container_.set<bool>(key, value.asBool());
            break;
        case finescript::Value::Type::Int:
            container_.set<int64_t>(key, value.asInt());
            break;
        case finescript::Value::Type::Float:
            container_.set<double>(key, value.asFloat());
            break;
        case finescript::Value::Type::String:
            container_.set<std::string>(key, value.asString());
            break;
        case finescript::Value::Type::Symbol:
            container_.set<InternedString>(key, InternedString(value.asSymbol()));
            break;
        default:
            // Arrays, maps, closures, native functions — not storable
            break;
    }
}

bool DataContainerProxy::has(uint32_t key) const {
    return container_.has(key);
}

bool DataContainerProxy::remove(uint32_t key) {
    if (container_.has(key)) {
        container_.remove(key);
        return true;
    }
    return false;
}

std::vector<uint32_t> DataContainerProxy::keys() const {
    std::vector<uint32_t> result;
    container_.forEach([&result](DataKey key, const DataValue&) {
        result.push_back(key);
    });
    return result;
}

}  // namespace finevox::script
