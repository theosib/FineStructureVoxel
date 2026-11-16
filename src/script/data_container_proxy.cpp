#include "finevox/script/data_container_proxy.hpp"
#include "finevox/core/block_handler.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"
#include <finescript/map_data.h>

namespace finevox::script {

DataContainerProxy::DataContainerProxy(DataContainer& container) : container_(&container) {}

DataContainerProxy::DataContainerProxy(World& world, BlockCoord cell, std::vector<DataKey> path)
    : world_(&world), cell_(cell), path_(std::move(path)) {}

DataContainerProxy::DataContainerProxy(BlockContext& blockCtx)
    : DataContainerProxy(blockCtx.world(), blockCtx.pos()) {}

const DataContainer* DataContainerProxy::find() const {
    if (container_) return container_;
    if (!world_) return nullptr;
    const SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_));
    if (!sub) return nullptr;
    auto local = cell_.local();
    const DataContainer* data = sub->blockData(local.x, local.y, local.z);
    for (DataKey key : path_) {
        if (!data) return nullptr;
        data = data->getChild(key);
    }
    return data;
}

DataContainer* DataContainerProxy::findForWrite() {
    if (container_) return container_;
    if (!world_) return nullptr;
    if (!path_.empty()) return const_cast<DataContainer*>(find());
    SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_));
    if (!sub) return nullptr;
    auto local = cell_.local();
    return &sub->getOrCreateBlockData(local.x, local.y, local.z);
}

void DataContainerProxy::changed() {
    if (world_) world_->noteBlockDataChanged(cell_);
}

finescript::Value DataContainerProxy::copyOut(const DataContainer& container) {
    finescript::Value out = finescript::Value::map();
    container.forEach([&out](DataKey key, const DataValue& value) {
        out.asMap().set(key, copyOut(value));
    });
    return out;
}

finescript::Value DataContainerProxy::copyOut(const DataValue& value) {
    return std::visit([](const auto& val) -> finescript::Value {
        using T = std::decay_t<decltype(val)>;
        using finescript::Value;
        if constexpr (std::is_same_v<T, bool>) {
            return Value::boolean(val);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return Value::integer(val);
        } else if constexpr (std::is_same_v<T, double>) {
            return Value::number(val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return Value::string(val);
        } else if constexpr (std::is_same_v<T, InternedString>) {
            return Value::symbol(val.id);
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            std::vector<Value> arr;
            arr.reserve(val.size());
            for (int64_t item : val) arr.push_back(Value::integer(item));
            return Value::array(std::move(arr));
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            std::vector<Value> arr;
            arr.reserve(val.size());
            for (double item : val) arr.push_back(Value::number(item));
            return Value::array(std::move(arr));
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            std::vector<Value> arr;
            arr.reserve(val.size());
            for (const std::string& item : val) arr.push_back(Value::string(item));
            return Value::array(std::move(arr));
        } else if constexpr (std::is_same_v<T, std::unique_ptr<DataContainer>>) {
            return val ? copyOut(*val) : Value::nil();
        } else if constexpr (std::is_same_v<T, std::unique_ptr<DataList>>) {
            if (!val) return Value::nil();
            std::vector<Value> arr;
            arr.reserve(val->items.size());
            for (const DataValue& item : val->items) arr.push_back(copyOut(item));
            return Value::array(std::move(arr));
        } else {
            return Value::nil();  // monostate, byte strings
        }
    }, value);
}

std::optional<DataValue> DataContainerProxy::toData(const finescript::Value& value) {
    using Type = finescript::Value::Type;
    switch (value.type()) {
        case Type::Nil:
            return DataValue{std::monostate{}};
        case Type::Bool:
            return DataValue{value.asBool()};
        case Type::Int:
            return DataValue{value.asInt()};
        case Type::Float:
            return DataValue{value.asFloat()};
        case Type::String:
            return DataValue{std::string(value.asString())};
        case Type::Symbol:
            return DataValue{InternedString(value.asSymbol())};
        case Type::Array: {
            const auto& arr = value.asArray();
            if (arr.empty()) return DataValue{std::vector<int64_t>{}};
            bool allInt = true, allNumeric = true, allString = true;
            for (const auto& elem : arr) {
                allInt = allInt && elem.isInt();
                allNumeric = allNumeric && elem.isNumeric();
                allString = allString && elem.isString();
            }
            if (allInt) {
                std::vector<int64_t> out;
                out.reserve(arr.size());
                for (const auto& elem : arr) out.push_back(elem.asInt());
                return DataValue{std::move(out)};
            }
            if (allNumeric) {
                std::vector<double> out;
                out.reserve(arr.size());
                for (const auto& elem : arr) out.push_back(elem.asNumber());
                return DataValue{std::move(out)};
            }
            if (allString) {
                std::vector<std::string> out;
                out.reserve(arr.size());
                for (const auto& elem : arr) out.emplace_back(elem.asString());
                return DataValue{std::move(out)};
            }
            auto list = std::make_unique<DataList>();
            list->items.reserve(arr.size());
            for (const auto& elem : arr) {
                auto item = toData(elem);
                list->items.push_back(item ? std::move(*item) : DataValue{std::monostate{}});
            }
            return DataValue{std::move(list)};
        }
        case Type::Map: {
            auto child = std::make_unique<DataContainer>();
            const auto& map = value.asMap();
            for (uint32_t k : map.keys()) {
                finescript::Value v = map.get(k);
                if (v.isNil()) continue;  // A key set to nil is no key
                if (auto item = toData(v)) child->set(k, std::move(*item));
            }
            return DataValue{std::move(child)};
        }
        default:
            return std::nullopt;
    }
}

finescript::Value DataContainerProxy::get(uint32_t key) const {
    const DataContainer* c = find();
    if (!c) return finescript::Value::nil();
    const DataValue* raw = c->getRaw(key);
    if (!raw) return finescript::Value::nil();

    // A nested map is live: writes through it reach the block
    if (const auto* child = std::get_if<std::unique_ptr<DataContainer>>(raw); child && *child) {
        if (container_) {
            return finescript::Value::proxyMap(std::make_shared<DataContainerProxy>(**child));
        }
        std::vector<DataKey> path = path_;
        path.push_back(key);
        return finescript::Value::proxyMap(
            std::make_shared<DataContainerProxy>(*world_, cell_, std::move(path)));
    }
    return copyOut(*raw);
}

void DataContainerProxy::set(uint32_t key, finescript::Value value) {
    if (value.isNil()) {
        remove(key);
        return;
    }
    auto data = toData(value);
    if (!data) return;  // A function can't be kept in a block
    DataContainer* c = findForWrite();
    if (!c) return;
    c->set(key, std::move(*data));
    changed();
}

bool DataContainerProxy::has(uint32_t key) const {
    const DataContainer* c = find();
    return c ? c->has(key) : false;
}

bool DataContainerProxy::remove(uint32_t key) {
    auto* c = const_cast<DataContainer*>(find());
    if (!c || !c->has(key)) return false;
    c->remove(key);
    // A block whose data is all gone has none
    if (world_ && path_.empty() && c->empty()) {
        if (SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_))) {
            auto local = cell_.local();
            sub->removeBlockData(local.x, local.y, local.z);
        }
    }
    changed();
    return true;
}

std::vector<uint32_t> DataContainerProxy::keys() const {
    std::vector<uint32_t> result;
    const DataContainer* c = find();
    if (!c) return result;
    c->forEach([&result](DataKey key, const DataValue&) {
        result.push_back(key);
    });
    return result;
}

}  // namespace finevox::script
