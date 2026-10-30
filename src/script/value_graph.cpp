#include "finevox/script/value_graph.hpp"

#include <finescript/cbor.h>
#include <finescript/map_data.h>

#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace finevox::script {

using finescript::Value;

namespace {

// The table is an array of nodes, each [kind, ...]. Within a node a one-element array is a
// reference to another node, which can't be mistaken for data: real containers are all nodes.
constexpr int64_t KIND_ARRAY = 0;
constexpr int64_t KIND_MAP = 1;

class Flattener {
public:
    Value flatten(const Value& value) {
        if (value.isArray()) return reference(&value.asArray(), value);
        if (value.isMap()) return reference(&const_cast<Value&>(value).asMap(), value);
        if (value.isNil() || value.isBool() || value.isInt() || value.isFloat() ||
            value.isString() || value.isSymbol()) {
            return value;
        }
        throw std::runtime_error("can't save a " + value.typeName());
    }

    [[nodiscard]] Value table() { return Value::array(std::move(nodes_)); }

private:
    Value reference(const void* identity, const Value& value) {
        auto found = seen_.find(identity);
        int64_t index;
        if (found != seen_.end()) {
            index = found->second;
        } else {
            // Claimed before going in, so a container that holds itself finds its own number
            index = static_cast<int64_t>(nodes_.size());
            seen_.emplace(identity, index);
            nodes_.push_back(Value::nil());

            std::vector<Value> node;
            if (value.isArray()) {
                node.push_back(Value::integer(KIND_ARRAY));
                for (const Value& item : value.asArray()) node.push_back(flatten(item));
            } else {
                node.push_back(Value::integer(KIND_MAP));
                auto& map = const_cast<Value&>(value).asMap();
                for (uint32_t key : map.keys()) {
                    node.push_back(Value::symbol(key));
                    node.push_back(flatten(map.get(key)));
                }
            }
            nodes_[index] = Value::array(std::move(node));
        }
        return Value::array(std::vector<Value>{Value::integer(index)});
    }

    std::unordered_map<const void*, int64_t> seen_;
    std::vector<Value> nodes_;
};

}  // namespace

std::vector<uint8_t> encodeValueGraph(const Value& value, finescript::Interner& interner) {
    Flattener flattener;
    Value root = flattener.flatten(value);
    return finescript::cborEncode(Value::array(std::vector<Value>{root, flattener.table()}), interner);
}

Value decodeValueGraph(const std::vector<uint8_t>& bytes, finescript::Interner& interner) {
    Value document = finescript::cborDecode(bytes, interner);
    if (!document.isArray() || document.asArray().size() != 2 || !document.asArray()[1].isArray()) {
        throw std::runtime_error("not a saved value");
    }
    const std::vector<Value>& table = document.asArray()[1].asArray();

    // Every container first, so that references can be made in any order
    std::vector<Value> built;
    std::vector<std::shared_ptr<std::vector<Value>>> arrays(table.size());
    built.reserve(table.size());
    for (size_t i = 0; i < table.size(); ++i) {
        const Value& node = table[i];
        if (!node.isArray() || node.asArray().empty()) throw std::runtime_error("bad saved node");
        if (node.asArray()[0].asInt() == KIND_MAP) {
            built.push_back(Value::map());
        } else {
            arrays[i] = std::make_shared<std::vector<Value>>();
            built.push_back(Value::array(arrays[i]));
        }
    }

    auto resolve = [&](const Value& item) -> Value {
        if (!item.isArray()) return item;
        int64_t index = item.asArray().at(0).asInt();
        if (index < 0 || static_cast<size_t>(index) >= built.size()) {
            throw std::runtime_error("saved reference out of range");
        }
        return built[index];
    };

    for (size_t i = 0; i < table.size(); ++i) {
        const std::vector<Value>& node = table[i].asArray();
        if (node[0].asInt() == KIND_MAP) {
            auto& map = built[i].asMap();
            for (size_t n = 1; n + 1 < node.size(); n += 2) {
                map.set(node[n].asSymbol(), resolve(node[n + 1]));
            }
        } else {
            for (size_t n = 1; n < node.size(); ++n) arrays[i]->push_back(resolve(node[n]));
        }
    }
    return resolve(document.asArray()[0]);
}

}  // namespace finevox::script
