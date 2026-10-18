#include "finevox/script/data_container_proxy.hpp"
#include "finevox/core/block_handler.hpp"
#include "finevox/core/string_interner.hpp"
#include <finescript/map_data.h>

namespace finevox::script {

DataContainerProxy::DataContainerProxy(DataContainer& container)
    : container_(&container), blockCtx_(nullptr) {}

DataContainerProxy::DataContainerProxy(BlockContext& blockCtx)
    : container_(nullptr), blockCtx_(&blockCtx) {}

const DataContainer* DataContainerProxy::readContainer() const {
    if (container_) return container_;
    if (blockCtx_) return blockCtx_->data();
    return nullptr;
}

DataContainer& DataContainerProxy::writeContainer() {
    if (!container_) {
        container_ = &blockCtx_->getOrCreateData();
    }
    return *container_;
}

finescript::Value DataContainerProxy::get(uint32_t key) const {
    const DataContainer* c = readContainer();
    if (!c) {
        return finescript::Value::nil();
    }

    const DataValue* raw = c->getRaw(key);
    if (!raw) {
        return finescript::Value::nil();
    }

    return std::visit([](const auto& val) -> finescript::Value {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return finescript::Value::nil();
        } else if constexpr (std::is_same_v<T, bool>) {
            return finescript::Value::boolean(val);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return finescript::Value::integer(val);
        } else if constexpr (std::is_same_v<T, double>) {
            return finescript::Value::number(val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return finescript::Value::string(val);
        } else if constexpr (std::is_same_v<T, InternedString>) {
            return finescript::Value::symbol(val.id);
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            std::vector<finescript::Value> arr;
            arr.reserve(val.size());
            for (int64_t item : val) {
                arr.push_back(finescript::Value::integer(item));
            }
            return finescript::Value::array(std::move(arr));
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            std::vector<finescript::Value> arr;
            arr.reserve(val.size());
            for (double item : val) {
                arr.push_back(finescript::Value::number(item));
            }
            return finescript::Value::array(std::move(arr));
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            std::vector<finescript::Value> arr;
            arr.reserve(val.size());
            for (const std::string& item : val) {
                arr.push_back(finescript::Value::string(item));
            }
            return finescript::Value::array(std::move(arr));
        } else if constexpr (std::is_same_v<T, std::unique_ptr<DataContainer>>) {
            if (!val) {
                return finescript::Value::nil();
            }
            return finescript::Value::proxyMap(std::make_shared<DataContainerProxy>(*val));
        } else {
            return finescript::Value::nil();
        }
    }, *raw);
}

void DataContainerProxy::set(uint32_t key, finescript::Value value) {
    if (value.isNil()) {
        if (auto* c = const_cast<DataContainer*>(readContainer())) {
            c->remove(key);
        }
        return;
    }

    DataContainer& c = writeContainer();

    switch (value.type()) {
        case finescript::Value::Type::Nil:
            c.remove(key);
            break;
        case finescript::Value::Type::Bool:
            c.set<bool>(key, value.asBool());
            break;
        case finescript::Value::Type::Int:
            c.set<int64_t>(key, value.asInt());
            break;
        case finescript::Value::Type::Float:
            c.set<double>(key, value.asFloat());
            break;
        case finescript::Value::Type::String:
            c.set<std::string>(key, std::string(value.asString()));
            break;
        case finescript::Value::Type::Symbol:
            c.set<InternedString>(key, InternedString(value.asSymbol()));
            break;
        case finescript::Value::Type::Array: {
            const auto& arr = value.asArray();
            if (arr.empty()) {
                c.set<std::vector<int64_t>>(key, {});
                break;
            }
            bool allNumeric = true;
            bool hasFloat = false;
            bool allStringOrSym = true;
            for (const auto& elem : arr) {
                if (elem.isFloat()) {
                    hasFloat = true;
                    allStringOrSym = false;
                } else if (elem.isInt()) {
                    allStringOrSym = false;
                } else if (elem.isString() || elem.isSymbol()) {
                    allNumeric = false;
                } else {
                    allNumeric = false;
                    allStringOrSym = false;
                }
            }
            if (allNumeric && hasFloat) {
                std::vector<double> out;
                out.reserve(arr.size());
                for (const auto& elem : arr) {
                    out.push_back(elem.asNumber());
                }
                c.set<std::vector<double>>(key, std::move(out));
            } else if (allNumeric) {
                std::vector<int64_t> out;
                out.reserve(arr.size());
                for (const auto& elem : arr) {
                    out.push_back(elem.asInt());
                }
                c.set<std::vector<int64_t>>(key, std::move(out));
            } else if (allStringOrSym) {
                std::vector<std::string> out;
                out.reserve(arr.size());
                for (const auto& elem : arr) {
                    if (elem.isString()) {
                        out.emplace_back(elem.asString());
                    } else {
                        out.emplace_back(StringInterner::global().lookup(elem.asSymbol()));
                    }
                }
                c.set<std::vector<std::string>>(key, std::move(out));
            }
            break;
        }
        case finescript::Value::Type::Map: {
            auto child = std::make_unique<DataContainer>();
            DataContainerProxy childProxy(*child);
            const auto& map = value.asMap();
            for (uint32_t k : map.keys()) {
                childProxy.set(k, map.get(k));
            }
            c.set(key, std::move(child));
            break;
        }
        default:
            break;
    }
}

bool DataContainerProxy::has(uint32_t key) const {
    const DataContainer* c = readContainer();
    return c ? c->has(key) : false;
}

bool DataContainerProxy::remove(uint32_t key) {
    auto* c = const_cast<DataContainer*>(readContainer());
    if (c && c->has(key)) {
        c->remove(key);
        return true;
    }
    return false;
}

std::vector<uint32_t> DataContainerProxy::keys() const {
    std::vector<uint32_t> result;
    const DataContainer* c = readContainer();
    if (!c) return result;
    c->forEach([&result](DataKey key, const DataValue&) {
        result.push_back(key);
    });
    return result;
}

}  // namespace finevox::script
